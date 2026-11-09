#include "monitors.h"

#include "core/number_format.h"

#include <algorithm>

namespace picowp {
namespace {

BOOL CALLBACK AddMonitor(HMONITOR handle, HDC, LPRECT, LPARAM parameter) {
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(parameter);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) return TRUE;

    MonitorInfo monitor;
    monitor.rect = info.rcMonitor;
    monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    monitor.number = DisplayNumber(info.szDevice);
    // The interface path names the physical monitor, so a saved choice stays
    // with that monitor when display numbers change.
    DISPLAY_DEVICEW device = {};
    device.cb = sizeof(device);
    monitor.id = EnumDisplayDevicesW(info.szDevice, 0, &device,
                                     EDD_GET_DEVICE_INTERFACE_NAME) && device.DeviceID[0]
                     ? device.DeviceID
                     : info.szDevice;
    monitors->push_back(std::move(monitor));
    return TRUE;
}

} // namespace

int DisplayNumber(const wchar_t* device_name) {
    if (!device_name) return 0;
    const wchar_t* end = device_name + wcslen(device_name);
    const wchar_t* digits = end;
    while (digits > device_name && digits[-1] >= L'0' && digits[-1] <= L'9') --digits;
    int number = 0;
    for (const wchar_t* digit = digits; digit < end && number < 100000; ++digit) {
        number = number * 10 + (*digit - L'0');
    }
    return number;
}

std::wstring MonitorLabel(const MonitorInfo& monitor) {
    std::wstring label = L"Monitor " + IntDecimal(monitor.number) + L" (" +
                         IntDecimal(monitor.rect.right - monitor.rect.left) + L" x " +
                         IntDecimal(monitor.rect.bottom - monitor.rect.top);
    if (monitor.primary) label += L", main";
    return label + L")";
}

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
    std::sort(monitors.begin(), monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.number != b.number ? a.number < b.number : a.rect.left < b.rect.left;
    });
    for (size_t i = 0; i < monitors.size(); ++i) {
        if (monitors[i].number == 0) monitors[i].number = static_cast<int>(i) + 1;
    }
    return monitors;
}

} // namespace picowp
