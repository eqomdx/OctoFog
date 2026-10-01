# Manual testing

OctoFog 0.1.x is a native Direct3D 9 proxy prototype. Test it against the OctoWoW client before integrating it into EqUpdater.

## Before you start

OctoFog 0.1.x and DXVK both use the filename `d3d9.dll`.

For this test, use **native DirectX 9**:

1. Close WoW.
2. Open the folder containing `WoW.exe`.
3. If `d3d9.dll` already exists, back it up somewhere outside the client folder. It is probably DXVK.
4. Download the latest successful `OctoFog-win32` artifact from GitHub Actions.
5. Copy these two files beside `WoW.exe`:
   - `d3d9.dll`
   - `OctoFog.ini`

Do not put OctoFog in `Interface/AddOns` or `dlls.txt`.

## First boot test

Launch the client normally.

A successful hook should create `OctoFog.log` beside `WoW.exe`.

The beginning of the log should contain lines similar to:

```text
OctoFog 0.1.1 loaded. Enabled=1 Diagnostics=1
IDirect3DDevice9 hooked: Present + SetRenderState.
Manual test hotkey: F8 toggles OctoFog effect on/off.
```

As the world renders, diagnostics should also report the first fog states seen, for example:

```text
Observed D3DRS_FOGENABLE=1
Observed D3DRS_FOGSTART=...
Observed D3DRS_FOGEND=...
Observed D3DRS_FOGCOLOR=...
```

The exact values vary by zone and weather.

## Visual A/B test

Stand somewhere with a long sightline: Elwynn, Westfall, Tirisfal, Dun Morogh or a similar outdoor zone.

Press **F8**.

F8 switches between:

- OctoFog active
- OctoFog bypassed, using WoW's original fog values

The current cached fog values are reapplied immediately, so the comparison should not require zoning or restarting.

Take screenshots from the same camera position if the difference is subtle.

## Strong-effect test

If the default values are too subtle, close the client and temporarily try:

```ini
StartMultiplier=0.45
EndMultiplier=0.60
DensityMultiplier=2.00
TintStrength=0.15
```

These values are intentionally strong for proving that the hook works; they are not proposed final defaults.

## What to report

If it works, the useful feedback is:

- zone
- weather / time of day
- whether F8 changes the scene
- whether terrain, doodads, water or sky look wrong
- whether UI rendering is affected
- FPS before/after if noticeably different
- `OctoFog.log`

If the client crashes or does not launch, send the log if one exists and note whether a previous `d3d9.dll` was present.

If the game launches and the log says the device was hooked but there are **no fog-state lines**, that is especially useful: it means the client is probably feeding fog through a different rendering path than this prototype currently intercepts.

## Uninstall

Delete OctoFog's `d3d9.dll`, `OctoFog.ini` and optional `OctoFog.log`, then restore the previous `d3d9.dll` if you backed up DXVK.
