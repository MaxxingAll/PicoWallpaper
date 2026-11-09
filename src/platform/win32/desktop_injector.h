#pragma once
#include <windows.h>

namespace picowp {

class DesktopInjector {
public:
    DesktopInjector();
    ~DesktopInjector();

    // Find (or ask Explorer to create) the WorkerW behind the desktop icons.
    bool Refresh();

    // Make the window a child of the WorkerW covering one monitor's screen rectangle.
    bool Place(HWND window, const RECT& screen_rect);

    // Turn a placed window back into a hidden top-level window before it is destroyed.
    static void Release(HWND window);

    // Get WorkerW handle
    HWND GetWorkerW() const;

    // Register for Explorer restart notification ("TaskbarCreated")
    void RegisterExplorerRestart(HWND targetHwnd);

private:
    HWND m_workerw = nullptr;
    HWND m_msg_receiver = nullptr;
    UINT m_taskbar_restart_msg = 0;

    static HWND FindDesktopWorkerW();
    static LRESULT CALLBACK MsgReceiverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace picowp
