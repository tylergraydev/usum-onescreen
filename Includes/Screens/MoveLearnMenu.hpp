#pragma once

// Native move learning (W6-1): when a Pokémon levels up and wants a fifth
// move, the game asks on the bottom screen whether to forget an old move and
// then lists the four moves and the new one (the WazaOshie screen). This is
// the top-screen version: the four moves and the new one with type, PP,
// power and category; pick the move to forget (or don't learn), confirm, and
// the choice is carried out on the game's own screens through the action
// layer (MoveLearnAction). Pure, with no CTRPF dependency, so tests/ can
// build it on the PC; Screens/MoveLearn.cpp feeds it the game
// (docs/MOVE-LEARN.md).

#include <string>

#include "ActionQueue.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
namespace Screens
{
    // What the game's bottom screen shows of the move learning.
    enum class LearnStage : u8
    {
        None,       // nothing of it
        Ask,        // "Forget an old move" / "Keep old moves!"
        List,       // WazaOshie: the four moves and the new one
        Confirm,    // WazaOshie: one move's page, "Forget this move" / "Give up on learning the move"
    };

    // One move as the screen shows it; type and category 0xFF if unknown.
    struct LearnMove
    {
        u16 move = 0;
        std::string name;
        u8 type = 0xFF;
        u8 category = 0xFF;     // 0 status, 1 physical, 2 special
        u8 power = 0;           // 0: none ("—")
        u8 accuracy = 0;        // 101: never misses
        u8 pp = 0, maxPp = 0;
    };

    struct LearnSnapshot
    {
        LearnStage stage = LearnStage::None;
        std::string monName;
        u8 moveCount = 0;
        LearnMove moves[4];
        LearnMove newMove;
        bool actionRunning = false; // an action is driving the bottom screen
        std::string panelKey = "L+R";
    };

    enum class LearnPage : u8
    {
        Hidden,     // not asked, or not settled yet
        Choose,     // the four moves and the new one (row 4 = don't learn)
        Confirm,    // "Forget X and learn Y?" / "Don't learn Y?"
        Working,    // the action runs
        Done,       // the game is printing the result
    };

    struct LearnOutput
    {
        const ActionDef *action = nullptr;  // start now
        bool capture = false;               // keep LearnKeys from the game this frame
    };

    // HID key bits (same values as CTRPF's Key::).
    namespace LearnKeys
    {
        const u32 A = 1u << 0, B = 1u << 1, DUp = 1u << 6, DDown = 1u << 7;
        const u32 CPadUp = 1u << 30, CPadDown = 1u << 31;
        const u32 Up = DUp | CPadUp, Down = DDown | CPadDown;
    }
    const u32 LearnCaptureKeys = LearnKeys::A | LearnKeys::B | LearnKeys::DUp | LearnKeys::DDown;

    // Row the "don't learn" choice has (after the four moves).
    const u8 LearnSkipRow = 4;

    // The action that carries out a choice from Ask or List: row 0-3 forgets
    // that move and learns the new one, LearnSkipRow gives up on it. Both go
    // through the game's WazaOshie screen: tap the row, then the first
    // button of its page ("Forget this move" / "Give up on learning the
    // move"). nullptr for another row.
    const ActionDef *MoveLearnAction(u8 row);

    class MoveLearnMenu
    {
    public:
        // Frames the game's question or list must have been up before the
        // screen shows (it slides in), and how long the result stays.
        static const u32 ShowFrames = 30;
        static const u32 DoneFrames = 240;
        // The action's own timeouts are shorter; this only frees the screen
        // if it never hears back.
        static const u32 WorkingFrames = 900;

        // `pressed`: keys newly pressed this frame (real input, before capture).
        LearnOutput Update(const LearnSnapshot &snap, u32 pressed);
        // The action from Update ended.
        void ActionDone(bool ok);

        LearnPage Page(void) const { return m_page; }
        u8 Cursor(void) const { return m_cursor; }
        bool Visible(void) const { return m_page != LearnPage::Hidden; }
        const std::string &Note(void) const { return m_note; }
        // What was chosen ("forgot Splash, learned Baby-Doll Eyes"), for the
        // Done page and the log.
        const std::string &Result(void) const { return m_result; }

        UI::Rect Area(void) const;
        void Draw(UI::Canvas &canvas, const LearnSnapshot &snap) const;
        std::string DrawKey(const LearnSnapshot &snap) const;
        // "move-learn: page choose, cursor 1 (Splash)" for the log.
        std::string Status(const LearnSnapshot &snap) const;

    private:
        void Go(LearnPage page);

        LearnPage m_page = LearnPage::Hidden;
        u8 m_cursor = 0;
        u32 m_stable = 0;       // frames the game's question / list has been up
        u32 m_frames = 0;       // frames on this page
        std::string m_note;
        std::string m_result;
        std::string m_choice;   // set when the action starts, becomes m_result
        bool m_answered = false;    // the game stays on Ask/List for a moment after the action
    };

    // "Forget Splash and learn Baby-Doll Eyes?" / "Don't learn Baby-Doll Eyes?"
    std::string LearnQuestion(const LearnSnapshot &snap, u8 row);
    // "40" or "-" (no power), "100" or "-" (never misses); the small font
    // has only ASCII.
    std::string LearnPowerText(u8 power);
    std::string LearnAccuracyText(u8 accuracy);
}
}
