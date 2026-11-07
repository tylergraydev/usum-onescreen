# Instant text and fast battles

For Pokemon Ultra Sun/Moon USA v1.2. All options default to stock timing:

```ini
[speed]
instant_text = off
battle_messages = normal
battle_bars = normal
```

`instant_text = on` removes typing delays for the table-based MessageWindow paths,
including field and battle text, at every in-game text-speed setting. Newly created
or re-sped windows pick it up; an already open field window can keep its old speed.
The Options screen and saved text-speed selection are unchanged.

`battle_messages = fast` shortens the normal message and auto-mode waits and the
page-break compare from 24 to 8 frames. `instant` uses 1 frame, never zero.
Key-only confirmations still require a button press. `battle_bars = fast` advances
HP by four frame steps and EXP by 8 percentage points per update (stock: 1 and 2).
`instant` jumps HP to the final frame and advances EXP by 100, keeping the game's
clamp and level-up handling. These options can be set independently in the INI.

**Select** opens the plugin menu. **Instant text: on/off** toggles typing;
**Battle speed: Normal/Fast/Instant** cycles both battle settings together. If the
INI sets different message/bar speeds the entry shows **Custom**, and its next
press selects Normal. Changes apply live and save through `SetIniValue`, preserving
other lines and comments. Reload settings also applies changes live. Setting
normal/off restores stock values owned by the plugin.

Commands in `/usum-cmd.txt`:

- `fasttext`: log requested/applied text state and requested battle settings.
- `fasttext on|off`: apply and save typing speed.
- `battlespeed normal|fast|instant`: apply and save both battle speeds.

## Patch addresses

All values below are **32-bit words**, not bytes. Main-image text tables are six
four-word arrays spanning `0x5D605C` through `0x5D60B8` inclusive (96 bytes).

| Table address | Stock four words | Instant four words |
|---|---|---|
| `0x5D605C` wait, flag 0 | 1,0,0,0 | 0,0,0,0 |
| `0x5D606C` CJK count, flag 0 | 1,1,2,255 | 255,255,255,255 |
| `0x5D607C` wait, flag 1 | 2,1,0,0 | 0,0,0,0 |
| `0x5D608C` CJK count, flag 1 | 1,1,1,255 | 255,255,255,255 |
| `0x5D609C` Western count, flag 0 | 2,2,4,255 | 255,255,255,255 |
| `0x5D60AC` Western count, flag 1 | 2,2,2,255 | 255,255,255,255 |

Code addresses are offsets from the loaded Battle.cro header:

| Offset | Stock / normal | Fast | Instant |
|---|---|---|---|
| `+0x6358C` key-or-timer message wait | `E3A02018` | `E3A02008` | `E3A02001` |
| `+0x63508` auto-mode message wait | `E3A02018` | `E3A02008` | `E3A02001` |
| `+0x9BF28` page-break compare | `E3500018` | `E3500008` | `E3500001` |
| `+0x2A27C` HP frame step | `B2800001` | `B2800004` | `B1A00001` |
| `+0x2A2D0` EXP percent step | `E2800002` | `E2800008` | `E2800064` |

The key-only wait instruction at `+0x63538` and existing zero-timer path at
`+0x63568` are unchanged. Both games share the patched addresses/stock words.

## Patching and diagnosis

Every frame checks the current scene object/vtable; `CroModuleOf` resolves the
module once when the scene changes, retrying until known. On the first Battle
frame with a known module, all five words are checked before any writes. The
expected values start at stock for each new battle; live changes accept only
values successfully written by this plugin. The text patch likewise checks all
24 expected words before writing. `MISMATCH` means no writes in that attempt.
No patch failure is retried every frame; changes/new battles trigger new checks.
A failed write can leave a partial patch and is logged as such.

Code writes use `Process::Patch`, whose installed CTRPF `Process.hpp` documents
cache invalidation. If it fails, `svcQueryMemory` captures the original permissions,
`Process::ProtectMemory` makes the containing 4 KiB page RWX, and `Process::Patch`
is retried before restoring the original permissions. The retry still uses
CTRPF's cache handling; there is no raw instruction store or extra cache syscall.
A successful fallback is named in the battle log. Patch, protection, and permission
restoration failures are logged. The main-image table uses `Process::Patch` too.

One result line is logged per battle, plus a line for each live battle-speed
change, for example `fast-text: battle.cro at XXXXXXXX patched messages 8f, bars fast`.
With default normal settings it verifies stock without writing. Check the log for
`MISMATCH` or `Process::Patch failed` before trusting the requested settings.

## Limits and verification

- Button presses are still needed at page/message ends wherever the game requires them.
- Timed text tags, cut-scene waits, battle animations, sounds and separate battle sequence timers are unchanged.
- Windows using the second constructor `0x3D1CA0` (Moon's shifted equivalent), or direct speed-setting calls with their own counts, are not covered.
- Research verified the stock text data live and the proposed paths statically. The new patches, RX-page fallback, cache effects and timing need a user build and in-game validation on both games.

After building, compare NPC text and two consecutive wild battles in Normal,
Fast and Instant. Check HP loss, EXP gain across a level-up, forced confirmations,
page breaks, and toggling back to stock during a battle. Check the single result
line for each load, persistence after restart, and INI reload with mixed speeds.

Research source: [instant-text.md](../analysis/private/research/instant-text.md)
(static disassembly/decompile, live stock-table reads, and Sun/Moon comparison).
