#pragma once

#include <windows.h>
#include <string>

struct FogConfig {
    bool enabled = true;
    bool logging = true;
    float densityMultiplier = 1.35f;
    float startMultiplier = 0.70f;
    float endMultiplier = 0.82f;
    float minimumStart = 8.0f;
    float minimumEnd = 32.0f;
    float tintStrength = 0.08f;
    float tintR = 0.72f;
    float tintG = 0.78f;
    float tintB = 0.82f;
};

const FogConfig& GetFogConfig();
const std::wstring& GetGameDirectory();
void Log(const char* format, ...);
