#include "desktop_injector.h"

#define WM_APP_ATTACH (WM_APP + 11)

namespace picowp {

static UINT g_taskbar_created_msg = 0;

DesktopInjector::DesktopInjector() = default;

DesktopInjector::~DesktopInjector() {
    if (m_msg_receiver) {
        DestroyWindow(m_msg_receiver);
        m_msg_receiver = nullptr;
    }
}

HWND DesktopInjector::FindDesktopWorkerW() {
    // 1. Find Progman window with multi-stage lookup
    HWND progman = FindWindowW(L"Progman", L"Program Manager");
    if (!progman) {
        progman = FindWindowW(L"Progman", nullptr);
    }
    if (!progman) {
        progman = GetShellWindow();
    }
    if (!progman) {
        EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
            wchar_t cls[64] = {};
            if (GetClassNameW(hwnd, cls, 64) > 0 && wcscmp(cls, L"Progman") == 0) {
                *reinterpret_cast<HWND*>(lparam) = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&progman));
    }

    if (progman) {
        // 2. Send message 0x052C to Progman to spawn WorkerW behind desktop icons
        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, &result);
        SendMessageTimeoutW(progman, 0x052C, 0x0000000D, 1, SMTO_NORMAL, 1000, &result);

        // 3. Strategy A (classic, Win10/11): top-level WorkerW that is the sibling following
        //    the window containing SHELLDLL_DefView (behind desktop icons).
        HWND workerw = nullptr;
        EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
            HWND defview = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
            if (defview != nullptr) {
                HWND nextWorker = FindWindowExW(nullptr, hwnd, L"WorkerW", nullptr);
                if (nextWorker != nullptr) {
                    *reinterpret_cast<HWND*>(lparam) = nextWorker;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&workerw));
        if (workerw) return workerw;

        // 4. Strategy C (Win11 24H2+): Child WorkerW under Progman
        EnumChildWindows(progman, [](HWND hwnd, LPARAM lparam) -> BOOL {
            wchar_t cls[64] = {};
            if (GetClassNameW(hwnd, cls, 64) > 0 && wcscmp(cls, L"WorkerW") == 0) {
                if (FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) == nullptr) {
                    *reinterpret_cast<HWND*>(lparam) = hwnd;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&workerw));
        if (workerw) return workerw;

        // 5. Fallback: attach directly to Progman (desktop icons hidden case or early boot)
        return progman;
    }

    return nullptr;
}

bool DesktopInjector::Refresh() {
    m_workerw = FindDesktopWorkerW();
    return m_workerw != nullptr;
}

bool DesktopInjector::Place(HWND window, const RECT& screen_rect) {
    if (!window || !IsWindow(window) || !IsWindow(m_workerw)) return false;
    if (GetParent(window) != m_workerw) {
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        style = (style & ~WS_POPUP) | WS_CHILD;
        SetWindowLongPtrW(window, GWL_STYLE, style);
        SetParent(window, m_workerw);
        if (GetParent(window) != m_workerw) return false;
    }
    // The WorkerW spans the virtual screen; its client origin need not be (0, 0)
    // in screen coordinates, so map the monitor rectangle into it.
    POINT origin = {screen_rect.left, screen_rect.top};
    if (!ScreenToClient(m_workerw, &origin)) return false;
    return SetWindowPos(window, HWND_BOTTOM, origin.x, origin.y,
                        screen_rect.right - screen_rect.left,
                        screen_rect.bottom - screen_rect.top, SWP_NOACTIVATE) != FALSE;
}

void DesktopInjector::Release(HWND window) {
    if (!IsWindow(window)) return;
    ShowWindow(window, SW_HIDE);
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    style = (style & ~WS_CHILD) | WS_POPUP;
    SetWindowLongPtrW(window, GWL_STYLE, style);
    SetParent(window, nullptr);
}

HWND DesktopInjector::GetWorkerW() const {
    return m_workerw;
}

LRESULT CALLBACK DesktopInjector::MsgReceiverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_taskbar_created_msg && msg == g_taskbar_created_msg) {
        HWND target = reinterpret_cast<HWND>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (target && IsWindow(target)) {
            PostMessageW(target, WM_APP_ATTACH, 0, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void DesktopInjector::RegisterExplorerRestart(HWND targetHwnd) {
    m_taskbar_restart_msg = RegisterWindowMessageW(L"TaskbarCreated");
    g_taskbar_created_msg = m_taskbar_restart_msg;

    if (!m_msg_receiver) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MsgReceiverWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PicoWallpaper_MsgReceiver";
        RegisterClassExW(&wc);

        // Top-level hidden window placed far off-screen with layered transparent style (0 visual artifacts)
        m_msg_receiver = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
            L"PicoWallpaper_MsgReceiver",
            L"",
            WS_POPUP,
            -32000, -32000, 0, 0,
            nullptr, nullptr, wc.hInstance, nullptr
        );

        if (m_msg_receiver) {
            SetLayeredWindowAttributes(m_msg_receiver, 0, 0, LWA_ALPHA);
            ShowWindow(m_msg_receiver, SW_HIDE);
            SetWindowLongPtrW(m_msg_receiver, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(targetHwnd));
        }
    }
}

} // namespace picowp
