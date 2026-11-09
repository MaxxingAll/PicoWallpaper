#include "tray_icon.h"

#include <shellapi.h>

namespace picowp {
namespace {

enum MenuId : UINT {
    MenuPause = 1,
    MenuPauseFullscreen,
    MenuPauseMaximized,
    MenuPauseBattery,
    MenuFps15,
    MenuFps30,
    MenuFps60,
    MenuFpsNative,
    MenuStartup,
    MenuConfigFolder,
    MenuAbout,
    MenuExit,
};

// Monitor i uses kMonitorMenuBase + 2 * i (Video...) and + 1 (Remove).
constexpr UINT kMonitorMenuBase = 1000;
constexpr size_t kMaxMonitors = 32;

UINT Checked(bool value) {
    return MF_STRING | (value ? MF_CHECKED : MF_UNCHECKED);
}

// '&' marks a menu mnemonic; double it so file names show as written.
std::wstring MenuText(const std::wstring& text) {
    std::wstring result;
    for (wchar_t c : text) {
        if (c == L'&') result += L'&';
        result += c;
    }
    return result;
}

} // namespace

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create(HWND window, TrayCallback callback) {
    m_callback = callback;
    m_icon.cbSize = sizeof(m_icon);
    m_icon.hWnd = window;
    m_icon.uID = 1;
    m_icon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    m_icon.uCallbackMessage = WM_APP_TRAYICON;
    m_icon.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    if (!m_icon.hIcon) m_icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(m_icon.szTip, L"PicoWallpaper");
    RebuildMenu();
    return Shell_NotifyIconW(NIM_ADD, &m_icon) != FALSE;
}

void TrayIcon::UpdateState(bool paused, bool pause_fullscreen,
                           bool pause_maximized, bool pause_battery,
                           int fps_cap, bool startup,
                           std::vector<TrayMonitor> monitors) {
    m_monitors = std::move(monitors);
    m_paused = paused;
    m_pause_fullscreen = pause_fullscreen;
    m_pause_maximized = pause_maximized;
    m_pause_battery = pause_battery;
    m_fps_cap = fps_cap;
    m_startup = startup;
}

void TrayIcon::SetTooltip(const wchar_t* text) {
    if (!text) return;
    wcsncpy_s(m_icon.szTip, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &m_icon);
}

void TrayIcon::HandleMessage(LPARAM event) {
    const UINT message = LOWORD(event);
    if (message == WM_CONTEXTMENU || message == WM_RBUTTONUP ||
        message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK) {
        ShowMenu();
    }
}

void TrayIcon::RebuildMenu() {
    if (m_menu) DestroyMenu(m_menu);
    m_menu = CreatePopupMenu();
    for (size_t i = 0; i < m_monitors.size() && i < kMaxMonitors; ++i) {
        const TrayMonitor& monitor = m_monitors[i];
        const UINT video_id = kMonitorMenuBase + static_cast<UINT>(i) * 2;
        HMENU monitor_menu = CreatePopupMenu();
        const std::wstring state = monitor.video.empty() ? L"Windows wallpaper"
                                                         : MenuText(monitor.video);
        AppendMenuW(monitor_menu, MF_STRING | MF_GRAYED, 0, state.c_str());
        AppendMenuW(monitor_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(monitor_menu, MF_STRING, video_id, L"Video...");
        AppendMenuW(monitor_menu, MF_STRING | (monitor.video.empty() ? MF_GRAYED : 0),
                    video_id + 1, L"Remove");
        AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(monitor_menu),
                    MenuText(monitor.label).c_str());
    }
    AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_menu, MF_STRING, MenuPause, m_paused ? L"Resume" : L"Pause");
    AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);

    HMENU pause_menu = CreatePopupMenu();
    AppendMenuW(pause_menu, Checked(m_pause_fullscreen), MenuPauseFullscreen, L"Fullscreen apps");
    AppendMenuW(pause_menu, Checked(m_pause_maximized), MenuPauseMaximized, L"Maximized apps");
    AppendMenuW(pause_menu, Checked(m_pause_battery), MenuPauseBattery, L"On battery");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pause_menu), L"Pause when");

    HMENU fps_menu = CreatePopupMenu();
    AppendMenuW(fps_menu, Checked(m_fps_cap == 15), MenuFps15, L"15 FPS");
    AppendMenuW(fps_menu, Checked(m_fps_cap == 30), MenuFps30, L"30 FPS");
    AppendMenuW(fps_menu, Checked(m_fps_cap == 60), MenuFps60, L"60 FPS");
    AppendMenuW(fps_menu, Checked(m_fps_cap == 0), MenuFpsNative, L"Native");
    AppendMenuW(m_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(fps_menu), L"FPS cap");

    AppendMenuW(m_menu, Checked(m_startup), MenuStartup, L"Run at startup");
    AppendMenuW(m_menu, MF_STRING, MenuConfigFolder, L"Open config folder");
    AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_menu, MF_STRING, MenuAbout, L"About PicoWallpaper");
    AppendMenuW(m_menu, MF_STRING, MenuExit, L"Exit");
}

void TrayIcon::ShowMenu() {
    RebuildMenu();
    POINT point = {};
    GetCursorPos(&point);
    SetForegroundWindow(m_icon.hWnd);
    const UINT command = TrackPopupMenu(
        m_menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        point.x, point.y, 0, m_icon.hWnd, nullptr);
    PostMessageW(m_icon.hWnd, WM_NULL, 0, 0);
    if (!m_callback || command == 0) return;

    if (command >= kMonitorMenuBase && command < kMonitorMenuBase + kMaxMonitors * 2) {
        const UINT offset = command - kMonitorMenuBase;
        m_callback(offset % 2 ? TrayAction::MonitorRemove : TrayAction::MonitorVideo,
                   static_cast<int>(offset / 2));
        return;
    }
    switch (command) {
        case MenuPause: m_callback(TrayAction::PauseResume, -1); break;
        case MenuPauseFullscreen: m_callback(TrayAction::TogglePauseFullscreen, -1); break;
        case MenuPauseMaximized: m_callback(TrayAction::TogglePauseMaximized, -1); break;
        case MenuPauseBattery: m_callback(TrayAction::TogglePauseBattery, -1); break;
        case MenuFps15: m_callback(TrayAction::Fps15, -1); break;
        case MenuFps30: m_callback(TrayAction::Fps30, -1); break;
        case MenuFps60: m_callback(TrayAction::Fps60, -1); break;
        case MenuFpsNative: m_callback(TrayAction::FpsNative, -1); break;
        case MenuStartup: m_callback(TrayAction::ToggleStartup, -1); break;
        case MenuConfigFolder: m_callback(TrayAction::OpenConfigFolder, -1); break;
        case MenuAbout: m_callback(TrayAction::About, -1); break;
        case MenuExit: m_callback(TrayAction::Exit, -1); break;
    }
}

void TrayIcon::Destroy() {
    if (m_icon.hWnd) Shell_NotifyIconW(NIM_DELETE, &m_icon);
    m_icon.hWnd = nullptr;
    if (m_menu) DestroyMenu(m_menu);
    m_menu = nullptr;
}

} // namespace picowp
