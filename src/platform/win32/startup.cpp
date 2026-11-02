#include "startup.h"

#include <windows.h>

#include <string>

namespace miniwp {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"LiteWallpaper-MF";

std::wstring StartupCommand() {
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable,
                                           static_cast<DWORD>(_countof(executable)));
    if (!length || length >= _countof(executable)) return {};
    return L"\"" + std::wstring(executable, length) + L"\" --startup";
}

} // namespace

bool SetRunAtStartup(bool enabled) {
    const std::wstring command = enabled ? StartupCommand() : std::wstring{};
    if (enabled && command.empty()) return false;
    HKEY key = nullptr;
    LSTATUS status = enabled
        ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                           KEY_SET_VALUE, nullptr, &key, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (!enabled && status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS) return false;
    if (enabled) {
        status = RegSetValueExW(key, kValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool ReconcileRunAtStartup(bool& enabled) {
    if (!enabled) return true;
    DWORD bytes = 0;
    constexpr DWORD flags = RRF_RT_REG_SZ;
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName,
                                  flags, nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        enabled = false;
        return true;
    }
    if (status != ERROR_SUCCESS || bytes == 0 || bytes % sizeof(wchar_t) != 0) return false;
    std::wstring existing(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName,
                          flags, nullptr, existing.data(), &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        enabled = false;
        return true;
    }
    if (status != ERROR_SUCCESS) return false;
    existing.resize(existing.find(L'\0'));
    const std::wstring expected = StartupCommand();
    if (expected.empty()) return false;
    return existing == expected || SetRunAtStartup(true);
}

} // namespace miniwp
