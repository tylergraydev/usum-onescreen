#pragma once

// Native battle menu (P2-1): the top-screen version of the game's battle
// command menu and move list. This header is the pure part, with no CTRPF
// dependency, so tests/ can build it on the PC: menu state, input, the move
// rows and drawing. Screens/BattleScreen.cpp feeds it the battle state (RE1)
// and names (RE2) once per frame and starts the actions it asks for.
//
// The menu follows the game: it shows the command bar while the game's
// command menu is up on the hidden bottom screen and the move list while the
// game's move list is up, and carries a choice out by tapping the game's own
// button (ActionQueue.hpp, battle-fight ... battle-moves-back). While the
// game shows its own Bag or Pokémon screen (P2-3/P2-4 will replace them) the
// player's buttons go to the game and a small helper says what is
// highlighted there.

#include <string>

#include "UI/Raster.hpp"

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
    enum class GameMenu : u8 { None, Command, Moves, Pokemon, Bag, Other };

    // One move as the menu shows it. type and effect come from the game's
    // move list (0xFF if unknown); effect: 0 no effect, 1 effective,
    // 2 super effective, 3 not very effective (docs/RE1-battle.md).
    struct MoveRow
    {
        u16 move = 0;
        std::string name;
        u8 type = 0xFF;
        u8 effect = 0xFF;
        u8 pp = 0, maxPp = 0;
    };

    // The battle as the menu needs it, once per frame.
    struct BattleSnapshot
    {
        bool inBattle = false;
        GameMenu menu = GameMenu::None;
        u8 gameCursor = 0;          // the game's highlighted entry
        u8 listCount = 0;           // entries in the game's list
        std::string activeName;     // the player's battler
        MoveRow moves[4];
        u8 moveCount = 0;
        // Bag (game screen): tab, item page, the highlighted item's name if
        // known (empty if not) and count.
        u8 bagPocket = 0;
        bool bagItemPage = false;
        std::string bagItem;
        u16 bagItemCount = 0;
        // Pokémon list (game screen): the highlighted battler.
        std::string partyName;
        u8 partyLevel = 0;
        u16 partyHp = 0, partyMaxHp = 0;
        bool actionRunning = false; // an action is driving the bottom screen
    };

    enum class BattlePage : u8
    {
        Hidden,     // nothing to choose (animations, messages) or not in battle
        Command,    // Fight / Bag / Pokémon / Run
        Moves,      // the move list
        Game,       // the game's own Bag / Pokémon / other screen: helper only
    };

    // Command bar entries, in the order the menu lists them.
    enum class BattleCommand : u8 { Fight, Bag, Pokemon, Run, Count };

    struct BattleMenuOutput
    {
        const char *action = nullptr;   // action to start now (ActionQueue name)
        bool capture = false;           // keep MenuKeys from the game this frame
    };

    // HID key bits (same values as CTRPF's Key::).
    namespace BattleKeys
    {
        const u32 A = 1u << 0, B = 1u << 1, DRight = 1u << 4, DLeft = 1u << 5, DUp = 1u << 6, DDown = 1u << 7;
        const u32 CPadRight = 1u << 28, CPadLeft = 1u << 29, CPadUp = 1u << 30, CPadDown = 1u << 31;
        const u32 Up = DUp | CPadUp, Down = DDown | CPadDown;
    }

    // Keys the menu keeps from the game while it is up (the circle pad too).
    // Others (Y: the game's quick Poké Ball, L: move info, touch) pass.
    const u32 BattleMenuKeys = BattleKeys::A | BattleKeys::B | BattleKeys::DRight | BattleKeys::DLeft
                               | BattleKeys::DUp | BattleKeys::DDown;

    class BattleMenu
    {
    public:
        // Frames the game's menu must have been up before a choice is sent
        // (a tap as the command menu slid in only moved its cursor), and how
        // long a choice may stay unanswered before the menu takes input again.
        static const u32 CommandSettleFrames = 30;
        static const u32 MovesSettleFrames = 10;
        static const u32 PendingFrames = 400;
        // After a move or an item the game's command menu shows for a few
        // frames before the turn starts; the command bar waits this long
        // before it shows.
        static const u32 ShowFrames = 30;

        // `pressed`: keys newly pressed this frame (real input, before capture).
        BattleMenuOutput Update(const BattleSnapshot &snap, u32 pressed);

        BattlePage Page(void) const { return m_page; }
        u8 Cursor(void) const { return m_cursor; }
        bool Pending(void) const { return m_pending; }
        u32 Stable(void) const { return m_stable; }   // frames on this page
        const std::string &Note(void) const { return m_note; }

        // Whether the page should be on screen now.
        bool Visible(void) const;
        // Where Draw draws for the current page (empty when hidden).
        UI::Rect Area(void) const;
        // Draws the current page inside Area(), in screen coordinates.
        void Draw(UI::Canvas &canvas, const BattleSnapshot &snap) const;
        // Changes whenever what Draw would show changes.
        std::string DrawKey(const BattleSnapshot &snap) const;

        // "battle-ui: page moves, cursor 1 of 4, ..." for the log.
        std::string Status(const BattleSnapshot &snap) const;

    private:
        void Enter(BattlePage page, const BattleSnapshot &snap);

        BattlePage m_page = BattlePage::Hidden;
        GameMenu m_menu = GameMenu::None;
        u8 m_cursor = 0;
        u32 m_stable = 0;           // frames the game's menu has been up
        bool m_pending = false;     // a choice was sent, the game hasn't moved yet
        u32 m_pendingFrames = 0;
        const char *m_queued = nullptr; // chosen before the menu settled
        std::string m_note;         // one-line message (e.g. no PP left)
    };

    // Gen 7 type order: 0 Normal ... 17 Fairy. "?" if unknown.
    const char *TypeName(u8 type);
    UI::Color TypeColor(u8 type);
    // "Super effective", "Not very effective", "No effect"; "" for plain
    // effective or unknown.
    const char *EffectLabel(u8 effect);
    // The battle Bag's tabs, 0 = leftmost: Rotom Powers, HP/PP Restore,
    // Status Restore, Poké Balls, Battle Items.
    const char *BattlePocketName(u8 pocket);
    static const u8 BattlePocketBalls = 3;
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

    // The battle Bag's Poké Balls tab lists the Items pocket's balls in the
    // pocket's order, leaving out slots with count 0 (seen 2026-11-02: Poké,
    // Great, Heal Ball; Premier Ball x0 not shown). Writes up to cap items
    // (and counts) and returns how many there are.
    u8 BallsInPocket(const u16 *items, const u16 *counts, u32 slots, u16 *outItems, u16 *outCounts, u8 cap);

    // cursor moved by delta in a list of count entries, wrapping.
    u8 StepCursor(u8 cursor, u8 count, s32 delta);
}
}
