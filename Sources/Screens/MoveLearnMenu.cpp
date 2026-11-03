#include "Screens/MoveLearnMenu.hpp"

#include <cstdio>

#include "Screens/BattleMenu.hpp"
#include "UI/GameAssets.hpp"
#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
namespace Screens
{
    using namespace UI;

    // In battle the game prints its question along the bottom (y > 192) and
    // draws the opponent's HP box at the top right (y < 34) and the player's
    // at the bottom left (x < 132), so the list stays in between.
    static constexpr Rect ChooseArea  = { 156, 36, 240, 158 };
    static constexpr Rect ConfirmArea = { 166, 96, 230, 80 };
    static constexpr Rect DoneArea    = { 166, 128, 230, 52 };
    static constexpr s32  RowH        = 21;     // a move button (the game's are taller; six rows fit here)
    static constexpr s32  SkipH       = 22;

    static constexpr Color NoteText = Rgb(0xFF, 0x8A, 0x80);    // "try again" on the dark window

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

    static Theme::TextStyle Style(Theme::TextSize size, Color color, Align align = Align::Left)
    {
        Theme::TextStyle style;
        style.size = size;
        style.color = color;
        style.align = align;
        return style;
    }

    static std::string TypeLabel(u8 type)
    {
        const GameAssets *assets = GetGameAssets();
        const std::string name = assets != nullptr ? assets->TypeName(type) : std::string();
        return Theme::Upper(name.empty() ? std::string(TypeName(type)) : name);
    }

    // One move as the game's move list shows it: a button in the move's
    // type colours, the name in white, then the type plate and the PP.
    static void DrawMoveRow(Canvas &canvas, const Rect &r, const LearnMove &move, bool selected)
    {
        const u8 type = move.type == 0xFF ? 0 : move.type;
        const Theme::TypeColors c = Theme::TypeColor(type);
        Theme::DrawMoveButton(canvas, r, type, selected);

        Theme::TextStyle pp = Style(Theme::TextSize::Small, Theme::PpTextColor(move.pp, move.maxPp), Align::Right);
        pp.outline = c.lineDark;
        const s32 ppW = 50;
        Theme::DrawText(canvas, Number("PP %u/%u", move.pp, move.maxPp), Rect{ r.Right() - 4 - ppW, r.y, ppW, r.h }, pp);
        s32 right = r.Right() - 4 - ppW - 4;
        if (move.type != 0xFF)
        {
            right -= Theme::TypePlateW;
            Theme::DrawTypePlate(canvas, Rect{ right, r.y + (r.h - Theme::TypePlateH) / 2, Theme::TypePlateW, Theme::TypePlateH },
                                 move.type, TypeLabel(move.type));
            right -= 4;
        }
        Theme::TextStyle name = Style(Theme::TextSize::Body, Theme::WindowText);
        name.outline = c.lineDark;
        Theme::DrawText(canvas, MoveLabel(move), Rect{ r.x + 18, r.y, right - r.x - 18, r.h }, name);
        if (selected)
            Theme::DrawCursor(canvas, r.x + 12, r.y + r.h / 2);
    }

    // Category icon, power and accuracy of a move, on one line.
    static void DrawMoveInfo(Canvas &canvas, const Rect &line, const LearnMove &move)
    {
        s32 x = line.x;
        if (move.category <= 2)
        {
            Theme::DrawCategoryIcon(canvas, x, line.y + (line.h - 20) / 2, move.category);
            x += 42 + 6;
        }
        const std::string info = "Power " + LearnPowerText(move.power) + "   Accuracy " + LearnAccuracyText(move.accuracy);
        Theme::DrawText(canvas, info, Rect{ x, line.y, line.Right() - x, line.h }, Style(Theme::TextSize::Small, Theme::WindowText));
    }

    static void DrawChoose(Canvas &canvas, const MoveLearnMenu &menu, const LearnSnapshot &snap)
    {
        // The game's own question ("Should an old move be forgotten and
        // replaced with ...?") stays readable below, so this only shows the
        // moves: the highlighted one's details, the new move, the four old
        // ones and "Don't learn".
        Theme::DrawWindow(canvas, ChooseArea);
        Stack body(Inset(ChooseArea, 6, 5, 6, 3), Stack::Column, 0);

        const Rect info = body.Next(18);
        if (!menu.Note().empty())
            Theme::DrawText(canvas, menu.Note(), info, Style(Theme::TextSize::Small, NoteText));
        else
            DrawMoveInfo(canvas, info, menu.Cursor() < snap.moveCount ? snap.moves[menu.Cursor()] : snap.newMove);
        body.Next(1);
        DrawMoveRow(canvas, body.Next(RowH), snap.newMove, false);
        body.Next(3);

        for (u8 row = 0; row < LearnSkipRow; row++)
        {
            const Rect line = body.Next(RowH);
            if (row < snap.moveCount)
                DrawMoveRow(canvas, line, snap.moves[row], row == menu.Cursor());
        }
        body.Next(1);
        Theme::DrawRow(canvas, body.Next(SkipH), Theme::RowKind::Option, "Don't learn " + MoveLabel(snap.newMove),
                       menu.Cursor() == LearnSkipRow);
    }

    static void DrawConfirm(Canvas &canvas, const MoveLearnMenu &menu, const LearnSnapshot &snap)
    {
        Theme::DrawWindow(canvas, ConfirmArea);
        const Rect inner = Inset(ConfirmArea, Theme::WindowPad + 2, 6, Theme::WindowPad + 2, 6);
        const s32 line = Theme::LineHeight(Theme::TextSize::Body);
        const bool skip = menu.Cursor() >= snap.moveCount;
        const std::string who = snap.monName.empty() ? std::string("It") : snap.monName;
        const std::string title = skip ? "Don't learn " + MoveLabel(snap.newMove) + "?"
                                       : "Forget " + MoveLabel(snap.moves[menu.Cursor()]) + "?";
        Theme::DrawText(canvas, title, Rect{ inner.x, inner.y, inner.w, line }, Style(Theme::TextSize::Body, Theme::WindowText));
        const std::string sub = skip ? who + " keeps its moves." : who + " learns " + MoveLabel(snap.newMove) + ".";
        Theme::DrawText(canvas, sub, Rect{ inner.x, inner.y + line, inner.w, line },
                        Style(Theme::TextSize::Body, skip ? Theme::WindowText : Theme::AccentText));

        const s32 y = inner.Bottom() - Theme::OptionHeight;
        if (menu.Page() == LearnPage::Working)
        {
            Theme::DrawText(canvas, "Telling the game...", Rect{ inner.x, y, inner.w, Theme::OptionHeight },
                            Style(Theme::TextSize::Small, Theme::WindowText));
            return;
        }
        // A takes the answer the cursor is on, B goes back to the list.
        const s32 back = Theme::PromptWidth(Theme::Button::B, "Back");
        Theme::DrawRow(canvas, Rect{ inner.x, y, inner.w - back - 8, Theme::OptionHeight }, Theme::RowKind::Option,
                       skip ? "Don't learn it" : "Forget it", true);
        Theme::DrawButtonPrompt(canvas, inner.Right() - back, y + (Theme::OptionHeight - Theme::PromptHeight) / 2,
                                Theme::Button::B, "Back");
    }

    static void DrawDone(Canvas &canvas, const MoveLearnMenu &menu)
    {
        Theme::DrawWindow(canvas, DoneArea);
        const Rect inner = Inset(DoneArea, Theme::WindowPad + 2, 7, Theme::WindowPad + 2, 7);
        const s32 line = Theme::LineHeight(Theme::TextSize::Body);
        // "X forgot Y" / "and learned Z!" on two lines (one is too long).
        const std::string &result = menu.Result();
        const size_t cut = result.find(" and learned ");
        const Theme::TextStyle style = Style(Theme::TextSize::Body, Theme::WindowText);
        if (cut == std::string::npos)
        {
            Theme::DrawText(canvas, result + ".", inner, style);
            return;
        }
        Theme::DrawText(canvas, result.substr(0, cut), Rect{ inner.x, inner.y, inner.w, line }, style);
        Theme::DrawText(canvas, result.substr(cut + 1) + "!", Rect{ inner.x, inner.y + line, inner.w, line }, style);
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
