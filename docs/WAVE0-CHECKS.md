# Wave 0 in-game checks

The in-game checks that Wave 0's PRs left for later, run on 2026-11-02 against `main` at `7bcde4f` in an isolated Azahar instance (`tools/instance.py`, instance `w0`, a copy of the save; the real save's mtime and SHA-256 were unchanged afterwards). All input was injected with `tools/cmd.py` (no screen or keyboard control).

Evidence files are local only (game imagery, not committed): screenshots in `analysis/private/shots/` (`NAME-top.png` / `NAME-bottom.png`), logs and crops in `analysis/private/w0-evidence/` of the `claude/w0-ingame-checks-d02ff4` worktree. Runs: **1** = default ini, written on first boot; **A** = edited ini with bad lines; **B** = `auto = off`; **C** = default ini again, with the Raster fix below.

Result key: **pass**, **fail**, **partial** (part checked, the rest is listed), **needs Tyler** (needs a real controller or keyboard).

## UI1: drawing layer (#5)

| Check | Result | Evidence |
|---|---|---|
| `uidemo on` draws the menu over the field: rounded panel, border, alpha shade, divider rules, highlight, icons (✕, ▸, ›, ✓, ▲▼), right-aligned values, `...` truncation, 5×7 footer | pass | `w0-ui1-field-top`, `zoom-ui1-topleft.png`, `zoom-ui1-rows.png` |
| Highlight moves once a second (redrawn every frame) | pass | highlight on Party, Bag, Auto-save, Options in successive shots |
| Over the corner panel (X menu) and over full view: the menu draws on top, the shade dims the panel | pass | `w0-ui1-xmenu-panel-top`, `w0-ui1-full-top` |
| `uidemo off` removes it, nothing left at the top-left (the sysfont scratch strip) | pass | `w0-ui1-off-panel-top`, `w0-ui1-field-top` (clean corner while on) |
| `tools/shot.py` includes the menu | pass | all `w0-ui1-*-top` shots |
| System-font mask: drawn width equals `GetTextWidth`; glyphs start at `posY` and stay within 16 px | pass | diagnostic build: raw `DrawSysfont` with guides at posY / posY+16 and a tick at the measured width, `w0-diag-field-top`, `zoom-sysfont-raw.png` |
| System-font look: descenders (`y p g j q`) are cut off, letters are widely spaced, a stray dot appears under some letters (`Options`) | fail, **open issue 1** | same in raw `DrawSysfont` output, so not caused by the mask trick: `zoom-sysfont-raw.png` |
| Frame cost | fail, then **fixed** (partly): top-screen fps in the field 47 → **5.8** with the demo on. Raster fix (below): **23** fps on, 50 off. Still **open issue 2** | fps from the `top frames` counter over 10 s; diagnostic tick counts in `log-3-runA.txt`, `log-4-runB.txt` (`W0DIAG` lines) |

## UI2: input capture (#2)

| # | Check | Result | Evidence |
|---|---|---|---|
| 1 | `capture on a` in the X menu, then `hold a` / `press a`: menu stays; status `capture 00000001`, `real held 00000001`, `last held seen 00000000` | pass | `w0-ui2-1-before`, `w0-ui2-1-after`, `log-2-ui1-ui2.txt` |
| 2 | Hold A, `capture off a`, keep holding: no action (`suppressing 00000001`); release, `press a`: Party opens | pass | `w0-ui2-2-held`, `w0-ui2-2-released`, `w0-ui2-2-pressA` |
| 3 | `capture on stick` + `stick 0 -156 900`: player doesn't move; `capture off stick` while tilted: still doesn't move (`suppressing 80000000`) until the stick is centred; a fresh `stick` walks | pass | `w0-ui2-3-a0` … `-e-walk` (diff vs a0: 1.7–2.3 k px of NPC motion while captured, 72 k px after walking out) |
| 4 | `capture on touch` + `touch on` on POKÉDEX: game sees no touch (`last seen 0,0 touching 0`, `touch captured 1`); `capture off touch` while touching: still none (`suppressed 1`); `touch off`, `tap`: the game sees it (`last seen 230,55 touching 1`, cursor moves to POKÉDEX) | pass | `w0-ui2-4-a` … `-d-tap` |
| 5 | Injected `press`/`hold`/`stick` during capture are hidden from the game but in `real held` | pass for keys and stick; touch: `CaptureRealTouch` isn't printed by any status line, so not observed | status lines in `log-2-ui1-ui2.txt` |
| 6 | Capture begun while a D-pad direction is held: no stuck key or phantom move after `capture off` + release; the next Down and Up each move one row | partial: pass as stated; whether menu key-repeat stopped *during* capture was not observable (the cursor had already reached the end row) | `ui2-dpad-held-at-begin.png`, `ui2-dpad-strip-full.png` |

## P1-4: settings file and menu (#4)

| Check | Result | Evidence |
|---|---|---|
| No ini on the SD: boot writes `/usum-onescreen.ini`, log `settings: wrote defaults to /usum-onescreen.ini` plus the summary | pass (runs 1 and C) | `ini-default-written.ini`, `log-1-boot-field-re2.txt` |
| Defaults: title off, field off, X menu panel, Party full, Bag panel, Pokédex panel, Battle panel | pass | run 1: field `mode off`, X menu `mode panel`, Party `mode full` (`w0-ui2-2-pressA-top`); run A: Bag `mode panel`; run C: Pokédex (`w0-C-zukan`) and Battle (`w0-C-battle`) `mode panel` |
| Edited ini (run A: `panel_key = L+R`, `touch_key = Y`, `cursor_speed = 12`, `corner = top-left`, `Battle = full`, Party `= panel`): summary shows each | pass | `ini-edit-A.ini`, `log-3-runA.txt` |
| … `corner = top-left` draws the panel top-left | pass | `w0-A-xmenu-topleft-top` |
| … Party `= panel`, `Battle = full` take effect | pass | `w0-A-LR-full-top` (Party as panel), `w0-A-battle` (`mode full`) |
| … `auto = off` (run B): X menu opens, view stays off | pass | `ini-edit-B.ini`, `w0-B-xmenu-autooff`, `log-4-runB.txt` |
| Bad lines give `line N` warnings, defaults kept, no crash: unknown section, bad corner, bad size, line without `=`, bad scene mode | pass | `log-3-runA.txt` lines `settings: usum-onescreen.ini line 43/46/47/48/50: ...` |
| `panel_key`, `touch_key`, `cursor_move`, `cursor_speed` change the controls | needs Tyler: they are read through CTRPF `Controller` (real HID), which injected input doesn't reach. Injected L+R did not cycle the view (it opened Party from the X menu: the game acted on the injected L/R, cause not checked) | `Sources/main.cpp` `Controller::IsKeysPressed(panelKey)`, `Sources/Cursor.cpp` `Controller::GetKeysDown()` |
| CTRPF menu (Select): four entries, Auto panel toggle, View settings MessageBox wrapping, Reload settings file, action entries run on A | needs Tyler: CTRPF opens its menu from real HID, not reachable headlessly | |

## RE2: game text (#6), checks from `docs/RE2-text.md`

| # | Check | Result | Evidence |
|---|---|---|---|
| 1 | `peek 671A10 1` = `02` on title, field, battle | pass | `00000002` in all three |
| 2 | `peek 671084 2` = two heap pointers | pass | `33F3B818 33F3BA88` |
| 3 | Species MsgData `+00 0064B104`, `+08 01`, `+0C 3C`, `+18` non-zero, `+2C FFFFFFFF` | pass | `0064B104 33F17FB0 00000001 0000003C 00000000 33F3BB20 33F3BB60 … FFFFFFFF`; header `03280001` (808 lines), section size `51DC` (20956) as documented |
| 4 | Line 25 decrypts to `Pikachu` | pass | entry `00001B04 00000008`, words `52CD8A04 A9759549 454248C6 4A912927` → `Pikachu` (same as the static reference). Item line 1 from slot 0 also decodes: `Master Ball[VAR 1101 00FE 0100]s` |
| 5 | Live MsgData reachable from globals per scene; any moves file (`0x76`)? | partial (field, X menu, Bag, Battle; Party not scanned) | `vtscan` finds, in every scene, one full-load dat `0x3A` object (reached from `0x671C0C`, `0x671C10`, `0x6A7570`) and one full-load dat `0x00` object (`0x6713DC`); the Bag adds a second dat `0x00` (`0x6A5DFC`). **No dat `0x76` one level from a global**, so move names still need a deeper pointer chain or a fallback. `usum-vt-w0{field,xmenu,bag,battle}.bin` |
| 6 | `peek 6A7368 32`: slot 0 `+08 = 002A`, `+0C` MsgData; slots 1–7 empty | pass | `… 0000002A 336CDD78`, rest zero; item MsgData `+0C = 0000002A`, section size `9EC8` (40648) |
| 7 | Battle: 2–4 and 6 again, same pointers | pass | identical words in battle, Pikachu decodes |
| 8 | After soft reset / save reload: 2 and 6 again | partial: after a full reboot (3 boots) the pointers are identical. Soft reset (injected L+R+Start in run C) made Azahar exit: **open issue 3** | `log-5-runC.txt`, `azahar-log-softreset.txt` |
| 9 | `peek 692760 1`, then `+0x4C`: arc path pointer and `0000003C` | pass | `33F1838C` → `33F18320 0000003C`; `33F18320` holds `"a/0/3/2"` |

## Fix in this PR

`Sources/UI/Raster.cpp`: translucent spans (full-screen shades, panel backgrounds) had per-pixel clip checks, format switches and three library divides by 255 (the ARM11 has no divide instruction). Now `Div255` is a shift-and-add (checked equal to `(x + 127) / 255` for every input up to 255·255 + 255), and `Span` has a hoisted path with a BGR8 inner loop. Host tests pass, and the host-rendered `uidemo` preview is pixel-identical before and after. In game, one full-screen alpha fill went from ~32 M to ~3.1 M emulated ticks and a demo frame from ~98 M to ~15 M; fps with the demo on went from 5.8 to 23.

## Open issues

1. **System-font text looks wrong in Azahar** (UI1): CTRPF's `DrawSysfont` draws with descenders cut off, wide letter spacing and a stray dot under some letters. Seen in raw `DrawSysfont` output, so the mask trick only copies it. Next step: check on hardware or in CTRPF's own menu (Tyler), or render glyphs from the shared system font (`CFNT`) directly.
2. **`uidemo` still costs about half the frame rate** (UI1): 23 fps vs 50. What's left: `DrawMask` and the anti-aliased corners still go through `BlendPixel` (clip check, format switch per pixel, float `sqrt` per corner pixel), plus 8 `DrawSysfont` calls and readbacks per frame (~2.3 M ticks). Likely fixes: cache text masks and corner coverage, draw masks with a hoisted span like `Span`, and redraw only on change. Overlaps the H5 perf item.
3. **Azahar exits on soft reset** (L+R+Start injected through the plugin, in battle → field). The log ends while Azahar recreates the renderer for the relaunched title. **Cause found (P1-8, `docs/SOFT-RESET.md`): an Azahar 2126.2 bug, not this plugin.** `System::Reset` carries the plugin loader's stale `memory_changed_handle` into the relaunched title, and `PLG_LDR::OnMemoryChanged` crashes on it (access violation in `HandleTable::GetGeneric`). A minimal CTRPF plugin crashes the same way. With the plugin loader off, a soft reset by input movie works in the field and on the title.
4. Test tooling: `cmd.py "mode X"` is undone by auto panel on the next frame (it doesn't set the manual override). Use `auto off` first. Not a player-facing bug.
