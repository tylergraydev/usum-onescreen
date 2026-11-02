#include "Screens/Options.hpp"

#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    // Rows and values as the game shows them (USA v1.2, English), and the
    // ConfigSave bits each one is kept in (docs/OFFSETS.md: checked by
    // changing all five and confirming: 0x0002A026 -> 0x00020028).
    static const OptionRow s_rows[OptionRows] =
    {
        { "Text Speed",     { "Slow", "Normal", "Fast" }, 3, 0, 3 },
        { "Battle Effects", { "On", "Off", nullptr },     2, 2, 1 },
        { "Battle Style",   { "Switch", "Set", nullptr }, 2, 3, 1 },
        { "Button Mode",    { "Default", "L=A", nullptr }, 2, 13, 3 },
        { "Party/Box",      { "Manual", "Automatic", nullptr }, 2, 15, 1 },
    };

    const OptionRow &OptionRowAt(int row)
    {
        return s_rows[row < 0 || row >= OptionRows ? 0 : row];
    }

    void OptionsFromSaveWord(u32 word, int values[OptionRows])
    {
        for (int i = 0; i < OptionRows; i++)
        {
            const int v = (int)((word >> s_rows[i].shift) & s_rows[i].mask);
            values[i] = v < s_rows[i].count ? v : -1;
        }
    }

    std::string DescribeOptions(const int values[OptionRows])
    {
        std::string out;
        for (int i = 0; i < OptionRows; i++)
        {
            if (i > 0)
                out += ", ";
            out += s_rows[i].label;
            out += " ";
            out += values[i] >= 0 && values[i] < s_rows[i].count ? s_rows[i].values[values[i]] : "?";
        }
        return out;
    }

    int OptionValueFromArrows(int row, bool left, bool right)
    {
        const int count = OptionRowAt(row).count;
        if (left && right)
            return count == 3 ? 1 : -1;
        if (right)
            return 0;
        if (left)
            return count - 1;
        return -1;
    }

    // Row centres on the bottom screen: y 19 + 32 * row. The red cursor
    // arrow sits at x 2-9 (it bobs), the arrows at x 166-176 and 300-312
    // (docs/NATIVE-SCREENS.md has the colours).
    static const s16 RowY0 = 19, RowPitch = 32;
    static const s16 CursorX[4] = { 3, 5, 7, 9 };
    static const s16 LeftX[2] = { 170, 172 };
    static const s16 RightX[2] = { 306, 308 };
    static const int PerRow = 8;
    // The question: a dark box at the top (37,30,23 at y 20), the Yes and No
    // pills (light at x 250, y 107 / 135) and the red cursor left of the
    // chosen one (x 206-213, y 108 / 136).
    static const SamplePoint DialogBox[2] = { { 40, 20 }, { 280, 20 } };
    static const SamplePoint DialogPill[2] = { { 250, 107 }, { 250, 135 } };
    static const s16 DialogCursorY[2] = { 108, 136 };
    static const s16 DialogCursorX[5] = { 205, 207, 209, 211, 213 };

    const std::vector<SamplePoint> &OptionsSamplePoints(void)
    {
        static std::vector<SamplePoint> points;
        if (points.empty())
            for (int row = 0; row < OptionRows; row++)
            {
                const s16 y = RowY0 + RowPitch * row;
                for (s16 x : CursorX)
                    points.push_back(SamplePoint{ x, y });
                for (s16 x : LeftX)
                    points.push_back(SamplePoint{ x, y });
                for (s16 x : RightX)
                    points.push_back(SamplePoint{ x, y });
            }
        if (points.size() == OptionRows * PerRow)
        {
            points.push_back(DialogBox[0]);
            points.push_back(DialogBox[1]);
            points.push_back(DialogPill[0]);
            points.push_back(DialogPill[1]);
            for (s16 y : DialogCursorY)
                for (s16 x : DialogCursorX)
                    points.push_back(SamplePoint{ x, y });
        }
        return points;
    }

    // The value strip's light cyan: (205,253,255), (191,249,255) on the
    // last row. Arrows are grey (118,118,118) or, on the cursor's row, blue.
    static bool IsStrip(const Sample &s)
    {
        return s.r > 170 && s.g > 235 && s.b > 235;
    }

    static bool IsPill(const Sample &s)
    {
        return s.r > 180 && s.g > 220 && s.b > 220;
    }

    OptionsShown DecodeOptions(const Sample *samples)
    {
        OptionsShown shown;
        const Sample *d = samples + OptionRows * PerRow;
        if (IsDark(d[0]) && IsDark(d[1]) && IsPill(d[2]) && IsPill(d[3]))
        {
            int found = 0;
            for (int c = 0; c < 2; c++)
                for (int i = 0; i < 5; i++)
                    if (IsRed(d[4 + c * 5 + i]))
                    {
                        shown.choice = c;
                        found |= 1 << c;
                        break;
                    }
            shown.dialog = true;
            shown.valid = found == 1 || found == 2;
            if (!shown.valid)
                shown.choice = -1;
            return shown;
        }
        int cursorRows = 0;
        bool all = true;
        for (int row = 0; row < OptionRows; row++)
        {
            const Sample *s = samples + row * PerRow;
            if (IsRed(s[0]) || IsRed(s[1]) || IsRed(s[2]) || IsRed(s[3]))
            {
                shown.row = row;
                cursorRows++;
            }
            const bool left = !IsStrip(s[4]) && !IsStrip(s[5]);
            const bool right = !IsStrip(s[6]) && !IsStrip(s[7]);
            shown.values[row] = OptionValueFromArrows(row, left, right);
            all = all && shown.values[row] >= 0;
        }
        shown.valid = all && cursorRows == 1;
        if (!shown.valid)
            shown.row = -1;
        return shown;
    }

    static bool SameShown(const OptionsShown &a, const OptionsShown &b)
    {
        if (a.valid != b.valid || a.row != b.row || a.dialog != b.dialog || a.choice != b.choice)
            return false;
        for (int i = 0; i < OptionRows; i++)
            if (a.values[i] != b.values[i])
                return false;
        return true;
    }

    // --- The screen ---

    u32 OptionsScreen::CaptureKeys(void) const
    {
        // L and R too: with Button Mode L=A the game reads L as A.
        return ScreenKey::A | ScreenKey::DPad | ScreenKey::CPad | ScreenKey::L | ScreenKey::R | ScreenKey::Start;
    }

    void OptionsScreen::Begin(std::vector<std::string> &log)
    {
        m_shown = OptionsShown();
        m_candidate = OptionsShown();
        m_candidateCount = 0;
        m_confirming = m_confirmEnded = false;
        m_loggedSave = false;
        if (m_saveValid)
        {
            int values[OptionRows];
            OptionsFromSaveWord(m_saveWord, values);
            char word[16];
            std::snprintf(word, sizeof(word), "%08X", (unsigned)m_saveWord);
            log.push_back(std::string("options: save word ") + word + ": " + DescribeOptions(values));
        }
        else
            log.push_back("options: save word not readable");
    }

    void OptionsScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        const OptionsShown now = DecodeOptions(samples);
        if (SameShown(now, m_candidate))
            m_candidateCount++;
        else
        {
            m_candidate = now;
            m_candidateCount = 1;
        }
        if (m_confirming && m_confirmEnded && now.valid && m_candidateCount >= StableSamples)
        {
            // Still on the list: the A didn't take. Let the player try again.
            m_confirming = m_confirmEnded = false;
            log.push_back("options: confirm didn't leave the screen");
        }
        // Only a valid reading replaces the last one: while the game slides
        // its screen out (Confirm, back) the samples stop decoding.
        if (m_candidateCount < StableSamples || !now.valid || SameShown(now, m_shown))
            return;

        const bool first = !m_shown.valid;
        if (now.dialog)
        {
            // The question covers part of the list: keep the values and
            // row from before it.
            if (first)
                return;
            const bool wasDialog = m_shown.dialog;
            OptionsShown kept = m_shown;
            kept.dialog = true;
            kept.choice = now.choice;
            m_shown = kept;
            log.push_back(std::string(wasDialog ? "options: question cursor " : "options: question (make these changes?), cursor ")
                          + (now.choice == 0 ? "Yes" : "No"));
            return;
        }
        const int oldRow = m_shown.row;
        int oldValues[OptionRows];
        for (int i = 0; i < OptionRows; i++)
            oldValues[i] = m_shown.values[i];
        if (m_shown.dialog)
            log.push_back("options: question gone, back on the list");
        m_shown = now;

        if (first)
        {
            log.push_back(std::string("options: shown, game row ") + std::to_string(now.row) + " ("
                          + s_rows[now.row].label + "), " + DescribeOptions(now.values));
            if (m_saveValid && !m_loggedSave)
            {
                int saved[OptionRows];
                OptionsFromSaveWord(m_saveWord, saved);
                bool same = true;
                for (int i = 0; i < OptionRows; i++)
                    same = same && saved[i] == now.values[i];
                log.push_back(same ? "options: screen matches the save word"
                                   : "options: screen differs from the save word: " + DescribeOptions(saved));
                m_loggedSave = true;
            }
            return;
        }
        if (now.row != oldRow)
            log.push_back(std::string("options: row ") + s_rows[now.row].label);
        for (int i = 0; i < OptionRows; i++)
            if (now.values[i] != oldValues[i])
                log.push_back(std::string("options: ") + s_rows[i].label + " " + s_rows[i].values[now.values[i]]);
    }

    static const ActionDef &ConfirmAction(void)
    {
        // A on the game's list is Confirm: it keeps the settings and goes
        // back to the X menu (page 2) in the field.
        static const ActionDef def = { "options-confirm", "Config: A (Confirm), wait for the field", InScene("Config"),
                                       { StepPress(ScreenKey::A), StepWaitFor(InScene(SceneField), 150) } };
        return def;
    }

    static const ActionDef &AnswerAction(void)
    {
        // Yes keeps the changes, No drops them; both go back to the X menu.
        static const ActionDef def = { "options-answer", "Config: A (answer the question), wait for the field",
                                       InScene("Config"),
                                       { StepPress(ScreenKey::A), StepWaitFor(InScene(SceneField), 150) } };
        return def;
    }

    const ActionDef *OptionsScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (!m_shown.valid || m_confirming)
            return nullptr;
        if (m_shown.dialog)
        {
            if (key == ScreenKey::Up || key == ScreenKey::Down)
                return &ScreenPressAction(key == ScreenKey::Up ? "options-up" : "options-down", Scene(), key, 4);
            if (key != ScreenKey::A)
                return nullptr;
            m_confirming = true;
            log.push_back(std::string("options: answer ") + (m_shown.choice == 0 ? "Yes: " : "No, keeping the saved settings; screen had: ")
                          + DescribeOptions(m_shown.values));
            return &AnswerAction();
        }
        const int row = m_shown.row;
        const int value = m_shown.values[row];
        const OptionRow &r = s_rows[row];
        if (key == ScreenKey::Up || key == ScreenKey::Down)
            return &ScreenPressAction(key == ScreenKey::Up ? "options-up" : "options-down", Scene(), key, 4);
        if (key == ScreenKey::Left || key == ScreenKey::Right)
        {
            // The game has no wrap; at an end nothing is pressed.
            const int next = value + (key == ScreenKey::Left ? -1 : 1);
            if (next < 0 || next >= r.count)
                return nullptr;
            return &ScreenPressAction(key == ScreenKey::Left ? "options-left" : "options-right", Scene(), key, 4);
        }
        if (key == ScreenKey::A)
        {
            m_confirming = true;
            log.push_back("options: confirm: " + DescribeOptions(m_shown.values));
            return &ConfirmAction();
        }
        return nullptr;
    }

    void OptionsScreen::ActionDone(std::vector<std::string> &log)
    {
        (void)log;
        // The confirm action ends early only when the field shows (then the
        // screen goes away); if it timed out, the next reading says so.
        if (m_confirming)
        {
            m_confirmEnded = true;
            m_candidateCount = 0;
        }
    }

    u32 OptionsScreen::DrawKey(void) const
    {
        u32 key = (u32)(m_shown.row + 1) | (m_confirming ? 0x80u : 0u) | (m_shown.valid ? 0x40u : 0u)
                  | (m_shown.dialog ? 0x100u : 0u) | (m_shown.choice == 1 ? 0x200u : 0u);
        for (int i = 0; i < OptionRows; i++)
            key = key * 5 + (u32)(m_shown.values[i] + 1);
        return key;
    }

    void OptionsScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace ScreenStyle;
        UI::Rect body = DrawScreenFrame(canvas, OptionsArea, "Options");
        const char *hints = m_confirming ? "..." : m_shown.dialog ? "A Choose    B Leave without changes" : "A Confirm    B Back";
        body = DrawScreenFooter(canvas, body, hints);

        const s32 rowH = 20, gap = 3;
        const s32 labelW = 150;
        UI::TextStyle label, value;
        label.color = TextMain;
        value.color = TextMain;
        value.align = UI::Align::Center;
        for (int row = 0; row < OptionRows; row++)
        {
            const UI::Rect r = { body.x, body.y + row * (rowH + gap), body.w, rowH };
            const bool selected = row == m_shown.row && !m_confirming && !m_shown.dialog;
            canvas.FillRoundRect(r, 8, RowBg);
            if (selected)
            {
                canvas.StrokeRoundRect(UI::Inset(r, -1), 9, Selected, 2);
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 3, r.y + 4, 12, 12 }, Selected);
            }
            UI::DrawText(canvas, s_rows[row].label, UI::Rect{ r.x + 18, r.y, labelW - 18, r.h }, label);

            const int v = m_shown.values[row];
            const UI::Rect valueBox = { r.x + labelW, r.y, r.w - labelW - 6, r.h };
            UI::DrawText(canvas, v >= 0 ? s_rows[row].values[v] : "?", UI::Inset(valueBox, 18, 0, 18, 0), value);
            const UI::Color arrow = selected ? Selected : TextDim;
            if (v > 0)
                UI::DrawIcon(canvas, UI::Icon::ArrowLeft, UI::Rect{ valueBox.x + 2, r.y + 4, 12, 12 }, arrow);
            if (v >= 0 && v < s_rows[row].count - 1)
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ valueBox.Right() - 14, r.y + 4, 12, 12 }, arrow);
        }

        if (m_shown.dialog)
        {
            // The game's question, over the list.
            const UI::Rect box = { body.x + 40, body.y + 10, body.w - 80, 100 };
            canvas.FillRoundRect(box, 10, PanelBg);
            canvas.StrokeRoundRect(box, 10, Border, 2);
            UI::TextStyle q;
            q.color = TextMain;
            q.align = UI::Align::Center;
            UI::DrawText(canvas, "Make these changes?", UI::Rect{ box.x, box.y + 6, box.w, 22 }, q);
            const char *answers[2] = { "Yes", "No (leave them)" };
            for (int c = 0; c < 2; c++)
            {
                const UI::Rect pill = { box.x + 40, box.y + 34 + c * 30, box.w - 80, 24 };
                canvas.FillRoundRect(pill, 10, RowBg);
                if (c == m_shown.choice && !m_confirming)
                {
                    canvas.StrokeRoundRect(UI::Inset(pill, -1), 11, Selected, 2);
                    UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ pill.x + 4, pill.y + 6, 12, 12 }, Selected);
                }
                UI::DrawText(canvas, answers[c], pill, q);
            }
        }
    }

    std::string OptionsScreen::Status(void) const
    {
        if (!m_shown.valid)
            return "options: not read yet";
        if (m_shown.dialog)
            return std::string("options: question, cursor ") + (m_shown.choice == 0 ? "Yes" : "No") + ", list had "
                   + DescribeOptions(m_shown.values) + (m_confirming ? ", answering" : "");
        return std::string("options: row ") + s_rows[m_shown.row].label + ", " + DescribeOptions(m_shown.values)
               + (m_confirming ? ", confirming" : "");
    }
}
