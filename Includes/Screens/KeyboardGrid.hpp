#pragma once

// The native text-entry keyboard (W5-1): its keys, cursor movement, what a
// key press turns into, and drawing. Pure C++ (no CTRPF), so tests/ can build
// it on the PC; Screens/Keyboard.cpp runs it in the plugin.

#include <string>
#include <vector>

#include "ActionQueue.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // The game's keyboard (StrInput) opens on a QWERTY page: four staggered
    // rows of character keys plus Delete, OK, Space and Shift
    // (docs/LAYOUTS.md, "strinput"). The native keyboard mirrors that page key
    // for key, so a press is one tap on the same key and the game decides
    // what it types: shift (one-shot) switches letters to capitals and the
    // digit row to ! ♂ ♀ + - * / = % ( ).
    enum class KeyKind : u8 { Char, Space, Delete, Ok, Shift };

    struct KeyboardKey
    {
        const char *name;       // LayoutButtons name after "strinput.": "q", "delete"
        KeyKind kind;
        u16 lower, upper;       // UTF-16 typed with shift off / on (Char and Space)
        int row;                // 0-3 character rows, 4 Space and Shift
    };

    const std::vector<KeyboardKey> &KeyboardKeys(void);
    // Index of the key named `name`, -1 if none.
    int KeyboardFind(const std::string &name);

    // The key's rect on the game's bottom screen (from Layouts.hpp).
    UI::Rect KeyboardGameRect(int index);

    // Cursor moves. Left / right go along the row and wrap. Up / down go to
    // the key in the next row whose centre is nearest (the rows are
    // staggered, like the game's), wrapping from the top row to the bottom.
    int KeyboardMove(int from, int dx, int dy);

    // The game's own codes for ♂ and ♀ (private use area); shown as U+2642 / U+2640.
    const u16 GameMale = 0xE08E, GameFemale = 0xE08F;
    // UTF-16 from the game -> UTF-8 for drawing and the log.
    std::string KeyboardUtf8(const u16 *text, int count);
    // What key `index` types now, as UTF-8 ("" for Delete / OK / Shift).
    std::string KeyboardKeyText(int index, bool shift);

    // The game's keyboard as read from its text object (Keyboard.cpp).
    struct KeyboardGame
    {
        u16 text[16] = {};
        int length = 0;
        int max = 0;            // the length limit (12 for a nickname)
        bool shift = false;     // capitals / symbol row
        bool qwerty = true;     // the QWERTY page is showing (not the grid page)
    };

    // A press of key `index` against the game as it is plus `pending` key
    // taps already queued (their characters count towards the limit).
    enum class KeyboardPress : u8
    {
        Tap,        // tap the key on the bottom screen
        Full,       // a character with no room left: nothing sent
        Empty,      // Delete with nothing to delete: nothing sent
    };
    KeyboardPress KeyboardPressKey(int index, const KeyboardGame &game, int pendingChars, int pendingDeletes);

    // The action that taps key `index` on the game's QWERTY page, and the one
    // that turns the grid page back to QWERTY. Long-lived definitions.
    const ActionDef &KeyboardAction(int index);
    const ActionDef &KeyboardToQwerty(void);

    // What the native keyboard shows.
    struct KeyboardView
    {
        KeyboardGame game;
        int cursor = 0;
        const char *message = nullptr;  // footer note ("Full"), nullptr for the key hints
    };

    // Screen area the keyboard draws in (top screen) and its corner radius.
    static constexpr UI::Rect KeyboardArea = { 12, 8, 376, 224 };
    static constexpr int KeyboardRadius = 12;
    // Rect of key `index` on the top screen.
    UI::Rect KeyboardKeyRect(int index);
    void DrawKeyboard(UI::Canvas &canvas, const KeyboardView &view);
}
