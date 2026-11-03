#pragma once

// The native Bag (P2-4, W7-3): the game's Bag as the plugin reads it, the
// pocket and list logic, the driver that carries out a choice on the hidden
// bottom screen one tap or press at a time, what a choice did, the pocket
// icons and item descriptions read from the RomFS, and drawing. Pure C++ (no
// CTRPF), so tests/ can build it on the PC; Screens/Bag.cpp runs it in the
// plugin. See docs/BAG.md.

#include <string>
#include <vector>

#include "ActionQueue.hpp"
#include "RomfsText.hpp"
#include "UI/GameAssets.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // The Bag's pocket tabs, left to right, as numbered by the game (its tab
    // index, docs/BAG.md). The Z-Crystals pocket has no tab in this save.
    static const int BagTabCount = 7;

    struct BagTab
    {
        const char *name;       // for commands and the log: "medicine"
        const char *label;      // the pocket's name, as the game shows it
        const char *button;     // LayoutButtons name of the game's tab
        const char *icon;       // its icon in the Bag layout (a/0/6/5) when selected; + "2" when not
    };

    const BagTab &BagTabAt(int tab);
    int BagTabFind(const std::string &name);    // -1 if unknown

    // Entries of the game's item menu (and its Yes/No question), as the game
    // lists them in its menu param (docs/BAG.md).
    namespace BagCommand
    {
        static const u8 Use = 0x00, On = 0x01, Off = 0x02, Give = 0x03, FreeSpace = 0x04, ReturnToPocket = 0x05,
                        Cancel = 0x11, Yes = 0x12, No = 0x13;
    }
    // Short labels ("Give" for the game's "Give to Pokémon"); "Option 0x07"
    // style for unknown ones.
    const char *BagCommandLabel(u8 command);

    // What the game's Bag is doing.
    enum class BagMode : u8
    {
        None,       // not the field Bag, or its objects don't check out
        List,       // the item list waits for input
        Menu,       // a menu: the item menu, or Yes/No (BagGame::YesNo)
        Pick,       // "Which Pokémon...?": the party column takes the cursor
        Message,    // a message box waits for A
        Busy,       // between those (animations)
    };

    struct BagItem
    {
        u16 id = 0;
        u16 count = 0;
        bool isNew = false;
    };

    // One frame of the game's Bag, read by Screens/Bag.cpp.
    struct BagGame
    {
        BagMode mode = BagMode::None;
        int tab = -1;                   // 0..BagTabCount-1
        std::vector<BagItem> items;     // the open pocket, in the game's order
        int visible = 0;                // rows in the game's window (7, fewer for short lists)
        int cursorRow = 0;              // the game's cursor, row in its window
        int scroll = -1;                // first item in the window; -1 while it scrolls
        std::vector<u8> commands;       // Menu: its entries (BagCommand)
        int partyCursor = -1;           // Pick: the party slot the cursor is on
        u16 item = 0;                   // the item the game's menu / pick is about

        bool Valid(void) const { return mode != BagMode::None; }
        int  Index(void) const { return scroll < 0 ? -1 : scroll + cursorRow; }
        bool YesNo(void) const
        {
            return mode == BagMode::Menu && commands.size() == 2 && commands[0] == BagCommand::Yes;
        }
        // Rough identity of what is on the screen, to tell when it settles.
        u32 Signature(void) const;
    };

    // --- Native list cursor ---

    // Rows the native list shows.
    static const int BagListRows = 5;
    // Up / down with wrap (as the game's list); 0 for an empty list.
    int BagMoveCursor(int cursor, int delta, int count);
    // First row to show so that `cursor` is visible, starting from `top`.
    int BagListTop(int top, int cursor, int count, int rows = BagListRows);

    // Whether the native menu can carry out entry `entry` of a menu: every
    // entry at a position the action layer may tap (1-4 from the bottom; a
    // fifth was never seen). The others are shown greyed.
    bool BagEntryEnabled(const std::vector<u8> &commands, int entry);

    // --- Items with a follow-up ---

    enum class BagItemKind : u8
    {
        Other,      // the plain flows: a Pokémon (Potion), a message (Escape Rope, Exp. Share), nothing
        PpOne,      // Ether, Max Ether, PP Up, PP Max: a Pokémon, then one of its moves (the PP screen)
        PpAll,      // Elixir, Max Elixir: a Pokémon
        Repel,      // Repel, Super Repel, Max Repel: a message, a question while one still works
        Stone,      // evolution stones: a Pokémon, then the evolution scene
        RareCandy,  // a Pokémon, its new level, maybe a new move and an evolution
        Tm,         // TM01-TM100: a Pokémon, then the move-forget scene (WazaOshie)
    };
    BagItemKind BagItemKindOf(u16 item);
    const char *BagItemKindName(BagItemKind kind);
    // The game's "Which Pokémon...?" for this item.
    const char *BagPickQuestion(u16 item);

    // During a Use the game can show a screen the plugin doesn't read (the
    // PP screen of an Ether, a level-up's stats). The native Bag then steps
    // aside: the panel takes over until the game is back in its list.
    static const u32 BagAsideFrames = 180;  // Busy this long during a Use
    enum class BagFollow : u8 { Native, Aside };
    // `busyFrames`: how long the game has been Busy without a break;
    // `usingItem`: a Use or Give is under way; `aside`: stepped aside already.
    BagFollow BagFollowGame(BagMode mode, u32 busyFrames, bool usingItem, bool aside);

    // What a choice in the menu did, read before and after, for the footer.
    // The Pokémon fields only when one was picked.
    struct BagResult
    {
        u8  command = 0;
        BagItemKind kind = BagItemKind::Other;
        std::string item;                   // the item's name
        int countBefore = 0, countAfter = 0;
        bool picked = false;
        std::string mon;                    // nickname, after
        int hpBefore = 0, hpAfter = 0, maxHp = 0;
        int levelBefore = 0, levelAfter = 0;
        u16 speciesBefore = 0, speciesAfter = 0;
        std::string speciesName;            // after, when it changed
        int ppBefore = 0, ppAfter = 0;      // sum over its moves
        std::vector<std::string> learned;   // moves it knows now and didn't before
        std::string heldBefore, heldAfter;  // names, "" if none
        bool holdsItem = false;             // it now holds the item
    };
    std::string BagResultText(const BagResult &r);

    // --- Driver ---

    // One thing to do on the bottom screen; each is a small ActionDef.
    enum class BagStep : u8
    {
        None,
        TapTab,     // arg: tab
        TapRow,     // arg: row in the game's window, 1..7
        PressUp,
        PressDown,
        TapMenu,    // arg: position from the bottom of the menu, 1 = last entry
        TapParty,   // arg: party slot 0..5
        TapYes,
        TapNo,
        PressA,
        PressB,
        TapBack,    // the Bag's back button; waits until the Bag scene is gone
    };

    struct BagCmd
    {
        BagStep step = BagStep::None;
        int arg = 0;
        bool operator==(const BagCmd &o) const { return step == o.step && arg == o.arg; }
    };

    // The action for a command (named "bag-..."); definitions live as long as
    // the program. nullptr for None or an argument out of range.
    const ActionDef *BagAction(const BagCmd &cmd);

    enum class BagGoal : u8
    {
        None,
        Tab,        // switch the game to a pocket
        Open,       // open the item menu of one item
        Choose,     // tap one entry of the open menu
        Pick,       // choose a party slot
        Answer,     // Yes / No
        Advance,    // A through messages until the Bag waits for input again
        Cancel,     // back to the list from a menu or the party pick
        Close,      // leave the Bag (back button)
    };

    const char *BagGoalName(BagGoal goal);

    // Carries out one goal with closed-loop taps and presses: after each
    // command it waits for the game to settle, looks again, and decides the
    // next one. Fails (and logs why) if the game doesn't get there in time.
    class BagDriver
    {
    public:
        void Tab(int tab);
        void Open(int index, u16 id);
        void Choose(int entry);
        void Pick(int slot);
        void Answer(bool yes);
        void Advance(void);
        void Cancel(void);
        void Close(void);
        void Stop(void) { m_goal = BagGoal::None; }

        // Once per frame. `actionRunning`: the action layer is busy (running
        // our last command, or anything else). Returns the command to start
        // now (None: nothing this frame). If the command can't be started,
        // call Refused.
        BagCmd Update(const BagGame &game, bool actionRunning, std::vector<std::string> &log);
        void Refused(const std::string &why, std::vector<std::string> &log);

        bool Busy(void) const { return m_goal != BagGoal::None; }
        BagGoal Goal(void) const { return m_goal; }
        // "" while running or after success; why the last goal failed.
        const std::string &Failure(void) const { return m_failure; }
        // Bumped every time a goal ends (either way).
        u32 Finished(void) const { return m_finished; }
        std::string Status(void) const;

        // Frames the game must look the same before the driver acts on it,
        // after an action and in general; how long a goal may take; how
        // many commands it may send.
        static const u32 SettleFrames = 6;
        static const u32 MessageSettleFrames = 12;
        static const u32 Timeout = 600;
        static const u32 MaxCommands = 64;

    private:
        void Begin(BagGoal goal);
        void Done(std::vector<std::string> &log);
        void Fail(const std::string &why, std::vector<std::string> &log);
        BagCmd Send(BagStep step, int arg = 0);

        BagGoal m_goal = BagGoal::None;
        int m_arg = 0;              // tab, index, entry, slot, yes
        u16 m_id = 0;               // Open: the item
        u32 m_frames = 0;           // frames on this goal
        u32 m_stable = 0;           // frames the game has looked the same
        u32 m_signature = 0;
        bool m_sent = false;        // a command was sent and the action layer hasn't finished it
        u32 m_commands = 0;         // commands sent for this goal
        u32 m_startSignature = 0;   // Choose / Answer: the menu when the goal began
        std::string m_failure;
        u32 m_finished = 0;
    };

    // --- What the native Bag shows ---

    struct BagMon
    {
        std::string name;
        int level = 0;
        int hp = 0, maxHp = 0;
        std::string held;       // held item's name, empty if none
        bool egg = false;
    };

    // The game's pocket icons and item descriptions, read once from the
    // RomFS (docs/BAG.md "Pocket icons"). Missing parts draw the fallback.
    struct BagAssets
    {
        UI::Image pockets[BagTabCount][2];  // [tab][0 selected, 1 not]
        // The description message file in the RomFS image (it is 190 KB, so
        // each description is read when it is needed, BagReadDescription).
        uint64_t descriptionsAt = 0;
        u32 descriptionsSize = 0;

        const UI::Image *Pocket(int tab, bool selected) const;
        int PocketCount(void) const;        // icons loaded, of BagTabCount * 2
    };
    // The Bag layout archive, and the message file with item descriptions
    // (in a/0/3/<lang>, line = item ID).
    static const char *const BagLayoutGarc = "a/0/6/5";
    static const u32 BagDescriptionFile = 39;
    // Reads files out of an LZ11-compressed layout archive (ALYT / SARC) at
    // `at` (`size` bytes) without unpacking all of it: the stream is read in
    // 4 KB chunks and decoded through a 4 KB window, and only the archive's
    // header and the wanted files are kept (the Bag layout is 290 KB
    // unpacked; this needs ~60 KB). files[i] gets names[i], empty if absent.
    bool Lz11ArchiveFiles(const RomfsRead &read, uint64_t at, u32 size, const std::vector<std::string> &names,
                          std::vector<std::vector<u8>> &files, std::string &error);
    // Reads the pocket icons and finds the description file through `read`
    // (the running game's RomFS, language `lang` 0-9); each part that fails
    // is logged and left empty.
    void LoadBagAssets(const RomfsRead &read, u32 lang, BagAssets &out, std::vector<std::string> &log);
    // One line of the message file at `fileAt` (`fileSize` bytes), read and
    // decrypted on its own, as UTF-8 with control tags removed.
    bool ReadMessageLine(const RomfsRead &read, uint64_t fileAt, u32 fileSize, u32 line, std::string &out,
                         std::string &error);
    // An item's description: its message line with the line breaks made
    // spaces. "" if it can't be read.
    std::string BagReadDescription(const RomfsRead &read, const BagAssets &assets, u16 item);

    struct BagView
    {
        BagMode mode = BagMode::None;
        int tab = -1;               // the game's pocket
        int wantTab = -1;           // the pocket being switched to (-1 none)
        std::vector<std::string> names;     // item names, as BagGame::items
        std::vector<BagItem> items;
        int cursor = 0, top = 0;    // native list cursor and first row shown
        std::vector<u8> commands;   // Menu entries
        int menuCursor = 0;
        std::string menuTitle;      // the question over the menu
        std::string pickQuestion;   // Pick: "Which Pokémon do you want to teach it to?"
        std::string description;    // the cursor's item; "" if not loaded
        const BagAssets *assets = nullptr;  // pocket icons, or nullptr
        std::vector<BagMon> party;
        int partyCursor = -1;       // Pick
        std::string status;         // footer: what is happening, or the last result
        bool busy = false;          // the driver is working: dim the input hints
    };

    // Screen area the Bag draws in (the whole top screen) and its corner radius.
    static constexpr UI::Rect BagArea = { 0, 0, 400, 240 };
    static constexpr int BagRadius = 0;
    void DrawBag(UI::Canvas &canvas, const BagView &view);
}
