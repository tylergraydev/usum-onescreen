#pragma once

// The native X menu (P2-2): its entries, where they sit on the top-screen
// grid, cursor movement, the action that opens each entry, and drawing. Pure
// C++ (no CTRPF), so tests/ can build it on the PC; Screens/XMenu.cpp runs it
// in the plugin.

#include <string>
#include <vector>

#include "ActionQueue.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // The game's X menu is two pages of a 2 x 3 grid (docs/LAYOUTS.md). The
    // native menu shows both pages side by side, page 1 in columns 0-1 and
    // page 2 in columns 2-3, with the field map and Save in a bottom row.
    static const int XMenuCols = 4;
    static const int XMenuRows = 4;

    struct XMenuGameLayout
    {
        u8 ids[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 11, 3, 7 };
        u16 unlocked = 0xF77;   // all but Photo Album and Poke Pelago
        bool read = false;
    };

    // Invalid tables use the default. Returns true only when it changes.
    bool XMenuSetLayout(const XMenuGameLayout &layout);
    const XMenuGameLayout &XMenuCurrentLayout(void);
    u32 XMenuLayoutVersion(void);
    bool XMenuVisible(int index);

    struct XMenuEntry
    {
        const char *name;       // for commands and the log: "pokedex"
        const char *line1;      // label, one or two lines
        const char *line2;
        int page;               // the game's X menu page (0, 1) and slot
        int slot;               // (0-5, row-major, 2 columns); -1 if not in the grid
        const char *button;     // current slot button; nullptr if hidden or done with keys
        const char *opens;      // scene the entry opens; nullptr: the X menu just goes away
        int col, row, span;     // place on the native grid
        UI::Color color;        // pill colour, after the game's icons
    };

    const std::vector<XMenuEntry> &XMenuEntries(void);
    // Index of the entry named `name`, or of the grid entry at the game's
    // (page, slot); -1 if none.
    int XMenuFind(const std::string &name);
    int XMenuAtSlot(int page, int slot);

    // Cursor moves. `dx`/`dy` is one step left/right/up/down. Left and right
    // stay in the row; up and down prefer entries above or below the cursor
    // (sharing a column), the nearest row first. No wrap: with nothing in that
    // direction the cursor stays. `enabled` has bit i set if entry i can take
    // the cursor.
    int XMenuMove(int from, int dx, int dy, unsigned enabled);
    // L / R: the same slot on the other page (the nearest enabled grid entry
    // there if that slot is empty); entries outside the grid stay.
    int XMenuFlipPage(int from, int page, unsigned enabled);

    // What the game's own cursor said, and ours. The game remembers its page
    // and entry (it reopens there, and a tap moves its cursor). We follow it
    // when it changes, so a choice made on the bottom screen (panel, touch)
    // carries over; entries the game doesn't have (Map) keep our cursor.
    class XMenuCursor
    {
    public:
        int  Index(void) const;
        void Set(int index) { m_index = index; }
        // A reading of the game's cursor (page, slot). Returns true if the
        // native cursor moved because of it.
        bool FromGame(int page, int slot);
        // An entry was opened: the game's cursor is now there too.
        void Chosen(int index);

    private:
        mutable int m_index = 0;
        mutable u32 m_layoutVersion = ~0u;
        mutable int m_gamePage = -1, m_gameSlot = -1;   // last game cursor we know of
    };

    // True if entry `index` opens a native top-screen screen
    // (Screens/SubScreens.hpp); the others show on the bottom screen
    // (L+R: the panel).
    bool XMenuOpensNative(int index);

    // The action that opens entry `index` from the open X menu: turns the
    // game's page if needed and taps the entry (Save: Y; Map: B, then the
    // field map); done when the entry's scene shows or the X menu goes away.
    // The definitions live as long as the program.
    const ActionDef &XMenuAction(int index);
    // Just closes the X menu (B), done when the field shows without it. The
    // Map entry uses it to hand over to the native map (Screens/Map.hpp).
    const ActionDef &XMenuCloseAction(void);

    // The native screen that takes over once entry `index` has opened the
    // game's own ("pokedex": Screens/Pokedex.hpp), or nullptr. The X menu
    // still opens the game's screen; it keeps the panel off until then.
    const char *XMenuNativeScreen(int index);

    // What the native menu shows.
    struct XMenuView
    {
        int cursor = 0;
        int opening = -1;           // entry being opened (dimmed menu), -1 none
        unsigned enabled = ~0u;     // as for XMenuMove
    };

    // Screen area the menu draws in (top screen) and its corner radius.
    static constexpr UI::Rect XMenuArea = { 12, 8, 376, 224 };
    static constexpr int XMenuRadius = 12;
    // Rect of entry `index` on the top screen.
    UI::Rect XMenuCellRect(int index);
    void DrawXMenu(UI::Canvas &canvas, const XMenuView &view);
}
