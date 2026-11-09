#pragma once

#include <string>

namespace picowp {

// Copy the legacy config only when the destination is absent.
bool MigrateLegacyConfig(const std::wstring& legacy_dir, const std::wstring& new_dir);

struct AppConfig {
    std::string video_path;
    int scaling_mode = 0;       // 0 = cover, 1 = fit, 2 = stretch
    int fps_cap = 30;           // 0 = native, otherwise 15/30/60
    bool pause_on_fullscreen = true;
    bool pause_on_maximized = true;
    bool pause_on_battery = false;
    bool run_on_startup = false;
};

class Config {
public:
    bool Load();
    bool Save() const;

    AppConfig& Get() { return m_config; }
    const AppConfig& Get() const { return m_config; }

    static std::wstring GetConfigDir();
    static std::wstring GetConfigFilePath();

private:
    AppConfig m_config;
};

extern Config g_config;

} // namespace picowp
