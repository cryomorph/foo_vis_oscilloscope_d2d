// This file defines common version information for the DLL's version resource
// and the component version shown in foobar2000 itself.

#pragma once

// Component filename (string literal)
// TODO Update component filename
#define COMPONENT_FILENAME "foo_vis_oscilloscope_d2d.dll"

// Component name (string literal)
// TODO Update component name
#define COMPONENT_NAME "Oscilloscope (Direct2D)"

// Component author
// TODO Update component author
#define COMPONENT_AUTHOR "Holger Stenger and contributors"

// The parts of the component version number (integer literals)
// TODO Update component version, see http://semver.org
#define COMPONENT_VERSION_MAJOR 1
#define COMPONENT_VERSION_MINOR 1
#define COMPONENT_VERSION_PATCH 1

// Year for copyright notice (string literal)
// TODO Update year for copyright notice
#define COMPONENT_COPYRIGHT_YEAR "2017"

// Helper macros for converting integers to string literals and concatenating them
#define MAKE_STRING(text) #text
#define MAKE_COMPONENT_VERSION(major,minor,patch) MAKE_STRING(major) "." MAKE_STRING(minor) "." MAKE_STRING(patch)

// Assemble the component version as string and as comma-separated list of integers
#define COMPONENT_VERSION MAKE_COMPONENT_VERSION(COMPONENT_VERSION_MAJOR,COMPONENT_VERSION_MINOR,COMPONENT_VERSION_PATCH)
#define COMPONENT_VERSION_NUMERIC COMPONENT_VERSION_MAJOR, COMPONENT_VERSION_MINOR, COMPONENT_VERSION_PATCH, 0
