#include "stdafx.h"

#include "oscilloscope_ui_element.h"

void oscilloscope_ui_element_instance::g_get_name(pfc::string_base & p_out) {
    p_out = "Oscilloscope (Direct2D)";
}

const char * oscilloscope_ui_element_instance::g_get_description() {
    return "Oscilloscope visualization using Direct2D.";
}

GUID oscilloscope_ui_element_instance::g_get_guid() {
    // {3DC976A0-F5DB-4B07-B9ED-01BDE2360249}
    static const GUID guid = 
    { 0x3dc976a0, 0xf5db, 0x4b07, { 0xb9, 0xed, 0x1, 0xbd, 0xe2, 0x36, 0x2, 0x49 } };

    return guid;
}

GUID oscilloscope_ui_element_instance::g_get_subclass() {
    return ui_element_subclass_playback_visualisation;
}

ui_element_config::ptr oscilloscope_ui_element_instance::g_get_default_configuration() {
    ui_element_config_builder builder;
    oscilloscope_config config;
    config.build(builder);
    return builder.finish(g_get_guid());
}

oscilloscope_ui_element_instance::oscilloscope_ui_element_instance(ui_element_config::ptr p_data, ui_element_instance_callback::ptr p_callback)
    : m_callback(p_callback)
    , m_last_refresh(0)
    , m_refresh_interval(10)
{
    set_configuration(p_data);
}

void oscilloscope_ui_element_instance::initialize_window(HWND p_parent) {
	this->Create(p_parent, nullptr, nullptr, 0, WS_EX_LEFT);
}

void oscilloscope_ui_element_instance::set_configuration(ui_element_config::ptr p_data) {
    ui_element_config_parser parser(p_data);
    oscilloscope_config config;
    config.parse(parser);
    m_config = config;

    UpdateChannelMode();
    UpdateRefreshRateLimit();
}

ui_element_config::ptr oscilloscope_ui_element_instance::get_configuration() {
    ui_element_config_builder builder;
    m_config.build(builder);
    return builder.finish(g_get_guid());
}

void oscilloscope_ui_element_instance::notify(const GUID & p_what, t_size p_param1, const void * p_param2, t_size p_param2size) {
    if (p_what == ui_element_notify_colors_changed) {
        m_pStrokeBrush.Release();
        Invalidate();
    }
}

CWndClassInfo& oscilloscope_ui_element_instance::GetWndClassInfo()
{
	static ATL::CWndClassInfo wc =
	{
        { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, StartWindowProc,
		  0, 0, NULL, NULL, NULL, (HBRUSH) NULL, NULL, TEXT("OscilloscopeD2D"), NULL },
		NULL, NULL, IDC_ARROW, TRUE, 0, _T("")
	};
	return wc;
}

LRESULT oscilloscope_ui_element_instance::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    HRESULT hr = S_OK;
    
    hr = CreateDeviceIndependentResources();

    if (FAILED(hr)) {
        console::formatter() << core_api::get_my_file_name() << ": could not create Direct2D factory";
    }

    try {
        static_api_ptr_t<visualisation_manager> vis_manager;

        vis_manager->create_stream(m_vis_stream, 0);

        m_vis_stream->request_backlog(0.8);
        UpdateChannelMode();
    } catch (std::exception & exc) {
        console::formatter() << core_api::get_my_file_name() << ": exception while creating visualisation stream: " << exc;
    }

    return 0;
}

void oscilloscope_ui_element_instance::OnDestroy() {
    m_vis_stream.release();

    m_pDirect2dFactory.Release();
    m_pRenderTarget.Release();
    m_pStrokeBrush.Release();
}

void oscilloscope_ui_element_instance::OnTimer(UINT_PTR nIDEvent) {
    KillTimer(ID_REFRESH_TIMER);
    Invalidate();
}

void oscilloscope_ui_element_instance::OnPaint(CDCHandle dc) {
    Render();
    ValidateRect(nullptr);

    DWORD now = GetTickCount();
    if (m_vis_stream.is_valid()) {
        DWORD next_refresh = m_last_refresh + m_refresh_interval;
        // (next_refresh < now) would break when GetTickCount() overflows
        if ((long) (next_refresh - now) < 0) {
            next_refresh = now;
        }
        SetTimer(ID_REFRESH_TIMER, next_refresh - now);
    }
    m_last_refresh = now;
}

void oscilloscope_ui_element_instance::OnSize(UINT nType, CSize size) {
    if (m_pRenderTarget) {
        m_pRenderTarget->Resize(D2D1::SizeU(size.cx, size.cy));
    }
}

HRESULT oscilloscope_ui_element_instance::Render() {
    HRESULT hr = S_OK;

    hr = CreateDeviceResources();

    if (SUCCEEDED(hr)) {
        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->SetAntialiasMode(m_config.m_low_quality_enabled ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        m_pRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

        t_ui_color colorBackground = m_callback->query_std_color(ui_color_background);

		m_pRenderTarget->Clear(D2D1::ColorF(GetRValue(colorBackground) / 255.0f, GetGValue(colorBackground) / 255.0f, GetBValue(colorBackground) / 255.0f));

        if (m_vis_stream.is_valid()) {
            double time;
            if (m_vis_stream->get_absolute_time(time)) {
                double window_duration = m_config.get_window_duration();
                audio_chunk_impl chunk;
                if (m_vis_stream->get_chunk_absolute(chunk, time - window_duration / 2, window_duration * (m_config.m_trigger_enabled ? 2 : 1))) {
                    RenderChunk(chunk);
                }
            }
        }

        hr = m_pRenderTarget->EndDraw();

        if (hr == D2DERR_RECREATE_TARGET)
        {
            hr = S_OK;
            DiscardDeviceResources();
        }
    }

    return hr;
}

HRESULT oscilloscope_ui_element_instance::RenderChunk(const audio_chunk &chunk) {
    HRESULT hr = S_OK;

    D2D1_SIZE_F rtSize = m_pRenderTarget->GetSize();

    CComPtr<ID2D1PathGeometry> pPath;

    hr = m_pDirect2dFactory->CreatePathGeometry(&pPath);

    if (SUCCEEDED(hr)) {
        CComPtr<ID2D1GeometrySink> pSink;
            
        hr = pPath->Open(&pSink);

        audio_chunk_impl chunk2;
        chunk2.copy(chunk);

        if (m_config.m_resample_enabled) {
            unsigned display_sample_rate = (unsigned) (rtSize.width / m_config.get_window_duration());
            unsigned target_sample_rate = chunk.get_sample_rate();
            while (target_sample_rate >= 2 && target_sample_rate > display_sample_rate) {
                target_sample_rate /= 2;
            }
            if (target_sample_rate != chunk.get_sample_rate()) {
                dsp::ptr resampler;
                metadb_handle::ptr track;
                if (static_api_ptr_t<playback_control>()->get_now_playing(track) && resampler_entry::g_create(resampler, chunk.get_sample_rate(), target_sample_rate, 1.0f)) {
                    dsp_chunk_list_impl chunk_list;
                    chunk_list.add_chunk(&chunk);
                    resampler->run(&chunk_list, track, dsp::FLUSH);
                    resampler->flush();

                    bool consistent_format = true;
                    unsigned total_sample_count = 0;
                    for (t_size chunk_index = 0; chunk_index < chunk_list.get_count(); ++chunk_index) {
                        if ((chunk_list.get_item(chunk_index)->get_sample_rate() == chunk_list.get_item(0)->get_sample_rate())
                            && (chunk_list.get_item(chunk_index)->get_channel_count() == chunk_list.get_item(0)->get_channel_count())) {
                                total_sample_count += chunk_list.get_item(chunk_index)->get_sample_count();
                        } else {
                            consistent_format = false;
                            break;
                        }
                    }
                    if (consistent_format && chunk_list.get_count() > 0) {
                        unsigned channel_count = chunk_list.get_item(0)->get_channels();
                        unsigned sample_rate = chunk_list.get_item(0)->get_sample_rate();

                        pfc::array_t<audio_sample> buffer;
                        buffer.prealloc(channel_count * total_sample_count);
                        for (t_size chunk_index = 0; chunk_index < chunk_list.get_count(); ++chunk_index) {
                            audio_chunk * c = chunk_list.get_item(chunk_index);
                            buffer.append_fromptr(c->get_data(), c->get_channel_count() * c->get_sample_count());
                        }

                        chunk2.set_data(buffer.get_ptr(), total_sample_count, channel_count, sample_rate);
                    }
                }
            }
        }

        t_uint32 channel_count = chunk2.get_channel_count();
        t_uint32 sample_count_total = chunk2.get_sample_count();
        t_uint32 sample_count = m_config.m_trigger_enabled ? sample_count_total / 2 : sample_count_total;
        const audio_sample *samples = chunk2.get_data();

        if (m_config.m_trigger_enabled) {
            t_uint32 cross_min = sample_count;
            t_uint32 cross_max = 0;

            for (t_uint32 channel_index = 0; channel_index < channel_count; ++channel_index) {
                audio_sample sample0 = samples[channel_index];
                audio_sample sample1 = samples[1 * channel_count + channel_index];
                audio_sample sample2;
                for (t_uint32 sample_index = 2; sample_index < sample_count; ++sample_index) {
                    sample2 = samples[sample_index * channel_count + channel_index];
                    if ((sample0 < 0.0) && (sample1 >= 0.0) && (sample2 >= 0.0)) {
                        if (cross_min > sample_index - 1)
                            cross_min = sample_index - 1;
                        if (cross_max < sample_index - 1)
                            cross_max = sample_index - 1;
                    }
                    sample0 = sample1;
                    sample1 = sample2;
                }
            }

            samples += cross_min * channel_count;
        }

        for (t_uint32 channel_index = 0; channel_index < channel_count; ++channel_index) {
            float zoom = (float) m_config.get_zoom_factor();
            float channel_baseline = (float) (channel_index + 0.5) / (float) channel_count * rtSize.height;
            for (t_uint32 sample_index = 0; sample_index < sample_count; ++sample_index) {
                audio_sample sample = samples[sample_index * channel_count + channel_index];
                float x = (float) sample_index / (float) (sample_count - 1) * rtSize.width;
                float y = channel_baseline - sample * zoom * rtSize.height / 2 / channel_count + 0.5f;
                if (sample_index == 0) {
                    pSink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
                } else {
                    pSink->AddLine(D2D1::Point2F(x, y));
                }
            }
            if (channel_count > 0 && sample_count > 0) {
                pSink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
        }

        if (SUCCEEDED(hr)) {
            hr = pSink->Close();
        }

        if (SUCCEEDED(hr)) {
            D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_BEVEL);

            CComPtr<ID2D1StrokeStyle> pStrokeStyle;
            m_pDirect2dFactory->CreateStrokeStyle(strokeStyleProperties, nullptr, 0, &pStrokeStyle);

            m_pRenderTarget->DrawGeometry(pPath, m_pStrokeBrush, (FLOAT)m_config.get_line_stroke_width(), pStrokeStyle);
        }
    }

    return hr;
}


void oscilloscope_ui_element_instance::OnContextMenu(CWindow wnd, CPoint point) {
	if (m_callback->is_edit_mode_enabled()) {
		SetMsgHandled(FALSE);
	}
	else {
		CMenu menu;
		menu.CreatePopupMenu();
		menu.AppendMenu(MF_STRING, IDM_TOGGLE_FULLSCREEN, TEXT("Toggle Full-Screen Mode"));
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING | (m_config.m_downmix_enabled ? MF_CHECKED : 0), IDM_DOWNMIX_ENABLED, TEXT("Downmix Channels"));
		menu.AppendMenu(MF_STRING | (m_config.m_low_quality_enabled ? MF_CHECKED : 0), IDM_LOW_QUALITY_ENABLED, TEXT("Low Quality Mode"));
		menu.AppendMenu(MF_STRING | (m_config.m_trigger_enabled ? MF_CHECKED : 0), IDM_TRIGGER_ENABLED, TEXT("Trigger on Zero Crossing"));

		CMenu durationMenu;
		durationMenu.CreatePopupMenu();
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 1) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_1, TEXT("1 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 2) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_2, TEXT("2 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 3) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_3, TEXT("3 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 4) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_4, TEXT("4 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 5) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_5, TEXT("5 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 6) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_6, TEXT("6 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 7) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_7, TEXT("7 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 8) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_8, TEXT("8 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 9) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_9, TEXT("9 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 10) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_10, TEXT("10 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 11) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_11, TEXT("11 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 12) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_12, TEXT("12 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 13) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_13, TEXT("13 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 14) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_14, TEXT("14 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 15) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_15, TEXT("15 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 16) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_16, TEXT("16 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 17) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_17, TEXT("17 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 18) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_18, TEXT("18 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 19) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_19, TEXT("19 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 20) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_20, TEXT("20 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 25) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_25, TEXT("25 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 30) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_30, TEXT("30 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 35) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_35, TEXT("35 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 40) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_40, TEXT("40 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 45) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_45, TEXT("45 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 50) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_50, TEXT("50 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 60) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_60, TEXT("60 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 80) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_80, TEXT("80 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 100) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_100, TEXT("100 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 150) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_150, TEXT("150 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 200) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_200, TEXT("200 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 300) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_300, TEXT("300 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 400) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_400, TEXT("400 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 500) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_500, TEXT("500 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 600) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_600, TEXT("600 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 800) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_800, TEXT("800 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 1000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_1000, TEXT("1000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 1500) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_1500, TEXT("1500 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 2000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_2000, TEXT("2000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 3000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_3000, TEXT("3000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 4000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_4000, TEXT("4000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 5000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_5000, TEXT("5000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 6000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_6000, TEXT("6000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 8000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_8000, TEXT("8000 ms"));
		durationMenu.AppendMenu(MF_STRING | ((m_config.m_window_duration_millis == 10000) ? MF_CHECKED : 0), IDM_WINDOW_DURATION_10000, TEXT("10000 ms"));

		menu.AppendMenu(MF_STRING, durationMenu, TEXT("Window Duration"));

		CMenu zoomMenu;
		zoomMenu.CreatePopupMenu();
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 5) ? MF_CHECKED : 0), IDM_ZOOM_5, TEXT("5 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 10) ? MF_CHECKED : 0), IDM_ZOOM_10, TEXT("10 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 15) ? MF_CHECKED : 0), IDM_ZOOM_15, TEXT("15 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 20) ? MF_CHECKED : 0), IDM_ZOOM_20, TEXT("20 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 25) ? MF_CHECKED : 0), IDM_ZOOM_25, TEXT("25 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 30) ? MF_CHECKED : 0), IDM_ZOOM_30, TEXT("30 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 35) ? MF_CHECKED : 0), IDM_ZOOM_35, TEXT("35 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 40) ? MF_CHECKED : 0), IDM_ZOOM_40, TEXT("40 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 45) ? MF_CHECKED : 0), IDM_ZOOM_45, TEXT("45 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 50) ? MF_CHECKED : 0), IDM_ZOOM_50, TEXT("50 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 55) ? MF_CHECKED : 0), IDM_ZOOM_55, TEXT("55 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 60) ? MF_CHECKED : 0), IDM_ZOOM_60, TEXT("60 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 65) ? MF_CHECKED : 0), IDM_ZOOM_65, TEXT("65 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 70) ? MF_CHECKED : 0), IDM_ZOOM_70, TEXT("70 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 75) ? MF_CHECKED : 0), IDM_ZOOM_75, TEXT("75 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 80) ? MF_CHECKED : 0), IDM_ZOOM_80, TEXT("80 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 85) ? MF_CHECKED : 0), IDM_ZOOM_85, TEXT("85 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 90) ? MF_CHECKED : 0), IDM_ZOOM_90, TEXT("90 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 95) ? MF_CHECKED : 0), IDM_ZOOM_95, TEXT("95 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 96) ? MF_CHECKED : 0), IDM_ZOOM_96, TEXT("96 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 97) ? MF_CHECKED : 0), IDM_ZOOM_97, TEXT("97 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 98) ? MF_CHECKED : 0), IDM_ZOOM_98, TEXT("98 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 99) ? MF_CHECKED : 0), IDM_ZOOM_99, TEXT("99 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 100) ? MF_CHECKED : 0), IDM_ZOOM_100, TEXT("100 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 125) ? MF_CHECKED : 0), IDM_ZOOM_125, TEXT("125 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 150) ? MF_CHECKED : 0), IDM_ZOOM_150, TEXT("150 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 200) ? MF_CHECKED : 0), IDM_ZOOM_200, TEXT("200 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 300) ? MF_CHECKED : 0), IDM_ZOOM_300, TEXT("300 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 400) ? MF_CHECKED : 0), IDM_ZOOM_400, TEXT("400 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 500) ? MF_CHECKED : 0), IDM_ZOOM_500, TEXT("500 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 600) ? MF_CHECKED : 0), IDM_ZOOM_600, TEXT("600 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 800) ? MF_CHECKED : 0), IDM_ZOOM_800, TEXT("800 %"));
		zoomMenu.AppendMenu(MF_STRING | ((m_config.m_zoom_percent == 1000) ? MF_CHECKED : 0), IDM_ZOOM_1000, TEXT("1000 %"));

		menu.AppendMenu(MF_STRING, zoomMenu, TEXT("Zoom"));

		CMenu refreshRateLimitMenu;
		refreshRateLimitMenu.CreatePopupMenu();
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 20) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_20, TEXT("20 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 30) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_30, TEXT("30 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 50) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_50, TEXT("50 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 60) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_60, TEXT("60 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 72) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_72, TEXT("72 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 75) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_75, TEXT("75 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 90) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_90, TEXT("90 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 120) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_120, TEXT("120 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 144) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_144, TEXT("144 Hz"));
		refreshRateLimitMenu.AppendMenu(MF_STRING | ((m_config.m_refresh_rate_limit_hz == 240) ? MF_CHECKED : 0), IDM_REFRESH_RATE_LIMIT_240, TEXT("240 Hz"));

		menu.AppendMenu(MF_STRING, refreshRateLimitMenu, TEXT("Refresh Rate Limit"));

		CMenu lineStrokeWidthMenu;
		lineStrokeWidthMenu.CreatePopupMenu();
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 1) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_1, TEXT("0.1 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 2) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_2, TEXT("0.2 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 3) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_3, TEXT("0.3 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 4) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_4, TEXT("0.4 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 5) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_5, TEXT("0.5 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 6) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_6, TEXT("0.6 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 7) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_7, TEXT("0.7 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 8) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_8, TEXT("0.8 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 9) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_9, TEXT("0.9 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 10) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_10, TEXT("1.0 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 11) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_11, TEXT("1.1 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 12) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_12, TEXT("1.2 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 13) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_13, TEXT("1.3 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 14) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_14, TEXT("1.4 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 15) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_15, TEXT("1.5 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 16) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_16, TEXT("1.6 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 17) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_17, TEXT("1.7 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 18) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_18, TEXT("1.8 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 19) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_19, TEXT("1.9 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 20) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_20, TEXT("2.0 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 21) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_21, TEXT("2.1 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 22) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_22, TEXT("2.2 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 23) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_23, TEXT("2.3 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 24) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_24, TEXT("2.4 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 25) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_25, TEXT("2.5 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 26) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_26, TEXT("2.6 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 27) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_27, TEXT("2.7 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 28) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_28, TEXT("2.8 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 29) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_29, TEXT("2.9 px"));
		lineStrokeWidthMenu.AppendMenu(MF_STRING | ((m_config.m_line_stroke_width == 30) ? MF_CHECKED : 0), IDM_LINE_STROKE_WIDTH_30, TEXT("3.0 px"));

		menu.AppendMenu(MF_STRING, lineStrokeWidthMenu, TEXT("Line Stroke Width"));

		menu.AppendMenu(MF_SEPARATOR);

		menu.AppendMenu(MF_STRING | (m_config.m_resample_enabled ? MF_CHECKED : 0), IDM_RESAMPLE_ENABLED, TEXT("Resample For Display"));
		menu.AppendMenu(MF_STRING | (m_config.m_hw_rendering_enabled ? MF_CHECKED : 0), IDM_HW_RENDERING_ENABLED, TEXT("Allow Hardware Rendering"));

		menu.SetMenuDefaultItem(IDM_TOGGLE_FULLSCREEN);

		int cmd = menu.TrackPopupMenu(TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, point.x, point.y, *this);

		switch (cmd) {
		case IDM_TOGGLE_FULLSCREEN:
			ToggleFullScreen();
			break;
		case IDM_HW_RENDERING_ENABLED:
			m_config.m_hw_rendering_enabled = !m_config.m_hw_rendering_enabled;
			DiscardDeviceResources();
			break;
		case IDM_DOWNMIX_ENABLED:
			m_config.m_downmix_enabled = !m_config.m_downmix_enabled;
			UpdateChannelMode();
			break;
		case IDM_LOW_QUALITY_ENABLED:
			m_config.m_low_quality_enabled = !m_config.m_low_quality_enabled;
			break;
		case IDM_TRIGGER_ENABLED:
			m_config.m_trigger_enabled = !m_config.m_trigger_enabled;
			break;
		case IDM_RESAMPLE_ENABLED:
			m_config.m_resample_enabled = !m_config.m_resample_enabled;
			break;
		case IDM_WINDOW_DURATION_1:
			m_config.m_window_duration_millis = 1;
			break;
		case IDM_WINDOW_DURATION_2:
			m_config.m_window_duration_millis = 2;
			break;
		case IDM_WINDOW_DURATION_3:
			m_config.m_window_duration_millis = 3;
			break;
		case IDM_WINDOW_DURATION_4:
			m_config.m_window_duration_millis = 4;
			break;
		case IDM_WINDOW_DURATION_5:
			m_config.m_window_duration_millis = 5;
			break;
		case IDM_WINDOW_DURATION_6:
			m_config.m_window_duration_millis = 6;
			break;
		case IDM_WINDOW_DURATION_7:
			m_config.m_window_duration_millis = 7;
			break;
		case IDM_WINDOW_DURATION_8:
			m_config.m_window_duration_millis = 8;
			break;
		case IDM_WINDOW_DURATION_9:
			m_config.m_window_duration_millis = 9;
			break;
		case IDM_WINDOW_DURATION_10:
			m_config.m_window_duration_millis = 10;
			break;
		case IDM_WINDOW_DURATION_11:
			m_config.m_window_duration_millis = 11;
			break;
		case IDM_WINDOW_DURATION_12:
			m_config.m_window_duration_millis = 12;
			break;
		case IDM_WINDOW_DURATION_13:
			m_config.m_window_duration_millis = 13;
			break;
		case IDM_WINDOW_DURATION_14:
			m_config.m_window_duration_millis = 14;
			break;
		case IDM_WINDOW_DURATION_15:
			m_config.m_window_duration_millis = 15;
			break;
		case IDM_WINDOW_DURATION_16:
			m_config.m_window_duration_millis = 16;
			break;
		case IDM_WINDOW_DURATION_17:
			m_config.m_window_duration_millis = 17;
			break;
		case IDM_WINDOW_DURATION_18:
			m_config.m_window_duration_millis = 18;
			break;
		case IDM_WINDOW_DURATION_19:
			m_config.m_window_duration_millis = 19;
			break;
		case IDM_WINDOW_DURATION_20:
			m_config.m_window_duration_millis = 20;
			break;
		case IDM_WINDOW_DURATION_25:
			m_config.m_window_duration_millis = 25;
			break;
		case IDM_WINDOW_DURATION_30:
			m_config.m_window_duration_millis = 30;
			break;
		case IDM_WINDOW_DURATION_35:
			m_config.m_window_duration_millis = 35;
			break;
		case IDM_WINDOW_DURATION_40:
			m_config.m_window_duration_millis = 40;
			break;
		case IDM_WINDOW_DURATION_45:
			m_config.m_window_duration_millis = 45;
			break;
		case IDM_WINDOW_DURATION_50:
			m_config.m_window_duration_millis = 50;
			break;
		case IDM_WINDOW_DURATION_60:
			m_config.m_window_duration_millis = 60;
			break;
		case IDM_WINDOW_DURATION_80:
			m_config.m_window_duration_millis = 80;
			break;
		case IDM_WINDOW_DURATION_100:
			m_config.m_window_duration_millis = 100;
			break;
		case IDM_WINDOW_DURATION_150:
			m_config.m_window_duration_millis = 150;
			break;
		case IDM_WINDOW_DURATION_200:
			m_config.m_window_duration_millis = 200;
			break;
		case IDM_WINDOW_DURATION_300:
			m_config.m_window_duration_millis = 300;
			break;
		case IDM_WINDOW_DURATION_400:
			m_config.m_window_duration_millis = 400;
			break;
		case IDM_WINDOW_DURATION_500:
			m_config.m_window_duration_millis = 500;
			break;
		case IDM_WINDOW_DURATION_600:
			m_config.m_window_duration_millis = 600;
			break;
		case IDM_WINDOW_DURATION_800:
			m_config.m_window_duration_millis = 800;
			break;
		case IDM_WINDOW_DURATION_1000:
			m_config.m_window_duration_millis = 1000;
			break;
		case IDM_WINDOW_DURATION_1500:
			m_config.m_window_duration_millis = 1500;
			break;
		case IDM_WINDOW_DURATION_2000:
			m_config.m_window_duration_millis = 2000;
			break;
		case IDM_WINDOW_DURATION_3000:
			m_config.m_window_duration_millis = 3000;
			break;
		case IDM_WINDOW_DURATION_4000:
			m_config.m_window_duration_millis = 4000;
			break;
		case IDM_WINDOW_DURATION_5000:
			m_config.m_window_duration_millis = 5000;
			break;
		case IDM_WINDOW_DURATION_6000:
			m_config.m_window_duration_millis = 6000;
			break;
		case IDM_WINDOW_DURATION_8000:
			m_config.m_window_duration_millis = 8000;
			break;
		case IDM_WINDOW_DURATION_10000:
			m_config.m_window_duration_millis = 10000;
			break;
		case IDM_ZOOM_5:
			m_config.m_zoom_percent = 5;
			break;
		case IDM_ZOOM_10:
			m_config.m_zoom_percent = 10;
			break;
		case IDM_ZOOM_15:
			m_config.m_zoom_percent = 15;
			break;
		case IDM_ZOOM_20:
			m_config.m_zoom_percent = 20;
			break;
		case IDM_ZOOM_25:
			m_config.m_zoom_percent = 25;
			break;
		case IDM_ZOOM_30:
			m_config.m_zoom_percent = 30;
			break;
		case IDM_ZOOM_35:
			m_config.m_zoom_percent = 35;
			break;
		case IDM_ZOOM_40:
			m_config.m_zoom_percent = 40;
			break;
		case IDM_ZOOM_45:
			m_config.m_zoom_percent = 45;
			break;
		case IDM_ZOOM_50:
			m_config.m_zoom_percent = 50;
			break;
		case IDM_ZOOM_55:
			m_config.m_zoom_percent = 55;
			break;
		case IDM_ZOOM_60:
			m_config.m_zoom_percent = 60;
			break;
		case IDM_ZOOM_65:
			m_config.m_zoom_percent = 65;
			break;
		case IDM_ZOOM_70:
			m_config.m_zoom_percent = 70;
			break;
		case IDM_ZOOM_75:
			m_config.m_zoom_percent = 75;
			break;
		case IDM_ZOOM_80:
			m_config.m_zoom_percent = 80;
			break;
		case IDM_ZOOM_85:
			m_config.m_zoom_percent = 85;
			break;
		case IDM_ZOOM_90:
			m_config.m_zoom_percent = 90;
			break;
		case IDM_ZOOM_95:
			m_config.m_zoom_percent = 95;
			break;
		case IDM_ZOOM_96:
			m_config.m_zoom_percent = 96;
			break;
		case IDM_ZOOM_97:
			m_config.m_zoom_percent = 97;
			break;
		case IDM_ZOOM_98:
			m_config.m_zoom_percent = 98;
			break;
		case IDM_ZOOM_99:
			m_config.m_zoom_percent = 99;
			break;
		case IDM_ZOOM_100:
			m_config.m_zoom_percent = 100;
			break;
		case IDM_ZOOM_125:
			m_config.m_zoom_percent = 125;
			break;
		case IDM_ZOOM_150:
			m_config.m_zoom_percent = 150;
			break;
		case IDM_ZOOM_200:
			m_config.m_zoom_percent = 200;
			break;
		case IDM_ZOOM_300:
			m_config.m_zoom_percent = 300;
			break;
		case IDM_ZOOM_400:
			m_config.m_zoom_percent = 400;
			break;
		case IDM_ZOOM_500:
			m_config.m_zoom_percent = 500;
			break;
		case IDM_ZOOM_600:
			m_config.m_zoom_percent = 600;
			break;
		case IDM_ZOOM_800:
			m_config.m_zoom_percent = 800;
			break;
		case IDM_ZOOM_1000:
			m_config.m_zoom_percent = 1000;
			break;
		case IDM_REFRESH_RATE_LIMIT_20:
			m_config.m_refresh_rate_limit_hz = 20;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_30:
			m_config.m_refresh_rate_limit_hz = 30;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_50:
			m_config.m_refresh_rate_limit_hz = 50;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_60:
			m_config.m_refresh_rate_limit_hz = 60;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_72:
			m_config.m_refresh_rate_limit_hz = 72;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_75:
			m_config.m_refresh_rate_limit_hz = 75;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_90:
			m_config.m_refresh_rate_limit_hz = 90;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_120:
			m_config.m_refresh_rate_limit_hz = 120;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_144:
			m_config.m_refresh_rate_limit_hz = 144;
			UpdateRefreshRateLimit();
			break;
		case IDM_REFRESH_RATE_LIMIT_240:
			m_config.m_refresh_rate_limit_hz = 240;
			UpdateRefreshRateLimit();
			break;
		case IDM_LINE_STROKE_WIDTH_1:
			m_config.m_line_stroke_width = 1;
			break;
		case IDM_LINE_STROKE_WIDTH_2:
			m_config.m_line_stroke_width = 2;
			break;
		case IDM_LINE_STROKE_WIDTH_3:
			m_config.m_line_stroke_width = 3;
			break;
		case IDM_LINE_STROKE_WIDTH_4:
			m_config.m_line_stroke_width = 4;
			break;
		case IDM_LINE_STROKE_WIDTH_5:
			m_config.m_line_stroke_width = 5;
			break;
		case IDM_LINE_STROKE_WIDTH_6:
			m_config.m_line_stroke_width = 6;
			break;
		case IDM_LINE_STROKE_WIDTH_7:
			m_config.m_line_stroke_width = 7;
			break;
		case IDM_LINE_STROKE_WIDTH_8:
			m_config.m_line_stroke_width = 8;
			break;
		case IDM_LINE_STROKE_WIDTH_9:
			m_config.m_line_stroke_width = 9;
			break;
		case IDM_LINE_STROKE_WIDTH_10:
			m_config.m_line_stroke_width = 10;
			break;
		case IDM_LINE_STROKE_WIDTH_11:
			m_config.m_line_stroke_width = 11;
			break;
		case IDM_LINE_STROKE_WIDTH_12:
			m_config.m_line_stroke_width = 12;
			break;
		case IDM_LINE_STROKE_WIDTH_13:
			m_config.m_line_stroke_width = 13;
			break;
		case IDM_LINE_STROKE_WIDTH_14:
			m_config.m_line_stroke_width = 14;
			break;
		case IDM_LINE_STROKE_WIDTH_15:
			m_config.m_line_stroke_width = 15;
			break;
		case IDM_LINE_STROKE_WIDTH_16:
			m_config.m_line_stroke_width = 16;
			break;
		case IDM_LINE_STROKE_WIDTH_17:
			m_config.m_line_stroke_width = 17;
			break;
		case IDM_LINE_STROKE_WIDTH_18:
			m_config.m_line_stroke_width = 18;
			break;
		case IDM_LINE_STROKE_WIDTH_19:
			m_config.m_line_stroke_width = 19;
			break;
		case IDM_LINE_STROKE_WIDTH_20:
			m_config.m_line_stroke_width = 20;
			break;
		case IDM_LINE_STROKE_WIDTH_21:
			m_config.m_line_stroke_width = 21;
			break;
		case IDM_LINE_STROKE_WIDTH_22:
			m_config.m_line_stroke_width = 22;
			break;
		case IDM_LINE_STROKE_WIDTH_23:
			m_config.m_line_stroke_width = 23;
			break;
		case IDM_LINE_STROKE_WIDTH_24:
			m_config.m_line_stroke_width = 24;
			break;
		case IDM_LINE_STROKE_WIDTH_25:
			m_config.m_line_stroke_width = 25;
			break;
		case IDM_LINE_STROKE_WIDTH_26:
			m_config.m_line_stroke_width = 26;
			break;
		case IDM_LINE_STROKE_WIDTH_27:
			m_config.m_line_stroke_width = 27;
			break;
		case IDM_LINE_STROKE_WIDTH_28:
			m_config.m_line_stroke_width = 28;
			break;
		case IDM_LINE_STROKE_WIDTH_29:
			m_config.m_line_stroke_width = 29;
			break;
		case IDM_LINE_STROKE_WIDTH_30:
			m_config.m_line_stroke_width = 30;
			break;
		}

		Invalidate();
	}
}

void oscilloscope_ui_element_instance::OnLButtonDblClk(UINT nFlags, CPoint point) {
    ToggleFullScreen();
}

void oscilloscope_ui_element_instance::ToggleFullScreen() {
    static_api_ptr_t<ui_element_common_methods_v2>()->toggle_fullscreen(g_get_guid(), core_api::get_main_window());
}

void oscilloscope_ui_element_instance::UpdateChannelMode() {
    if (m_vis_stream.is_valid()) {
        m_vis_stream->set_channel_mode(m_config.m_downmix_enabled ? visualisation_stream_v2::channel_mode_mono : visualisation_stream_v2::channel_mode_default);
    }
}

void oscilloscope_ui_element_instance::UpdateRefreshRateLimit() {
    m_refresh_interval = pfc::clip_t<DWORD>(1000 / m_config.m_refresh_rate_limit_hz, 5, 1000);
}

HRESULT oscilloscope_ui_element_instance::CreateDeviceIndependentResources() {
    HRESULT hr = S_OK;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pDirect2dFactory);

    return hr;
}

HRESULT oscilloscope_ui_element_instance::CreateDeviceResources() {
    HRESULT hr = S_OK;

    if (m_pDirect2dFactory) {
        if (!m_pRenderTarget) {
            CRect rcClient;
            GetClientRect(rcClient);

            D2D1_SIZE_U size = D2D1::SizeU(rcClient.Width(), rcClient.Height());

            D2D1_RENDER_TARGET_PROPERTIES renderTargetProperties = D2D1::RenderTargetProperties(m_config.m_hw_rendering_enabled ? D2D1_RENDER_TARGET_TYPE_DEFAULT : D2D1_RENDER_TARGET_TYPE_SOFTWARE);

            D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRenderTargetProperties = D2D1::HwndRenderTargetProperties(m_hWnd, size);

            hr = m_pDirect2dFactory->CreateHwndRenderTarget(renderTargetProperties, hwndRenderTargetProperties, &m_pRenderTarget);
        }

        if (SUCCEEDED(hr) && !m_pStrokeBrush) {
            t_ui_color colorText = m_callback->query_std_color(ui_color_text);

			hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(GetRValue(colorText) / 255.0f, GetGValue(colorText) / 255.0f, GetBValue(colorText) / 255.0f), &m_pStrokeBrush);
        }
    } else {
        hr = S_FALSE;
    }

    return hr;
}

void oscilloscope_ui_element_instance::DiscardDeviceResources() {
    m_pRenderTarget.Release();
    m_pStrokeBrush.Release();
}

static service_factory_single_t< ui_element_impl_visualisation< oscilloscope_ui_element_instance> > g_ui_element_factory;
