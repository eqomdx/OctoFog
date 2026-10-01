#include "config.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <mutex>

namespace {
std::once_flag g_directoryOnce;
std::once_flag g_configOnce;
std::mutex g_configMutex;

FogConfig g_config;
std::wstring g_gameDirectory;

void InitializeDirectory() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_gameDirectory = std::filesystem::path(exePath).parent_path().wstring();
}

std::wstring IniPath() {
    std::call_once(g_directoryOnce, InitializeDirectory);
    return (std::filesystem::path(g_gameDirectory) / L"OctoFog.ini").wstring();
}

float ReadFloat(const wchar_t* ini, const wchar_t* key, float fallback) {
    wchar_t fallbackText[64]{};
    wchar_t value[64]{};
    swprintf_s(fallbackText, L"%.6f", fallback);
    GetPrivateProfileStringW(L"OctoFog", key, fallbackText, value, 64, ini);
    wchar_t* end = nullptr;
    const float parsed = std::wcstof(value, &end);
    return (end == value) ? fallback : parsed;
}

FogConfig LoadConfigFromDisk() {
    FogConfig cfg;
    const std::wstring ini = IniPath();

    cfg.enabled =
        GetPrivateProfileIntW(L"OctoFog", L"Enabled", 1, ini.c_str()) != 0;
    cfg.logging =
        GetPrivateProfileIntW(L"OctoFog", L"Logging", 1, ini.c_str()) != 0;
    cfg.diagnostics =
        GetPrivateProfileIntW(L"OctoFog", L"Diagnostics", 1, ini.c_str()) != 0;

    cfg.densityMultiplier = std::max(
        0.0f,
        ReadFloat(ini.c_str(), L"DensityMultiplier",
                  cfg.densityMultiplier));
    cfg.startMultiplier = std::max(
        0.0f,
        ReadFloat(ini.c_str(), L"StartMultiplier",
                  cfg.startMultiplier));
    cfg.endMultiplier = std::max(
        0.0f,
        ReadFloat(ini.c_str(), L"EndMultiplier",
                  cfg.endMultiplier));
    cfg.minimumStart = std::max(
        0.0f,
        ReadFloat(ini.c_str(), L"MinimumStart",
                  cfg.minimumStart));
    cfg.minimumEnd = std::max(
        cfg.minimumStart,
        ReadFloat(ini.c_str(), L"MinimumEnd",
                  cfg.minimumEnd));

    cfg.tintStrength = std::clamp(
        ReadFloat(ini.c_str(), L"TintStrength", cfg.tintStrength),
        0.0f, 1.0f);
    cfg.tintR = std::clamp(
        ReadFloat(ini.c_str(), L"TintR", cfg.tintR),
        0.0f, 1.0f);
    cfg.tintG = std::clamp(
        ReadFloat(ini.c_str(), L"TintG", cfg.tintG),
        0.0f, 1.0f);
    cfg.tintB = std::clamp(
        ReadFloat(ini.c_str(), L"TintB", cfg.tintB),
        0.0f, 1.0f);

    return cfg;
}

void InitializeConfig() {
    const FogConfig cfg = LoadConfigFromDisk();
    std::lock_guard<std::mutex> lock(g_configMutex);
    g_config = cfg;
}
}

FogConfig GetFogConfig() {
    std::call_once(g_configOnce, InitializeConfig);
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;
}

FogConfig ReloadFogConfig() {
    std::call_once(g_configOnce, InitializeConfig);
    const FogConfig cfg = LoadConfigFromDisk();
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        g_config = cfg;
    }
    return cfg;
}

const std::wstring& GetGameDirectory() {
    std::call_once(g_directoryOnce, InitializeDirectory);
    return g_gameDirectory;
}

void Log(const char* format, ...) {
    const auto cfg = GetFogConfig();
    if (!cfg.logging) return;

    const auto path =
        std::filesystem::path(GetGameDirectory()) / L"OctoFog.log";

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(
        file, "[%02u:%02u:%02u.%03u] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);

    std::fputc('\n', file);
    std::fclose(file);
}
