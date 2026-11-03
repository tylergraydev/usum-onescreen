# Bottom-screen layouts (RE5)

Where to tap on Ultra Sun's bottom screen, for Phase 2 (UI3: plugin menus on the top screen that carry out the choice by tapping the hidden bottom screen). Machine-readable copy: [`layouts/usum-v1.2.json`](../layouts/usum-v1.2.json) (screen → button → `rect`, `tap`, `does`, `verified`, `evidence`).

- Game: Ultra Sun `00040000001B5000` v2048 (0x0800). Measured 2026-11-02 in an isolated Azahar instance on the early save (Route 2, six Pokémon, no Z-Crystal).
- Coordinates are bottom-screen pixels (x 0-319, y 0-239). Rects are `x0,y0-x1,y1`, inclusive. **Tap** is the point that was tapped to verify; Phase 2 should use it rather than the rect centre.
- Verified: **tap** = a tap there gave the result shown (evidence: screenshot names under `analysis/private/shots/`, kept out of git, plus the plugin's `scene:` log); **measured** = bounds from a screenshot only, deliberately not tapped (reason given); **none** = tapped, nothing happens.
- A tap is `move X Y`, `tap 6` (6 frames) through `tools/cmd.py`. Rects come from [`tools/layout_measure.py`](../tools/layout_measure.py) (flood-fill from a seed pixel → bounding box) and column/row colour scans of the screenshots; rects of slanted or rounded shapes are their bounding boxes.

**Never drag in the Bag.** Holding a touch on an item row and moving (even a fast flick) picks the item up and reorders the pocket. Only tap.

## Field (`FieldRo`, X menu closed)

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Rotom's face (dark mouth area) | 113,0-207,89 | 160,35 | Pokédex (`Zukan`) | tap (also 160,5) |
| Map | 29,84-290,231 | 160,100 | Town Map (`TownMap`) | tap (also 60,200 and 280,110) |
| Rotom's eyes | 56,17-116,80 / 203,17-263,80 | 86,48 / 233,48 | nothing | none (also 40,40) |
| Player marker on the map | 150,148-169,167 | 160,160 | nothing | none (tried twice) |
| Red frame outside the map | | 300,225 | nothing | none |

## Town Map (`TownMap`)

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Back | 0,210-33,239 | 16,224 | back to the field | tap |
| Zoom (Y) | 284,19-313,48 | 298,33 | zoom in / out | tap |
| ◀ previous island | 8,111-20,128 (glyph) | 14,120 | previous area (Melemele → Aether Paradise) | tap (also 12,131) |
| ▶ next island | 299,111-311,128 (glyph) | 305,120 | next area | tap (also 306,131) |

## Pokédex top level (`Zukan`)

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Alola Pokédex | 20,26-309,53 | 164,39 | Alola list | tap |
| Island Pokédex 1 (Melemele) | 27,63-309,88 | 168,75 | Melemele grid | tap |
| Island rows 2-4 | 29,95-307,116; 29,125-307,146; 29,155-307,176 | 168,105 / 135 / 165 | locked (blank) in this save: nothing | none (row 2); rows 3-4 measured |
| Pokédex Evaluation | 60,191-309,212 | 184,201 | Rotom asks which Pokédex to evaluate (B cancels) | tap |
| Back | 0,212-31,234 | 15,223 | list → top level; top level → wherever it was opened from (field or X menu) | tap |

Row colours are fixed per row; the red arrow is the cursor.

## X menu (`FieldRo` overlay)

Page 1. Only the pills are buttons: the text labels under them and the gap between columns do nothing (tested 88,80 and 160,48). Pill edges tested: 88,65 and 40,48 open Party.

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Pokémon | 34,29-143,68 | 88,48 | Party (`main:00647FCC`) | tap |
| Pokédex | 176,29-286,68 | 230,48 | Pokédex (`Zukan`); its back returns to the X menu | tap |
| Bag | 34,89-143,128 | 88,108 | Bag (`main:006488C8`) | tap |
| Quick Link | 176,89-286,128 | 230,108 | Quick Link screen, no scene change; back 15,223 returns | tap |
| Festival Plaza | 34,149-143,188 | 88,168 | goes straight into Festival Plaza (scene stays `FieldRo`); leave with back 16,223 → "quit?" → press Y | tap |
| Trainer Passport | 176,149-286,188 | 230,168 | `TrainerPass`; back 15,223 returns | tap |
| Save (Y) | 183,206-319,239 | 251,222 | save | measured: never tapped (rules) |
| Back | 0,210-33,239 | 16,224 | closes the X menu | tap |
| ▶ page arrow | 299,9-308,16 (glyph) | 303,12 | page 2 | tap |

Page 2 uses the same grid: Pokémon Refresh (88,48), QR Scanner (230,48), Battle Video (88,108), Options (230,108, opens `Config`, verified; back leaves without confirming), two empty slots. ◀ back to page 1 at 16,12 (glyph 13,11-19,14), verified. Pokémon Refresh (opens `Kawaigari`, a tutorial the first time that B skips; its back at 14,222 sometimes needs a second tap) and Battle Video (opens `BattleVideoPlayer`; back 16,224) were verified by P2-2; QR Scanner was not tapped (Azahar hangs in it: no camera).

The X menu remembers its page and cursor: it reopens on the page and entry last used, and tapping an entry moves the cursor there. The cursor is a red arrow left of the pill (x 33-39 in the left column, 176-181 in the right, y 44-51 + 60·row); the page arrow (blue, 119,187,255) is at 303,12 on page 1 and at 16,12 on page 2. `Sources/Actions.cpp` and `Sources/Screens/XMenu.cpp` read these.

## Party (`main:00647FCC`)

Slots alternate left/right: slot n is in column (n-1) mod 2, row (n-1) div 2. Left panels sit at y 6+60·row, right panels 18 px lower.

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Slot 1 | 6,6-169,62 | 87,35 | slot menu ("What would you like to do with Torracat?") | tap; edge 165,45 is still slot 1 |
| Slot 2 | 170,24-319,80 | 247,52 | slot menu (Zorua) | tap; edge 176,45 is slot 2 |
| Slot 3 | 6,66-169,122 | 87,95 | slot menu (Pikipek) | tap; 87,72 is slot 3, 87,66 is a gap (nothing) |
| Slot 4 | 170,84-319,140 | 247,112 | slot menu (Pichu) | tap |
| Slot 5 | 6,126-169,182 | 87,155 | slot menu (Ledyba) | tap |
| Slot 6 | 170,144-319,200 | 247,172 | slot menu (Buneary) | tap |
| Back | 0,212-32,235 | 16,223 | back to the X menu | tap |
| Pokémon (Y) | 90,212-203,235 | 146,223 | reorder mode, "Which Pokémon do you want to move?" (B cancels) | tap |
| Held Item (X) | 215,212-319,235 | 267,223 | held-item move mode, "Which item do you want to move?" (B cancels) | tap (at 266,218) |

Slot menu (right side, 28 px pitch; inner highlight measured at x 223-308, outer edges approximate):

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Summary | 215,123-315,145 | 266,134 | summary (`Status`); back 15,223 returns to Party | tap |
| Restore | 215,151-315,173 | 266,162 | the Bag (`main:006488C8`) for this Pokémon, on the pocket it showed last; back 15,224 returns to Party | tap (P2-3) |
| Held Item | 215,179-315,201 | 266,190 | held-item submenu | measured: avoided item changes |
| Back | 215,207-315,229 | 266,218 | closes the slot menu | tap |

## Bag (`main:006488C8`)

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Item row 1-7 | x 157-315, y 4+28k to 29+28k (k = 0-6) | 235, 16+28k (16, 44, 72, 100, 128, 156, 184) | item menu for that row | tap: all 7 (rows 3, 5, 6 in P2-4: Poké Ball, Burn Heal, Ice Heal menus; and by P2-3 in the Restore bag: Awakening, Burn Heal, Ice Heal) |
| Half-visible row 8 | 157,200-315,212 | 235,210 | nothing | none |
| Pocket tabs | 24 px wide from x 122, y 216-239 | x 134+24k, y 230 | k = 0 Rotom Powers, 1 Medicine, 2 Items, 3 TMs, 4 Berries, 5 Key Items, 6 Free Space | tap, all 7 (pocket name read on the top screen) |
| Scroll bar | 316,10-318,210 | 317,190 | display only: tap and drag did nothing | none |
| Back | 0,214-31,234 | 15,224 | back to the X menu | tap |

- **Scrolling**: no touch scrolling found (scroll bar tap/drag and the half-visible row do nothing; dragging a row reorders items). D-pad Up/Down scrolls once the cursor passes row 7, and wraps (Up on row 1 jumps to the last item).
- The party column on the left (x 0-150) only takes taps while the Bag asks "Which Pokémon...?" (below).

Item menu (`bag_item_menu`): it grows upwards from Cancel, so its buttons are named by position from the bottom; which entries it has depends on the item (they are in the Bag param, docs/BAG.md). 28 px pitch:

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| from_bottom5 | 161,97-316,122 | 236,110 | no 5-entry menu seen | measured |
| from_bottom4 | 161,125-316,150 | 236,138 | Use in 4-entry menus (Medicine, Escape Rope, Oran Berry) | tap (Potion: Use) |
| from_bottom3 | 161,153-316,178 | 236,166 | Give in 3- and 4-entry menus; Use for TMs | tap (Quick Claw, Escape Rope: Give) |
| from_bottom2 | 161,181-316,206 | 236,194 | Move to Free Space in every menu seen | measured: changes the bag |
| from_bottom1 | 161,209-316,236 | 236,222 | Cancel | tap |

Party pick (`bag_party`, after Use or Give: "Which Pokémon do you want to use this item on?"): slots in party order, 32 px pitch, x 0-150; tap x 70, y 18+32k. A tap on a slot moves the cursor there; a tap on the slot under the cursor chooses it. All six tapped (cursor moves; slots 2 and 4 also chosen).

Yes / No (`bag_yes_no`, Give to a Pokémon that holds an item: "Would you like to switch the two items?"): Yes 206,94-310,120 tap 258,107 (tap: switched); No 206,122-310,148 tap 258,135 (tap: back to the list, nothing switched).

Restore item menu (Party → slot → Restore → tap a row: "What would you like to do with the Potion and Pichu?"; 3 entries, 28 px pitch; B closes it):

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Use this item | 177,155-313,178 | 245,166 | uses it on that Pokémon; a message ("Pichu's HP was restored by 9 points." / "It won't have any effect.") stays until A or B, then the item list | tap (P2-3) |
| Give to Pokémon | 178,183-313,206 | 245,194 | give | measured: changes held items |
| Cancel | 178,211-313,236 | 245,222 | closes the menu | measured (B used instead) |

- The Restore bag keeps the field Bag's tabs, rows and back button. It remembers cursor and scroll per pocket, also across tab taps; read them from memory (`docs/OFFSETS.md`, Bag item list).
- Party move mode (the Y button): a tap on a slot only moves the game's cursor; A picks that Pokémon ("Where do you want to move it to?"); a tap on another slot swaps them at once and returns to "Which Pokémon do you want to move?"; B leaves move mode (B with a Pokémon picked un-picks it first).
- Party screen, plain list: the bottom bar's Y and X buttons are blue at 120,226 and 310,226 with white at 205,226; a slot menu dims them, move mode and messages cover them (`WorldPartyList`, `Sources/Actions.cpp`).
- Summary (`Status`): the party balls run down the left edge; the one shown has a green ring (x 5, centre y 16 + 32·position). Up/Down change Pokémon (wrapping), Left/Right change page, A checks moves, B returns to Party with its cursor on the Pokémon shown last.

## Battle command (`Battle`)

The four shapes are not rectangles; rects are the bounding boxes of each shape's colour (Run's includes its blue outline). Use the tap points.

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Fight | 235,20-319,219 | 285,120 | move list | tap |
| Pokémon | 0,32-67,107 | 35,75 | battle party list | tap |
| Bag | 0,130-73,211 | 35,165 | battle bag | tap |
| Run | 60,193-259,239 | 160,225 | runs; against a wild Pokémon it went straight back to the field (log `Battle` → `(none)` → `FieldRo`), no A needed | tap, two battles |
| Poké Ball (Y) | 0,0-28,28 | 12,12 | throws a ball | measured: would use an item |

No Z-move or Mega button: this save has no Z-Crystal pocket and the move list has no Z button.

## Battle moves (`Battle`)

50 px pitch. Tapping a move uses it at once.

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| Move 1 | 150,14-319,66 | 235,41 | uses move 1 | tap (turn ran) |
| Move 2 | 150,72-319,116 | 235,95 | uses move 2 | tap (Fire Fang KO'd the foe, battle ended) |
| Move 3 | 150,122-319,166 | 235,145 | uses move 3 | tap (Double Kick PP 30 → 29) |
| Move 4 | 150,172-319,216 | 235,195 | uses move 4 | tap (Lick PP 29 → 28) |
| ⓘ info 1-4 | 145,22+50k-164,41+50k | 154, 31+50k (31, 81, 131, 181) | move info; a tap anywhere closes it | tap, all four |
| Back | 0,213-31,235 | 15,224 | back to the command screen | tap |

- The ⓘ circles are small: 154,195 is below info 4 and **uses move 4**.
- **Target selection does not appear in single battles** (all four moves went straight to the turn). No double battle is reachable from this save.

## Battle party and bag (`Battle`)

- **Party list**: same slot layout as Party; tapping a slot opens that Pokémon's page (verified slot 5 at 35,165). Back 15,224 (verified). The bottom bar says "Choose a Pokémon." instead of the Y/X buttons.
- **Pokémon page**: Switch 182,194 (rect ~57,181-310,207; tapped in W7-1: switches at once, also from the list after a faint) and Restore 118,226 (~57,213-178,238, measured only); Summary 248,226 (186,213-310,238) verified; party icon strip at x 0-40, about 34 px pitch, centres y 18, 52, 87, 120, 155, 189 (52 verified: shows slot 2); back 15,224 verified (summary → page → list).
- **Bag**: same item-row layout as the field Bag (row 1 at 235,16 verified: opens the item page). Five pockets, 42 px wide from x 106, y 216-239, tap x 127+42k, y 228: Rotom Powers, HP/PP Restore, status restore, Poké Balls, battle items (all verified). Back 15,224 (verified).
- **Item page**: Use 182,222 (~65,208-300,236; verified on the Poké Balls pocket only, where it throws the ball: W5-1's catches. Other items are still never used); ◀ 18,81 and ▶ 301,81 switch between items in the pocket (verified; 306,95 misses); back 15,224 (verified).

- **Which Pokémon...?** (after Use on an item that needs a Pokémon, W7-1): the party column at the left uses the field Bag's `bag_party.slot1..6` places (battle order); a tap on a slot off the game's cursor only moves the cursor, a tap on the cursor's slot uses the item (Potion on Pikipek and on Zorua).

## Battle question and target (`Battle`, W7-1)

- *Use next Pokémon?* (after a faint in a wild battle): the same two-entry object as move learning's question. `battle_ask.first` 160,88 (Use Next Pokémon, tapped twice: opens the Pokémon list without a back button), `battle_ask.second` 160,153 (Run; marked tap because the same object's second entry took the tap as *Keep old moves!*; Run's own outcome not seen).
- Target screen (`battle_target`, measured from the game's layout file `Battle_TRG_LOW_000`, never seen): top left 83,78, top right 236,78, bottom left 83,162, bottom right 236,162 (150x78 each), back 22,224. See `docs/OFFSETS.md`.

## Move learning (`Battle`)

After a level-up with four moves known (W6-1, docs/MOVE-LEARN.md). Seen with Buneary in instance `w61`.

| Screen | Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|---|
| question | Forget an old move | 8,74-306,102 | 160,88 | opens the forget screen | tap (the native screen's action) |
| question | Keep old moves! | 8,139-306,167 | 160,153 | gives up the new move | tap (W7-1: *Buneary did not learn Baby-Doll Eyes*) |
| forget screen | move 1-4 | 165,3-318,40 / 47-83 / 90-126 / 133-169 | 245, 22 / 70 / 108 / 151 | opens that move's page (doesn't forget yet) | tap, all four (pages of Defense Curl, Splash, Pound, Foresight) |
| forget screen | new move | 165,186-318,226 | 245,208 | opens its page: *Give up on learning the move* | tap |
| forget screen | back | 0,210-32,239 | 16,222 | back | none |
| move page | Forget this move / Give up on learning the move | 12,150-306,180 | 160,165 | forgets the move (or gives up the new one) | tap, both (*Buneary forgot how to use Splash*; *Buneary did not learn Baby-Doll Eyes*) |
| move page | Return to the move-selection screen | 12,184-306,212 | 160,197 | back to the list, cursor on that row | tap |

## Keyboard (`StrInput`, any scene; seen over `Zukan`)

Reached by Yes to the nickname question after a catch (W5-1, [KEYBOARD.md](KEYBOARD.md)). The QWERTY page it opens on, every key verified by a tap with shift off and on (the typed character read back from the game's text object):

| Keys | Rects | Taps |
|---|---|---|
| Row 0: `! 1 2 3 4 5 6 7 8 9 0` (shift: `! ♂ ♀ + - * / = % ( )`) | 21+20i,60-38+20i,88 | 30+20i, 74 |
| Row 1: `q w e r t y u i o p` | 31+20i,95-48+20i,123 | 40+20i, 109 |
| Row 2: `a s d f g h j k l ’ : é` | 41+20i,130-58+20i,158 | 50+20i, 144 |
| Row 3: `z x c v b n m , . ? “ ”` | 51+20i,165-68+20i,193 | 60+20i, 179 |
| Delete / OK (↵) | 240,60-289,88 / 230,95-289,123 | 265,74 / 260,109 |
| Space / Shift (⇧, orange while on) | 20,200-218,228 / 221,200-289,228 | 119,214 / 255,214 |
| ◀ to the grid page / ▶ back to QWERTY (grid page) | 10,132-24,148 / 296,132-312,148 | 16,140 / 304,140 |

Shift is one-shot and on when the keyboard opens; Delete turns it off. The game's own D-pad cursor (a yellow box) follows the last tapped key. Past the limit (12 for a nickname) a key does nothing. The grid page (CAPS / lower / Sym. tabs, Delete, OK) isn't recorded beyond its ▶.

## PC boxes (`Box`)

Added by P2-6 (docs/RE6-boxes.md), instance `p26`, start point `pc`. Every slot tap was checked by reading the game's cursor back (`BoxLowBgDraw +0xDC`) after it: all 36 landed on their own index.

| Button | Rect | Tap | Does | Verified |
|---|---|---|---|---|
| `box.slot_0` … `slot_29` | 8+32·col,58+29·row (32x29 cells) | 24+32·col, 72+29·row | cursor there; a Pokémon: opens the slot menu; holding one: drops it there (swaps with a Pokémon already there) | tap (all 30) |
| `box.party_0` … `party_5` | 45x40 around the tap | 238,76 288,96 238,121 288,141 238,166 288,186 | the same for the party (cursor 30-35); the game compacts the party, empty slots are at the end | tap (all 6) |
| ◀ `box.prev_box` | 6,33-20,49 | 13,41 | previous box (Box 1 → the last unlocked box) | tap |
| ▶ `box.next_box` | 184,33-198,49 | 191,41 | next box; also while holding a Pokémon | tap |
| `box.box_list` | 56,206-140,239 | 95,222 | all-boxes view (cursor 38) | measured |
| `box.back` | 0,214-31,239 | 16,226 | leaves the PC (B does the same) | measured |

Slot menu (`box_menu`, after a tap on a Pokémon; row pitch 28, entries 212-310 wide): Move 265,44 (tap: picks it up), Summary 265,72, Held Item 265,100, Marking 265,128, Release 265,156 (all measured, not tapped; Release never), Cancel 265,184 (tap: closes the menu).

L and R do not turn the box. The cursor is a red arrow above the slot it points at.

## Not reached

- Z-move button, Mega Evolution: not in this save.
- Target selection: not shown in single battles; no double battle available, no SOS call came (layout known from the game's files, see above).
- Save confirmation: Save is never tapped.
- Not tapped by choice (listed above as measured): field Bag item Use (except Poké Balls in battle)/Give/Move, Party Held Item, Restore Give, battle Restore, the Y Poké Ball, X menu page 2 entries other than Options.

## Side observations

- After entering and leaving Festival Plaza, the plugin's `x menu open/closed` flag was inverted for the rest of the session (it said open in the field, closed with the X menu showing), so `FieldRo.xmenu` auto-panel switched the wrong way. Not investigated here.
