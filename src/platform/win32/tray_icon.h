#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#define WM_APP_TRAYICON (WM_APP + 1)

namespace picowp {

enum class TrayAction {
    MonitorVideo,
    MonitorRemove,
    PauseResume,
    TogglePauseFullscreen,
    TogglePauseMaximized,
    TogglePauseBattery,
    Fps15,
    Fps30,
    Fps60,
    FpsNative,
    ToggleStartup,
    OpenConfigFolder,
    About,
    Exit,
};

// One "Monitor N" submenu; video is the file name, empty for the Windows wallpaper.
struct TrayMonitor {
    std::wstring label;
    std::wstring video;
};

// monitor is the TrayMonitor index for MonitorVideo / MonitorRemove, otherwise -1.
using TrayCallback = void (*)(TrayAction action, int monitor);

class TrayIcon {
public:
    ~TrayIcon();

    bool Create(HWND window, TrayCallback callback);
    void UpdateState(bool paused, bool pause_fullscreen, bool pause_maximized,
                     bool pause_battery, int fps_cap, bool startup,
                     std::vector<TrayMonitor> monitors);
    void SetTooltip(const wchar_t* text);
    void HandleMessage(LPARAM event);
    void Destroy();

private:
    void ShowMenu();
    void RebuildMenu();

    NOTIFYICONDATAW m_icon = {};
    TrayCallback m_callback = nullptr;
    HMENU m_menu = nullptr;
    bool m_paused = false;
    bool m_pause_fullscreen = true;
    bool m_pause_maximized = true;
    bool m_pause_battery = false;
    int m_fps_cap = 30;
    bool m_startup = false;
    std::vector<TrayMonitor> m_monitors;
};

} // namespace picowp
