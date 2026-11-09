#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace picowp {

struct MonitorInfo {
    std::wstring id;    // config.ini key: monitor device interface path, or the GDI name
    int number = 0;     // Windows display number (\\.\DISPLAYn)
    RECT rect = {};     // physical pixels, virtual-screen coordinates
    bool primary = false;
};

// Trailing number of a GDI device name such as \\.\DISPLAY2; 0 when there is none.
int DisplayNumber(const wchar_t* device_name);

// Tray text, e.g. "Monitor 1 (3840 x 2160, main)".
std::wstring MonitorLabel(const MonitorInfo& monitor);

// Connected monitors, ordered by display number.
std::vector<MonitorInfo> EnumerateMonitors();

} // namespace picowp
