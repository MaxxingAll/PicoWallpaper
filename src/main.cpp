#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/number_format.h"
#include "core/playback_clock.h"
#include "decoder/mf_decoder.h"
#include "platform/win32/d3d11_presenter.h"
#include "platform/win32/desktop_injector.h"
#include "platform/win32/monitors.h"
#include "platform/win32/power_governor.h"
#include "platform/win32/startup.h"
#include "platform/win32/tray_icon.h"
#include "platform/win32/window_classes.h"

#define WM_APP_ATTACH (WM_APP + 11)
#define WM_APP_PLAY (WM_APP + 12)

using namespace picowp;

namespace {

// One monitor that plays a video. Monitors without a video have no Screen, so
// the Windows wallpaper stays visible there.
struct Screen {
    MonitorInfo monitor;
    std::string video_path;
    HWND window = nullptr;
    D3D11Presenter presenter;
    MFDecoder decoder;
    PlaybackClock clock;
    VideoFrame frame;
    VideoInfo video_info;
    bool decoder_open = false;
    bool suspended = false;
    bool has_presented_frame = false;
    double playback_seconds = 0.0;
    uint64_t decoded_frames = 0;
    double decode_accumulator = 0.0;
};

// Playback position of a destroyed screen, used when the same monitor and video return.
struct ResumePoint {
    std::wstring monitor_id;
    std::string video_path;
    double seconds = 0.0;
};

D3D11Device g_device;
DesktopInjector g_injector;
PowerGovernor g_governor;
TrayIcon g_tray;

std::vector<MonitorInfo> g_monitors;
std::vector<std::unique_ptr<Screen>> g_screens;
std::vector<ResumePoint> g_resume;
std::vector<std::string> g_failed_videos;
std::vector<std::wstring> g_pending_errors;

HWND g_control = nullptr;
bool g_running = true;
bool g_manual_paused = false;
bool g_device_lost = false;
bool g_syncing = false;
bool g_playing = false;
bool g_screens_dirty = false;
ULONGLONG g_last_attach_attempt = 0;

constexpr wchar_t kControlClassName[] = L"PicoWallpaperControl";
LONG volatile g_play_posted = 0;

// Menus, dialogs and message boxes run their own message loop, so the main loop
// stops. A thread-pool timer posts WM_APP_PLAY to keep playback going: their loops
// dispatch it, and posted messages come before mouse input (WM_TIMER is starved
// while the mouse moves over a menu). One message is pending at most. The timer
// exists only while a menu or dialog is open, so normal playback has no extra wake-ups.
VOID CALLBACK PostPlay(PVOID, BOOLEAN) {
    if (InterlockedExchange(&g_play_posted, 1) == 0 &&
        !PostMessageW(g_control, WM_APP_PLAY, 0, 0)) {
        InterlockedExchange(&g_play_posted, 0);
    }
}

struct ModalPlayback {
    ModalPlayback() {
        if (g_control && !CreateTimerQueueTimer(&m_timer, nullptr, PostPlay, nullptr, 0, 10,
                                                WT_EXECUTEDEFAULT)) {
            m_timer = nullptr;
        }
    }
    ~ModalPlayback() {
        // Waits for a running callback; callbacks only post, so this cannot block long.
        if (m_timer) DeleteTimerQueueTimer(nullptr, m_timer, INVALID_HANDLE_VALUE);
    }
    ModalPlayback(const ModalPlayback&) = delete;
    ModalPlayback& operator=(const ModalPlayback&) = delete;

private:
    HANDLE m_timer = nullptr;
};

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           value.c_str(), -1, nullptr, 0);
    if (count <= 1) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
                        result.data(), count);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), count,
                        nullptr, nullptr);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}

std::wstring FileName(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

std::string VideoFor(const std::wstring& monitor_id) {
    const auto& videos = g_config.Get().monitor_videos;
    const auto found = videos.find(monitor_id);
    return found == videos.end() ? std::string() : found->second;
}

const MonitorInfo* FindMonitor(const std::wstring& monitor_id) {
    for (const MonitorInfo& monitor : g_monitors) {
        if (monitor.id == monitor_id) return &monitor;
    }
    return nullptr;
}

Screen* FindScreen(const std::wstring& monitor_id) {
    for (auto& screen : g_screens) {
        if (screen->monitor.id == monitor_id) return screen.get();
    }
    return nullptr;
}

bool IsFailedVideo(const std::string& path) {
    return std::find(g_failed_videos.begin(), g_failed_videos.end(), path) !=
           g_failed_videos.end();
}

int Width(const RECT& rect) { return (std::max)(1L, rect.right - rect.left); }
int Height(const RECT& rect) { return (std::max)(1L, rect.bottom - rect.top); }

void UpdateTrayState() {
    const AppConfig& config = g_config.Get();
    std::vector<TrayMonitor> monitors;
    std::wstring tooltip = L"PicoWallpaper";
    for (const MonitorInfo& monitor : g_monitors) {
        const std::wstring video = FileName(Utf8ToWide(VideoFor(monitor.id)));
        monitors.push_back({MonitorLabel(monitor), video});
        if (!video.empty()) {
            tooltip += L"\nMonitor " + IntDecimal(monitor.number) + L": " + video;
        }
    }
    g_tray.UpdateState(g_manual_paused, config.pause_on_fullscreen,
                       config.pause_on_maximized, config.pause_on_battery,
                       config.fps_cap, config.run_on_startup, std::move(monitors));
    g_tray.SetTooltip(tooltip.c_str());
}

// Errors are shown from the main loop, never while g_screens is being iterated:
// a message box pumps messages, and display changes rebuild the screen list.
void QueueDecoderError(const Screen& screen, const std::string& fallback) {
    std::string message = screen.decoder.GetLastError();
    if (message.empty()) message = fallback;
    g_pending_errors.push_back(L"Monitor " + IntDecimal(screen.monitor.number) + L": " +
                               Utf8ToWide(message));
}

void ShowPendingErrors() {
    if (g_pending_errors.empty()) return;
    ModalPlayback playback;
    while (!g_pending_errors.empty()) {
        const std::wstring message = g_pending_errors.front();
        g_pending_errors.erase(g_pending_errors.begin());
        MessageBoxW(nullptr, message.c_str(), L"PicoWallpaper", MB_OK | MB_ICONERROR);
    }
}

bool CheckDeviceLost() {
    ID3D11Device* device = g_device.Get();
    if (device && device->GetDeviceRemovedReason() != S_OK) {
        if (!g_device_lost) g_last_attach_attempt = 0;
        g_device_lost = true;
    }
    return g_device_lost;
}

int NativeFps(const Screen& screen) {
    if (screen.video_info.fps < 1.0 || screen.video_info.fps > 240.0) return 30;
    return static_cast<int>(std::lround(screen.video_info.fps));
}

int EffectiveFpsCap(const Screen& screen, PowerState state) {
    int cap = g_config.Get().fps_cap;
    if (cap == 0) cap = NativeFps(screen);
    if (state == PowerState::Reduced) cap = (std::min)(cap, 15);
    cap = (std::min)(cap, NativeFps(screen));
    return (std::max)(1, cap);
}

void CloseVideo(Screen& screen) {
    screen.presenter.ReleaseFrameResources();
    screen.frame = {};
    screen.decoder.Close();
    screen.decoder_open = false;
}

bool OpenVideo(Screen& screen, double seek_seconds, bool show_error) {
    CloseVideo(screen);
    screen.video_info = {};
    if (!screen.window || !g_device.Get()) return false;
    // A reset can occur while suspended, when neither Decode nor Present runs.
    if (CheckDeviceLost()) return false;

    const std::string& path = screen.video_path;
    if (IsFailedVideo(path)) return false;
    const std::wstring wide_path = Utf8ToWide(path);
    const DWORD attributes = GetFileAttributesW(wide_path.c_str());
    if (wide_path.empty() || attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    if (!screen.decoder.Open(path.c_str(), g_device.Get())) {
        if (CheckDeviceLost()) return false;
        // Device failures stay on the recovery path. Ordinary file failures
        // must not cause a healthy graphics device to be rebuilt.
        if (show_error) {
            g_failed_videos.push_back(path);
            QueueDecoderError(screen, "Windows could not open this video.");
        }
        return false;
    }

    screen.video_info = screen.decoder.GetInfo();
    if (seek_seconds > 0.0) screen.decoder.SeekTo(seek_seconds);
    screen.playback_seconds = (std::max)(0.0, seek_seconds);
    screen.decoded_frames = static_cast<uint64_t>(screen.playback_seconds * NativeFps(screen));
    screen.decode_accumulator = 0.0;
    screen.clock.SetTargetFPS(g_config.Get().fps_cap == 0 ? NativeFps(screen)
                                                          : g_config.Get().fps_cap);
    screen.clock.Reset();
    screen.decoder_open = true;
    screen.suspended = false;
    return true;
}

bool EnterSuspend(Screen& screen) {
    if (screen.suspended) return false;
    CloseVideo(screen);
    screen.suspended = true;
    return true;
}

void LeaveSuspend(Screen& screen) {
    if (!screen.suspended) return;
    screen.suspended = false;
    OpenVideo(screen, screen.playback_seconds, true);
}

bool RenderNextFrame(Screen& screen, PowerState power_state) {
    if (!screen.window || !screen.decoder_open || g_device_lost) return false;
    const int cap = EffectiveFpsCap(screen, power_state);
    if (screen.clock.GetTargetFPS() != cap) screen.clock.SetTargetFPS(cap);
    if (!screen.clock.ShouldRenderFrame()) return false;

    const double native_fps = screen.video_info.fps > 0.0 ? screen.video_info.fps : 30.0;
    screen.decode_accumulator += native_fps / cap;
    int frames = static_cast<int>(std::floor(screen.decode_accumulator));
    screen.decode_accumulator -= frames;
    frames = std::clamp(frames, 1, 16);
    bool decoded = false;
    for (int i = 0; i < frames; ++i) {
        if (!screen.decoder.DecodeNextFrame(screen.frame)) break;
        decoded = true;
        ++screen.decoded_frames;
    }
    if (!decoded || !screen.frame.texture) {
        if (CheckDeviceLost()) return false;
        if (!screen.decoder.GetLastError().empty()) {
            g_failed_videos.push_back(screen.video_path);
            QueueDecoderError(screen, "Video decoding failed.");
            CloseVideo(screen);
        }
        return false;
    }

    screen.playback_seconds =
        screen.frame.pts >= 0 ? static_cast<double>(screen.frame.pts) / 1000000.0
                              : static_cast<double>(screen.decoded_frames) / native_fps;
    if (screen.video_info.duration_seconds > 0.0) {
        screen.playback_seconds =
            std::fmod(screen.playback_seconds, screen.video_info.duration_seconds);
    }
    if (!screen.presenter.RenderFrame(screen.frame, g_config.Get().scaling_mode)) {
        CheckDeviceLost();
        return false;
    }
    const HRESULT present_result = screen.presenter.Present(0);
    if (present_result == DXGI_ERROR_DEVICE_REMOVED ||
        present_result == DXGI_ERROR_DEVICE_RESET) {
        g_device_lost = true;
    }
    if (FAILED(present_result)) return false;
    if (!screen.has_presented_frame) {
        screen.has_presented_frame = true;
        ShowWindow(screen.window, SW_SHOWNA);
    }
    return true;
}

std::wstring ChooseVideoFile() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    const COMDLG_FILTERSPEC filters[] = {
        {L"Video files", L"*.mp4;*.webm;*.mkv;*.mov;*.avi"},
        {L"All files", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(_countof(filters)), filters);
    dialog->SetTitle(L"Choose wallpaper video");
    if (FAILED(dialog->Show(nullptr))) return {};

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return {};
    PWSTR selected = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) return {};
    std::wstring result(selected);
    CoTaskMemFree(selected);
    return result;
}

// Repaint the Windows wallpaper where a screen window was removed.
void RedrawDesktop() {
    HWND desktop_window = g_injector.GetWorkerW();
    if (desktop_window && IsWindow(desktop_window)) {
        RedrawWindow(desktop_window, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void DestroyScreen(size_t index) {
    Screen& screen = *g_screens[index];
    g_resume.push_back({screen.monitor.id, screen.video_path, screen.playback_seconds});
    CloseVideo(screen);
    screen.presenter.Cleanup();
    if (screen.window && IsWindow(screen.window)) {
        const HWND window = screen.window;
        DesktopInjector::Release(window);
        DestroyWindow(window);
    }
    g_screens.erase(g_screens.begin() + static_cast<std::ptrdiff_t>(index));
}

double TakeResumePoint(const std::wstring& monitor_id, const std::string& path) {
    for (auto it = g_resume.begin(); it != g_resume.end(); ++it) {
        if (it->monitor_id == monitor_id && it->video_path == path) {
            const double seconds = it->seconds;
            g_resume.erase(it);
            return seconds;
        }
    }
    return 0.0;
}

bool CreateScreen(const MonitorInfo& monitor, const std::string& path, bool show_error) {
    auto created = std::make_unique<Screen>();
    created->monitor = monitor;
    created->video_path = path;
    created->playback_seconds = TakeResumePoint(monitor.id, path);
    const int width = Width(monitor.rect);
    const int height = Height(monitor.rect);
    created->window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kRenderClassName, L"PicoWallpaper",
        WS_POPUP, monitor.rect.left, monitor.rect.top, width, height, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!created->window) {
        g_resume.push_back({monitor.id, path, created->playback_seconds});
        return false;
    }
    g_screens.push_back(std::move(created));
    Screen& screen = *g_screens.back();
    if (!g_injector.Place(screen.window, monitor.rect) ||
        !screen.presenter.Init(g_device, screen.window, width, height)) {
        CheckDeviceLost();
        DestroyScreen(g_screens.size() - 1);
        return false;
    }
    // A file that cannot be opened keeps its hidden window; the wallpaper shows.
    OpenVideo(screen, screen.playback_seconds, show_error);
    return true;
}

// Match the screen windows to the connected monitors and the configured videos.
// Called at startup, on display changes, Explorer restarts and tray changes.
void SyncScreens(bool show_error = true) {
    if (g_syncing) {
        g_screens_dirty = true;
        return;
    }
    g_syncing = true;
    g_screens_dirty = false;
    g_monitors = EnumerateMonitors();
    const bool attached = g_injector.Refresh();

    bool removed = false;
    for (size_t i = g_screens.size(); i-- > 0;) {
        Screen& screen = *g_screens[i];
        const MonitorInfo* monitor = FindMonitor(screen.monitor.id);
        if (attached && monitor && screen.window &&
            VideoFor(monitor->id) == screen.video_path &&
            g_injector.Place(screen.window, monitor->rect)) {
            screen.monitor = *monitor;
            if (screen.presenter.Resize(Width(monitor->rect), Height(monitor->rect))) {
                screen.has_presented_frame = false;
                ShowWindow(screen.window, SW_HIDE);
            }
            continue;
        }
        DestroyScreen(i);
        removed = true;
    }

    if (attached && g_device.Get() && !g_device_lost) {
        for (const MonitorInfo& monitor : g_monitors) {
            const std::string path = VideoFor(monitor.id);
            if (path.empty() || FindScreen(monitor.id)) continue;
            if (!CreateScreen(monitor, path, show_error)) g_screens_dirty = true;
        }
    } else {
        g_screens_dirty = true;
    }
    if (removed) RedrawDesktop();
    if (!g_screens_dirty) g_resume.clear();
    g_syncing = false;
    UpdateTrayState();
}

// Every screen window is still a child of the current WorkerW, on its monitor.
bool ScreensPlaced() {
    const HWND workerw = g_injector.GetWorkerW();
    for (const auto& screen : g_screens) {
        RECT rect = {};
        if (!screen->window || !IsWindow(workerw) || GetParent(screen->window) != workerw ||
            !GetWindowRect(screen->window, &rect) || !EqualRect(&rect, &screen->monitor.rect)) {
            return false;
        }
    }
    return true;
}

void RecoverDevice() {
    for (size_t i = g_screens.size(); i-- > 0;) DestroyScreen(i);
    g_device.Cleanup();
    if (!g_device.Create()) {
        g_device_lost = true;
        return;
    }
    g_device_lost = false;
    // A missing or undecodable file alone does not mean the GPU was lost.
    SyncScreens(false);
}

void AssignVideo(int monitor_index) {
    if (monitor_index < 0 || static_cast<size_t>(monitor_index) >= g_monitors.size()) return;
    const std::wstring monitor_id = g_monitors[static_cast<size_t>(monitor_index)].id;
    const std::wstring selected = ChooseVideoFile();
    if (selected.empty()) return;

    const std::string path = WideToUtf8(selected);
    std::erase(g_failed_videos, path);
    g_config.Get().monitor_videos[monitor_id] = path;
    g_config.Save();
    g_manual_paused = false;
    if (Screen* screen = FindScreen(monitor_id)) {
        screen->video_path = path;
        if (!OpenVideo(*screen, 0.0, true)) {
            screen->has_presented_frame = false;
            ShowWindow(screen->window, SW_HIDE);
        }
        UpdateTrayState();
    } else {
        SyncScreens();
    }
}

void RemoveVideo(int monitor_index) {
    if (monitor_index < 0 || static_cast<size_t>(monitor_index) >= g_monitors.size()) return;
    g_config.Get().monitor_videos.erase(g_monitors[static_cast<size_t>(monitor_index)].id);
    g_config.Save();
    SyncScreens();
}

void ShowAbout() {
    MessageBoxW(g_control,
                L"PicoWallpaper " PICOWP_VERSION L"\n\n"
                L"A small video wallpaper for Windows 10 and 11.\n"
                L"GPL-3.0 fork of f4rdani/LiteWallpaper.\n\n"
                L"https://github.com/MaxxingAll/PicoWallpaper",
                L"About PicoWallpaper", MB_OK | MB_ICONINFORMATION);
}

void OnTrayAction(TrayAction action, int monitor) {
    AppConfig& config = g_config.Get();
    switch (action) {
        case TrayAction::MonitorVideo:
            AssignVideo(monitor);
            break;
        case TrayAction::MonitorRemove:
            RemoveVideo(monitor);
            break;
        case TrayAction::PauseResume:
            g_manual_paused = !g_manual_paused;
            break;
        case TrayAction::TogglePauseFullscreen:
            config.pause_on_fullscreen = !config.pause_on_fullscreen;
            g_config.Save();
            break;
        case TrayAction::TogglePauseMaximized:
            config.pause_on_maximized = !config.pause_on_maximized;
            g_config.Save();
            break;
        case TrayAction::TogglePauseBattery:
            config.pause_on_battery = !config.pause_on_battery;
            g_config.Save();
            break;
        case TrayAction::Fps15: config.fps_cap = 15; g_config.Save(); break;
        case TrayAction::Fps30: config.fps_cap = 30; g_config.Save(); break;
        case TrayAction::Fps60: config.fps_cap = 60; g_config.Save(); break;
        case TrayAction::FpsNative: config.fps_cap = 0; g_config.Save(); break;
        case TrayAction::ToggleStartup: {
            const bool requested = !config.run_on_startup;
            if (SetRunAtStartup(requested)) {
                config.run_on_startup = requested;
                g_config.Save();
            }
            break;
        }
        case TrayAction::OpenConfigFolder:
            ShellExecuteW(g_control, L"open", Config::GetConfigDir().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case TrayAction::About:
            ShowAbout();
            break;
        case TrayAction::Exit:
            PostMessageW(g_control, WM_CLOSE, 0, 0);
            break;
    }
    UpdateTrayState();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
    if (message == WM_DESTROY) {
        for (auto& screen : g_screens) {
            if (screen->window == window) screen->window = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// One playback step for every screen: pause/resume and render the frames that are
// due. Returns the milliseconds until the next frame; adds frame-latency handles.
DWORD PlayScreens(HANDLE* frame_waits, DWORD capacity, DWORD& wait_count) {
    if (g_playing || g_syncing) return 200;
    g_playing = true;
    const AppConfig& config = g_config.Get();
    const PowerState power = g_governor.GetCurrentState(
        config.pause_on_fullscreen, config.pause_on_maximized,
        config.pause_on_battery);
    const bool all_paused = g_manual_paused || power == PowerState::Paused ||
                            power == PowerState::Sleeping;

    DWORD wait_ms = 200;
    bool suspended_now = false;
    bool all_suspended = true;
    for (auto& screen : g_screens) {
        // A maximized window pauses only the monitor it is on.
        if (all_paused || (config.pause_on_maximized &&
                           g_governor.IsMaximizedOn(screen->monitor.rect))) {
            suspended_now |= EnterSuspend(*screen);
            continue;
        }
        all_suspended = false;
        LeaveSuspend(*screen);
        if (!screen->window || !screen->decoder_open || g_device_lost) continue;
        RenderNextFrame(*screen, power);
        wait_ms = (std::min)(wait_ms,
                             static_cast<DWORD>((std::min)(16u, screen->clock.GetSleepDurationMs())));
        HANDLE frame_wait = screen->presenter.GetWaitableObject();
        if (frame_wait && wait_count < capacity) frame_waits[wait_count++] = frame_wait;
    }
    // Trim only when nothing plays; a playing screen would fault the pages back in.
    if (suspended_now && all_suspended) {
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                                 static_cast<SIZE_T>(-1));
    }
    g_playing = false;
    return wait_ms;
}

LRESULT CALLBACK ControlProc(HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
    if (message == WM_APP_PLAY) {
        InterlockedExchange(&g_play_posted, 0);
        DWORD unused = 0;
        PlayScreens(nullptr, 0, unused);
        return 0;
    }
    if (message == WM_APP_TRAYICON) {
        // The tray menu and the dialogs it opens run modal loops on this thread.
        ModalPlayback playback;
        g_tray.HandleMessage(lparam);
        return 0;
    }
    if (message == WM_APP_ATTACH) {
        g_last_attach_attempt = 0;
        g_tray.Destroy();
        g_tray.Create(window, OnTrayAction);
        SyncScreens();
        return 0;
    }
    switch (message) {
        case WM_WTSSESSION_CHANGE:
            g_governor.HandleSessionChange(wparam);
            return 0;
        case WM_POWERBROADCAST:
            g_governor.HandlePowerChange(wparam);
            return TRUE;
        case WM_DISPLAYCHANGE:
            SyncScreens();
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (window == g_control) g_control = nullptr;
            g_running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    // Overlay and monitoring tools (RTSS, Discord, Steam) inject DLLs into every
    // D3D process; a wallpaper needs none of them. Load only Microsoft-signed
    // images: Windows, WHQL GPU drivers and Store codec extensions still load.
    // Without SEM_FAILCRITICALERRORS each blocked DLL raises a modal "Bad Image"
    // box that stalls the thread loading it (seen with RTSSHooks64.dll).
    SetErrorMode(SEM_FAILCRITICALERRORS);
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY signature_policy = {};
    signature_policy.MicrosoftSignedOnly = 1;
    SetProcessMitigationPolicy(ProcessSignaturePolicy, &signature_policy,
                               sizeof(signature_policy));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize_com = SUCCEEDED(com_result);

    HANDLE instance_mutex = CreateMutexW(nullptr, TRUE,
                                         L"Local\\PicoWallpaper.Instance");
    if (!instance_mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (instance_mutex) CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 0;
    }

    if (!g_config.Load()) {
        MessageBoxW(nullptr, L"Could not load or migrate the configuration. Check access to the configuration folder and try again.",
                    L"PicoWallpaper", MB_OK | MB_ICONERROR);
        CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 1;
    }
    bool startup_enabled = g_config.Get().run_on_startup;
    if (MigrateLegacyRunAtStartup(startup_enabled) &&
        ReconcileRunAtStartup(startup_enabled) &&
        startup_enabled != g_config.Get().run_on_startup) {
        g_config.Get().run_on_startup = startup_enabled;
        g_config.Save();
    }
    std::wstring command_line_video;
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments) {
        for (int i = 1; i + 1 < argument_count; ++i) {
            if (_wcsicmp(arguments[i], L"--video") == 0) {
                command_line_video = arguments[++i];
            }
        }
        LocalFree(arguments);
    }

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    window_class.hIconSm = window_class.hIcon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kRenderClassName;
    if (!RegisterClassExW(&window_class)) {
        CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 1;
    }

    WNDCLASSEXW control_class = window_class;
    control_class.lpfnWndProc = ControlProc;
    control_class.lpszClassName = kControlClassName;
    if (!RegisterClassExW(&control_class)) {
        CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 1;
    }

    g_control = CreateWindowExW(
        WS_EX_TOOLWINDOW, kControlClassName, L"PicoWallpaper Control",
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_control) {
        CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 1;
    }

    if (!g_device.Create()) {
        MessageBoxW(nullptr, L"Direct3D 11 video presentation is unavailable.",
                    L"PicoWallpaper", MB_OK | MB_ICONERROR);
        DestroyWindow(g_control);
        CloseHandle(instance_mutex);
        if (uninitialize_com) CoUninitialize();
        return 1;
    }

    // Older versions had one video for the whole desktop; it moves to the main
    // monitor. --video also sets the main monitor's video.
    g_monitors = EnumerateMonitors();
    const MonitorInfo* main_monitor = g_monitors.empty() ? nullptr : &g_monitors.front();
    for (const MonitorInfo& monitor : g_monitors) {
        if (monitor.primary) main_monitor = &monitor;
    }
    if (main_monitor) {
        bool changed = AssignLegacyVideo(g_config.Get(), main_monitor->id);
        if (!command_line_video.empty()) {
            g_config.Get().monitor_videos[main_monitor->id] = WideToUtf8(command_line_video);
            changed = true;
        }
        if (changed) g_config.Save();
    }

    g_injector.RegisterExplorerRestart(g_control);
    g_governor.Init(g_control);
    g_tray.Create(g_control, OnTrayAction);
    SyncScreens();

    MSG message = {};
    while (g_running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) g_running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!g_running) break;
        ShowPendingErrors();

        const ULONGLONG now = GetTickCount64();
        if (now - g_last_attach_attempt >= 5000) {
            g_last_attach_attempt = now;
            CheckDeviceLost();
            if (g_device_lost) {
                RecoverDevice();
            } else if (g_screens_dirty || !ScreensPlaced()) {
                SyncScreens();
            }
        }

        HANDLE frame_waits[MAXIMUM_WAIT_OBJECTS - 1] = {};
        DWORD wait_count = 0;
        DWORD wait_ms = PlayScreens(frame_waits, _countof(frame_waits), wait_count);
        if (wait_ms == 0) wait_ms = 1;
        MsgWaitForMultipleObjectsEx(wait_count, frame_waits, wait_ms, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }

    for (size_t i = g_screens.size(); i-- > 0;) DestroyScreen(i);
    RedrawDesktop();
    g_tray.Destroy();
    g_governor.Shutdown();
    g_device.Cleanup();
    if (IsWindow(g_control)) DestroyWindow(g_control);
    ReleaseMutex(instance_mutex);
    CloseHandle(instance_mutex);
    if (uninitialize_com) CoUninitialize();
    return 0;
}
