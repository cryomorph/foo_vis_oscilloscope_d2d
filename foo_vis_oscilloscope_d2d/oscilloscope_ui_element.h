#pragma once

#include "oscilloscope_config.h"

class oscilloscope_ui_element_instance : public ui_element_instance, public CWindowImpl<oscilloscope_ui_element_instance> {
public:
    static void g_get_name(pfc::string_base & p_out);
    static const char * g_get_description();
    static GUID g_get_guid();
    static GUID g_get_subclass();
    static ui_element_config::ptr g_get_default_configuration();

    oscilloscope_ui_element_instance(ui_element_config::ptr p_data, ui_element_instance_callback::ptr p_callback);

    void initialize_window(HWND p_parent);
	virtual void set_configuration(ui_element_config::ptr p_data);
	virtual ui_element_config::ptr get_configuration();
    virtual void notify(const GUID & p_what, t_size p_param1, const void * p_param2, t_size p_param2size);

    static CWndClassInfo& GetWndClassInfo();

    LRESULT OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();
    void OnTimer(UINT_PTR nIDEvent);
    void OnPaint(CDCHandle dc);
    void OnSize(UINT nType, CSize size);
    void OnContextMenu(CWindow wnd, CPoint point);
    void OnLButtonDblClk(UINT nFlags, CPoint point);

    void ToggleFullScreen();
    void UpdateChannelMode();
    void UpdateRefreshRateLimit();

    HRESULT Render();
    HRESULT RenderChunk(const audio_chunk &chunk);
    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();

    BEGIN_MSG_MAP_EX(oscilloscope_ui_element_instance)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
    END_MSG_MAP()

protected:
    ui_element_instance_callback::ptr m_callback;

private:
    enum {
        ID_REFRESH_TIMER = 1
    };

	enum {
		IDM_TOGGLE_FULLSCREEN = 1,
		IDM_HW_RENDERING_ENABLED,
		IDM_DOWNMIX_ENABLED,
		IDM_TRIGGER_ENABLED,
		IDM_RESAMPLE_ENABLED,
		IDM_LOW_QUALITY_ENABLED,
		IDM_WINDOW_DURATION_1,
		IDM_WINDOW_DURATION_2,
		IDM_WINDOW_DURATION_3,
		IDM_WINDOW_DURATION_4,
		IDM_WINDOW_DURATION_5,
		IDM_WINDOW_DURATION_6,
		IDM_WINDOW_DURATION_7,
		IDM_WINDOW_DURATION_8,
		IDM_WINDOW_DURATION_9,
		IDM_WINDOW_DURATION_10,
		IDM_WINDOW_DURATION_11,
		IDM_WINDOW_DURATION_12,
		IDM_WINDOW_DURATION_13,
		IDM_WINDOW_DURATION_14,
		IDM_WINDOW_DURATION_15,
		IDM_WINDOW_DURATION_16,
		IDM_WINDOW_DURATION_17,
		IDM_WINDOW_DURATION_18,
		IDM_WINDOW_DURATION_19,
		IDM_WINDOW_DURATION_20,
		IDM_WINDOW_DURATION_25,
		IDM_WINDOW_DURATION_30,
		IDM_WINDOW_DURATION_35,
		IDM_WINDOW_DURATION_40,
		IDM_WINDOW_DURATION_45,
		IDM_WINDOW_DURATION_50,
		IDM_WINDOW_DURATION_60,
		IDM_WINDOW_DURATION_80,
		IDM_WINDOW_DURATION_100,
		IDM_WINDOW_DURATION_150,
		IDM_WINDOW_DURATION_200,
		IDM_WINDOW_DURATION_300,
		IDM_WINDOW_DURATION_400,
		IDM_WINDOW_DURATION_500,
		IDM_WINDOW_DURATION_600,
		IDM_WINDOW_DURATION_800,
		IDM_ZOOM_5,
		IDM_ZOOM_10,
		IDM_ZOOM_15,
		IDM_ZOOM_20,
		IDM_ZOOM_25,
		IDM_ZOOM_30,
		IDM_ZOOM_35,
		IDM_ZOOM_40,
		IDM_ZOOM_45,
		IDM_ZOOM_50,
		IDM_ZOOM_55,
		IDM_ZOOM_60,
		IDM_ZOOM_65,
		IDM_ZOOM_70,
		IDM_ZOOM_75,
		IDM_ZOOM_80,
		IDM_ZOOM_85,
		IDM_ZOOM_90,
		IDM_ZOOM_95,
		IDM_ZOOM_96,
		IDM_ZOOM_97,
		IDM_ZOOM_98,
		IDM_ZOOM_99,
		IDM_ZOOM_100,
		IDM_ZOOM_125,
		IDM_ZOOM_150,
		IDM_ZOOM_200,
		IDM_ZOOM_300,
		IDM_ZOOM_400,
		IDM_ZOOM_500,
		IDM_ZOOM_600,
		IDM_ZOOM_800,
		IDM_ZOOM_1000,
		IDM_REFRESH_RATE_LIMIT_20,
		IDM_REFRESH_RATE_LIMIT_30,
		IDM_REFRESH_RATE_LIMIT_50,
		IDM_REFRESH_RATE_LIMIT_60,
		IDM_REFRESH_RATE_LIMIT_72,
		IDM_REFRESH_RATE_LIMIT_75,
		IDM_REFRESH_RATE_LIMIT_90,
		IDM_REFRESH_RATE_LIMIT_120,
		IDM_REFRESH_RATE_LIMIT_144,
		IDM_REFRESH_RATE_LIMIT_240,
		IDM_LINE_STROKE_WIDTH_1,
		IDM_LINE_STROKE_WIDTH_2,
		IDM_LINE_STROKE_WIDTH_3,
		IDM_LINE_STROKE_WIDTH_4,
		IDM_LINE_STROKE_WIDTH_5,
		IDM_LINE_STROKE_WIDTH_6,
		IDM_LINE_STROKE_WIDTH_7,
		IDM_LINE_STROKE_WIDTH_8,
		IDM_LINE_STROKE_WIDTH_9,
		IDM_LINE_STROKE_WIDTH_10,
		IDM_LINE_STROKE_WIDTH_11,
		IDM_LINE_STROKE_WIDTH_12,
		IDM_LINE_STROKE_WIDTH_13,
		IDM_LINE_STROKE_WIDTH_14,
		IDM_LINE_STROKE_WIDTH_15,
		IDM_LINE_STROKE_WIDTH_16,
		IDM_LINE_STROKE_WIDTH_17,
		IDM_LINE_STROKE_WIDTH_18,
		IDM_LINE_STROKE_WIDTH_19,
		IDM_LINE_STROKE_WIDTH_20,
		IDM_LINE_STROKE_WIDTH_21,
		IDM_LINE_STROKE_WIDTH_22,
		IDM_LINE_STROKE_WIDTH_23,
		IDM_LINE_STROKE_WIDTH_24,
		IDM_LINE_STROKE_WIDTH_25,
		IDM_LINE_STROKE_WIDTH_26,
		IDM_LINE_STROKE_WIDTH_27,
		IDM_LINE_STROKE_WIDTH_28,
		IDM_LINE_STROKE_WIDTH_29,
		IDM_LINE_STROKE_WIDTH_30
	};
    oscilloscope_config m_config;
    DWORD m_last_refresh;
    DWORD m_refresh_interval;

    visualisation_stream_v2::ptr m_vis_stream;

    CComPtr<ID2D1Factory> m_pDirect2dFactory;
    CComPtr<ID2D1HwndRenderTarget> m_pRenderTarget;
    CComPtr<ID2D1SolidColorBrush> m_pStrokeBrush;
};
