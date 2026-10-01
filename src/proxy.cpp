#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>

#include "config.h"

namespace {
HMODULE g_realD3D9 = nullptr;
using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
Direct3DCreate9Fn g_realDirect3DCreate9 = nullptr;

using SetRenderStateFn = HRESULT (STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

SetRenderStateFn g_originalSetRenderState = nullptr;
PresentFn g_originalPresent = nullptr;

std::atomic_bool g_runtimeEnabled{true};

constexpr std::size_t kDeviceVtableEntries = 119;
constexpr std::size_t kPresentIndex = 17;
constexpr std::size_t kSetRenderStateIndex = 57;

struct SeenFogStates {
    std::atomic_bool fogEnable{false};
    std::atomic_bool fogColor{false};
    std::atomic_bool fogStart{false};
    std::atomic_bool fogEnd{false};
    std::atomic_bool fogDensity{false};
    std::atomic_bool fogTableMode{false};
    std::atomic_bool fogVertexMode{false};
    std::atomic_bool rangeFogEnable{false};
} g_seen;

struct FogStateCache {
    bool hasColor = false;
    bool hasStart = false;
    bool hasEnd = false;
    bool hasDensity = false;
    DWORD color = 0;
    DWORD start = 0;
    DWORD end = 0;
    DWORD density = 0;
} g_fogCache;

bool MarkFirst(std::atomic_bool& flag) {
    bool expected = false;
    return flag.compare_exchange_strong(expected, true);
}

float DwordToFloat(DWORD value) {
    float result = 0.0f;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

DWORD FloatToDword(float value) {
    DWORD result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

BYTE BlendByte(BYTE original, float target, float strength) {
    const float blended = static_cast<float>(original) * (1.0f - strength)
        + (target * 255.0f) * strength;
    return static_cast<BYTE>(std::clamp(blended, 0.0f, 255.0f));
}

DWORD TransformFogColor(DWORD value, const FogConfig& cfg) {
    const BYTE a = static_cast<BYTE>((value >> 24) & 0xFF);
    const BYTE r = static_cast<BYTE>((value >> 16) & 0xFF);
    const BYTE g = static_cast<BYTE>((value >> 8) & 0xFF);
    const BYTE b = static_cast<BYTE>(value & 0xFF);

    return D3DCOLOR_ARGB(
        a,
        BlendByte(r, cfg.tintR, cfg.tintStrength),
        BlendByte(g, cfg.tintG, cfg.tintStrength),
        BlendByte(b, cfg.tintB, cfg.tintStrength));
}

DWORD TransformFogState(
    D3DRENDERSTATETYPE state,
    DWORD value,
    const FogConfig& cfg,
    bool active) {

    if (!active) return value;

    switch (state) {
    case D3DRS_FOGSTART:
        return FloatToDword(std::max(
            cfg.minimumStart,
            DwordToFloat(value) * cfg.startMultiplier));
    case D3DRS_FOGEND:
        return FloatToDword(std::max(
            cfg.minimumEnd,
            DwordToFloat(value) * cfg.endMultiplier));
    case D3DRS_FOGDENSITY:
        return FloatToDword(std::max(
            0.0f,
            DwordToFloat(value) * cfg.densityMultiplier));
    case D3DRS_FOGCOLOR:
        return TransformFogColor(value, cfg);
    default:
        return value;
    }
}

void CacheOriginalFogState(D3DRENDERSTATETYPE state, DWORD value) {
    switch (state) {
    case D3DRS_FOGCOLOR:
        g_fogCache.hasColor = true;
        g_fogCache.color = value;
        break;
    case D3DRS_FOGSTART:
        g_fogCache.hasStart = true;
        g_fogCache.start = value;
        break;
    case D3DRS_FOGEND:
        g_fogCache.hasEnd = true;
        g_fogCache.end = value;
        break;
    case D3DRS_FOGDENSITY:
        g_fogCache.hasDensity = true;
        g_fogCache.density = value;
        break;
    default:
        break;
    }
}

void ApplyCachedFogStates(IDirect3DDevice9* device, bool active) {
    if (!g_originalSetRenderState) return;
    const auto cfg = GetFogConfig();

    if (g_fogCache.hasColor)
        g_originalSetRenderState(
            device, D3DRS_FOGCOLOR,
            TransformFogState(D3DRS_FOGCOLOR, g_fogCache.color, cfg, active));
    if (g_fogCache.hasStart)
        g_originalSetRenderState(
            device, D3DRS_FOGSTART,
            TransformFogState(D3DRS_FOGSTART, g_fogCache.start, cfg, active));
    if (g_fogCache.hasEnd)
        g_originalSetRenderState(
            device, D3DRS_FOGEND,
            TransformFogState(D3DRS_FOGEND, g_fogCache.end, cfg, active));
    if (g_fogCache.hasDensity)
        g_originalSetRenderState(
            device, D3DRS_FOGDENSITY,
            TransformFogState(D3DRS_FOGDENSITY, g_fogCache.density, cfg, active));
}

bool IsFogStateOfInterest(D3DRENDERSTATETYPE state) {
    switch (state) {
    case D3DRS_FOGENABLE:
    case D3DRS_FOGCOLOR:
    case D3DRS_FOGSTART:
    case D3DRS_FOGEND:
    case D3DRS_FOGDENSITY:
    case D3DRS_FOGTABLEMODE:
    case D3DRS_FOGVERTEXMODE:
    case D3DRS_RANGEFOGENABLE:
        return true;
    default:
        return false;
    }
}

void LogFogStateOnce(D3DRENDERSTATETYPE state, DWORD original, DWORD transformed) {
    const auto cfg = GetFogConfig();
    if (!cfg.diagnostics) return;

    switch (state) {
    case D3DRS_FOGENABLE:
        if (MarkFirst(g_seen.fogEnable))
            Log("Observed D3DRS_FOGENABLE=%lu", static_cast<unsigned long>(original));
        break;
    case D3DRS_FOGCOLOR:
        if (MarkFirst(g_seen.fogColor))
            Log("Observed D3DRS_FOGCOLOR=0x%08lX -> 0x%08lX",
                static_cast<unsigned long>(original),
                static_cast<unsigned long>(transformed));
        break;
    case D3DRS_FOGSTART:
        if (MarkFirst(g_seen.fogStart))
            Log("Observed D3DRS_FOGSTART=%.3f -> %.3f",
                DwordToFloat(original), DwordToFloat(transformed));
        break;
    case D3DRS_FOGEND:
        if (MarkFirst(g_seen.fogEnd))
            Log("Observed D3DRS_FOGEND=%.3f -> %.3f",
                DwordToFloat(original), DwordToFloat(transformed));
        break;
    case D3DRS_FOGDENSITY:
        if (MarkFirst(g_seen.fogDensity))
            Log("Observed D3DRS_FOGDENSITY=%.6f -> %.6f",
                DwordToFloat(original), DwordToFloat(transformed));
        break;
    case D3DRS_FOGTABLEMODE:
        if (MarkFirst(g_seen.fogTableMode))
            Log("Observed D3DRS_FOGTABLEMODE=%lu", static_cast<unsigned long>(original));
        break;
    case D3DRS_FOGVERTEXMODE:
        if (MarkFirst(g_seen.fogVertexMode))
            Log("Observed D3DRS_FOGVERTEXMODE=%lu", static_cast<unsigned long>(original));
        break;
    case D3DRS_RANGEFOGENABLE:
        if (MarkFirst(g_seen.rangeFogEnable))
            Log("Observed D3DRS_RANGEFOGENABLE=%lu", static_cast<unsigned long>(original));
        break;
    default:
        break;
    }
}

HRESULT STDMETHODCALLTYPE HookedPresent(
    IDirect3DDevice9* device,
    const RECT* sourceRect,
    const RECT* destRect,
    HWND destWindowOverride,
    const RGNDATA* dirtyRegion) {

    if ((GetAsyncKeyState(VK_F8) & 1) != 0) {
        const bool enabled = !g_runtimeEnabled.load();
        g_runtimeEnabled.store(enabled);
        const auto cfg = GetFogConfig();
        ApplyCachedFogStates(device, cfg.enabled && enabled);
        Log("F8: OctoFog runtime effect %s.", enabled ? "ENABLED" : "BYPASSED");
    }

    if ((GetAsyncKeyState(VK_F9) & 1) != 0) {
        const auto cfg = ReloadFogConfig();
        const bool active = cfg.enabled && g_runtimeEnabled.load();
        ApplyCachedFogStates(device, active);
        Log("F9: reloaded OctoFog.ini: Enabled=%d Start=%.3f End=%.3f Density=%.3f Tint=%.3f",
            cfg.enabled ? 1 : 0,
            cfg.startMultiplier,
            cfg.endMultiplier,
            cfg.densityMultiplier,
            cfg.tintStrength);
    }

    return g_originalPresent
        ? g_originalPresent(device, sourceRect, destRect, destWindowOverride, dirtyRegion)
        : D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE HookedSetRenderState(
    IDirect3DDevice9* device,
    D3DRENDERSTATETYPE state,
    DWORD value) {

    if (!g_originalSetRenderState) {
        return D3DERR_INVALIDCALL;
    }

    if (!IsFogStateOfInterest(state)) {
        return g_originalSetRenderState(device, state, value);
    }

    const auto cfg = GetFogConfig();
    const bool active = cfg.enabled && g_runtimeEnabled.load();

    CacheOriginalFogState(state, value);
    const DWORD transformed = TransformFogState(state, value, cfg, active);

    LogFogStateOnce(state, value, transformed);
    return g_originalSetRenderState(device, state, transformed);
}

bool HookDevice(IDirect3DDevice9* device) {
    if (!device) return false;

    auto*** objectVtable = reinterpret_cast<void***>(device);
    if (!objectVtable || !*objectVtable) return false;

    void** originalVtable = *objectVtable;
    if (originalVtable[kSetRenderStateIndex] ==
        reinterpret_cast<void*>(&HookedSetRenderState)) {
        return true;
    }

    auto** shadow = static_cast<void**>(
        VirtualAlloc(nullptr,
                     sizeof(void*) * kDeviceVtableEntries,
                     MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    if (!shadow) {
        Log("Failed to allocate shadow IDirect3DDevice9 vtable.");
        return false;
    }

    std::memcpy(shadow, originalVtable, sizeof(void*) * kDeviceVtableEntries);

    if (!g_originalSetRenderState) {
        g_originalSetRenderState =
            reinterpret_cast<SetRenderStateFn>(
                originalVtable[kSetRenderStateIndex]);
    }
    if (!g_originalPresent) {
        g_originalPresent =
            reinterpret_cast<PresentFn>(originalVtable[kPresentIndex]);
    }

    shadow[kPresentIndex] = reinterpret_cast<void*>(&HookedPresent);
    shadow[kSetRenderStateIndex] = reinterpret_cast<void*>(&HookedSetRenderState);
    *objectVtable = shadow;

    Log("IDirect3DDevice9 hooked: Present + SetRenderState.");
    Log("Manual test hotkeys: F8 toggles effect; F9 reloads OctoFog.ini.");
    return true;
}

bool LoadRealD3D9() {
    if (g_realD3D9 && g_realDirect3DCreate9) return true;

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        Log("GetSystemDirectoryW failed.");
        return false;
    }

    const auto path = std::filesystem::path(systemDir) / L"d3d9.dll";
    g_realD3D9 = LoadLibraryW(path.c_str());
    if (!g_realD3D9) {
        Log("Failed to load system d3d9.dll (error %lu).",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }

    g_realDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(
        GetProcAddress(g_realD3D9, "Direct3DCreate9"));

    if (!g_realDirect3DCreate9) {
        Log("System d3d9.dll does not export Direct3DCreate9.");
        return false;
    }

    return true;
}

class Direct3D9Proxy final : public IDirect3D9 {
public:
    explicit Direct3D9Proxy(IDirect3D9* inner) : inner_(inner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirect3D9) {
            *ppvObj = static_cast<IDirect3D9*>(this);
            AddRef();
            return S_OK;
        }
        return inner_->QueryInterface(riid, ppvObj);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return inner_->AddRef();
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = inner_->Release();
        if (refs == 0) delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initFunction) override {
        return inner_->RegisterSoftwareDevice(initFunction);
    }

    UINT STDMETHODCALLTYPE GetAdapterCount() override {
        return inner_->GetAdapterCount();
    }

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9* identifier) override {
        return inner_->GetAdapterIdentifier(adapter, flags, identifier);
    }

    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override {
        return inner_->GetAdapterModeCount(adapter, format);
    }

    HRESULT STDMETHODCALLTYPE EnumAdapterModes(
        UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE* displayMode) override {
        return inner_->EnumAdapterModes(adapter, format, mode, displayMode);
    }

    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(
        UINT adapter, D3DDISPLAYMODE* mode) override {
        return inner_->GetAdapterDisplayMode(adapter, mode);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceType(
        UINT adapter, D3DDEVTYPE deviceType, D3DFORMAT adapterFormat,
        D3DFORMAT backBufferFormat, BOOL windowed) override {
        return inner_->CheckDeviceType(
            adapter, deviceType, adapterFormat, backBufferFormat, windowed);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(
        UINT adapter, D3DDEVTYPE deviceType, D3DFORMAT adapterFormat,
        DWORD usage, D3DRESOURCETYPE resourceType, D3DFORMAT checkFormat) override {
        return inner_->CheckDeviceFormat(
            adapter, deviceType, adapterFormat, usage, resourceType, checkFormat);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(
        UINT adapter, D3DDEVTYPE deviceType, D3DFORMAT surfaceFormat,
        BOOL windowed, D3DMULTISAMPLE_TYPE multiSampleType, DWORD* qualityLevels) override {
        return inner_->CheckDeviceMultiSampleType(
            adapter, deviceType, surfaceFormat, windowed, multiSampleType, qualityLevels);
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(
        UINT adapter, D3DDEVTYPE deviceType, D3DFORMAT adapterFormat,
        D3DFORMAT renderTargetFormat, D3DFORMAT depthStencilFormat) override {
        return inner_->CheckDepthStencilMatch(
            adapter, deviceType, adapterFormat, renderTargetFormat, depthStencilFormat);
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(
        UINT adapter, D3DDEVTYPE deviceType,
        D3DFORMAT sourceFormat, D3DFORMAT targetFormat) override {
        return inner_->CheckDeviceFormatConversion(
            adapter, deviceType, sourceFormat, targetFormat);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceCaps(
        UINT adapter, D3DDEVTYPE deviceType, D3DCAPS9* caps) override {
        return inner_->GetDeviceCaps(adapter, deviceType, caps);
    }

    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override {
        return inner_->GetAdapterMonitor(adapter);
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT adapter,
        D3DDEVTYPE deviceType,
        HWND focusWindow,
        DWORD behaviorFlags,
        D3DPRESENT_PARAMETERS* presentationParameters,
        IDirect3DDevice9** returnedDeviceInterface) override {

        const HRESULT hr = inner_->CreateDevice(
            adapter,
            deviceType,
            focusWindow,
            behaviorFlags,
            presentationParameters,
            returnedDeviceInterface);

        if (SUCCEEDED(hr) && returnedDeviceInterface && *returnedDeviceInterface) {
            HookDevice(*returnedDeviceInterface);
        } else {
            Log("CreateDevice failed: 0x%08lX",
                static_cast<unsigned long>(hr));
        }

        return hr;
    }

private:
    IDirect3D9* inner_;
};
}

extern "C" __declspec(dllexport)
IDirect3D9* WINAPI OctoFogDirect3DCreate9(UINT sdkVersion) {
    if (!LoadRealD3D9()) {
        return nullptr;
    }

    IDirect3D9* real = g_realDirect3DCreate9(sdkVersion);
    if (!real) {
        Log("System Direct3DCreate9 returned null.");
        return nullptr;
    }

    const auto cfg = GetFogConfig();
    g_runtimeEnabled.store(true);

    Log("OctoFog %s loaded. Enabled=%d Diagnostics=%d", OCTOFOG_VERSION,
        cfg.enabled ? 1 : 0,
        cfg.diagnostics ? 1 : 0);

    return new Direct3D9Proxy(real);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
