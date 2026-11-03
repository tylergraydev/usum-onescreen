#include "Screens/MoveLearnMenu.hpp"

#include <cstdio>

#include "Screens/BattleMenu.hpp"
#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
namespace Screens
{
    using namespace UI;

    // In battle the game prints its question along the bottom (y > 192) and
    // draws the opponent's HP box at the top right (y < 34) and the player's
    // at the bottom left (x < 132), so the list stays in between.
    static constexpr Rect ChooseArea  = { 156, 36, 240, 156 };
    static constexpr Rect ConfirmArea = { 166, 96, 230, 80 };
    static constexpr Rect DoneArea    = { 166, 128, 230, 52 };
    static constexpr s32  Radius      = 10;

    static constexpr Color PanelBg   = Rgb(22, 30, 52);
    static constexpr Color Border    = Rgb(120, 150, 220);
    static constexpr Color Divider   = Rgb(120, 150, 220, 90);
    static constexpr Color Highlight = Rgb(70, 110, 200, 220);
    static constexpr Color TextMain  = Rgb(240, 244, 255);
    static constexpr Color TextDim   = Rgb(150, 165, 200);
    static constexpr Color NewMove   = Rgb(255, 236, 120);
    static constexpr Color Good      = Rgb(110, 220, 140);
    static constexpr Color Danger    = Rgb(235, 110, 110);

    static std::string Number(const char *fmt, unsigned a, unsigned b = 0)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    static std::string MoveLabel(const LearnMove &move)
    {
        return move.name.empty() ? Number("Move %u", move.move) : move.name;
    }

    std::string LearnPowerText(u8 power)
    {
        return power == 0 ? std::string("-") : Number("%u", power);
    }

    std::string LearnAccuracyText(u8 accuracy)
    {
        return accuracy == 0 || accuracy > 100 ? std::string("-") : Number("%u", accuracy);
    }

    std::string LearnQuestion(const LearnSnapshot &snap, u8 row)
    {
        if (row < snap.moveCount && row < 4)
            return "Forget " + MoveLabel(snap.moves[row]) + " and learn " + MoveLabel(snap.newMove) + "?";
        return "Don't learn " + MoveLabel(snap.newMove) + "?";
    }

    // --- Actions ---

    static ActionDef MakeAction(u8 row)
    {
        static const char *const names[] = { "learn-forget1", "learn-forget2", "learn-forget3", "learn-forget4", "learn-skip" };
        static const char *const rows[] = { "learn_moves.move1", "learn_moves.move2", "learn_moves.move3",
                                            "learn_moves.move4", "learn_moves.new" };
        static const char *const summaries[] = {
            "move learning: forget move 1", "move learning: forget move 2", "move learning: forget move 3",
            "move learning: forget move 4", "move learning: give up the new move",
        };
        const ActionCond battle = InScene(SceneBattle);
        const u32 all = WorldLearnAsk | WorldLearnList | WorldLearnConfirm;
        // The question and the list slide in; a tap as they arrive can be
        // lost, so each tap waits a little and is tried again once.
        return { names[row], summaries[row], battle, {
            StepTap("learn_ask.forget").SkipIf(battle.Without(WorldLearnAsk)),
            StepWaitFor(battle.With(WorldLearnList), 240).Retry(0, 1),
            StepWait(30),
            StepTap(rows[row]),
            StepWaitFor(battle.With(WorldLearnConfirm), 120).Retry(3, 1),
            StepWait(20),
            StepTap("learn_confirm.yes"),
            StepWaitFor(battle.Without(all), 240).Retry(6, 1),
        } };
    }

    const ActionDef *MoveLearnAction(u8 row)
    {
        static const ActionDef defs[] = { MakeAction(0), MakeAction(1), MakeAction(2), MakeAction(3), MakeAction(4) };
        return row <= LearnSkipRow ? &defs[row] : nullptr;
    }

    // --- Menu ---

    void MoveLearnMenu::Go(LearnPage page)
    {
        m_page = page;
        m_frames = 0;
    }

    static u8 NextRow(u8 cursor, u8 moveCount, bool down)
    {
        // Rows 0 .. moveCount-1, then the "don't learn" row.
        u8 rows[5];
        u8 n = 0;
        for (u8 i = 0; i < moveCount && i < 4; i++)
            rows[n++] = i;
        rows[n++] = LearnSkipRow;
        u8 at = 0;
        for (u8 i = 0; i < n; i++)
            if (rows[i] == cursor)
                at = i;
        return rows[down ? (at + 1) % n : (at + n - 1) % n];
    }

    LearnOutput MoveLearnMenu::Update(const LearnSnapshot &snap, u32 pressed)
    {
        LearnOutput out;
        if (m_frames < 0xFFFF)
            m_frames++;
        const bool asked = snap.stage == LearnStage::Ask || snap.stage == LearnStage::List;
        if (snap.stage == LearnStage::None)
        {
            m_stable = 0;
            m_answered = false;
        }
        else if (m_stable < 0xFFFF)
            m_stable++;

        switch (m_page)
        {
        case LearnPage::Hidden:
            // Keys pressed for the messages before it don't reach the
            // question while it slides in (B there could mean "keep").
            out.capture = asked && !m_answered;
            if (asked && !m_answered && !snap.actionRunning && m_stable >= ShowFrames)
            {
                m_cursor = 0;
                m_note.clear();
                Go(LearnPage::Choose);
                out.capture = true;
            }
            break;

        case LearnPage::Choose:
        case LearnPage::Confirm:
            if (!asked)
            {
                // Answered some other way (the panel), or gone.
                Go(LearnPage::Hidden);
                break;
            }
            out.capture = true;
            if (snap.actionRunning)
                break;
            if (m_page == LearnPage::Choose)
            {
                if (pressed & LearnKeys::Up)
                    m_cursor = NextRow(m_cursor, snap.moveCount, false);
                else if (pressed & LearnKeys::Down)
                    m_cursor = NextRow(m_cursor, snap.moveCount, true);
                else if (pressed & LearnKeys::B)
                    m_cursor = LearnSkipRow;
                else if (pressed & LearnKeys::A)
                    Go(LearnPage::Confirm);
                if (pressed & (LearnKeys::Up | LearnKeys::Down | LearnKeys::A | LearnKeys::B))
                    m_note.clear();
            }
            else if (pressed & LearnKeys::B)
                Go(LearnPage::Choose);
            else if (pressed & LearnKeys::A)
            {
                out.action = MoveLearnAction(m_cursor);
                m_choice = m_cursor < snap.moveCount
                    ? snap.monName + " forgot " + MoveLabel(snap.moves[m_cursor]) + " and learned " + MoveLabel(snap.newMove)
                    : snap.monName + " did not learn " + MoveLabel(snap.newMove);
                Go(LearnPage::Working);
            }
            break;

        case LearnPage::Working:
            if (m_frames > WorkingFrames)
            {
                m_note = "No answer from the game, try again";
                Go(asked ? LearnPage::Choose : LearnPage::Hidden);
            }
            break;

        case LearnPage::Done:
            // Another Pokémon may ask next (Exp. Share); the question went
            // away in between, so m_answered is clear again.
            if (asked && !m_answered && !snap.actionRunning && m_stable >= ShowFrames)
            {
                m_cursor = 0;
                m_note.clear();
                Go(LearnPage::Choose);
            }
            else if (m_frames > DoneFrames)
                Go(LearnPage::Hidden);
            break;
        }
        return out;
    }

    void MoveLearnMenu::ActionDone(bool ok)
    {
        if (m_page != LearnPage::Working)
            return;
        if (ok)
        {
            m_result = m_choice;
            m_answered = true;
            Go(LearnPage::Done);
        }
        else
        {
            m_note = "The game didn't take it, try again";
            Go(LearnPage::Choose);
        }
    }

    Rect MoveLearnMenu::Area(void) const
    {
        switch (m_page)
        {
        case LearnPage::Choose:  return ChooseArea;
        case LearnPage::Confirm:
        case LearnPage::Working: return ConfirmArea;
        case LearnPage::Done:    return DoneArea;
        default:                 return Rect{ 0, 0, 0, 0 };
        }
    }

    static TextStyle Style(Font font, Color color, Align align = Align::Left)
    {
        TextStyle style;
        style.font = font;
        style.color = color;
        style.align = align;
        return style;
    }

    static void DrawPanel(Canvas &canvas, const Rect &area)
    {
        canvas.FillRoundRect(area, Radius, PanelBg);
        canvas.StrokeRoundRect(area, Radius, Border, 2);
    }

    static s32 DrawPill(Canvas &canvas, s32 x, s32 y, u8 type)
    {
        std::string name = TypeName(type);
        for (char &c : name)
            c = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        const TextStyle style = Style(Font::Small, Rgb(255, 255, 255), Align::Center);
        const s32 w = MeasureText(name, style) + 8;
        const Rect pill = { x, y, w, 11 };
        canvas.FillRoundRect(pill, 5, TypeColor(type));
        DrawText(canvas, name, Rect{ pill.x, pill.y + 1, pill.w, pill.h }, style);
        return w;
    }

    // One row: name on the left, type and PP on the right.
    static void DrawMoveRow(Canvas &canvas, const Rect &line, const LearnMove &move, Color nameColor)
    {
        const Rect inner = Inset(line, 6, 0, 6, 0);
        const TextStyle pp = Style(Font::Small, move.pp == 0 && move.maxPp != 0 ? Danger : TextDim, Align::Right);
        const std::string ppText = Number("PP %u/%u", move.pp, move.maxPp);
        const s32 ppWidth = 52;
        DrawText(canvas, ppText, Rect{ inner.Right() - ppWidth, inner.y + 6, ppWidth, 8 }, pp);
        s32 right = inner.Right() - ppWidth - 4;
        if (move.type != 0xFF)
        {
            std::string name = TypeName(move.type);
            const s32 w = MeasureText(name, Style(Font::Small, TextMain)) + 8;
            DrawPill(canvas, right - w, inner.y + 4, move.type);
            right -= w + 4;
        }
        DrawText(canvas, MoveLabel(move), Rect{ inner.x, inner.y, right - inner.x, inner.h }, Style(Font::System, nameColor));
    }

    static void DrawChoose(Canvas &canvas, const MoveLearnMenu &menu, const LearnSnapshot &snap)
    {
        DrawPanel(canvas, ChooseArea);
        Stack body(Inset(ChooseArea, 6, 5, 6, 5), Stack::Column, 1);
        const std::string who = snap.monName.empty() ? std::string("Your Pok\xC3\xA9mon") : snap.monName;
        DrawText(canvas, who + " wants to learn:", Inset(body.Next(9), 6, 0, 6, 0), Style(Font::Small, TextDim));
        DrawMoveRow(canvas, body.Next(18), snap.newMove, NewMove);
        const Rect rule = body.Next(1);
        canvas.HLine(rule.x, rule.y, rule.w, Divider);

        const Rect hints = body.NextFromEnd(8);
        const Rect detail = body.NextFromEnd(8);
        if (!menu.Note().empty())
            DrawText(canvas, menu.Note(), Inset(hints, 6, 0, 6, 0), Style(Font::Small, Danger));
        else
        {
            DrawText(canvas, "Up/Down, A: choose", Inset(hints, 6, 0, 6, 0), Style(Font::Small, TextDim));
            DrawText(canvas, "B: skip", Inset(hints, 6, 0, 6, 0), Style(Font::Small, TextDim, Align::Right));
        }

        for (u8 row = 0; row <= LearnSkipRow; row++)
        {
            const Rect line = body.Next(18);
            if (row < LearnSkipRow && row >= snap.moveCount)
                continue;
            const bool on = row == menu.Cursor();
            if (on)
                canvas.FillRoundRect(line, 5, Highlight);
            if (row == LearnSkipRow)
            {
                DrawText(canvas, "Don't learn " + MoveLabel(snap.newMove), Inset(line, 6, 0, 6, 0),
                         Style(Font::System, on ? TextMain : TextDim));
                continue;
            }
            DrawMoveRow(canvas, line, snap.moves[row], on ? TextMain : TextDim);
        }

        // The highlighted move's category, power and accuracy (the new
        // move's on the "don't learn" row).
        const LearnMove &move = menu.Cursor() < snap.moveCount ? snap.moves[menu.Cursor()] : snap.newMove;
        const std::string info = std::string(move.category == 0 ? "Status" : move.category == 1 ? "Physical"
                                             : move.category == 2 ? "Special" : "?")
                                 + "  Power " + LearnPowerText(move.power) + "  Accuracy " + LearnAccuracyText(move.accuracy);
        DrawText(canvas, info, Inset(detail, 6, 0, 6, 0), Style(Font::Small, TextMain));
    }

    static void DrawConfirm(Canvas &canvas, const MoveLearnMenu &menu, const LearnSnapshot &snap)
    {
        DrawPanel(canvas, ConfirmArea);
        Stack body(Inset(ConfirmArea, 10, 6, 10, 6), Stack::Column, 3);
        const bool skip = menu.Cursor() >= snap.moveCount;
        const std::string title = skip ? "Don't learn it?" : "Forget " + MoveLabel(snap.moves[menu.Cursor()]) + "?";
        DrawText(canvas, title, body.Next(18), Style(Font::System, TextMain));
        const std::string line = skip ? (snap.monName.empty() ? std::string("It") : snap.monName) + " keeps its moves"
                                      : (snap.monName.empty() ? std::string("It") : snap.monName) + " learns " + MoveLabel(snap.newMove);
        DrawText(canvas, line, body.Next(18), Style(Font::System, skip ? TextDim : NewMove));
        const Rect hints = body.NextFromEnd(8);
        if (menu.Page() == LearnPage::Working)
            DrawText(canvas, "Telling the game...", hints, Style(Font::Small, Good));
        else
        {
            DrawText(canvas, "A: yes", hints, Style(Font::Small, TextDim));
            DrawText(canvas, "B: back", hints, Style(Font::Small, TextDim, Align::Right));
        }
    }

    static void DrawDone(Canvas &canvas, const MoveLearnMenu &menu)
    {
        DrawPanel(canvas, DoneArea);
        Stack body(Inset(DoneArea, 10, 6, 10, 6), Stack::Column, 2);
        DrawText(canvas, "Done", body.Next(18), Style(Font::System, Good));
        // "X forgot Y" / "and learned Z" on two lines (one is too long).
        const std::string &result = menu.Result();
        const size_t cut = result.find(" and learned ");
        const TextStyle style = Style(Font::Small, TextMain);
        DrawText(canvas, result.substr(0, cut), body.Next(9), style);
        if (cut != std::string::npos)
            DrawText(canvas, result.substr(cut + 1), body.Next(9), style);
    }

    void MoveLearnMenu::Draw(Canvas &canvas, const LearnSnapshot &snap) const
    {
        switch (m_page)
        {
        case LearnPage::Choose:  DrawChoose(canvas, *this, snap); break;
        case LearnPage::Confirm:
        case LearnPage::Working: DrawConfirm(canvas, *this, snap); break;
        case LearnPage::Done:    DrawDone(canvas, *this); break;
        default: break;
        }
    }

    static std::string MoveKey(const LearnMove &move)
    {
        return move.name + Number(" %u %u", move.move, move.type) + Number(" %u %u", move.category, move.power)
               + Number(" %u %u", move.accuracy, move.pp) + Number(" %u", move.maxPp);
    }

    std::string MoveLearnMenu::DrawKey(const LearnSnapshot &snap) const
    {
        std::string key = Number("%u %u|", (unsigned)m_page, m_cursor) + m_note + "|" + snap.monName + "|" + MoveKey(snap.newMove);
        for (u8 i = 0; i < snap.moveCount && i < 4; i++)
            key += "|" + MoveKey(snap.moves[i]);
        if (m_page == LearnPage::Done)
            key += "|" + m_result;
        return key;
    }

    std::string MoveLearnMenu::Status(const LearnSnapshot &snap) const
    {
        static const char *const pages[] = { "hidden", "choose", "confirm", "working", "done" };
        static const char *const stages[] = { "none", "ask", "list", "confirm" };
        std::string out = std::string("move-learn: page ") + pages[(int)m_page];
        if (m_page == LearnPage::Hidden)
            return out;
        out += std::string(", game ") + stages[(int)snap.stage];
        if (m_page == LearnPage::Choose || m_page == LearnPage::Confirm)
        {
            out += ", " + (snap.monName.empty() ? std::string("?") : snap.monName) + " wants " + MoveLabel(snap.newMove);
            out += Number(", cursor %u (", m_cursor)
                   + (m_cursor < snap.moveCount ? MoveLabel(snap.moves[m_cursor]) : std::string("don't learn")) + ")";
        }
        if (m_page == LearnPage::Done)
            out += ": " + m_result;
        if (!m_note.empty())
            out += ", note: " + m_note;
        return out;
    }
}
}
