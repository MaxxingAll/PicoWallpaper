#pragma once

#include <windows.h>
#include <wtsapi32.h>

#include <vector>

namespace picowp {

enum class PowerState {
    Active,
    Reduced,
    Paused,
    Sleeping,
};

class PowerGovernor {
public:
    ~PowerGovernor();

    bool Init(HWND message_window);
    PowerState GetCurrentState(bool pause_fullscreen,
                               bool pause_maximized,
                               bool pause_on_battery);
    // A maximized app window is on this monitor (as of the last GetCurrentState
    // check with pause_maximized on). Only that monitor's video pauses.
    bool IsMaximizedOn(const RECT& monitor) const;
    void HandleSessionChange(WPARAM event);
    void HandlePowerChange(WPARAM event, LPARAM data);
    void Shutdown();

private:
    bool IsFullscreenAppRunning() const;
    void FindMaximizedMonitors();
    static bool IsOnBattery();

    std::vector<RECT> m_maximized_monitors;
    HWND m_hwnd = nullptr;
    HPOWERNOTIFY m_display_notification = nullptr;
    bool m_locked = false;
    bool m_suspended = false;
    bool m_display_off = false;
    bool m_on_battery = false;
    ULONGLONG m_last_check = 0;
    PowerState m_cached_state = PowerState::Active;
};

} // namespace picowp
