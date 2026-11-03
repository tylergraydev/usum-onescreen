# Rotom's field prompts (W6-2)

In the field the bottom screen is the Rotom Dex. Besides the map, Rotom
sometimes asks the player something and waits for a tap on one of two
answers, and it runs the Roto Loto, which waits for taps too. With the panel
off none of that can be seen or answered. The plugin now shows those prompts
on the top screen and answers them by tapping the hidden bottom screen.

## What the player sees

| On the top screen | Keys | What happens on the bottom screen |
|---|---|---|
| A badge in the top-right corner, "Rotom wants to talk ▲" | **Up** (D-pad) | taps Rotom's left eye (86,48): Rotom starts a chat, or the Roto Loto |
| (Rotom also starts chats by itself; the box below then just appears) | | |
| A box at the bottom: "ROTOM", Rotom's question, two answers | **Up/Down** choose, **A** answers | taps the answer (210,135) or (210,162) |
| The same box with Rotom's reply, for 5 seconds | none | Rotom says the reply |
| "ROTOM - ROTO LOTO" and Rotom's line ("It's Roto Loto time!", "That's an amazing power!", the power's description; "The reel is spinning!" while it spins) | **A** goes on / stops the reel | taps the text box (160,210), or the reel (160,150) |

The circle pad and **B** stay with the game while a box is up, so the
player keeps walking (and running). **A** is the prompt's while a question
or a Roto Loto step waits, so the player can't talk to people until it is
answered; Rotom's chats never block the field otherwise.

Every line is the game's own text, read at runtime from the message file
the game has loaded (below); the plugin ships none. The player's name comes
from the save (MyStatus).

### The map, the X menu, the panel

- **Native map open when a box comes up**: the map closes (`map: closed
  (Rotom)`) and opens again once the box is gone (the reply has been shown,
  or the Roto Loto is over; `map: open (Rotom)`). ZR while Rotom asks does
  the same: the map comes up once Rotom is done. While Rotom talks, the bottom screen shows
  Rotom's face instead of the map, so the map's copied window would only
  show that. The badge is not shown while the map is open (it would sit on
  the map's header); Up then does nothing.
- **X menu**: nothing shows while it is open (field state 4). A chat that is
  still waiting shows again when the X menu closes.
- **Panel**: a box holds the panel off (AutoPanel's manual override), as the
  X menu and the map do. Turning the panel on by hand (L+R) hides the box
  until it ends, so the player can tap the bottom screen instead. The badge
  doesn't hold the panel.
- **Another action running** (X menu entries, the native map's ...): the keys
  wait for it to finish.

## How it is found (USUM v1.2, runtime, 2026-11-02, instance w62)

### Rotom's applet

The bottom screen in the field is the FieldMenu CRO (loaded at `0x7F1000`
here). Its scene-stack entries (scene manager `*(0x6A599C)`, first eight
words) are, in this order: the FieldRo scene object, an object with vtable
FieldMenu `+0x18284`, two FieldRo objects, FieldMenu `+0x18310`, and the
frame with the map view (FieldMenu `+0x183A0`, RE4). The `+0x18284` one is
Rotom's applet: its methods take the touches on Rotom (FieldMenu `0x7F7C74`,
vtable slot 21) and run the chat and the Roto Loto. Offsets in it:

| Offset | What | Seen |
|---|---|---|
| `+0x90` | event sink (vtable call `(sink, event)`) | |
| `+0xA0` | `Savedata::MyStatus` (vtable `0x64F460`; name at `+0x40`) | `0x33012810` |
| `+0xA4` | Rotom's data (main image, vtable `0x648698`) | `0x33F81310` |
| `+0xC4` | `gfl2::str::MsgData` of message file 89, full load (1488 lines) | `0x318432F8` |
| `+0xC8` | u32 answers: 2 while the two answers are on screen, 1 just after one was tapped, 0 otherwise | 0 → 2 → 1 → 0 |
| `+0xD0` | u32 talk kind (the second argument of FieldMenu `0x7FB52C`, "say line"); 6 for a chat, 8 idle | 8 → 6 → 8 |
| `+0xF0` | u32 Rotom's animation; **0x5C = Rotom wants to talk**, 6 otherwise (FieldMenu `0x7F99E8`) | 6 → 0x5C → 6 |
| `+0xF8` | pointer to the chat table entry being asked, 0 when none | `0x80761C` (entry 20) → 0 |
| `+0x100` | u8 Roto Loto state (FieldMenu `0x7F7104`, `0x7F72C0`) | 0, 3, 5, 6, 7, 8, 9, 0 |
| `+0x104` | pointer to the Roto Loto power entry | `0x807474` (Roto Prize Money) |

The plugin finds the applet on the scene stack by its vtable and module
name (as `Where.cpp` finds the map frame) and checks the chat table's first
question (961) once per module load.

### The chat table

FieldMenu `+0x164DC` (`0x8074DC`): `0x9A` entries of eight u16, found by
searching the module for the answer lines of the chat seen in H2 ("I'm
having fun!" = line 1001); FieldMenu `0x7F80C8` and `0x7F8A98` walk it
(`for i < 0x9A: if entry[i].question == line`) and store the match in
`+0xF8`, and `0x7F8754` writes the entry's `+2` and `+4` lines into the two
answer panes. Layout: question, answer 1, answer 2, reply 1, reply 2
(`0xFFFF` none), then three words not used here. 116 entries are chats
(four variants of each question by name/nickname), the rest story chats
after events (trial passed, Gladion, ...), the save reminder (1419/1420:
"Yeah, maybe I should" / "No, I'm good!"), the nickname offer (956: "Sure!" /
"Uhhh...") and the photo offer after an evolution (1455). All have two
answers, so one prompt covers them all.

### Rotom's text

Message file 89 of `a/0/3/<lang>` (English `a/0/3/2`, file 89, 1488 lines;
found by decoding every file of the user's dump for "I'm having fun!"). The
applet keeps it loaded (`+0xC4`), so the plugin reads the line from there
(no RomFS read) and decrypts it like any message line (`docs/RE2-text.md`).
Tags: `[VAR 0100]` is the player's name, `[VAR 0189]` what Rotom calls the
player: on this save Rotom showed the player's name for both ("Heya, Tyler,
how're you doing?" from line 1033, which uses `0189`). After the nickname
offer (entry 116) `0189` may be "Roto-..." or "Master" (lines 953-955); the
plugin shows the name, and this was not checked (the offer isn't reachable
on this save). `[VAR BE01]` (next page) and `[VAR BE00]` (scroll) become line
breaks, `[VAR BE02 n]` (a pause) is dropped. Checked on the user's dump
(`tests/rotom_prompt_test.cpp`) and on screen.

### When Rotom wants to talk

Rotom's data (applet `+0xA4`, vtable `0x648698`, in the main image):

- `+0x5A` bit 0: a chat is ready. Bits 1-7 count up (main image `0x2ED50C`,
  called from `0x3A67E0` with the amount); at 10 bit 0 is set and counting
  stops. The applet then plays animation `0x5C` (Rotom's eyes) as long as it
  can (no Roto Loto, nothing open; `0x7F99E8`).
- `+0x5B` bits 0-6: Rotom's affection, capped at 60 (`0x2ED66C` tests ≥ 60),
  raised by the same call.
- `+0x08` …: eight request slots (u32 line | flag, u32 extra; empty
  `0x7FFFFFFF`, `-1`) that field events fill to make Rotom say something
  (`0x2ED650` returns slot `n`'s line; FieldMenu `0x7F80C8` looks it up in
  the chat table): the story chats and the save reminder come this way, by
  themselves.

Both counters were 0 after Continue (they are not in the save, or not
loaded yet) and went up by one about every 30-60 s in the field, walking or
standing; Rotom was ready about 5 minutes after boot or after the last chat
(w62: 20:19:30 chat, 20:25:11 ready). When it is ready, a tap on Rotom's eye
(touch ids 3-8 in `0x7F7C74`; the face, id 0, opens the Pokédex) starts a
random chat from the 26 at `0x807EE8` (`0x7F7F6C`, chosen with the game's
RNG and the name variant), **or** the Roto Loto (seen on the second ready
tap of the run). Without a ready flag the same tap gives one of Rotom's
idle lines (no answers). A tap while not ready did nothing before this
work (LAYOUTS: "rotom_eye_left ... nothing").

Rotom also starts chats **by itself**: in one run (w62, 20:42, four minutes
after Continue) "Which do you prefer, Tyler? Cute Pokémon or powerful
Pokémon?" (entry 16) came up with the ready counter at 6-7 and no tap, as H2
had seen ("So, how're you doing...", docs/ACTIONS.md). What starts those
was not traced (the request slots above, or a timer of the applet's); the
prompt doesn't need to know, since it reads the chat itself.

Bottom-screen keys: the applet's input method (`0x7F7104`) also reads the
game's button object (`gfl2::ui::Button::IsTrigger`, `0x4B6D54`, masks
`0x40`, `0x80`, `0x200`, and `0x10` during the Roto Loto). Pressing the
D-pad, L, R and Y in the field changed nothing visible on either screen
(screenshots key-*), and the D-pad didn't move the player; which pad keys
those masks are was not traced. So Up as the talk key is observed to be
free, not proven from the code.

### Roto Loto

Reachable on this save: the second ready tap started it ("Sorry to keep you
waiting! It's Roto Loto time!", line 1470). Applet `+0x100`:

| State | Bottom screen | Goes on with |
|---|---|---|
| 3 | "It's Roto Loto time!" | a tap (text box) |
| 5 | the reel spins | a tap (stops it) |
| 6, 7 | "Hey, look at that! That's an amazing power!" (1469) | a tap |
| 8 (`0x00010008` as a u32) | the screen dims | nothing (a few frames) |
| 9 | the power's description (power table entry `+4`) | a tap |
| 0 | back to the map | |

The power table is FieldMenu `+0x1645C` (11 entries of 8 bytes; `+4` the
description line, 1471-1481); `+0x104` pointed at entry 3, description 1473
"Roto Prize Money is a power that increases the money you win after each
battle!", as the screen said. Any touch goes on while `+0x100` is not 0
(`0x7F72C0`); the plugin taps the text box, or the reel while it spins. The
power lands in the Bag's Rotom Powers pocket (not saved by the tests).

### Not handled / not reachable

- **The nickname offer, story chats, the save reminder, the photo offer**:
  same table and same prompt, but not reached on this save (they come with
  events and affection). The save reminder's "Yeah, maybe I should" would
  open the save dialog; the scenario never picks it.
- **Rotom's idle lines and Rotom Power tips** (a tap on Rotom when it isn't
  ready; `0x7F7C74` with touch ids 3/4 and a power held, table `0x80745C`):
  lines without answers, so nothing to choose; with the panel off they only
  show on the bottom screen (they need a tap there to start, which the
  plugin doesn't make).
- **Rotom's Pokédex evaluation** ("which Pokédex to evaluate") is in the
  Pokédex scene, not the field (docs/POKEDEX.md).
- Rotom Powers used from the Bag and the Ride Pager are other screens.

## Files

- `Includes/Screens/RotomPromptView.hpp`, `Sources/Screens/RotomPromptView.cpp`:
  the pure part: the chat table entry, Roto Loto steps, a message line as
  text, wrapping, the view, keys, layout and drawing. Host tests in
  `tests/rotom_prompt_test.cpp` (preview `analysis/private/rotom-preview.ppm`;
  the real lines when the user's `a/0/3/2` is there).
- `Includes/Screens/RotomPrompt.hpp`, `Sources/Screens/RotomPrompt.cpp`: the
  plugin glue: finding the applet, reading it, the actions (one tap each),
  the map and panel, input capture, the OSD layer.
- `layouts/usum-v1.2.json`: `field.rotom_eye_left` (now "starts a chat
  while Rotom wants to talk", verified), `rotom_chat.answer_1/2`,
  `rotom_loto.text_box/reel`.
- `Sources/Screens/Map.cpp`: `MapIsOpen`, `MapClose` for the map handover.

## Commands and log

`rotom on|off` (default on; off shows nothing and takes no keys),
`rotom status`, `rotom read` (the raw reading), `rotom select 1|2`,
`rotom answer [1|2]`, `rotom talk`, `rotom next` (as the keys would).
Log lines:

```
rotom: applet 31842600, FieldMenu at 007F1000, chat table ok
rotom: ready (talk key Up)
rotom: drawing
rotom: talk (Up)
rotom: talk started
rotom: choice "Heya, Tyler, how’re you doing? Zzrt!" | 1 "I’m doing fine!" | 2 "A bit tired..." | cursor 1
rotom: cursor 2
rotom: answer 2 "A bit tired..."
rotom: answered 2
rotom: reply "Hey, don’t push yourself, kid! Maybe we should / take a break at a Pokémon Center?"
rotom: none
rotom: hidden
rotom: roto loto state 3
rotom: roto loto intro "Sorry to keep you waiting! It’s Roto Loto time!"
rotom: roto loto next
```

## Tests

- `tests/rotom_prompt_test.cpp`: table pointers, entries, Roto Loto states,
  line decoding (tags, private glyphs, a cut-off tag), the real lines from
  the user's dump, wrapping, keys, the log line, layout (inside the screen
  and the layer's buffer, answers inside the box) and drawing.
- `scenarios/native-rotom-chat.txt` (instance w62): boots, waits up to 12
  minutes for Rotom to want to talk (the badge) or to start a chat by
  itself, Up, and plays a Roto Loto through (`rotom next`) and asks again
  (`rotom talk`) until a question shows; then ZR (the map steps aside), Down
  to answer 2, A, the reply, the map back. Last run: PASS in 369 s, by the
  badge path: `rotom: talk (Up)`, "D'you ever organize the itemzzz and
  stuff in your Bag, Tyler?" → "Does anyone really do that?", reply "Oh, you
  take after your mother, then! ...", `map: open (Rotom)`; real save
  unchanged.
- By hand (same instance, earlier build): a chat Rotom started by itself
  showed natively and the second answer was taken; the Roto Loto was played
  by taps on the bottom screen to learn its states.

### Not verified

- **The Roto Loto through the native prompt** (A on each step): Rotom chose
  a chat on the scenario's run, and the Roto Loto came only once, before the
  prompt existed. Its states, lines and tap points were checked by hand
  (above); the prompt's Loto box and `rotom-loto-next` / `rotom-loto-stop`
  were not seen running. The scenario plays one through if Rotom picks it.
- The X menu over a waiting chat, and turning the panel on by hand during a
  chat, follow the code above but were not run.
- `[VAR 0189]` after Rotom's nickname offer (not reachable here).
