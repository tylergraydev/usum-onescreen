# PC anywhere (Ultra Sun / Ultra Moon USA v1.2)

Opens the Pokémon Storage System (the Boxes) from the overworld, wherever the player stands, as if they had used a Pokémon Center PC. Closing the Boxes (B) returns to the field at the same spot.

- **Key:** `pc_key` in `[controls]` of `usum-onescreen.ini`, default `none` (off). Any combo the other key settings take, for example `pc_key = R+X`: hold **R**, then press **X**. The combo's keys are hidden from the game from the moment the combo is complete until each is released, so the **X** that completes `R+X` does not also open the X menu.
- **Plugin menu (Select):** *Open PC (Boxes)*. The Boxes open once the menu closes; a refusal shows in a message box.
- **Command:** `pc` (open), `pc status`.
- **Log:** `pc: opened [source] (EventAppCall …)`, `pc: box started (BoxEvent …)`, `pc: back in the field`, or `pc: refused (reason) [source]`.

`Select+X` is not a good combo: **Select** alone opens the plugin menu (CTRPF's hotkey, which reads the buttons itself, so the plugin cannot hide it). Combos with **L** press **A** too on saves with the Options button mode *L=A*. Not checked with a real controller: only injected keys (`hold r`, `press x`) were used.

## How it works

The game opens its own full-screen apps from the X menu with an event, `Field::EventAppCall`: it saves the player's place, closes the field (`EventFieldmapClose`), starts the app, and when the app ends reopens the field at the saved place (`EventFieldmapOpen`). The PC's script starts the Box with `App::Event::BoxEvent`. PC anywhere puts the two together, on the game's own thread:

1. **Plugin thread** (`PcAnywhereRequest`): checks the scene and field (rules below), remembers the scene object and the field state object, and marks a request as pending.
2. **Game thread**, a hook on `GameEventManager::Main` (runs once per frame and runs the top event): while a request is pending, it checks again that the game manager, scene object and field state are as seen, and that no event is running or queued. Then it calls the game's EventAppCall start with app type 0, which the event's own app switch doesn't know (so it starts no app itself), and sets the event's between-app callback to the plugin's. If an event is still running after 30 frames the request is refused.
3. EventAppCall closes the field (fade, field CRO unloaded), then calls the callback (game thread, inside the event manager). The callback starts a BoxEvent on top of it with mode 0, as the PC script's Box command does, and no result listener.
4. The Box app runs (scene `Box`; the plugin's native Boxes screen appears as at a real PC). When it closes, EventAppCall reopens the field at the saved place.

Nothing is patched in the game's code apart from the hook, and the game's own functions do all the work, so the field, the party (the Box screen edits a copy that the game writes back on close) and the boxes are handled as at a real PC.

### Addresses (USA v1.2)

| What | Address | Notes |
|---|---|---|
| Game manager | `*(0x6A59C4)` | `+0x20` → GameEventManager, `+0x24` → field (0 while closed) |
| `GameEventManager::Main(manager)` | `0x404350` (Ultra Moon `0x404354`) | hooked (MITM). Manager `+0` top event, `+4` gm, `+8` queued event, `+0xC` "event active". Pushing an event: `0x4044A8` (`ev+4 = top; top = ev`). Its only caller is `0x402B44`. |
| EventAppCall start `(gm, type, flag)` | `0x37F54C` | allocates `0xA8` bytes, constructor `0x392B28`, pushes, sets type `+0x58`, fade `+0x1C` (table at `0x5CA274`), reopen `+0x48 = 1`, callbacks `+0x2C` (after reopen) and `+0x30` (between), `+0x34` = the event (the callbacks' argument is `ev + 0x34`) via `0x3925E0`. The X menu's dispatcher is `0x37F6A4`. |
| EventAppCall vtable | `0x64BCD0` | `MainFunc` `0x39268C`: state 1 saves zone and the player's position and rotation and pushes `EventFieldmapClose` (vtable `0x64BFFC`, ctor `0x39E20C`); 3 starts the app (`0x37F110(type, gm, ev + 0x80)`, nothing for type 0); 4 calls `+0x30`; 5 pushes `EventFieldmapOpen` (vtable `0x64BF80`, ctor `0x39D8FC`) with the saved place; 6 calls `+0x2C`. |
| BoxEvent start `(gm)` | `0x2DE91C` | allocates `0xB8` bytes, constructs (vtable `0x647F58`), pushes. Mode u8 `+0x54` (not set by the constructor; 0 = the PC, as the script's command `0x384DC4` sets; 1-6 are network and other callers), listener `+0xB4` (0 = none). `MainFunc` `0x2DEC0C` starts the Box app through `0x402014` and waits for it. |
| Script: Box command | `0x384DC4` | `BoxEvent start`, mode 0, a `ScriptBoxEvenListener` (vtable `0x64BCC0`); the script closes and opens the field with its own commands (`0x37FF60` close, `0x37FCE0` open). |

All of these were found in `analysis/private/usun-v1.2-0820dec/code.bin` with `tools/find_refs.py`, `tools/find_calls.py` and Ghidra (RTTI names: `N5Field12EventAppCallE`, `N3App5Event8BoxEventE`, `N5Field17EventFieldmapOpenE`, `N5Field18EventFieldmapCloseE`). Ultra Moon's `code.bin` has the same words at every address except `GameEventManager::Main`, 4 bytes later. At boot the plugin finds each function by its first instruction words within ±0x40 bytes of the Ultra Sun address, checks the two vtables, and logs `pc: event main …, app call …, box event …, vtables ok; hook -> 0`. If anything doesn't match, PC anywhere stays off and every request is refused with that line. Ultra Moon was not booted with this build.

## When it refuses

`Includes/PcAnywhereRules.hpp` (host-tested in `tests/pc_anywhere_test.cpp`):

| Refusal | When |
|---|---|
| `already opening` | a request is still on its way |
| `in battle` / `the boxes are already open` / `loading` / `not in the field (scene …)` | scene is not `FieldRo` |
| `a plugin action is running` | the action layer is driving the bottom screen |
| `in Festival Plaza` | the `JoinFesta` CRO is loaded (not tried in-game) |
| `a menu is open` / `talking` / `encounter starting` / `field busy (state N)` / `field state unknown` | field state (docs/OFFSETS.md) is not 0 |
| `a script or event is running` | the event manager has a running or queued event (cut-scenes, scripts, the X menu, doors) |
| `the scene changed` / `the field is busy` / `the field closed` / `a script or event is running` / `the game didn't start the event` | late, on the game thread, if things changed before the next frame or an event stayed for 30 frames |

Riding and surfing aren't refused: the field close and reopen are the same ones the X menu uses for its apps, which the game allows while riding. This save has no Ride Pager, so it was not tried.

## Checked in game (2026-11-07, instance `player2`)

- From the `pokecenter` start point (Route 2 Pokémon Center, not at the PC): opened three times with the final build (`pc`, `R+X`, `pc`), each time moving a Pokémon between Box 1 and Box 2 with the native Boxes (A, R or L, A) and leaving with B. `where` before and after was the same every time: zone 79, position 2175.9 4.0 2522.7, facing −58 (SW). Before that, with the build before the battle fix: three more (`pc`, `R+X`, `pc`), the third swapping Torracat (party) with Caterpie (box): after B the party read Caterpie first, as the Box had left it.
- Route 2 (`grass` start point): opened with `R+X` and with `pc` after a wild battle, moved Buneary to Box 2, closed; back at the same position (zone 7, 10841.8 −157.3 7696.0 and 10885.6 −157.1 7838.7). Wild battles before and after.
- Refused: with the X menu open (`a menu is open`), while the Pokémon Center nurse talked (`talking`), and in a wild battle by command and by key (`in battle`). The X menu opened and closed normally after the PC.
- Opened while walking (stick held): the field reopened where the field closed, a few steps on (2582 → 2634 along x), facing the same way.
- **Save data** (not saved): the SaveData object (`*(*(0x6A59C4) + 0x24) + 4`, `0x80000` bytes) dumped before and after one open/move/close. The only differences were the game clock (`+0x3B38` object, vtable `0x64F440`, which also changes while idle), the BOX block byte `+0x417F` (0 → 1: the box the Box screen last showed), and the two box slots the Pokémon moved between (`+0x4188` box 1 slot 0, `+0x5CB8` box 2 slot 0). Nothing else in the save data changed.
- Music: not checked; test instances run muted.

Screenshots (local only, `analysis/private/shots/`): Box opened from the Pokémon Center entrance `20261107-095711-top.png` / `-bottom.png`, back in the field `20261107-095753-top.png`; final build runs `20261107-110318`, `-110328`, `-110352`, `-110402`, `-110422`, `-110432` (`-top.png`); Route 2 Box `20261107-100453-*`, back in the grass `20261107-100503-top.png`, Box 2 after a battle `20261107-110112-bottom.png`; the nurse refusal `20261107-100257-top.png`.

## The battle freeze (fixed in `main.cpp`)

The first build with PC anywhere froze the game at the start of every wild battle on Route 2, whether or not the PC had been opened, and with the PC code switched off; `main` didn't. The cause, found with Azahar's GDB stub (each instance has one; `arm-none-eabi-gdb`, `target remote localhost:<gdbstub_port>`) and a watchpoint:

- CTRPF also runs the plugin menu's per-frame entries from its `LoadCRO` hook, on the game thread that loads a CRO. That thread's stack is small and lies in the game's heap.
- When `Battle.cro` loaded, `PanelEntry` (→ `RotomPromptUpdate` → `DescribeRotomView`) ran on that thread and its stack went below the thread's stack, over the mutex of the game's ExpHeap at `gm + 0x104` (mutex `gm + 0x15C`): a `std::string` was built on top of it. The main thread then spun forever trying to lock that heap (`0x1076A8` → `0x108BF8`).
- `main` was just under the limit; any extra stack in `PanelEntry` (here, the PC log vector and calls) went over.

`PanelEntry` now returns at once unless the stack pointer is in the plugin's memory (`0x06000000`-`0x07FFFFFF`), so it only runs on the plugin's own thread. With the check, wild battles started normally every time (three battles on Route 2, one right after the PC); without it the same code froze on every try, and `main` started six of six.

## Limits and risks

- Calling game functions and pushing events from a hook is new for this plugin. The checks (no event, field state 0, same scene object, the vtables) keep it to moments the game itself would allow an X menu app. Unusual maps (Festival Plaza, Battle Tree, Ultra Space, Mantine Surf, trials, places where the X menu is limited) were not tried; the field reopen uses the saved zone and position, which those maps may not expect.
- Box mode 0 is the PC script's. What the Box screen offers (Battle Box, team tabs) was not compared with a real PC beyond opening it, moving Pokémon and leaving.
- The `JoinFesta` check for Festival Plaza is untested, and other "no PC here" places (if the game has any) aren't detected.
- A request is refused rather than queued when the field isn't idle; holding the combo again later works.
- Ultra Moon: the addresses were checked in its `code.bin`, not in game.
