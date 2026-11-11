# Photo mode

Set an optional chord in `/usum-onescreen.ini`, then reload settings:

```ini
[controls]
photo_key = R+Select
```

The default is `none`. Hold R before pressing Select so Select does not open
CTRPF's menu first. The completed chord is hidden from the game's pad reader
until its keys are released, following the PC/save combo pattern. Simultaneous
photo/save/PC hits give photo priority, then save, then PC. Choose distinct
combos; a prefix key pressed before completing a chord can already reach the
game, just as with the existing PC/save combos.

Use the chord to enter, release it, and press it again to exit. Alternatively,
select **Photo mode (close menu to enter)** in the plugin menu and close the
menu, or put `photo on`, `photo off`, or `photo status` in `/usum-cmd.txt`.
With no configured chord, B exits. A short **Photo mode — R+Select to exit**
hint (or the configured key/B) disappears after two seconds. Capture through
the emulator's screenshot function after the hint disappears. Plugin `shot`
commands and other mutating commands are refused while frozen; photo commands
continue polling about twice per second.

## What freezes and what disappears

`Process::Pause()` / `Process::Play()` own exactly one CTRPF pause. Entry runs
from `PanelEntry` only on the plugin stack, after the plugin menu closes. An
already paused process is refused, so photo mode never releases someone else's
pause. The paused loop uses `Controller::Update()` (direct `hidScanInput`),
`OSD::SwapBuffers()` and a 16 ms sleep, without waiting for game frame events.
Normal plugin actions, scene updates, UI updates and injected-input countdowns
do not advance during this loop. Process exit clears the loop's request.

The installed `libctrpf.a` dated 2026-05-22 was inspected with
`arm-none-eabi-objdump -drC`: `Process::Pause` calls `ProcessImpl::Pause`, which
waits for a frame boundary; `OSDImpl::PauseFrame` locks game threads and waits
for resume. Its thread predicate excludes plugin TLS. `Controller::Update`
calls `hidScanInput`, `hidKeysDown/Held/Up`, independently of the game's reader.
The menu uses the same framework pause machinery. The framework's
[ProcessImpl source mirror](https://github.com/Tekito-256/CTRPluginFramework/blob/develop/Library/source/CTRPluginFrameworkImpl/System/ProcessImpl.cpp)
also documents this thread separation; the installed binary is the authority
for this change.

All registered plugin OSD callbacks and notices are disabled together using
`OSD::LightweightMode`, with the previous value restored on exit. This includes
native menu layers, the battle overlay, panel, cursor, UI demos and probes.
The panel also explicitly declines drawing while active. GPU target overlays
run outside OSD, so all four published GPU quad slots are hidden through the
existing public `HideGpuQuad` API. Their owners republish on resume. GPU backend
settings, panel mode, native-screen settings and widescreen settings are kept.
The developer GPU test must be off; entry refuses it rather than changing its
configuration. Game-rendered graphics, including the game's own HUD and bottom
screen, remain part of the frozen image.

Entry first captures every input and waits eight 16 ms intervals while hiding
GPU publications, allowing fresh game images to replace old composited ones.
This roughly 128 ms preparation advances the game before the chosen frame is
frozen. At least two pad reads must have occurred to prime input capture, or
entry fails. Safety is checked again with the game paused. It refuses quick
saves, running actions, pending/open PC requests, unsupported/loading scenes,
and busy field states (including menus, events, saves and transitions).
Idle field and battle are supported. Refusals use an OSD notice.

The prior input-capture request is restored on exit. The existing pure
`MaskPadSamples` / `MaskTouchSamples` guards keep keys, circle pad and touch
hidden until release, including a held A or the exit chord. The exit chord
also cannot immediately reopen CTRPF's menu. Framebuffer snapshots are of
CTRPF-owned paused buffers; game state is not copied, patched or rewound.

## Framework compatibility and runtime checks

This installed framework unconditionally acquires **faded** buffers inside
`ProcessImpl::Pause`, even though the public wrapper passes false. To retain
the game's brightness, PhotoMode.cpp declares and calls the installed internal
symbols `ScreenImpl::AcquireFromGsp(false)` and `OSDImpl::UpdateScreens()` after
pausing. This reacquires from the saved game framebuffer information without
the menu fade. Both symbols were verified in the installed library. A CTRPF
upgrade must recheck their signatures and acquisition semantics. Acquisition
or snapshot allocation failure resumes the game and reports a refusal.

These are source/host checks, **not emulator validation**. Before release:

1. Build/link the plugin with the project's normal CTRPF toolchain. In both
   field and battle, test entry/exit through the configured chord, commands and
   menu. Confirm Select does not open the menu when R is held first.
2. Start with panel and native battle/menu overlays visible where supported.
   Confirm no plugin overlay, pillarbox tint/pattern, cursor or queued OSD
   notice remains after the two-second hint. Compare brightness against the
   unpaused game; test both stereo buffers if using 3D.
3. Watch an animation while active for ten seconds, then resume. Confirm it
   continues from the frozen state. Hold A, directions, stick and touch while
   exiting; nothing should reach the game until released and pressed again.
4. Test overlapping combos, settings reload, repeated entry/exit, default
   `none`/B, and exit before the hint times out. Confirm the panel/native UI
   and prior capture settings return, and Select does not reopen the menu on
   exit. Check queued save/action/PC refusals and a native save in progress.
5. Test slow emulation, screen transitions, application exit while paused and
   low memory. Check that entry preparation yields a clean image even at low
   frame rates. CTRPF's acquired buffers are screen-resolution buffers; verify
   screenshot quality at higher Azahar internal resolutions. Preservation of
   the emulator's higher-resolution render target is not guaranteed.

## Camera research deferred

Neither `docs/OFFSETS.md` nor `docs/WIDESCREEN.md` documents camera position or
target fields. Vtable `0x64E308` has documented projection parameters at
`+0xA4` near, `+0xA8` far, `+0xAC` fovy and `+0xB0` aspect. Those are not camera
position/target fields. No camera nudge or guessed address is included.

Research must identify field and battle camera ownership/lifetimes, document
position/target coordinates and units, verify writes across transitions and
both titles, and prove restoration on exit. Also determine whether the engine
rebuilds view matrices only while running; changing a paused position alone
may not update the frozen picture.

## Host tests

`photo_mode_test.cpp` builds the pure functions in PhotoMode.cpp without
`__3DS__`, plus the actual input-capture and settings implementations. It tests
entry/exit/idempotence, every refusal flag, hint boundaries and reentry, combo
edges, held A/chord/stick suppression and release, touch suppression, prior
capture ownership, and settings parsing/defaults. `tests/Makefile` includes it.
The pure tests cannot prove framework scheduling, buffer acquisition or GPU
presentation; the runtime checklist above covers those.

Executed directly from the repository root in this Windows sandbox (no make or
WSL). MSVC 14.44.35207 used its `include` / `lib/x64` directories plus Windows
SDK 10.0.26100.0 `ucrt`, `shared`, `um` includes and `ucrt/x64`, `um/x64` libs:

```powershell
cl /nologo /std:c++17 /EHsc /W4 /WX /I Includes tests/photo_mode_test.cpp Sources/PhotoMode.cpp Sources/InputCapture.cpp Sources/Settings.cpp Sources/SettingsFile.cpp /Fo:tests/build/ /Fe:tests/build/photo_mode_test.exe
./tests/build/photo_mode_test.exe
cl /nologo /std:c++17 /EHsc /W4 /WX /I Includes tests/settings_test.cpp Sources/Settings.cpp Sources/SettingsFile.cpp /Fo:tests/build/ /Fe:tests/build/settings_test.exe
./tests/build/settings_test.exe
cl /nologo /std:c++17 /EHsc /W4 /WX /I Includes tests/input_capture_test.cpp Sources/InputCapture.cpp /Fo:tests/build/ /Fe:tests/build/input_capture_test.exe
./tests/build/input_capture_test.exe
```

Results: photo mode **70 checks passed**, settings **219 checks, 0 failed**,
input capture **all passed**. The installed devkitARM compiler also checked all
changed runtime translation units with the following direct command:

```powershell
& C:/devkitPro/devkitARM/bin/arm-none-eabi-g++.exe -std=gnu++17 -fno-exceptions -fno-rtti -D__3DS__ -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -I Includes -I C:/devkitPro/libctrpf/include -I C:/devkitPro/libctru/include -Wall -fsyntax-only Sources/PhotoMode.cpp Sources/Buttons.cpp Sources/Settings.cpp Sources/main.cpp Sources/Panel.cpp
```

This passed. An earlier check with `-Wextra` also passed with existing unused
parameter warnings in framework headers and main.cpp. PhotoMode.cpp was
additionally compiled with the same ARM flags (`-Os -c` in place of
`-fsyntax-only`) to `tests/build/PhotoMode.arm.o`. A relocatable symbol check:

```powershell
& C:/devkitPro/devkitARM/bin/arm-none-eabi-ld.exe -r tests/build/PhotoMode.arm.o -L C:/devkitPro/libctrpf/lib -lctrpf -o tests/build/photo_mode_linkcheck.o
& C:/devkitPro/devkitARM/bin/arm-none-eabi-nm.exe -C tests/build/photo_mode_linkcheck.o
```

resolved both internal framework symbols (`T`, not `U`). This is not a full
plugin link or an emulator test. `git diff --check` also passed.
