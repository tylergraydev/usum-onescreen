# Native battle menu (P2-1)

In battle the top screen shows the game's choices itself, so a battle is played with buttons only and no panel. Code: `Sources/Screens/BattleMenu.cpp` (pure: pages, input, the step driver), `Sources/Screens/BattleMenuDraw.cpp` (pure: drawing in the game's look, `docs/VISUAL-STYLE.md`), both host-tested by `tests/battle_menu_test.cpp` (which also writes `analysis/private/battle-*.ppm` previews), and `Sources/Screens/BattleScreen.cpp` (plugin glue). It is on by default; `battleui off` gives battles back to the panel (the `[scenes]` `Battle` setting).

## What it shows

| The game's bottom screen | Top screen | Keys |
|---|---|---|
| intro, messages, animations | nothing (the battle text and HP bars stay clear) | go to the game |
| command menu | command bar: *What will X do?* over Fight / Bag / Pokémon / Run as option pills | Up/Down, A |
| move list | the game's move buttons in the move's type colours: name, type plate, PP (yellow / orange / red as it runs out), effectiveness against the target (*Super effective*, *Not very effective*, *No effect*) | Up/Down, A, B back |
| Pokémon list (and a Pokémon's page) | the native Party screen's list (`Screens/PartyMenu.hpp` `DrawParty`) in battle order, nicknames, HP, status, held item; A: *Switch* / *Cancel* | D-pad, A, B back (none after a faint) |
| Bag | the five battle pockets as tabs, the open pocket's items with counts (ball icons in Poké Balls); after *Use* on an item that needs a Pokémon, *Which Pokémon do you want to use this item on?* with the party (level, HP bar) | Left/Right or L/R pocket, Up/Down, A use, B back |
| *Use next Pokémon?* (a Pokémon fainted) | *Use Next Pokémon* / *Run* | Up/Down, A |
| nickname question after a catch (scene `Zukan`) | helper at the top right (the question is along the bottom): *Give it a nickname?* | go to the game: B no, A yes (the keyboard is on the bottom screen) |

Where it draws: the command bar, move list and question right of x 166 and below y 36 (the after-catch helper above y 86), so the opponent's HP box (top right), the player's HP box (bottom left) and the battle messages (bottom, only shown while no menu is up) stay readable. The Pokémon list and the Bag cover the scene (376x224 at 12,8, as the field screens), as the game's own cover the bottom screen. Pages are drawn into a `UI::Layer` when what they show changes and copied every frame (`docs/UI.md`); the plugin never reads the top framebuffer for them.

## Layouts

`[battle] layout` selects `classic` (the default), `bar`, or `rail`; see [Playing: Battle menu layout](PLAYING.md#battle-menu-layout) for the ini, plugin menu and `battlelayout` command. A change also takes effect during a battle without resetting the selection.

- **Classic** keeps the original right-hand command column and Up/Down navigation. The moves page uses compact rows and a selected-move detail strip above the A/B prompts; its area stays x 186–396, y 36–238.
- **Bar** puts a small caption above one horizontal row of rounded, colour-coded Fight / Bag / Pokémon / Run pills at x 170–396, y 166–236. Its moves are a 2×2 grid at x 170–396, y 74–238, with type plates, PP and triangle markers (up: super effective, down: not very effective; X: no effect). Left/Right cycle commands; on moves they switch columns within the same row (a lone final move stays put). Up/Down cycle all entries in list order on both pages.
- **Rail** stacks slanted pills along the right edge, each lower command shifted left, with coloured tabs and light bodies. Commands occupy x 202–396, y 52–238; type-coloured move bars occupy x 170–396, y 38–238 and spell out effectiveness. Up/Down cycle entries as in Classic.

On every moves page, the strip below the list/grid shows the selected move's category icon and name (Physical / Special / Status), power, and accuracy (`Acc`). Classic and Rail tighten their rows; Bar grows upward to y 74 to retain its 2x2 tiles. The strip uses measured Small game-font text, falling back to the existing compact bitmap text if it does not fit. Command and other pages are unchanged.

Details flow from the already-loaded `UI::AssetMove` table (`a/0/1/1`, `DecodeMoveEntry`) through the snapshot's `MoveRow.power`, `.accuracy`, and `.category`. Type/effectiveness still come from the game's move list when available, with table type as the fallback; PP still comes from the battler. Status moves and zero power show a dash; accuracy 0 or greater than 100 also shows a dash (drawn as an ASCII hyphen, since the move text may fall back to the bitmap font). `MoveData.hpp` and `OFFSETS.md` document 101 as never misses; `DecodeMoveEntry` preserves raw bytes, and the move-learning formatter already treats both 0 and >100 as a dash. Unknown category remains a dash without an icon. Detail values and cursor changes invalidate the draw key.

The command order is always Fight, Bag, Pokémon, Run: Down twice selects Pokémon and Up from Fight wraps to Run. A chooses/uses and B backs out of moves in every layout. PP uses the same colour thresholds as Classic. All three leave the opponent HP box (x ≥ 240, y < 36) and player HP/message region (x < 166, y ≥ 180) clear. Each command/moves page is one small opaque window in a `UI::Layer`; changing layout invalidates its draw key and rebuilds the layer when its rectangle changes. Party, Bag, Question, Target and after-catch helpers are identical in every layout.

## How a choice is carried out

The menu follows the game's bottom-screen menu (RE1 `ReadBattle`, read every 2 frames) and acts by tapping the game's own button through the action layer: `battle-fight`, `battle-bag`, `battle-pokemon`, `battle-flee`, `battle-move1..4`, `battle-moves-back` (`docs/ACTIONS.md`). Rules, each from a run:

- A tap the moment the command menu slides in only moves its cursor (UI3), so a choice made earlier is held until the game's menu has been up 30 frames (10 for the move list).
- After a move or an item the game shows its command menu for about 22 frames before the turn starts. The command bar waits 30 frames before it shows (and logs), so it doesn't flash.
- While the command bar or move list is up, A, B, the D-pad and the circle pad are captured (`InputCaptureBegin`); Y (the game's quick Poké Ball) and touch still reach the game. The move list also captures L for native descriptions. On the game's own screens nothing is captured.
- A move with no PP left isn't sent while another move has PP (*X has no PP left*); with none left, any move is sent and the game uses Struggle.
- A move the game refuses (seen: Scratch under Drowzee's Disable) leaves the move list up; after the action's three taps the menu says *The game refused that move, pick another*. Disabled/Taunted moves aren't known in advance (not in RE1).
- Learning a fifth move (*Forget an old move?*) has its own screen, `docs/MOVE-LEARN.md`; while it is up the helper below stays away.
- Any other game screen in battle (seen: *Use Next Pokémon / Run* after a faint) gets a helper at the top right once it has stayed 1.5 s: *The game asks — the choices are below*, with its vtable in the log (`prompt, vt …`). The choices themselves are only on the bottom screen (the panel key, L+R by default).
- A new species' catch shows its Pokédex page before the nickname question; B goes through all of it (No to the nickname).
- Effectiveness and type come from the game's own move list, which is up whenever the move list is shown, so no type chart is needed. Before it is up the type comes from the game's move table, and type names from its text (W7-0 assets, `UI/Assets.hpp`).

### Pokémon list, Bag and *Use next Pokémon?* (W7-1)

These pages carry a choice out one step at a time: each step is one tap (or B) through the action layer (`battle-step`), then the page waits until the game has looked the same for 12 frames and decides the next step from what it reads (`docs/OFFSETS.md`, "Battle Pokémon list, Bag and questions"). A goal that takes more than 16 steps or 15 s, or sends the same tap three times without the game moving, ends with *The game didn't answer, try again*.

- **Switch**: tap the slot (`party.slotN`: the battle list uses the Party screen's slot places), which opens that Pokémon's page; tap *Switch*. Between the list and the page the game shows no screen and then its command menu for a few frames; the page stays up through that until the last tap is sent. Refused natively: the Pokémon in battle (*X is already in battle!*), a fainted one (*X has no energy left to battle!*), an Egg.
- After a faint the game asks *Use next Pokémon?* (wild battles; message `0x4E`): the native question taps `battle_ask.first` / `.second` (Use Next Pokémon was tapped; Run's place is verified on the same object through *Keep old moves!*, but Run's own outcome was not seen). *Use Next Pokémon* opens the list without a back button; the native list then says *Choose a Pokémon to send out* for B. Other questions of that kind (not learning a move) keep the *The game asks* helper.
- **Bag**: Left/Right tap the pocket tabs until the game shows the pocket. A on an item taps the first row (its page), then ◀ / ▶ until the page shows that item (work `+0x240`), checks it is still the item chosen, and taps *Use*. A ball is thrown and the Bag closes. An item that needs a Pokémon makes the game ask *Which Pokémon...?*: the native pick starts on the first Pokémon the item would help (HP items: hurt and not fainted); A taps that slot (twice when the game's cursor is elsewhere: the first tap only moves it). HP-only items (Potion ... Sitrus Berry) are refused on a Pokémon at full HP or fainted (*It won't have any effect.*), because the game took the turn anyway when that was tried. B in the pick presses B (back to the list), B on the list taps the Bag's back button.
- Nicknames, held items, status and shiny/HA markers use checked battle PokemonParam copies, with identity-matched field-party fallback; never index the field party by battle-team index. See below.
- Move names come from the battle's own copy of the move-name file (`docs/OFFSETS.md`, "Battle state"); species and item names as in RE2/RE3.

Log lines: `battle-ui: page command, cursor 0 of 4 (Fight)`, `battle-ui: page moves, cursor 1 of 4 (Fire Fang, Fire, PP 15/15)`, `battle-ui: chose battle-fight`, `battle-ui: page party, cursor 1 of 6 (Zorua Lv14 35/35), menu Switch`, `battle-ui: page bag, HP/PP Restore, cursor 0 of 4 (Potion x9)`, `battle-ui: page bag, HP/PP Restore, pick 0 of 6 (Zorua 32/35), busy: Using Potion on Zorua...`, `battle-ui: step bag_party.slot1`, `battle-ui: page question, cursor 0 (Use next Pokémon)`, `battle-ui: page game, after catch`, plus the actions' own lines. The `battle` command also prints the open pocket's items, the pick and its cursor, and the battle order with party indexes.

## Not done / not verified

- **Target choice**: single battles never show a target screen (RE5), and no double battle is reachable from the test save. An SOS ally (1 vs 2) would need one; W7-1 hunted for an SOS call on Route 2 without one coming. What is built: the screen's layout from the game's files (`docs/OFFSETS.md`, `battle_target` in the layouts, measured only), and a native target page (`BattlePage::Target`) that takes any unknown game screen appearing within 120 frames of a move being sent for the target choice: the four buttons as the game lays them out (top left ... bottom right; which row is which side is not known), D-pad and A, B back. Its taps are measured, not verified, so the action layer would refuse them: A and B say *Not tried in the game yet: use the bottom screen* and the panel (L+R) is the way through. Host-tested only (`TestTarget`). To finish it: reach a double or SOS battle, check the screen, tap each button once, mark them `tap` in `layouts/usum-v1.2.json`, and read the battlers of the other clients to name the targets.
- **Trainer battles** were not played: none is reachable from the start points (`title`, `pokecenter`, `grass`, `pc`; Route 2's trainers in this save were not located). `battle-flee` is written for Run's refusal and host-tested against a fake; unverified in the game.
- Not used natively: Rotom Powers and Battle Items were opened but not used (Use is the same tap; whether they ask for a Pokémon was not seen), Status Restore items (same flow as HP/PP), PP items (Ether asks for a move: not handled), the Pokémon page's *Restore* and *Summary*, the Y quick ball.
- Illusion (RE1): the menu would name Zorua, not its disguise.
- Z-moves and Mega Evolution: not present in the test save.

## Move descriptions (backlog #2)

On Moves, **L toggles** the selected move's description in place of the move
rows; **L or B closes** it. Up/Down still selects moves (Bar also retains
Left/Right column navigation), and the description follows that selection.
A is ignored while reading. Leaving Moves resets the toggle. L is captured
for the whole native Moves page, including when the description is closed,
so it cannot open the game's hidden move-info screen. Bag L/R is unchanged;
the configured panel chord still belongs to the panel controls.

Descriptions come from the game's language GARC `a/0/3/<language>`, message
file **117**, indexed by move ID; names use 118. This was found by decoding
the local update archive: English file 117 has 730 lines, with move 1 matching
the Pound description (118 has 729 names). No game text is shipped. The
existing asynchronous asset loader reads/decrypts it via `ReadMessageFile`
and caches it for the session; BattleScreen also uses its bounded per-ID
cache. Missing assets show "Description unavailable" and retry the lookup.
The message decoder limit grows from 128 to 512 UTF-16 units because English
descriptions exceed 128 units (longest decoded rendered line: 158 characters).

The panel uses the existing Moves rectangle in each layout, inset by 8 px
horizontally and 25 px vertically. Measured UTF-8 word wrapping preserves
message newlines and splits long words at codepoint boundaries; lines draw
through `UI::DrawText` in the compact 5x7 font (10 px pitch). This retains
the existing bitmap fallback's non-ASCII glyph limitations. The header and
close hint stay inside the same rectangle. Drawing remains in 400x240 screen
coordinates, including the 2.5x Layer/Canvas path; no new layer or GPU slot.

## Battle shiny / hidden ability markers (backlog #13)

The Pokemon list passes shiny and hiddenAbility to the shared Party renderer:
a shiny star follows the name and an **HA** pill precedes gender. The shared
field-party list now shows that pill too (its summary already had it).
Command menus show small **P** (player) and **F** (client 1's first foe)
marker groups only when a flag is present. Classic/Rail use the command
footer beside A; Bar reserves a row above the command buttons by extending
its window upward to y 166. Both protected HP regions remain clear. These
badges are absent during playback, on unrecognised pages and when data is
unavailable. They describe the original ability slot, not a Skill Swap or
other temporary ability change. F refers to client 1 record 0 only; it does
not identify every foe in SOS, doubles or Battle Royal.

ReadBattle exposes record word `+0x00` as a **candidate PokemonParam pointer**.
Static evidence in `battle_rel1.bin`: Battle+0x7DEF4 loads `[record]` and
passes it to the imported Pokemon accessor at +0x1810; +0x7DF00 passes the
same object into the routine at +0x7DF18 that fills battle HP/ability data.
This pointer path is **statically inferred, not emulator verified**. Reads
require vtable `0x649374`, readable core/stats pointers at Param+4/+8, a valid
PK7 checksum and matching species. Failed reads suppress foe markers.
The battle-owned copies' lifetime/availability on both games remains to test.

PK7 decoding is shared with Party/Boxes: unshuffled core +0x0C/+0x0E are
TID/SID, +0x18 is PID; `(TID ^ SID ^ PID_hi ^ PID_lo) < 16` means shiny.
The ability slot at core +0x15, masked with 7, equals 4 for HA. The ability ID
at battler +0x16 alone is never used to guess an HA slot.

Metadata refreshes every 30 snapshots (about a second), on entering a list,
and immediately on record reorder/foe change; caches clear outside battle.
Own Pokemon use the battle copy first. Field names are matched by species
and PID when decoded, including PID zero. Without a readable battle copy,
only a unique field species may supply metadata. Ambiguous duplicate species
or identities leave metadata unknown instead of borrowing a different slot.
Battle HP/level still come directly from battler records, allowing level-scaled
teams. This fixes the three-of-six team name/item mix-up without assuming
that battle-team indices are field-party indices.

### Review / emulator checks

Host tests cover description toggling/capture, wrapping, UTF-8 boundaries,
placement in all layouts, identity matching under team selection/reordering,
ambiguity, and PK7 shiny XOR 15/16 plus all block shuffles and HA slot masks.
Check in Azahar on Sun and Moon:

1. All layouts: open Fight, L, change selection, L/B close, A after closing;
   confirm descriptions match the game's bottom-screen info, L does not open
   that screen, and neither HP region is covered. Include long/status moves,
   missing assets, a layout change with info open, and 2.5x rendering.
2. Party: choose a team of three in an order different from the field party;
   check nicknames/items/badges before and after switching and fainting.
   Include duplicate species with different PID/items and level-scaled teams.
3. Enable launcher's **Always shiny (wild Pokemon)** before a new encounter
   (codes in `player/cheats/00040000001B5000.txt` / `...1B5100.txt`). Check the
   foe F star; catch it, put it into the team and check the P/list stars in a
   later battle. The cheat changes newly generated encounters, not existing
   party members. Disable it and apply its undo code when done.
4. HA: bring a known HA Pokemon from a prepared test save or a caught HA SOS
   encounter, confirm its HA pill in the native field summary, then enter
   battle and switch it in/out. Ability Capsule only swaps normal slots in
   Gen 7 and cannot create an HA. Compare normal-slot controls. No launcher
   HA cheat is assumed. For foes, a known HA encounter can validate F HA.
5. Confirm battle Param+0 really has vtable 0x649374 and checksum-valid core
   buffers for own and wild foe. Failed/absent candidates should show no foe
   badge and should not crash. Check consecutive battles and SOS/royal clients.

### Host verification in this worktree

- All 26 Windows-compatible C++ test executables compiled with Clang C++17,
  `-Wall -Wextra -Werror` and `_CRT_SECURE_NO_WARNINGS`, and passed. The
  existing `screen_buffer_test` could not compile with this Windows toolchain
  because POSIX `unistd.h`/allocator APIs are unavailable. MSYS make itself
  was denied its signal pipe by the sandbox, so compiler recipes were invoked
  directly. Full `make -C tests` remains for Claude's normal build environment.
- The optional `battle_menu_test <preview-dir> <text-GARC>` check decoded all
  730 descriptions from each of the ten local update language GARCs, and
  every wrapped description fitted each layout. Previews use synthetic text;
  no game text was saved in the repo. Unicode wrapping passing does not add
  glyph coverage to the compact bitmap fallback.
- All ten changed translation units passed devkitARM C++17 syntax checking
  with `-Wall -Wextra -Werror`; external CTRPF/libctru headers were passed with
  `-isystem` (CTRPF itself emits unused-parameter warnings under `-Wextra`).
  This is not a linked plugin build. Layout generation check, synthetic
  signature tests and `git diff --check` passed. No emulator was run.
