#include "config.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace {
std::once_flag g_once;
FogConfig g_config;
std::wstring g_gameDirectory;

float ReadFloat(const wchar_t* ini, const wchar_t* key, float fallback) {
    wchar_t fallbackText[64]{};
    wchar_t value[64]{};
    swprintf_s(fallbackText, L"%.6f", fallback);
    GetPrivateProfileStringW(L"OctoFog", key, fallbackText, value, 64, ini);
    wchar_t* end = nullptr;
    const float parsed = std::wcstof(value, &end);
    return (end == value) ? fallback : parsed;
}

void Initialize() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_gameDirectory = std::filesystem::path(exePath).parent_path().wstring();

    const std::wstring ini = (std::filesystem::path(g_gameDirectory) / L"OctoFog.ini").wstring();
    g_config.enabled = GetPrivateProfileIntW(L"OctoFog", L"Enabled", 1, ini.c_str()) != 0;
    g_config.logging = GetPrivateProfileIntW(L"OctoFog", L"Logging", 1, ini.c_str()) != 0;
    g_config.densityMultiplier = std::max(0.0f, ReadFloat(ini.c_str(), L"DensityMultiplier", g_config.densityMultiplier));
    g_config.startMultiplier = std::max(0.0f, ReadFloat(ini.c_str(), L"StartMultiplier", g_config.startMultiplier));
    g_config.endMultiplier = std::max(0.0f, ReadFloat(ini.c_str(), L"EndMultiplier", g_config.endMultiplier));
    g_config.minimumStart = std::max(0.0f, ReadFloat(ini.c_str(), L"MinimumStart", g_config.minimumStart));
    g_config.minimumEnd = std::max(g_config.minimumStart, ReadFloat(ini.c_str(), L"MinimumEnd", g_config.minimumEnd));
    g_config.tintStrength = std::clamp(ReadFloat(ini.c_str(), L"TintStrength", g_config.tintStrength), 0.0f, 1.0f);
    g_config.tintR = std::clamp(ReadFloat(ini.c_str(), L"TintR", g_config.tintR), 0.0f, 1.0f);
    g_config.tintG = std::clamp(ReadFloat(ini.c_str(), L"TintG", g_config.tintG), 0.0f, 1.0f);
    g_config.tintB = std::clamp(ReadFloat(ini.c_str(), L"TintB", g_config.tintB), 0.0f, 1.0f);
}
}

const FogConfig& GetFogConfig() {
    std::call_once(g_once, Initialize);
    return g_config;
}

const std::wstring& GetGameDirectory() {
    std::call_once(g_once, Initialize);
    return g_gameDirectory;
}

void Log(const char* format, ...) {
    const auto& cfg = GetFogConfig();
    if (!cfg.logging) return;

    const auto path = std::filesystem::path(GetGameDirectory()) / L"OctoFog.log";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(file, "[%02u:%02u:%02u] ", st.wHour, st.wMinute, st.wSecond);

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);

    std::fputc('\n', file);
    std::fclose(file);
}
