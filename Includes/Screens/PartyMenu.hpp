#pragma once

// The native Party screen and Pokémon summary (P2-3): what they show, cursor
// and menu logic, the actions that carry a choice out on the game's own
// Party screen, and drawing. Pure C++ (no CTRPF), so tests/ can build it on
// the PC; Screens/PartyScreen.cpp runs it in the plugin. See docs/PARTY-UI.md.

#include <string>
#include <vector>

#include "ActionQueue.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // HID key bits (same values as CTRPF's Key::); the plugin folds the
    // circle pad into the D-pad bits before handing keys over.
    namespace PartyKey
    {
        const u32 A = 1u << 0, B = 1u << 1, Select = 1u << 2, Start = 1u << 3;
        const u32 Right = 1u << 4, Left = 1u << 5, Up = 1u << 6, Down = 1u << 7;
        const u32 R = 1u << 8, L = 1u << 9, X = 1u << 10, Y = 1u << 11;
    }

    // One party member as shown (screen position order).
    struct PartyMonView
    {
        bool valid = false;         // decoded; otherwise drawn as "?"
        bool egg = false;
        std::string name;           // nickname
        std::string species;
        u8  level = 0;
        u16 hp = 0, maxHp = 0;
        u32 status = 0;             // raw status word (PK7 party stats +0)
        std::string item;           // held item, empty if none
        // Summary only.
        u16 moves[4] = {};
        u8  pp[4] = {};
        std::string moveNames[4];   // empty: not known (drawn as "Move N")
        std::string ability, nature;
    };

    // Short status label ("PSN", "PAR", ...), empty for none. The word is a
    // number, not the older games' bit field: 5 is poison (the game's Party
    // screen said POISONED). 1 paralysis, 2 sleep, 3 freeze, 4 burn are the
    // usual Gen 7 values, not seen here yet; anything else is "?".
    const char *PartyStatusName(u32 status);

    // The pockets items can be used from (the Restore bag's tabs).
    enum class PartyPocket : u8 { Medicine, Berries, Count };
    const char *PartyPocketName(PartyPocket pocket);
    const char *PartyPocketButton(PartyPocket pocket);   // LayoutButtons name of its tab

    struct PartyBagItem
    {
        u16 id = 0;
        u16 count = 0;
        std::string name;
    };

    // Screen positions are laid out as the game's Party screen: position n
    // in column n % 2, row n / 2. Left/right stay in the row, up/down move a
    // row; no wrap. Returns `from` if there is nothing that way.
    int PartyMove(int from, int dx, int dy, int count);

    enum class PartyPage : u8
    {
        List,       // the party; A opens the menu, Y starts a move
        Menu,       // Summary / Use item / Move / Cancel for the cursor's Pokémon
        Swap,       // pick where the Pokémon goes
        Items,      // pick an item to use on it
    };

    enum class PartyMenuItem : u8 { Summary, UseItem, Move, Cancel, Count };
    const char *PartyMenuLabel(PartyMenuItem item);

    // What a key press asks the plugin to do.
    struct PartyCommand
    {
        enum Kind : u8 { None, Summary, Swap, UseItem } kind = None;
        int pos = -1;               // screen position
        int other = -1;             // Swap: the other position
        PartyPocket pocket = PartyPocket::Medicine;
        int item = -1;              // UseItem: index in that pocket's list
    };

    // The native screen's state: page and cursors. Press() takes the keys
    // pressed this frame (one direction at most) and the current party size
    // and item lists.
    class PartyMenuState
    {
    public:
        void Reset(int count);                  // back to the list, cursor kept in range
        void SetCursor(int pos, int count);     // e.g. from the game's cursor
        PartyCommand Press(u32 keys, int count, const std::vector<PartyBagItem> *items);

        PartyPage Page(void) const { return m_page; }
        int Cursor(void) const { return m_cursor; }
        int MenuCursor(void) const { return m_menu; }
        int SwapFrom(void) const { return m_swapFrom; }
        PartyPocket Pocket(void) const { return m_pocket; }
        int ItemCursor(void) const { return m_item; }
        // Keys the screen uses on this page (B only off the list: on the
        // list B is left to the game, which closes its Party screen).
        u32 Keys(void) const;

    private:
        PartyPage m_page = PartyPage::List;
        int m_cursor = 0;
        int m_menu = 0;
        int m_swapFrom = -1;
        PartyPocket m_pocket = PartyPocket::Medicine;
        int m_item = 0;
    };

    // --- Actions on the game's Party screen ---

    // An action built for one choice. The plugin keeps it until it is done
    // (ActionStart needs the definition to outlive the run).
    struct PartyAction
    {
        std::string name;
        std::string summary;
        ActionDef def = { nullptr, nullptr, {}, {} };
    };

    // Summary of position `pos`: tap the slot, then Summary; done in Status.
    void MakePartySummaryAction(PartyAction &out, int pos);
    // Move mode (the Y button), put the game's cursor on `from` (a tap moves
    // it; skipped if `gameCursor` is already there), A picks it, a tap on
    // `to` swaps them, B leaves move mode; done on the plain list.
    void MakePartySwapAction(PartyAction &out, int from, int to, int gameCursor);
    // Restore for `pos`: the Bag for that Pokémon, then the pocket's tab.
    void MakePartyRestoreAction(PartyAction &out, int pos, PartyPocket pocket);
    // In that Bag: tap shown row `row` (0-6; opens the item menu), or one
    // D-pad step to scroll.
    void MakePartyBagRowAction(PartyAction &out, int row);
    void MakePartyBagStepAction(PartyAction &out, bool up);
    // From the item menu: "Use this item", B through the message, B out of
    // the Bag; done on the plain list.
    void MakePartyUseAction(PartyAction &out);
    // Back out of the Restore bag without using anything.
    void MakePartyBagBackAction(PartyAction &out);

    // How to bring item `target` of the Restore bag's list into reach, from
    // what the list shows: tap a shown row (row >= 0) or step (dir -1 up,
    // +1 down; the list scrolls once the cursor reaches its edge). `scroll`
    // is the first shown index (whole number), `rows` how many are shown.
    struct PartyBagPlan
    {
        int row = -1;
        int dir = 0;
    };
    PartyBagPlan PlanPartyBag(int target, int scroll, int rows, int count);

    // --- Drawing ---

    struct PartyView
    {
        std::vector<PartyMonView> mons;     // screen order
        PartyPage page = PartyPage::List;
        int cursor = 0;
        int menuCursor = 0;
        int swapFrom = -1;
        PartyPocket pocket = PartyPocket::Medicine;
        int itemCursor = 0;
        std::vector<PartyBagItem> items;    // the pocket's list (Items page)
        std::string busy;                   // non-empty: an action runs ("Using Potion on Pichu...")
        std::string note;                   // last result or hint for the footer
    };

    struct SummaryView
    {
        PartyMonView mon;
        int pos = 0, count = 0;
        bool hidden = false;                // Y: the game's own top screen shows instead
    };

    // Same area as the native X menu.
    static constexpr UI::Rect PartyArea = { 12, 8, 376, 224 };
    static constexpr int PartyRadius = 12;
    // Card of screen position `pos`.
    UI::Rect PartyCardRect(int pos);
    void DrawParty(UI::Canvas &canvas, const PartyView &view);
    void DrawSummary(UI::Canvas &canvas, const SummaryView &view);
    // A string that changes whenever the drawing would (cheap redraw test).
    std::string PartyViewKey(const PartyView &view);
    std::string SummaryViewKey(const SummaryView &view);
}
