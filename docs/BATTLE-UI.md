# Native battle menu (P2-1)

In battle the top screen shows the game's choices itself, so a battle is played with buttons only and no panel. Code: `Sources/Screens/BattleMenu.cpp` (pure: pages, input, drawing; host-tested by `tests/battle_menu_test.cpp`) and `Sources/Screens/BattleScreen.cpp` (plugin glue). It is on by default; `battleui off` gives battles back to the panel (the `[scenes]` `Battle` setting).

## What it shows

| The game's bottom screen | Top screen | Keys |
|---|---|---|
| intro, messages, animations | nothing (the battle text and HP bars stay clear) | go to the game |
| command menu | command bar: *What will X do?* Fight / Bag / Pokémon / Run | Up/Down, A |
| move list | move list: name, PP, type, effectiveness against the target (*Super effective*, *Not very effective*, *No effect*) | Up/Down, A, B back |
| Bag | helper: tab, highlighted ball's name and count (Poké Balls tab), *Use X?* on an item's page | go to the game: Left/Right tab, Up/Down item, A, B |
| Pokémon list (and its Switch / Summary screen) | helper: highlighted Pokémon, level, HP | go to the game |
| nickname question after a catch (scene `Zukan`) | helper: *Give it a nickname?* | go to the game: B no, A yes (the keyboard is on the bottom screen) |

Where it draws: right of x 166 and below y 36, so the opponent's HP box (top right), the player's HP box (bottom left) and the battle messages (bottom, only shown while no menu is up) stay readable. Pages are drawn into a `UI::Layer` when what they show changes and copied every frame (`docs/UI.md`); the plugin never reads the top framebuffer for them.

## How a choice is carried out

The menu follows the game's bottom-screen menu (RE1 `ReadBattle`, read every 2 frames) and acts by tapping the game's own button through the action layer: `battle-fight`, `battle-bag`, `battle-pokemon`, `battle-flee`, `battle-move1..4`, `battle-moves-back` (`docs/ACTIONS.md`). Rules, each from a run:

- A tap the moment the command menu slides in only moves its cursor (UI3), so a choice made earlier is held until the game's menu has been up 30 frames (10 for the move list).
- After a move or an item the game shows its command menu for about 22 frames before the turn starts. The command bar waits 30 frames before it shows (and logs), so it doesn't flash.
- While the command bar or move list is up, A, B, the D-pad and the circle pad are captured (`InputCaptureBegin`); Y (the game's quick Poké Ball), L (move info) and touch still reach the game. On the game's own screens nothing is captured.
- A move with no PP left isn't sent while another move has PP (*X has no PP left*); with none left, any move is sent and the game uses Struggle.
- Effectiveness and type come from the game's own move list, which is up whenever the move list is shown, so no type chart is needed.
- Move names come from the battle's own copy of the move-name file (`docs/OFFSETS.md`, "Battle state"); species and item names as in RE2/RE3.

Log lines: `battle-ui: page command, cursor 0 of 4 (Fight)`, `battle-ui: page moves, cursor 1 of 4 (Fire Fang, Fire, PP 15/15)`, `battle-ui: chose battle-fight`, `battle-ui: page game, game bag, Poké Balls, item page, Poké Ball`, `battle-ui: page game, after catch`, plus the actions' own lines.

## Not done / not verified

- **Target choice**: single battles never show a target screen (RE5), and no double battle is reachable from the test save, so there is none. A double battle would show the game's target screen with the menu hidden (`menu other`, no helper); the panel (ZR) is the way through until it is found.
- **Trainer battles** were not played: none is reachable from the start points (`title`, `pokecenter`, `grass`, `pc`; Route 2's trainers in this save were not located). `battle-flee` is written for Run's refusal and host-tested against a fake; unverified in the game.
- Native Bag and Party screens are P2-3/P2-4; the helper only names the highlighted ball on the Poké Balls tab (other tabs show *Item n of m*).
- Illusion (RE1): the menu would name Zorua, not its disguise.
- Z-moves and Mega Evolution: not present in the test save.
