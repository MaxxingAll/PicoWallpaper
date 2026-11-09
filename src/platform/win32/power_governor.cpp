#include "power_governor.h"
#include "window_classes.h"

#include <cstdlib>
#include <dwmapi.h>
#include <shellapi.h>

namespace picowp {
namespace {

// GUID_CONSOLE_DISPLAY_STATE: Data[0] is 0 = off, 1 = on, 2 = dimmed.
constexpr GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0,
                                       {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

bool IsAppWindow(HWND window) {
    if (!window || !IsWindowVisible(window) || IsIconic(window)) return false;

    // Click-through overlays (cursor overlays, OSDs) can span the desktop but are not apps.
    constexpr LONG_PTR kClickThrough = WS_EX_LAYERED | WS_EX_TRANSPARENT;
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & kClickThrough) == kClickThrough) return false;

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
                                        sizeof(cloaked))) && cloaked) {
        return false;
    }

    wchar_t class_name[64] = {};
    GetClassNameW(window, class_name, static_cast<int>(_countof(class_name)));
    // Shell surfaces are not apps. The taskbar flyouts (XamlExplorerHostIslandWindow)
    // and Start/Search/notification hosts (Windows.UI.Core.CoreWindow) can be sized to
    // the whole monitor and looked fullscreen (traced on a 4K monitor, 2026-11-09).
    return wcscmp(class_name, L"Progman") != 0 &&
           wcscmp(class_name, L"WorkerW") != 0 &&
           wcscmp(class_name, L"Shell_TrayWnd") != 0 &&
           wcscmp(class_name, L"Shell_SecondaryTrayWnd") != 0 &&
           wcscmp(class_name, L"XamlExplorerHostIslandWindow") != 0 &&
           wcscmp(class_name, L"Windows.UI.Core.CoreWindow") != 0 &&
           wcscmp(class_name, kRenderClassName) != 0;
}

} // namespace

PowerGovernor::~PowerGovernor() {
    Shutdown();
}

bool PowerGovernor::Init(HWND message_window) {
    m_hwnd = message_window;
    if (m_hwnd) {
        WTSRegisterSessionNotification(m_hwnd, NOTIFY_FOR_THIS_SESSION);
        // Windows answers with the current display state right away.
        m_display_notification = RegisterPowerSettingNotification(
            m_hwnd, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
    m_on_battery = IsOnBattery();
    return true;
}

PowerState PowerGovernor::GetCurrentState(bool pause_fullscreen,
                                          bool pause_maximized,
                                          bool pause_on_battery) {
    // Screens turned off by the display timeout: nobody sees the wallpaper.
    if (m_locked || m_suspended || m_display_off) return PowerState::Sleeping;

    const ULONGLONG now = GetTickCount64();
    if (now - m_last_check < 250) return m_cached_state;
    m_last_check = now;
    m_on_battery = IsOnBattery();
    m_maximized_monitors.clear();
    if (pause_maximized) FindMaximizedMonitors();
    if (pause_fullscreen && IsFullscreenAppRunning()) {
        m_cached_state = PowerState::Paused;
    } else if (m_on_battery && pause_on_battery) {
        m_cached_state = PowerState::Paused;
    } else if (m_on_battery) {
        m_cached_state = PowerState::Reduced;
    } else {
        m_cached_state = PowerState::Active;
    }    return m_cached_state;
}

void PowerGovernor::HandleSessionChange(WPARAM event) {
    if (event == WTS_SESSION_LOCK) m_locked = true;
    if (event == WTS_SESSION_UNLOCK) {
        m_locked = false;
        m_last_check = 0;
    }
}

void PowerGovernor::HandlePowerChange(WPARAM event, LPARAM data) {
    if (event == PBT_POWERSETTINGCHANGE && data) {
        const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(data);
        if (IsEqualGUID(setting->PowerSetting, kConsoleDisplayState) &&
            setting->DataLength >= sizeof(DWORD)) {
            m_display_off = *reinterpret_cast<const DWORD*>(setting->Data) == 0;
        }
        return;
    }
    if (event == PBT_APMSUSPEND) m_suspended = true;
    if (event == PBT_APMRESUMEAUTOMATIC) m_suspended = false;
    if (event == PBT_APMPOWERSTATUSCHANGE || event == PBT_APMRESUMEAUTOMATIC) {
        m_on_battery = IsOnBattery();
        m_last_check = 0;
    }
}

bool PowerGovernor::IsFullscreenAppRunning() const {
    QUERY_USER_NOTIFICATION_STATE notification_state;
    if (SUCCEEDED(SHQueryUserNotificationState(&notification_state)) &&
        notification_state == QUNS_RUNNING_D3D_FULL_SCREEN) {
        return true;
    }

    HWND window = GetForegroundWindow();
    if (!IsAppWindow(window)) return false;
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    RECT rect = {};
    return GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
           GetWindowRect(window, &rect) &&
           rect.left <= monitor.rcMonitor.left && rect.top <= monitor.rcMonitor.top &&
           rect.right >= monitor.rcMonitor.right && rect.bottom >= monitor.rcMonitor.bottom;
}

void PowerGovernor::FindMaximizedMonitors() {
    EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL {
        if (!IsAppWindow(candidate) || !IsZoomed(candidate)) return TRUE;
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(candidate, MONITOR_DEFAULTTONEAREST), &monitor)) {
            return TRUE;
        }
        auto* monitors = reinterpret_cast<std::vector<RECT>*>(parameter);
        for (const RECT& known : *monitors) {
            if (EqualRect(&known, &monitor.rcMonitor)) return TRUE;
        }
        monitors->push_back(monitor.rcMonitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&m_maximized_monitors));
}

bool PowerGovernor::IsMaximizedOn(const RECT& monitor) const {
    for (const RECT& maximized : m_maximized_monitors) {
        if (EqualRect(&maximized, &monitor)) return true;
    }
    return false;
}

bool PowerGovernor::IsOnBattery() {
    SYSTEM_POWER_STATUS status = {};
    return GetSystemPowerStatus(&status) && status.ACLineStatus == 0;
}

void PowerGovernor::Shutdown() {
    if (m_display_notification) UnregisterPowerSettingNotification(m_display_notification);
    m_display_notification = nullptr;
    if (m_hwnd) WTSUnRegisterSessionNotification(m_hwnd);
    m_hwnd = nullptr;
}

} // namespace picowp
