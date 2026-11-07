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

- **Classic** keeps the original right-hand command column and move list, with the original pixels and Up/Down navigation.
- **Bar** puts a small caption above one horizontal row of rounded, colour-coded Fight / Bag / Pok?mon / Run pills at x 170?396, y 182?236. Its moves are a 2?2 grid at x 170?396, y 106?238, with type plates, PP and triangle markers (up: super effective, down: not very effective; X: no effect). Left/Right cycle commands; on moves they switch columns within the same row (a lone final move stays put). Up/Down cycle all entries in list order on both pages.
- **Rail** stacks slanted pills along the right edge, each lower command shifted left, with coloured tabs and light bodies. Commands occupy x 202?396, y 52?238; type-coloured move bars occupy x 170?396, y 38?238 and spell out effectiveness. Up/Down cycle entries as in Classic.

The command order is always Fight, Bag, Pok?mon, Run: Down twice selects Pok?mon and Up from Fight wraps to Run. A chooses/uses and B backs out of moves in every layout. PP uses the same colour thresholds as Classic. All three leave the opponent HP box (x ? 240, y < 36) and player HP/message region (x < 166, y ? 180) clear. Each command/moves page is one small opaque window in a `UI::Layer`; changing layout invalidates its draw key and rebuilds the layer when its rectangle changes. Party, Bag, Question, Target and after-catch helpers are identical in every layout.

## How a choice is carried out

The menu follows the game's bottom-screen menu (RE1 `ReadBattle`, read every 2 frames) and acts by tapping the game's own button through the action layer: `battle-fight`, `battle-bag`, `battle-pokemon`, `battle-flee`, `battle-move1..4`, `battle-moves-back` (`docs/ACTIONS.md`). Rules, each from a run:

- A tap the moment the command menu slides in only moves its cursor (UI3), so a choice made earlier is held until the game's menu has been up 30 frames (10 for the move list).
- After a move or an item the game shows its command menu for about 22 frames before the turn starts. The command bar waits 30 frames before it shows (and logs), so it doesn't flash.
- While the command bar or move list is up, A, B, the D-pad and the circle pad are captured (`InputCaptureBegin`); Y (the game's quick Poké Ball), L (move info) and touch still reach the game. On the game's own screens nothing is captured.
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
- Nicknames, held items and status come from the party (RE3 `ReadParty`) through the battler record's party index; read when the list or Bag opens and once a second while it shows.
- Move names come from the battle's own copy of the move-name file (`docs/OFFSETS.md`, "Battle state"); species and item names as in RE2/RE3.

Log lines: `battle-ui: page command, cursor 0 of 4 (Fight)`, `battle-ui: page moves, cursor 1 of 4 (Fire Fang, Fire, PP 15/15)`, `battle-ui: chose battle-fight`, `battle-ui: page party, cursor 1 of 6 (Zorua Lv14 35/35), menu Switch`, `battle-ui: page bag, HP/PP Restore, cursor 0 of 4 (Potion x9)`, `battle-ui: page bag, HP/PP Restore, pick 0 of 6 (Zorua 32/35), busy: Using Potion on Zorua...`, `battle-ui: step bag_party.slot1`, `battle-ui: page question, cursor 0 (Use next Pokémon)`, `battle-ui: page game, after catch`, plus the actions' own lines. The `battle` command also prints the open pocket's items, the pick and its cursor, and the battle order with party indexes.

## Not done / not verified

- **Target choice**: single battles never show a target screen (RE5), and no double battle is reachable from the test save. An SOS ally (1 vs 2) would need one; W7-1 hunted for an SOS call on Route 2 without one coming. What is built: the screen's layout from the game's files (`docs/OFFSETS.md`, `battle_target` in the layouts, measured only), and a native target page (`BattlePage::Target`) that takes any unknown game screen appearing within 120 frames of a move being sent for the target choice: the four buttons as the game lays them out (top left ... bottom right; which row is which side is not known), D-pad and A, B back. Its taps are measured, not verified, so the action layer would refuse them: A and B say *Not tried in the game yet: use the bottom screen* and the panel (L+R) is the way through. Host-tested only (`TestTarget`). To finish it: reach a double or SOS battle, check the screen, tap each button once, mark them `tap` in `layouts/usum-v1.2.json`, and read the battlers of the other clients to name the targets.
- **Trainer battles** were not played: none is reachable from the start points (`title`, `pokecenter`, `grass`, `pc`; Route 2's trainers in this save were not located). `battle-flee` is written for Run's refusal and host-tested against a fake; unverified in the game.
- Not used natively: Rotom Powers and Battle Items were opened but not used (Use is the same tap; whether they ask for a Pokémon was not seen), Status Restore items (same flow as HP/PP), PP items (Ether asks for a move: not handled), the Pokémon page's *Restore* and *Summary*, the Y quick ball.
- Illusion (RE1): the menu would name Zorua, not its disguise.
- Z-moves and Mega Evolution: not present in the test save.
