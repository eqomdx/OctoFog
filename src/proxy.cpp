#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

#include "config.h"

namespace {
HMODULE g_realD3D9 = nullptr;
using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
Direct3DCreate9Fn g_realDirect3DCreate9 = nullptr;

using SetRenderStateFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
SetRenderStateFn g_originalSetRenderState = nullptr;

constexpr std::size_t kDeviceVtableEntries = 119;
constexpr std::size_t kSetRenderStateIndex = 57;

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

HRESULT STDMETHODCALLTYPE HookedSetRenderState(
    IDirect3DDevice9* device,
    D3DRENDERSTATETYPE state,
    DWORD value) {

    const auto& cfg = GetFogConfig();
    if (!cfg.enabled || !g_originalSetRenderState) {
        return g_originalSetRenderState
            ? g_originalSetRenderState(device, state, value)
            : D3DERR_INVALIDCALL;
    }

    DWORD transformed = value;

    switch (state) {
    case D3DRS_FOGSTART: {
        const float original = DwordToFloat(value);
        const float adjusted = std::max(cfg.minimumStart, original * cfg.startMultiplier);
        transformed = FloatToDword(adjusted);
        break;
    }
    case D3DRS_FOGEND: {
        const float original = DwordToFloat(value);
        const float adjusted = std::max(cfg.minimumEnd, original * cfg.endMultiplier);
        transformed = FloatToDword(adjusted);
        break;
    }
    case D3DRS_FOGDENSITY: {
        const float original = DwordToFloat(value);
        transformed = FloatToDword(std::max(0.0f, original * cfg.densityMultiplier));
        break;
    }
    case D3DRS_FOGCOLOR:
        transformed = TransformFogColor(value, cfg);
        break;
    default:
        break;
    }

    return g_originalSetRenderState(device, state, transformed);
}

bool HookDevice(IDirect3DDevice9* device) {
    if (!device) return false;

    auto*** objectVtable = reinterpret_cast<void***>(device);
    if (!objectVtable || !*objectVtable) return false;

    void** originalVtable = *objectVtable;
    if (originalVtable[kSetRenderStateIndex] == reinterpret_cast<void*>(&HookedSetRenderState)) {
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
            reinterpret_cast<SetRenderStateFn>(originalVtable[kSetRenderStateIndex]);
    }

    shadow[kSetRenderStateIndex] = reinterpret_cast<void*>(&HookedSetRenderState);
    *objectVtable = shadow;

    Log("IDirect3DDevice9 hooked. Native fog render states will be transformed.");
    return true;
}

bool LoadRealD3D9() {
    if (g_realD3D9 && g_realDirect3DCreate9) return true;

    wchar_t systemDir[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;

    const auto path = std::filesystem::path(systemDir) / L"d3d9.dll";
    g_realD3D9 = LoadLibraryW(path.c_str());
    if (!g_realD3D9) return false;

    g_realDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(
        GetProcAddress(g_realD3D9, "Direct3DCreate9"));

    return g_realDirect3DCreate9 != nullptr;
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
            Log("CreateDevice failed: 0x%08lX", static_cast<unsigned long>(hr));
        }

        return hr;
    }

private:
    IDirect3D9* inner_;
};
}

extern "C" __declspec(dllexport)
IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion) {
    if (!LoadRealD3D9()) {
        return nullptr;
    }

    IDirect3D9* real = g_realDirect3DCreate9(sdkVersion);
    if (!real) {
        Log("System Direct3DCreate9 returned null.");
        return nullptr;
    }

    const auto& cfg = GetFogConfig();
    Log("OctoFog 0.1.0 loaded. Enabled=%d", cfg.enabled ? 1 : 0);

    return new Direct3D9Proxy(real);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_realD3D9) {
            FreeLibrary(g_realD3D9);
            g_realD3D9 = nullptr;
        }
    }
    return TRUE;
}
