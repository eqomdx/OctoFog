# OctoFog

Client-side atmospheric fog experiments for the OctoWoW 1.12 client.

## What this is

OctoFog is a **client mod**, not a WoW addon.

Version 0.1 hooks the native Direct3D 9 renderer by acting as a local `d3d9.dll` proxy. When WoW creates its D3D9 device, OctoFog intercepts `IDirect3DDevice9::SetRenderState` and transforms the fog values the game already sends to DirectX.

That lets us test denser, closer and slightly tinted atmospheric fog without changing the server or patching `WoW.exe`.

This first prototype deliberately does **not** claim to provide true volumetric fog. It is the stability/proof-of-hook stage for the larger renderer-side project.

## Current features

- Native 32-bit DirectX 9 proxy
- Preserves WoW's decision about when fog is enabled
- Pulls linear fog start/end distances inward
- Strengthens exponential fog density
- Optional colour tint blended with WoW's zone/weather fog colour
- INI configuration
- Local logging to `OctoFog.log`
- No server changes
- No Lua addon required

## Installation

1. Build the project as **Win32 / x86**.
2. Copy the resulting `d3d9.dll` into the same directory as `WoW.exe`.
3. Copy `OctoFog.ini` into the same directory.
4. Start the game normally.
5. Check `OctoFog.log` if the client does not start or the effect is not visible.

To uninstall, remove OctoFog's `d3d9.dll` and `OctoFog.ini`.

> **Important:** do not overwrite another `d3d9.dll` without backing it up.

## DXVK

This initial prototype targets **native DirectX 9**.

DXVK also normally occupies the local `d3d9.dll` slot, so OctoFog 0.1 and a standard DXVK install cannot simply be dropped into the same folder together. Chaining/support for DXVK is a follow-up milestone after the native hook is proven stable.

## Configuration

```ini
[OctoFog]
Enabled=1
DensityMultiplier=1.35
StartMultiplier=0.70
EndMultiplier=0.82
MinimumStart=8.0
MinimumEnd=32.0
TintStrength=0.08
TintR=0.72
TintG=0.78
TintB=0.82
Logging=1
```

### Useful values

- Lower `StartMultiplier` = fog starts closer.
- Lower `EndMultiplier` = distant geometry disappears into fog sooner.
- Higher `DensityMultiplier` = stronger EXP/EXP2 fog.
- `TintStrength=0` preserves the original WoW fog colour exactly.

Start conservatively. Old WoW zones already contain strongly varying fog settings.

## Build

Requirements:

- Windows
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The DLL will normally be at:

```
build\Release\d3d9.dll
```

## How it works

```text
WoW.exe
  |
  | Direct3DCreate9()
  v
OctoFog d3d9.dll
  |
  | loads C:\Windows\System32\d3d9.dll
  v
Real Direct3D 9
  |
  | CreateDevice()
  v
IDirect3DDevice9
  |
  | SetRenderState(D3DRS_FOG*)
  v
OctoFog transforms fog values
  |
  v
GPU
```

The device hook uses a per-device shadow vtable. Only the `SetRenderState` entry is replaced; all other device methods continue to call Direct3D normally.

## Roadmap

### 0.1 - native fog hook
Prove that the proxy loads reliably in OctoWoW and that modifying D3D9 fog states gives a useful visual result.

### 0.2 - better atmosphere
- presets
- hot reload
- zone-aware tuning
- weather multipliers
- better logging / diagnostics

### 0.3 - renderer experiments
Investigate depth-buffer access and a fullscreen post-process. This is where height fog / depth-based atmospheric scattering could become possible.

### 0.4 - DXVK compatibility
Investigate clean proxy chaining or a separate injection route so OctoFog can coexist with DXVK.

### Later - volumetric-style fog
If usable scene depth can be acquired reliably, prototype depth reconstruction, height fog and eventually a lightweight ray-marched or froxel-style volume.

## Safety / scope

OctoFog changes only local rendering state. It does not alter movement, combat, networking, packets, game data or server state.

This project is experimental. Keep a backup of any existing renderer DLL before testing.
