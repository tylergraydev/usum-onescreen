# Research: 60 fps for USUM v1.2 (backlog #26)

Research only, 2026-11-09. No plugin code changed and no emulator was run.
Every claim is tagged **[verified]** (seen in the code, a dump, or a source) or
**[unverified]** (inferred or recalled, not checked).

## Verdict (short)

- **Smooth 60 fps at normal game speed: not feasible.** The game logic is
  frame-locked: one update per drawn frame, with no delta time
  **[verified for the frame loop; see "What runs per frame"]**. Running 60 frames
  a second runs the whole game at double speed. Making it run at normal speed
  would mean halving every per-frame step in the engine and every module.
- **A "60 fps = 2x speed" mode: feasible, low risk, small effort.** The engine
  has a built-in frame-mode switch (`GameSys::FrameMode`, 30 or 60) in
  `GameSys::GameManager` **[verified]**, and the community 60 fps cheat for
  Ultra Sun/Ultra Moon v1.2 writes exactly that field **[verified]**. A plugin
  can request the mode through the game's own request field once per frame.
  About half a day of code plus one to two days of testing **[estimate]**.

## What exists elsewhere

### Reshiban's 60FPS-AR-CHEATS-3DS (the main source)

https://github.com/Reshiban/60FPS-AR-CHEATS-3DS (last commit 2023-02-02), built
on the GBAtemp thread by Hazerou:
https://gbatemp.net/threads/60-fps-patches-cheat-codes-releases-and-discussion.550527/
(GBAtemp blocked automated fetches, so the thread itself was not read.)

Ultra Sun USA (`Cheats/Pokémon Ultra Sun (USA)/00040000001B5000.txt`) and Ultra
Moon USA (`00040000001B5100.txt`) have the same codes **[verified]**:

```
[60FPS v1.2]               [Default 30FPS v1.2]
D3000000 30000000          D3000000 30000000
23F67FE9 00000000          23F67FE9 00000001
D2000000 00000000          D2000000 00000000
```

That is an 8-bit write of 0 (60 fps) or 1 (30 fps) to `0x33F67FE9`, a fixed
heap address. The v1.0 entry uses the same address. There is also a "hold R for
30 fps" variant (`DD000000 00000100`).

Other Pokémon titles in the same repo **[verified]**:

| Game | Address | Notes |
| --- | --- | --- |
| Sun / Moon USA v1.0, v1.2 | `0x3417E569` | 8-bit write, 0/1 as above (heap) |
| Omega Ruby USA v1.4 | `0x08C690A1` (v1.0: `0x08C650C1`) | 8-bit write 0/1 |
| X USA v1.0 and v1.5 | `0x08C61A85` | 8-bit write 0/1 |

All are one byte with 0 = 60 and 1 = 30, so the whole 3DS Pokémon line
probably shares the same frame-mode design **[unverified for XY/ORAS/SM; only
the cheat format was seen]**.

`docs/STATUS.md` in the repo marks every Pokémon entry (X/Y, ORAS, SM, USUM)
"Game speed: ❌ faster", default 30 fps, "Checked: ❌ no" **[verified]**: the
game runs faster, not smoother, and nobody recorded a full test. The README
warns "60FPS CHEATS ARE NOT CITRA PERF IMPROVERS" **[verified]**. No list of
broken cutscenes or battle bugs was found for USUM **[searched, none found]**.

### Other searches

Web searches for Ultra Sun/Moon 60 fps patches (IPS / Luma code patches)
turned up nothing beyond copies of these cheats. One PokéCommunity post about
a fan hack mentions a 60 fps code that would not work for that user
(https://www.pokecommunity.com/threads/pok%C3%A9mon-photonic-sun-prismatic-moon.411743/post-10173851,
not relevant to retail). No code.bin patch for USUM was found.

## What is in v1.2

All addresses are Ultra Sun v1.2 (`analysis/private/usun-v1.2/code.bin`,
base `0x100000`) unless noted.

### The cheat byte is `GameManager + 9` [verified]

- `0x33F67FE0` in the heap dumps
  (`analysis/private/instances/player1/azahar/user/sdmc/usum-dump-lin1.bin`,
  `-batl.bin`, `-bat2.bin`; `dump 30000000 4184000`) is the start of a heap
  block (the `0x5544` block header is at `0x33F67FB0`, size `0xD4`). The bytes
  at `+8`/`+9` read `02 01` in all three dumps, and `+0xC` changes between
  dumps (`0xAB3`, `0x1063`, `0x166D`), like a counter.
- The heap object at `0x300000D4` has vtable `0x649404`, whose RTTI says
  `Main::CommonApplication`. Its `+4` holds `0x33F67FE0`.
- `CommonApplication` init (`0x3287D0`) allocates `0xB4` bytes and constructs
  them with `0x403010`, stores the result at `+4`. `0x403010` stores `this` in
  the singleton `0x6A59C4` (`gfl2::base::Singleton<GameSys::GameManager>`; the
  getter `0x1048B4` is `ldr r0,=0x6A59C4; ldr r0,[r0]; bx lr`, and docs/OFFSETS.md
  already uses `*(0x6A59C4)` as the GameManager). So the cheat writes
  `GameManager + 9` (`0x33F67FE0 + 9 = 0x33F67FE9`).

### The frame-mode fields [verified in code]

GameManager constructor `0x403010`:

```
+0x08 u8  = 2      requested frame mode, 2 = no request
+0x09 u8  = 1      current frame mode: 1 = 30 fps, 0 = 60 fps
+0x0C u32 = 0      vsync count at the last swap
+0x10 f32 = 1.0    (use unknown)
...
0x34F3B0(*0x6A7558, mode)   tell the input devices the frame mode
0x34F4F8(*0x6A7558)         reset their counters
```

GameManager update `0x402A5C` (Ultra Moon `0x402A60`), every frame:

```c
if (gm->req != 2) {                     // +8
    if (gm->cur != gm->req) {           // +9
        0x34F3B0(devices, req == 0 ? 0 : 1);
        0x34F4F8(devices);
        gm->cur = gm->req;
    }
    gm->req = 2;
}
... then the game events, procs, etc.
```

GameManager draw `0x402D38`, every frame (`0x547AF4(0x402)` returns a vsync
count; the `0x400/0x401/0x402` arguments look like the CTR SDK's
`NN_GX_DISPLAY0/1/BOTH` **[that name is unverified]**; `0x1082C4(1000000, 0)`
sleeps 1 ms **[inferred from the argument]**):

- `cur == 1` (30 fps): first sleeps in 1 ms steps until one vsync has passed
  since the last swap, then draws.
- The swap target is `last + 2` vsyncs when `cur == 1`, `last + 1` otherwise.

`0x402518` (end of frame) stores the vsync count in `+0xC`.

So the game already has a working 60 fps path: it is the `cur == 0` branch of
the same functions. Nothing else needs patching to get the frames.

### Input devices follow the frame mode [verified in code; effect inferred]

`0x34F3B0` writes the mode to byte `+0x14` of every device in the device
manager at `*0x6A7558` (six lists). A device's update (virtual, `0x350300`)
increments a counter at `+0x18` and takes it modulo `table[+0x14]`, with the
table at `0x671030` = `{1, 2}` (mode 0 → 1, mode 1 → 2). So in 30 fps mode the
device does its latch step every second update, in 60 fps mode every update
**[the modulo is verified; what that step does was not traced]**.

The cheat writes `+9` directly and skips this call, so with the cheat the input
devices stay in 30 fps mode. Writing the request byte `+8` instead goes
through the game's own path and switches the devices too. This is the main
reason to prefer `+8`.

### Who else uses the frame mode

- No main-code caller sets `+8` **[searched: all 4 literal loads of
  `0x6A59C4` and all 672 calls of the getter `0x1048B4` with a `strb [r0,#8/#9]`
  shortly after; none found]**. The shipped game seems to stay at 30 fps.
- CROs import `App::Tool::TimeIcon::StartAnime(LytWk*, unsigned, GameSys::FrameMode)`
  (Box, FieldMenu, MyAlbum, StrInput, TitleMenu) **[verified, mangled symbol in
  the CRO import tables]**. So some UI animations take a frame mode argument.
  What those callers pass was not checked **[unverified]**.
- No CRO imports a GameManager frame-mode getter or setter by name
  **[searched all `GameSys` symbols in the Ultra Moon CROs]**. CROs could still
  read the field through `SingletonAccessor<GameManager>::GetInstance`
  (74 imports) **[not checked]**.
- No delta-time float was found next to the frame mode. `+0x10` = 1.0 could be
  a time scale, but no reader of it was found after the getter
  **[searched, none found; open question]**.

### Ultra Moon v1.2 [verified]

Same singleton `0x6A59C4`, same getter at `0x1048B4`. The constructor is at
`0x403014`, the update at `0x402A60` (+4, like the other GameManager functions
in docs/OFFSETS.md). The field offsets `+8`/`+9` are the same (same constructor
bytes). One address for both games is enough.

## How the plugin could apply it

No code patch is needed. Per-frame data write, in the plugin's frame callback:

```cpp
// desired: 1 = 30 fps (game default), 0 = 60 fps
u32 gm = *(u32 *)0x6A59C4;              // GameSys::GameManager*
if (gm && *(u8 *)(gm + 9) != desired)
    *(u8 *)(gm + 8) = desired;          // the game switches on its next update
```

- `+8` is a request that the game clears (back to 2) on its next update, so
  writing it only when `+9` differs is enough. Also safe to write it every frame.
- Add `0x6A59C4` to `Offsets`/`signatures` (the getter `0x1048B4` is a 3-word
  function, easy to sign: `E59F0004 E5900000 E12FFF1E` + the literal).
- Settings: one key, e.g. `[game] frame_rate = 30 | 60` in `usum-onescreen.ini`,
  default 30, plus an optional hotkey to toggle (the community cheat uses hold R
  for 30, which clashes with the game's R). Show it as a "2x speed" option, not
  as "smoother".
- Turning it off: write 1 to `+8`. The game switches back through the same path.

## Side effects and how to test them

All **[unverified]** until tested in Azahar:

1. **Everything runs at 2x**: walking, NPCs, menus, text scroll, battle
   animations, Rotom, cutscene animation. Expected by design (frame-locked
   logic) and by STATUS.md's "faster".
2. **Music and voices stay at normal speed** (audio is played by the DSP in real
   time), so scripted cutscenes and evolution/hatching scenes timed to music may
   drift out of sync. Test: the Necrozma and Ultra Wormhole cutscenes, an
   evolution, an egg hatch, the Hall of Fame, a Z-move.
3. **Frame drops become slowdowns.** At 60 the game must finish a frame in one
   vsync (16.7 ms); where it can't (double battles, Festival Plaza, crowded
   areas), it falls back to a slower speed, frame by frame. Test with
   `tools/perf.py` on the field, a double battle, Festival Plaza. Azahar's CPU
   clock setting may help (not checked).
4. **Azahar host cost doubles**: twice the emulated GPU work per second, and the
   panel's GPU copies run twice as often (docs/PERF.md measured 29.9 fps game
   rate with the panel on).
5. **Input**: with `+8` the game's own device switch runs. Check key repeat in
   menus, circle-pad walking, touch on the bottom screen and the panel cursor.
   With the raw cheat write (`+9`) inputs may be read every other frame.
6. **The plugin's own frame counts** (FieldSettle's 60 frames, the 2/3 panel
   redraw every 2nd frame, readback every 128 frames, command polling every 30
   frames, scenario timeouts "in frames, 60 a second") will run at a different
   real-time rate if the plugin's callback follows the game's frames.
   Whether CTRPF's `SynchronizeWithFrame` callback is per game frame or per
   vsync was not checked. Run the scenarios in `scenarios/` with 60 on.
7. **RNG / timers**: anything advanced per frame advances twice as fast. Real
   time clock features (Poké Pelago, berries, day/night) should not change.
   Online features (GTS, battles over the network) were not considered; keep
   the mode at 30 for them or leave it to the user.
8. **Saving**: no reason to expect problems; still check a save/load cycle
   with 60 on.

## Open questions

- Is the device "latch every 2nd update" really about game frames, and does
  switching to mode 0 change key-repeat timing in real time?
- What do the `TimeIcon::StartAnime` callers pass, and does any CRO read the
  frame mode through `GameManager` (e.g. Battle, FieldRo)?
- What is `GameManager + 0x10` (1.0f)?
- How often does the plugin's per-frame callback run in each mode?
- How far below 60 does the game fall in Azahar at the usual settings?
