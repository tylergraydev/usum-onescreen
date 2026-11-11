#pragma once

// Native battle menu (P2-1, W7-1): the top-screen version of the game's
// battle command menu, move list, Pokémon list, Bag and questions. This
// header is the pure part, with no CTRPF dependency, so tests/ can build it
// on the PC: menu state, input, the steps that carry a choice out, and
// drawing. Screens/BattleScreen.cpp feeds it the battle state (RE1) and
// names (RE2) once per frame and starts the actions it asks for.
//
// The menu follows the game: each page shows while the matching game screen
// is up on the hidden bottom screen, and a choice is carried out by tapping
// the game's own buttons (ActionQueue.hpp: battle-fight ... battle-moves-back,
// and single taps for the Pokémon list, the Bag and the questions), one step
// at a time, looking at the game again after each.

#include <string>
#include "BattleRules.hpp"

#include "UI/Raster.hpp"
#include "Settings.hpp"

namespace OneScreen
{
namespace Screens
{
    using UI::u8;
    using UI::u16;
    using UI::u32;
    using UI::s32;

    // What the game's bottom screen shows in battle (BattleMenu in
    // Battle.hpp, same order; mirrored so this builds on the PC).
    enum class GameMenu : u8 { None, Command, Moves, Pokemon, Bag, Other, PokemonPage, Question, Target };

    // One move as the menu shows it. type and effect come from the game's
    // move list (0xFF if unknown; the plugin fills type from the game's move
    // table when the list isn't up); effect: 0 no effect, 1 effective,
    // 2 super effective, 3 not very effective (docs/RE1-battle.md).
    struct MoveRow
    {
        u16 move = 0;
        std::string name;
        std::string description;
        u8 type = 0xFF;
        u8 effect = 0xFF;
        u8 pp = 0, maxPp = 0;
        u8 power = 0, accuracy = 0; // raw move-table values; accuracy 101 = never misses
        bool spread = false;
        u8 category = 0xFF;        // 0 status, 1 physical, 2 special; 0xFF unknown
        std::string typeName;   // the game's name for the type, if known (else TypeName)
    };

    // One Pokémon of the battle party, in battle order (slot 0 = in battle),
    // which is the order of the game's Pokémon list and of the Bag's
    // "Which Pokémon...?" column.
    struct BattlePartyMon
    {
        std::string name;       // nickname if known, else the species
        std::string species;
        u8 level = 0;
        u16 hp = 0, maxHp = 0;
        u32 status = 0;         // PK7 status word (PartyStatusName)
        std::string item;       // held item, empty if none
        bool egg = false;
        bool shiny = false, hiddenAbility = false;
    };

    // One entry of the battle Bag's open pocket.
    struct BattleBagItem
    {
        u16 id = 0;
        u16 count = 0;
        std::string name;
    };

    struct BattlerInfo
    {
        std::string name;
        u8 types[3] = { 18, 18, 18 };
        u8 stages[7] = { 6, 6, 6, 6, 6, 6, 6 };
        bool caught = false;
    };
    struct TargetSlot
    {
        std::string name;
        bool live = false, foe = false, self = false;
    };
    template<class Mon>
    TargetSlot MakeTargetSlot(BattleKind kind, u8 format, u8 plate, const Mon &mon, const std::string &name)
    {
        const bool occupied = TargetOccupied(kind, format, plate)
            && mon.address && mon.species >= 1 && mon.species <= 807;
        return {occupied ? name : std::string(), occupied && LiveBattleRecord(mon),
                TargetRecord(kind, plate).client != 0, plate == 2};
    }
    bool TargetSelectable(const TargetSlot &slot);
    u8 FirstTarget(const TargetSlot slots[4]); // 0xFF when none
    u8 NextTarget(const TargetSlot slots[4], u8 cursor, int delta);
    std::string StatStagesText(const u8 stages[7]);
    std::string BattlerTypesText(const u8 types[3]);
    UI::Rect BattleInfoArea(BattleLayout layout);

    // The battle as the menu needs it, once per frame.
    struct BattleSnapshot
    {
        bool inBattle = false;
        BattleKind kind = BattleKind::Unknown;
        BattlerInfo foeInfo, playerInfo;
        TargetSlot targets[4];
        bool spread = false;
        GameMenu menu = GameMenu::None;
        u8 gameCursor = 0;          // the game's highlighted entry
        u8 listCount = 0;           // entries in the game's list
        std::string activeName;     // the player's battler
        bool activeShiny = false, activeHiddenAbility = false;
        bool foeShiny = false, foeHiddenAbility = false;
        MoveRow moves[4];
        u8 moveCount = 0;
        // The battle party, in battle order.
        BattlePartyMon party[6];
        u8 partyCount = 0;
        // Bag (menu Bag): tab, item page, the open pocket's items, the item
        // an item page shows, "Which Pokémon...?" and its cursor (0xFF
        // unknown).
        u8 bagPocket = 0;
        bool bagItemPage = false;
        BattleBagItem bagItems[64];
        u8 bagItemCount = 0;
        u8 bagPageIndex = 0xFF;
        bool bagPick = false;
        u8 bagPickCursor = 0xFF;
        // Question (menu Question): its message ID.
        u16 question = 0;
        bool actionRunning = false; // an action is driving the bottom screen
        // After a catch, out of the battle: the Pokédex scene asks whether
        // to nickname the new Pokémon (Yes/No on the bottom screen).
        bool afterCatch = false;
        // menu == Other: the game's screen object's vtable (module-relative
        // in a CRO), for the log.
        u32 otherVtable = 0;
        // The panel_key setting as text ("L+R"), for the helpers' hints.
        std::string panelKey = "L+R";
    };

    std::string BattleInfoKey(const BattleSnapshot &snap);
    void DrawBattleInfo(UI::Canvas &canvas, const BattleSnapshot &snap, BattleLayout layout);

    enum class BattlePage : u8
    {
        Hidden,     // nothing to choose (animations, messages) or not in battle
        Command,    // Fight / Bag / Pokémon / Run
        Moves,      // the move list
        Game,       // a game screen the menu doesn't know: helper only
        Party,      // the Pokémon list (switch)
        Bag,        // the Bag: pockets, items, "Which Pokémon...?"
        Question,   // "Use next Pokémon?": Use next Pokémon / Run
        Target,     // verified target vtable: explicit choice/confirmation (SOS / Royal)
    };

    // Command bar entries, in the order the menu lists them.
    enum class BattleCommand : u8 { Fight, Bag, Pokemon, Run, Count };

    // Messages of the battle's two-choice question (docs/OFFSETS.md).
    static const u16 QuestionNextPokemon = 0x4E;   // "Use next Pokémon?": Use Next Pokémon / Run
    static const u16 QuestionForgetMove = 0x2F;    // learning a move (Screens/MoveLearn.hpp)

    struct BattleMenuOutput
    {
        const char *action = nullptr;   // built-in action to start now (ActionQueue name)
        const char *tap = nullptr;      // or one tap of this layout button (then a short wait)
        u32 press = 0;                  // or one press of these keys
        bool capture = false;           // keep the page's keys from the game this frame
        u32 keys = 0;                   // which (BattleMenu::CaptureKeys)
    };

    // HID key bits (same values as CTRPF's Key::).
    namespace BattleKeys
    {
        const u32 A = 1u << 0, B = 1u << 1, DRight = 1u << 4, DLeft = 1u << 5, DUp = 1u << 6, DDown = 1u << 7;
        const u32 R = 1u << 8, L = 1u << 9;
        const u32 CPadRight = 1u << 28, CPadLeft = 1u << 29, CPadUp = 1u << 30, CPadDown = 1u << 31;
        const u32 Up = DUp | CPadUp, Down = DDown | CPadDown, Left = DLeft | CPadLeft, Right = DRight | CPadRight;
    }

    // Keys the menu keeps from the game while it is up (the circle pad too).
    // Y (quick ball) and touch pass. Moves also capture L (description);
    // the Bag captures L/R (pockets).
    const u32 BattleMenuKeys = BattleKeys::A | BattleKeys::B | BattleKeys::DRight | BattleKeys::DLeft
                               | BattleKeys::DUp | BattleKeys::DDown;
    const u32 BattleBagKeys = BattleMenuKeys | BattleKeys::L | BattleKeys::R;

    // SOS/Royal target screen and taps verified in trainer-double.md.
    static const u32 TargetVtable = 0x10B848;
    static const u8 TargetCount = 4;
    const char *TargetButton(u8 slot);
    const char *TargetLabel(u8 slot);

    // Item IDs that only restore HP (Potion ... Sitrus Berry): the menu
    // won't use one on a Pokémon at full HP or fainted ("It won't have any
    // effect"); the game would take the turn anyway (seen 2026-11-02).
    bool IsHpOnlyItem(u16 item);

    class BattleMenu
    {
    public:
        // Frames the game's menu must have been up before a choice is sent
        // (a tap as the command menu slid in only moved its cursor), and how
        // long a choice may stay unanswered before the menu takes input again.
        static const u32 CommandSettleFrames = 30;
        static const u32 MovesSettleFrames = 10;
        static const u32 PendingFrames = 310;   // just past the action's 3 taps (~297 frames)
        // After a move or an item the game's command menu shows for a few
        // frames before the turn starts; the command bar waits this long
        // before it shows.
        static const u32 ShowFrames = 30;
        // Step driver (Pokémon list, Bag, questions): the game must look the
        // same this long before the next step is chosen; a goal fails after
        // GoalFrames or GoalSteps.
        static const u32 StepSettleFrames = 12;
        static const u32 ListSettleFrames = 24;   // a list that just slid in
        static const u32 GoalFrames = 900;
        static const u32 GoalSteps = 16;
        // How long a page with a goal running stays up while the game shows
        // no screen between two of its own.
        static const u32 GapFrames = 45;

        // `pressed`: keys newly pressed this frame (real input, before capture).
        BattleMenuOutput Update(const BattleSnapshot &snap, u32 pressed);

        void SetLayout(BattleLayout layout) { m_layout = layout; }
        BattleLayout Layout(void) const { return m_layout; }

        BattlePage Page(void) const { return m_page; }
        u8 Cursor(void) const { return m_cursor; }
        bool Spread(void) const { return m_spread; }
        bool DescriptionOpen(void) const { return m_description; }
        bool Pending(void) const { return m_pending; }
        u32 Stable(void) const { return m_stable; }   // frames on this page
        const std::string &Note(void) const { return m_note; }
        // Party page: the Switch / Cancel popup is open (its cursor).
        bool PopupOpen(void) const { return m_popup; }
        u8 PopupCursor(void) const { return m_popupCursor; }
        // Party page: chosen by the game (a Pokémon fainted): no way back.
        bool Forced(void) const { return m_forced; }
        // Bag page: the native list cursor, its first shown row, and the
        // pocket being switched to (-1 none).
        u8 BagCursor(void) const { return m_bagCursor; }
        u8 BagTop(void) const { return m_bagTop; }
        s32 WantPocket(void) const { return m_wantPocket; }
        u8 PickCursor(void) const { return m_pickCursor; }
        // A goal is being carried out (what for the footer, "" if none).
        bool Busy(void) const { return m_goal != Goal::None; }
        const std::string &BusyText(void) const { return m_busyText; }

        // Whether the page should be on screen now.
        bool Visible(void) const;
        // A game screen in battle not reached from the Bag or Pokémon list
        // (a question the menu doesn't know): shown after PromptShowFrames
        // so intro objects don't flash it.
        bool Prompt(void) const;
        static const u32 PromptShowFrames = 90;
        // Where Draw draws for the current page (empty when hidden).
        UI::Rect Area(void) const;
        // Corner radius of Area() (the layer copies a rounded rect).
        s32 Radius(void) const;
        // Draws the current page inside Area(), in screen coordinates.
        void Draw(UI::Canvas &canvas, const BattleSnapshot &snap) const;
        // Changes whenever what Draw would show changes.
        std::string DrawKey(const BattleSnapshot &snap) const;

        // "battle-ui: page moves, cursor 1 of 4, ..." for the log.
        std::string Status(const BattleSnapshot &snap) const;

        // Bag rows shown at once.
        static const u8 BagRows = 5;

    private:
        enum class Goal : u8
        {
            None,
            Switch,     // the Pokémon list: tap the slot, then Switch
            PartyBack,  // leave the Pokémon list (back button)
            Tab,        // the Bag: show pocket m_goalArg
            Use,        // the Bag: open item m_goalArg (m_goalItem) and tap Use
            Pick,       // "Which Pokémon...?": slot m_goalArg (a tap off the cursor only moves it)
            BagList,    // back to the Bag's list (from an item page or the pick)
            BagBack,    // leave the Bag (from an item page or the pick: back to the list first)
            Answer,     // the question: entry m_goalArg
            Target,     // the target screen: button m_goalArg
            TargetBack, // the target screen: back to the move list
        };

        void Enter(BattlePage page, const BattleSnapshot &snap);
        void StartGoal(Goal goal, int arg, const std::string &busy, u16 item = 0);
        void EndGoal(const std::string &note);
        void UpdateParty(const BattleSnapshot &snap, u32 pressed);
        void UpdateBag(const BattleSnapshot &snap, u32 pressed);
        void UpdateQuestion(const BattleSnapshot &snap, u32 pressed);
        void UpdateTarget(const BattleSnapshot &snap, u32 pressed);
        // The next step of the goal, or done / failed (m_goal None).
        void Drive(const BattleSnapshot &snap, BattleMenuOutput &out);
        // Sends one step; the same button more than `limit` times in a row
        // ends the goal (the game isn't answering).
        void Tap(BattleMenuOutput &out, const char *button, u32 limit = 3);
        void PressB(BattleMenuOutput &out);
        u32 GameSignature(const BattleSnapshot &snap) const;

        BattleLayout m_layout = BattleLayout::Classic;
        BattlePage m_page = BattlePage::Hidden;
        GameMenu m_menu = GameMenu::None;
        bool m_afterCatch = false;
        bool m_fromGame = false;    // this page came from the game's Bag / Pokémon list
        u8 m_cursor = 0;
        u32 m_stable = 0;           // frames the game's menu has been up
        bool m_spread = false;
        bool m_description = false; // L toggles; reset on leaving moves
        bool m_pending = false;     // a choice was sent, the game hasn't moved yet
        u32 m_pendingFrames = 0;
        const char *m_queued = nullptr; // chosen before the menu settled
        std::string m_note;         // one-line message (e.g. no PP left)

        // The game's command menu has been up in this battle: the intro's
        // objects come before it, the battle's questions after.
        bool m_seenCommand = false;
        // Party page.
        bool m_sentPokemon = false; // the Pokémon command was sent (not a faint)
        bool m_forced = false;
        bool m_popup = false;
        u8 m_popupCursor = 0;
        // Bag page.
        u8 m_bagCursor = 0, m_bagTop = 0;
        s32 m_wantPocket = -1;
        u8 m_seenPocket = 0xFF;
        bool m_seenPick = false;
        u8 m_pickCursor = 0;

        // Step driver.
        Goal m_goal = Goal::None;
        int m_goalArg = 0;
        u16 m_goalItem = 0;
        u32 m_goalFrames = 0;
        u32 m_goalSteps = 0;
        u32 m_tries = 0;            // the same step sent this many times in a row
        const char *m_lastTap = nullptr;
        bool m_stepSent = false;    // waiting for the action layer to finish our step
        bool m_used = false;        // Use / Switch: the final tap was sent
        u32 m_gameSig = 0;          // the game as last seen, and for how long
        u32 m_gameStable = 0;
        u32 m_gapFrames = 0;
        std::string m_busyText;
    };

    // Description body, inside the existing moves window in every layout.
    UI::Rect BattleDescriptionArea(BattleLayout layout);

    // Gen 7 type order: 0 Normal ... 17 Fairy. "?" if unknown.
    const char *TypeName(u8 type);
    // Selected-move details (UTF-8 em dash for absent power / always-hit accuracy).
    std::string MoveDetailsText(const MoveRow &row);
    const char *BattleCategoryName(u8 category);
    // A flat colour per type (the move-learning screen's chips).
    UI::Color TypeColor(u8 type);
    // "Super effective", "Not very effective", "No effect"; "" for plain
    // effective or unknown.
    const char *EffectLabel(u8 effect);
    // The battle Bag's tabs, 0 = leftmost: Rotom Powers, HP/PP Restore,
    // Status Restore, Poké Balls, Battle Items.
    const char *BattlePocketName(u8 pocket);
    static const u8 BattlePocketCount = 5;
    static const u8 BattlePocketBalls = 3;
    // LayoutButtons name of a battle Bag tab.
    const char *BattlePocketButton(u8 pocket);
    // True for Poké Ball items (the battle Bag's Poké Balls tab).
    bool IsBallItem(u16 item);
    // Commands as the menu labels them.
    const char *CommandLabel(BattleCommand command);

    // The move rows from the battler's record (move IDs and PP, 4 slots) and
    // the game's move list (type and effectiveness, listCount entries in the
    // same order; pass listCount 0 when the list isn't up). The rows stop at
    // the first empty slot (move 0). Names are left empty. Returns the count.
    u8 BuildMoveRows(MoveRow rows[4], const u16 moves[4], const u8 pp[4], const u8 maxPp[4], const u8 type[4],
                     const u8 effect[4], u8 listCount);

    // cursor moved by delta in a list of count entries, wrapping.
    u8 StepCursor(u8 cursor, u8 count, s32 delta);
}
}
