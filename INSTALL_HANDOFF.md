# OctoFog — Install Handoff

This file is for manual testers installing the current OctoFog prototype into an OctoWoW client.

## Scope

OctoFog is currently a **native Direct3D 9 client mod prototype**.

It is not a WoW addon and does not go in `Interface/AddOns`.

The current build works by providing a local `d3d9.dll` beside `WoW.exe`, then intercepting the fog render states the client sends to Direct3D 9.

This manual test is intended to verify:

- the client launches successfully with OctoFog
- the Direct3D 9 hook is active
- Octo's fog render states are being observed
- the fog parameters can be modified visibly
- F8 A/B switching works
- F9 live configuration reload works

## Download

Open the latest successful GitHub Actions build for:

https://github.com/eqomdx/OctoFog

Download the artifact named:

```text
OctoFog-win32
```

The artifact contains:

```text
d3d9.dll
OctoFog.ini
README.md
TESTING.md
CHECKSUMS.txt
```

Use the **Win32 / x86** build only. The OctoWoW 1.12 client is 32-bit.

## Important: DXVK

OctoFog 0.1.x and DXVK both normally use:

```text
d3d9.dll
```

They cannot currently be installed in the same client folder at the same time.

Before testing OctoFog:

1. Close WoW.
2. Open the folder containing `WoW.exe`.
3. If a `d3d9.dll` is already present, move it somewhere safe outside the client folder.
4. Keep that backup so DXVK can be restored after testing.

Do not simply overwrite an existing `d3d9.dll` without backing it up first.

## Install

Copy these two files from the OctoFog artifact into the same folder as `WoW.exe`:

```text
d3d9.dll
OctoFog.ini
```

Example:

```text
OctoWoW/
├─ WoW.exe
├─ d3d9.dll
├─ OctoFog.ini
├─ Data/
├─ Interface/
└─ WTF/
```

Do **not**:

- place OctoFog in `Interface/AddOns`
- add OctoFog to `dlls.txt`
- rename the OctoFog `d3d9.dll`
- run it alongside DXVK yet

## First Launch

Launch OctoWoW normally.

If OctoFog loads successfully, a new file should appear beside `WoW.exe`:

```text
OctoFog.log
```

The start of the log should contain messages similar to:

```text
OctoFog 0.1.1 loaded. Enabled=1 Diagnostics=1
IDirect3DDevice9 hooked: Present + SetRenderState.
Manual test hotkeys: F8 toggles effect; F9 reloads OctoFog.ini.
```

As the world renders, the log should also report the first fog states Octo sends to Direct3D, such as:

```text
Observed D3DRS_FOGENABLE=1
Observed D3DRS_FOGSTART=...
Observed D3DRS_FOGEND=...
Observed D3DRS_FOGCOLOR=...
Observed D3DRS_FOGDENSITY=...
```

Not every zone necessarily uses every fog mode.

## Manual Test

Use an outdoor area with a long sightline.

Good examples include:

- Elwynn Forest
- Westfall
- Tirisfal Glades
- Dun Morogh
- other open outdoor zones

Stand still and keep the same camera angle.

### F8 — A/B Toggle

Press:

```text
F8
```

This switches between:

```text
OctoFog active
↕
Original WoW fog values
```

The cached fog values are reapplied immediately, so the visual comparison should happen without zoning or restarting the client.

If the difference is subtle, take screenshots from exactly the same camera position.

### F9 — Live INI Reload

You can tune OctoFog while the game is running.

1. Open `OctoFog.ini`.
2. Change the fog values.
3. Save the file.
4. Return to WoW.
5. Press:

```text
F9
```

OctoFog reloads the configuration and reapplies the current fog state.

This is the preferred manual tuning workflow.

## Default Configuration

The default test configuration is intentionally conservative:

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
Diagnostics=1
```

## Strong Test Configuration

If the default effect is difficult to see, try this temporary configuration:

```ini
[OctoFog]
Enabled=1

DensityMultiplier=2.00

StartMultiplier=0.45
EndMultiplier=0.60

MinimumStart=8.0
MinimumEnd=32.0

TintStrength=0.15
TintR=0.72
TintG=0.78
TintB=0.82

Logging=1
Diagnostics=1
```

Save the file and press **F9**.

These values are only intended to prove that the renderer hook is affecting the scene. They are not proposed final visual defaults.

## What To Report

For a useful test result, include:

- whether the client launched
- whether `OctoFog.log` was created
- whether the log says the D3D9 device was hooked
- zone tested
- weather / approximate time of day
- whether F8 visibly changed the scene
- whether F9 successfully applied edited values
- whether terrain looked correct
- whether trees / doodads looked correct
- whether water looked correct
- whether sky rendering looked correct
- whether UI rendering was affected
- any obvious FPS change
- screenshots if useful
- the contents of `OctoFog.log`

The log is especially important if the visual effect does not appear.

## Useful Failure Cases

### Client does not launch

Remove OctoFog's `d3d9.dll` and try again.

If the client then launches normally, provide:

- `OctoFog.log`, if one was created
- whether another `d3d9.dll` had existed before installation
- whether DXVK had been installed previously

### Client launches, log is created, but there is no visual difference

Try the strong test configuration and press F9.

If there is still no difference, provide the log.

### Device hook succeeds but no fog render states are logged

This is useful information.

It likely means the client is feeding atmospheric fog through a rendering path this prototype does not currently intercept.

That would be a signal to move the project toward the deeper shader / depth-buffer approach.

### F8 does not appear to change anything

Check `OctoFog.log` for:

```text
F8: OctoFog runtime effect ENABLED.
```

or:

```text
F8: OctoFog runtime effect BYPASSED.
```

If those lines appear but the scene does not change, send the log and test screenshots.

### F9 does not apply configuration changes

Check the log for a line similar to:

```text
F9: reloaded OctoFog.ini
```

If the line appears, confirm the INI was edited in the same folder as the active `WoW.exe`.

## Uninstall / Rollback

Close WoW.

Remove:

```text
d3d9.dll
OctoFog.ini
OctoFog.log
```

from the client folder.

If DXVK was backed up before the test, restore its original:

```text
d3d9.dll
```

to the client folder.

The rest of the WoW installation does not need to be changed.

## Current Limitation

OctoFog 0.1.x is **not true volumetric fog**.

The current version modifies the native fog render states already used by the old WoW client.

The purpose of this stage is to validate the renderer hook and determine how far the existing Direct3D fog path can be pushed before moving to depth-based post-processing, height fog, and later volumetric-style rendering.
