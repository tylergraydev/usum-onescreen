#pragma once

// Native Options (scene Config, X menu page 2 → Options). The game's screen
// is a list of five settings; each row shows its value between ◀ ▶ arrows,
// and an arrow is only drawn where the value can still move (no wrap). So
// the bottom screen alone says which row the game's cursor is on and every
// value: the native screen samples it, mirrors it, and passes the player's
// D-pad to the game as presses (rows wrap, values don't). A presses Confirm
// (keeps the settings, back to the X menu). B is left to the game: with
// nothing changed it goes back; with changes the game asks "Would you like
// to make these changes?" (Yes / No; No and B leave without them), which
// the native screen mirrors too. See docs/NATIVE-SCREENS.md.

#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    const int OptionRows = 5;

    struct OptionRow
    {
        const char *label;
        const char *values[3];
        int count;          // number of values
        u32 shift, mask;    // where Savedata::ConfigSave's word keeps it
    };

    // The game's rows, top to bottom, values left to right.
    const OptionRow &OptionRowAt(int row);

    // Values from the save's ConfigSave word (docs/OFFSETS.md); a field out
    // of range gives -1.
    void OptionsFromSaveWord(u32 word, int values[OptionRows]);
    std::string DescribeOptions(const int values[OptionRows]);

    // A row's value from which arrows the game draws; -1 if no value has
    // that pair (e.g. neither arrow).
    int OptionValueFromArrows(int row, bool left, bool right);

    // What the bottom screen shows: the game's cursor row and each value,
    // or the "make these changes?" question and its cursor.
    struct OptionsShown
    {
        bool valid = false;     // the list (one cursor row, every row decoded) or the question
        bool dialog = false;    // the question is up (values: the list's, from before it)
        int choice = -1;        // dialog: 0 Yes, 1 No
        int row = -1;
        int values[OptionRows] = { -1, -1, -1, -1, -1 };
    };

    // Sample points (8 per row: cursor ×4, left arrow ×2, right arrow ×2;
    // then the question: its box ×2, the Yes / No pills, the cursor ×5 at each).
    const std::vector<SamplePoint> &OptionsSamplePoints(void);
    OptionsShown DecodeOptions(const Sample *samples);

    // Top-screen area: above the game's own description box (on the
    // top screen, from y 193), which stays visible and explains the selected row.
    static constexpr UI::Rect OptionsArea = { 12, 6, 376, 182 };

    class OptionsScreen : public NativeScreen
    {
    public:
        const char *Name(void) const override { return "options"; }
        const char *Scene(void) const override { return "Config"; }
        UI::Rect Area(void) const override { return OptionsArea; }
        u32 CaptureKeys(void) const override;
        const std::vector<SamplePoint> &Samples(void) const override { return OptionsSamplePoints(); }

        // The save's settings word when the scene opened (what the game's
        // list starts from); logged and compared with the first reading.
        void SetSaveWord(u32 word, bool valid) { m_saveWord = word; m_saveValid = valid; }

        void Begin(std::vector<std::string> &log) override;
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready(void) const override { return m_shown.valid; }
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override;

        u32 DrawKey(void) const override;
        void Draw(UI::Canvas &canvas) const override;
        std::string Status(void) const override;

        const OptionsShown &Shown(void) const { return m_shown; }
        bool Confirming(void) const { return m_confirming; }

        // Frames the samples must agree before they count (the arrow and
        // the cursor bob, and a press redraws the row).
        static const u32 StableSamples = 3;

    private:
        OptionsShown m_shown;           // last stable reading
        OptionsShown m_candidate;
        u32 m_candidateCount = 0;
        u32 m_saveWord = 0;
        bool m_saveValid = false;
        bool m_confirming = false;
        bool m_confirmEnded = false;    // the confirm action ended: a valid reading now means it didn't take
        bool m_loggedSave = false;
    };
}
