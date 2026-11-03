# Native move learning (W6-1)

When a Pokémon levels up and already knows four moves, the game asks on the bottom screen whether it should forget one, then lists the four moves and the new one. With the panel off, the top screen now shows that list itself: pick the move to forget (or don't learn the new one), confirm, and the plugin answers the game's own screens through the action layer.

Code: `Sources/Screens/MoveLearnMenu.cpp` (pure: pages, keys, actions, drawing; host-tested by `tests/move_learn_test.cpp`), `Sources/Screens/MoveLearn.cpp` (plugin glue: reads the game, starts the actions, draws), `Sources/MoveData.cpp` (the game's move data from its RomFS). The native battle menu (`Sources/Screens/BattleScreen.cpp`) runs it every frame and, while the game asks, hides its *The game asks* helper and hands the keys to it.

## What the game does (seen in instance `w61`, 2026-11-02)

1. Messages: *Buneary grew to Lv. 13!*, *Buneary wants to learn the move Baby-Doll Eyes. But Buneary can't learn more than four moves!* (B or A to advance).
2. The question, top screen text *Should an old move be forgotten and replaced with Baby-Doll Eyes?*; bottom screen *Forget an old move* / *Keep old moves!* (layout `learn_ask`).
3. *Forget an old move* opens the forget screen (WazaOshie CRO): the Pokémon's stats on the left, its four moves with type and PP on the right, the new move apart at the bottom (layout `learn_moves`). A tap on a row (or A) opens that move's page: category, power, accuracy, description, and *Forget this move* / *Return to the move-selection screen*; on the new move's row *Give up on learning the move* (layout `learn_confirm`).
4. *Forget this move*: back to the battle with *1, 2, and... ... ... Ta-da!*, *Buneary forgot how to use Splash.*, *And...*, *Buneary learned Baby-Doll Eyes!* (B to advance). The battle record's move slot still read Splash right after the first message; the party had Baby-Doll Eyes in Splash's slot once the battle ended (`party`: `moves 111:40 608:30 1:35 193:40`).

How each is recognised is in `docs/OFFSETS.md`, "Move learning": the question is a two-entry list object of the Battle CRO, checked against the battle's last message (0x2F, with the party index and the new move); the forget screen is found on the scene stack by its WazaOshie vtable, and its page by a flag in it.

## The screen

| Page | Shows | Keys |
|---|---|---|
| choose | *Buneary wants to learn:* the new move (name, type, PP), the four moves (name, type, PP), *Don't learn Baby-Doll Eyes*; under them the highlighted move's category, power and accuracy | Up/Down, A, B (jumps to *Don't learn*) |
| confirm | *Forget Splash?* / *Buneary learns Baby-Doll Eyes*, or *Don't learn it?* | A yes, B back |
| working | the same, *Telling the game...* | none (the action holds input) |
| done | *Buneary forgot Splash and learned Baby-Doll Eyes* for 4 s, while the game prints its messages along the bottom | go to the game (B advances the messages) |

It shows 30 frames after the question (or the forget screen) appears, so it doesn't flash while the question slides in, and from the moment the question is seen it keeps A, B and Up/Down from the game: a B meant for the level-up messages must not answer the question. It draws right of x 156 and between y 36 and 192, clear of both HP boxes and of the game's message along the bottom.

Names come from the battle's move-name file (RE1), the moves' PP from the learner's battle record (found by the record's party index, `+0x19`), and type, category, power, accuracy and the new move's PP from the game's move data (RomFS `a/0/1/1`, read once; `move-learn: move data: 729 moves from a/0/1/1`).

## How a choice is carried out

One action per choice, `learn-forget1` … `learn-forget4` and `learn-skip` (`MoveLearnAction`), all through the forget screen:

1. tap `learn_ask.forget`, unless the forget screen is already up (`WorldLearnAsk` clear);
2. wait for the forget screen (`WorldLearnList`, retap once), then 30 frames for it to slide in;
3. tap the row (`learn_moves.move1` … `move4`, or `learn_moves.new` for *don't learn*);
4. wait for the move's page (`WorldLearnConfirm`, retap once), then 20 frames;
5. tap `learn_confirm.yes` (*Forget this move* / *Give up on learning the move*);
6. wait until none of the three screens is up (retap once).

The action layer gets the three World flags (bits 16-18) from `MoveLearnWorldFlags` in battle. The screen calls a choice done when the action has ended and the game has left all three screens.

*Keep old moves!* on the question isn't used: going through the forget screen for both choices keeps one path, and it is the one whose result was seen.

Log lines: `move-learn: party 5 Buneary wants Baby-Doll Eyes (608); knows Defense Curl, Splash, Pound, Foresight`, `move-learn: page choose, game ask, Buneary wants Baby-Doll Eyes, cursor 1 (Splash)`, `move-learn: chose learn-forget2`, the action's own lines (`action: learn-forget2 ok in N frames`), `move-learn: done, Buneary forgot Splash and learned Baby-Doll Eyes`. `battleui` prints the page too.

## Tests

- `make -C tests`: `move_learn_test` (the move data pack, with the user's own `a/0/1/1` at `analysis/private/romfs-base/a/0/1/1` when present; pages and keys; every action against a fake of the three screens, including lost taps; drawing inside the area; preview `analysis/private/move-learn-preview.ppm`).
- `scenarios/native-move-learn.txt`: from the `grass` start point, wild battles until Buneary asks, then the list, the confirm page and back, *forget Splash*, and the party after the battle.

## Not done / not verified

- **Out of battle**: a level-up outside battle isn't reachable in this save (no Rare Candy; the move relearner not reached). The screen only runs in battle. Teaching a TM in the field (the bag has TM01 and TM100) would also ask which move to forget; whether that is the same WazaOshie screen is unverified, and it would need the native Bag (P2-4) to hand over to this screen.
- **Don't learn** was played by hand on Buneary's question (`learn-skip ok in 84 frames`, *Buneary did not learn Baby-Doll Eyes.*, the party kept Splash), not in the scenario.
- **Several Pokémon at once** (two level-ups from one battle): the screen shows again for the next question (host-tested); not seen in the game.
- Only Buneary's question was seen; the message ID (0x2F) and its argument layout are from that one battle.
- Evolution moves and moves learned while evolving (the evolution scene) were not seen.
