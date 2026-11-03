#pragma once

// The native Party screen and Pokémon summary (P2-3, W7-2): what they show,
// cursor and menu logic, the actions that carry a choice out on the game's
// own Party screen, and drawing. Pure C++ (no CTRPF), so tests/ can build it
// on the PC; Screens/PartyScreen.cpp runs it in the plugin. See
// docs/PARTY-UI.md.

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

    // Scene module of the move-teaching screen (a TM from the Party's Bag).
    extern const char *const SceneTeach;

    // One party member as shown (screen position order).
    struct PartyMonView
    {
        bool valid = false;         // decoded; otherwise drawn as "?"
        bool egg = false;
        std::string name;           // nickname
        std::string species;
        u8  level = 0;
        u8  gender = 2;             // 0 male, 1 female, 2 none
        u16 hp = 0, maxHp = 0;
        u32 status = 0;             // raw status word (PK7 party stats +0)
        u16 itemId = 0;             // held item, 0 if none
        std::string item;           // its name, empty if none
        u16 moves[4] = {};
        u8  pp[4] = {};
        // Summary only.
        std::string moveNames[4];   // empty: not known (drawn as "Move N")
        std::string ability, nature;
    };

    // Short status label ("PSN", "PAR", ...), empty for none. The word is a
    // number, not the older games' bit field: 5 is poison (the game's Party
    // screen said POISONED). 1 paralysis, 2 sleep, 3 freeze, 4 burn are the
    // usual Gen 7 values, not seen here yet; anything else is "?".
    const char *PartyStatusName(u32 status);

    // The Bag pockets an item can be chosen from, in the Bag's tab order.
    // Use: all four; Give: not TMs (they can't be held).
    enum class PartyPocket : u8 { Medicine, Items, TMs, Berries, Count };
    const char *PartyPocketName(PartyPocket pocket);
    const char *PartyPocketButton(PartyPocket pocket);   // LayoutButtons name of its tab
    bool PartyPocketAllowed(PartyPocket pocket, bool give);
    // The next allowed pocket that way (dir +1 / -1), wrapping.
    PartyPocket PartyNextPocket(PartyPocket pocket, int dir, bool give);
    // Use from Medicine and Berries goes through the Party screen's Restore
    // (P2-3's path); everything else through Held Item -> Use/Give, whose
    // Bag has every pocket.
    bool PartyUseViaRestore(PartyPocket pocket);

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
        Menu,       // Summary / Use item / Give item / Take item / Move / Cancel
        Swap,       // pick where the Pokémon goes
        Items,      // pick an item to use on it or give it
        Ask,        // the game asks something mid-way (Yes/No, which move, teach)
    };

    enum class PartyMenuItem : u8 { Summary, UseItem, GiveItem, TakeItem, Move, Cancel, Count };
    const char *PartyMenuLabel(PartyMenuItem item);
    // The menu for a Pokémon: Take only while it holds an item, Move only
    // with two or more Pokémon.
    std::vector<PartyMenuItem> PartyMenuEntries(bool holding, int count);

    // What a key press asks the plugin to do.
    struct PartyCommand
    {
        enum Kind : u8 { None, Summary, Swap, UseItem, GiveItem, TakeItem, Answer } kind = None;
        int pos = -1;               // screen position
        int other = -1;             // Swap: the other position
        PartyPocket pocket = PartyPocket::Medicine;
        int item = -1;              // UseItem / GiveItem: index in that pocket's list
        int choice = -1;            // Answer: the choice (Ask page)
    };

    // What the current page needs to know besides the keys.
    struct PartyMenuInput
    {
        int count = 0;                                  // party size
        const std::vector<PartyBagItem> *items = nullptr; // the open pocket's list (Items page)
        bool holding = false;                           // the cursor's Pokémon holds an item
        int choices = 0;                                // Ask page: how many
        int cancelChoice = -1;                          // Ask page: what B answers (-1: B does nothing)
    };

    // The native screen's state: page and cursors. Press() takes the keys
    // pressed this frame (one direction at most).
    class PartyMenuState
    {
    public:
        void Reset(int count);                  // back to the list, cursor kept in range
        void SetCursor(int pos, int count);     // e.g. from the game's cursor
        PartyCommand Press(u32 keys, const PartyMenuInput &in);
        // The game asks something: show `choices` answers (Ask page).
        void Ask(int cursor = 0);
        // Leave the Ask page (answered, or the game moved on).
        void EndAsk(void);

        PartyPage Page(void) const { return m_page; }
        int Cursor(void) const { return m_cursor; }
        int MenuCursor(void) const { return m_menu; }       // index into PartyMenuEntries
        int SwapFrom(void) const { return m_swapFrom; }
        PartyPocket Pocket(void) const { return m_pocket; }
        int ItemCursor(void) const { return m_item; }
        bool Giving(void) const { return m_give; }          // Items page: for Give (else Use)
        int AskCursor(void) const { return m_ask; }
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
        bool m_give = false;
        int m_ask = 0;
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
    // Held Item -> Use/Give for `pos`: the Bag for that Pokémon with every
    // pocket, then the pocket's tab.
    void MakePartyHeldBagAction(PartyAction &out, int pos, PartyPocket pocket);
    // Held Item -> Take: "You received the X from Y.", B; done on the list.
    void MakePartyTakeAction(PartyAction &out, int pos);
    // In that Bag: tap shown row `row` (0-6; opens the item menu), or one
    // D-pad step to scroll.
    void MakePartyBagRowAction(PartyAction &out, int row);
    void MakePartyBagStepAction(PartyAction &out, bool up);
    // Tap the pocket's tab again (the first tap wasn't seen).
    void MakePartyBagTabAction(PartyAction &out, PartyPocket pocket);
    // The Bag's menu (item menu, or a question's answers): tap the entry
    // `fromBottom` places from the bottom (1 = the last one, 1-5).
    void MakePartyBagMenuAction(PartyAction &out, int fromBottom);
    // Yes / No of the Bag's question ("switch the two items?").
    void MakePartyYesNoAction(PartyAction &out, bool yes);
    // One A (the Bag, as the native Bag does) or B (the Party screen's
    // "You received ...") through a message.
    void MakePartyMessageAction(PartyAction &out, bool bag);
    // The teaching screen: row 0-3 forgets that move for the new one, 4
    // gives the new one up (its page's first button either way); done when
    // the screen has gone.
    void MakePartyTeachAction(PartyAction &out, int row);
    // Back out of the Bag (B until it is gone); done on the plain list.
    void MakePartyBagBackAction(PartyAction &out);

    // Which entry of the game's item menu (its command bytes, top to
    // bottom, docs/BAG.md) does Use or Give: its place from the bottom (1 =
    // the last entry), 0 if the menu has none. Give is 0x07 in the Party
    // screen's Bag (0x03 in the field Bag); Use is the first entry that is
    // none of Give, Move to Free Space, Cancel, Yes, No (0x06 here).
    int PartyItemMenuEntry(const std::vector<u8> &commands, bool give);

    // How to bring item `target` of the Bag's list into reach, from what the
    // list shows: tap a shown row (row >= 0) or step (dir -1 up, +1 down;
    // the list scrolls once the cursor reaches its edge). `scroll` is the
    // first shown index (whole number), `rows` how many are shown.
    struct PartyBagPlan
    {
        int row = -1;
        int dir = 0;
    };
    PartyBagPlan PlanPartyBag(int target, int scroll, int rows, int count);

    // --- After Use / Give: following the game ---

    // What the game shows while a Use or Give plays out.
    enum class PartyGame : u8
    {
        Gap,            // no scene (loading)
        PartyList,      // the Party screen's plain list
        PartyOther,     // the Party screen with a message or menu
        BagList,        // the Bag's item list waits for input
        BagMenu,        // a menu the plugin didn't open (e.g. which move)
        BagYesNo,       // the Bag asks Yes / No
        BagMessage,     // a message waits for A
        BagBusy,        // the Bag between those
        Teach,          // the teaching screen's move list
        TeachPage,      // one move's page on it
        Other,          // another scene (evolution, ...)
    };

    enum class PartyFollowDo : u8
    {
        Wait,
        Advance,        // through a message (A in the Bag, B on the Party screen)
        Ask,            // show the question natively (Yes/No, a menu)
        Teach,          // show the teaching screen natively
        LeaveBag,       // back out of the Bag to the Party screen
        Done,           // back on the plain list
    };
    // `stable`: frames the game has looked like `game`. Nothing is done
    // until it has settled; A and B are only ever pressed in the Bag or on the
    // Party screen (outside them a B could stop an evolution).
    PartyFollowDo PartyFollow(PartyGame game, u32 stable);

    // --- Drawing ---

    // One answer on the Ask page.
    struct PartyChoice
    {
        std::string label;
        u16 move = 0;               // a move: drawn with its type and PP
        u8  pp = 0;
    };

    struct PartyAskView
    {
        std::string title;          // up to two lines, '\n' between
        std::vector<PartyChoice> choices;
        bool moves = false;         // choices are moves (+ "Don't learn")
    };

    struct PartyView
    {
        std::vector<PartyMonView> mons;     // screen order
        PartyPage page = PartyPage::List;
        int cursor = 0;
        int menuCursor = 0;
        int swapFrom = -1;
        PartyPocket pocket = PartyPocket::Medicine;
        bool giving = false;
        int itemCursor = 0;
        std::vector<PartyBagItem> items;    // the pocket's list (Items page)
        PartyAskView ask;                   // Ask page
        int askCursor = 0;
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
    // Plate of screen position `pos`.
    UI::Rect PartyCardRect(int pos);
    void DrawParty(UI::Canvas &canvas, const PartyView &view);
    void DrawSummary(UI::Canvas &canvas, const SummaryView &view);
    // A string that changes whenever the drawing would (cheap redraw test).
    std::string PartyViewKey(const PartyView &view);
    std::string SummaryViewKey(const SummaryView &view);
}
