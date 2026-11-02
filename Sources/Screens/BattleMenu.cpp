#include "Screens/BattleMenu.hpp"

#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
namespace Screens
{
    using namespace UI;

    // Where each page sits on the top screen (400x240). The game draws the
    // opponent's HP box across the top right (y < 34), the player's across
    // the bottom left (x < 132, y > 192) and battle messages along the bottom
    // only while no menu is up (the menu is hidden then), so the pages stay
    // right of x 166 and below y 36.
    static constexpr Rect CommandArea = { 206, 68, 190, 168 };
    static constexpr Rect MovesArea   = { 166, 36, 230, 200 };
    static constexpr Rect GameArea    = { 166, 158, 230, 78 };
    // After a catch the Pokédex scene asks its question along the bottom,
    // and so do the battle's own questions (learning a move): their helper
    // goes at the top right.
    static constexpr Rect CatchArea   = { 166, 8, 230, 78 };
    static constexpr s32  Radius      = 10;

    static constexpr Color PanelBg   = Rgb(22, 30, 52);
    static constexpr Color Border    = Rgb(120, 150, 220);
    static constexpr Color Divider   = Rgb(120, 150, 220, 90);
    static constexpr Color Highlight = Rgb(70, 110, 200, 220);
    static constexpr Color Sent      = Rgb(60, 150, 110, 220);
    static constexpr Color TextMain  = Rgb(240, 244, 255);
    static constexpr Color TextDim   = Rgb(150, 165, 200);
    static constexpr Color Good      = Rgb(110, 220, 140);
    static constexpr Color Weak      = Rgb(240, 170, 90);
    static constexpr Color Danger    = Rgb(235, 110, 110);

    const char *TypeName(u8 type)
    {
        static const char *const names[] = {
            "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel",
            "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy",
        };
        return type < 18 ? names[type] : "?";
    }

    Color TypeColor(u8 type)
    {
        static const Color colors[] = {
            Rgb(168, 168, 120), Rgb(192, 48, 40),   Rgb(168, 144, 240), Rgb(160, 64, 160),
            Rgb(224, 192, 104), Rgb(184, 160, 56),  Rgb(168, 184, 32),  Rgb(112, 88, 152),
            Rgb(184, 184, 208), Rgb(240, 128, 48),  Rgb(104, 144, 240), Rgb(120, 200, 80),
            Rgb(248, 208, 48),  Rgb(248, 88, 136),  Rgb(152, 216, 216), Rgb(112, 56, 248),
            Rgb(112, 88, 72),   Rgb(238, 153, 172),
        };
        return type < 18 ? colors[type] : Rgb(110, 110, 110);
    }

    const char *EffectLabel(u8 effect)
    {
        switch (effect)
        {
        case 0:  return "No effect";
        case 2:  return "Super effective";
        case 3:  return "Not very effective";
        default: return "";
        }
    }

    static Color EffectColor(u8 effect)
    {
        return effect == 2 ? Good : effect == 3 ? Weak : TextDim;
    }

    const char *BattlePocketName(u8 pocket)
    {
        static const char *const names[] = {
            "Rotom Powers", "HP/PP Restore", "Status Restore", "Pok\xC3\xA9 Balls", "Battle Items",
        };
        return pocket < 5 ? names[pocket] : "?";
    }

    bool IsBallItem(u16 item)
    {
        // Master .. Cherish, Fast .. Park, Dream, Beast.
        return (item >= 1 && item <= 16) || (item >= 492 && item <= 500) || item == 576 || item == 851;
    }

    const char *CommandLabel(BattleCommand command)
    {
        static const char *const names[] = { "Fight", "Bag", "Pok\xC3\xA9mon", "Run" };
        return (u8)command < (u8)BattleCommand::Count ? names[(u8)command] : "?";
    }

    u8 BuildMoveRows(MoveRow rows[4], const u16 moves[4], const u8 pp[4], const u8 maxPp[4], const u8 type[4],
                     const u8 effect[4], u8 listCount)
    {
        u8 count = 0;
        for (u8 i = 0; i < 4 && moves[i] != 0; i++)
        {
            MoveRow &row = rows[count++];
            row = MoveRow();
            row.move = moves[i];
            row.pp = pp[i];
            row.maxPp = maxPp[i];
            if (i < listCount)
            {
                row.type = type[i];
                row.effect = effect[i];
            }
        }
        for (u8 i = count; i < 4; i++)
            rows[i] = MoveRow();
        return count;
    }

    u8 BallsInPocket(const u16 *items, const u16 *counts, u32 slots, u16 *outItems, u16 *outCounts, u8 cap)
    {
        u8 n = 0;
        for (u32 i = 0; i < slots; i++)
        {
            if (!IsBallItem(items[i]) || counts[i] == 0)
                continue;
            if (n < cap)
            {
                outItems[n] = items[i];
                outCounts[n] = counts[i];
            }
            if (n < 0xFF)
                n++;
        }
        return n;
    }

    u8 StepCursor(u8 cursor, u8 count, s32 delta)
    {
        if (count == 0)
            return 0;
        return (u8)(((s32)cursor + delta % count + count) % count);
    }

    static const char *const CommandActions[] = { "battle-fight", "battle-bag", "battle-pokemon", "battle-flee" };
    static const char *const MoveActions[] = { "battle-move1", "battle-move2", "battle-move3", "battle-move4" };

    // Other screens: the Pokémon action screen (from the game's Pokémon
    // list), the battle's questions (learning a move after a level-up) and
    // objects of the intro. All get the helper; one not reached from the
    // Bag or Pokémon list shows only once it has stayed (Visible).
    static BattlePage PageFor(const BattleSnapshot &snap)
    {
        if (snap.afterCatch)
            return BattlePage::Game;
        if (!snap.inBattle)
            return BattlePage::Hidden;
        switch (snap.menu)
        {
        case GameMenu::Command: return BattlePage::Command;
        case GameMenu::Moves:   return BattlePage::Moves;
        case GameMenu::Pokemon:
        case GameMenu::Bag:     return BattlePage::Game;
        case GameMenu::Other:   return BattlePage::Game;
        default:                return BattlePage::Hidden;
        }
    }

    void BattleMenu::Enter(BattlePage page, const BattleSnapshot &snap)
    {
        m_fromGame = m_page == BattlePage::Game && m_menu != GameMenu::Other;
        m_page = page;
        m_menu = snap.menu;
        m_afterCatch = snap.afterCatch;
        m_stable = 0;
        m_pending = false;
        m_pendingFrames = 0;
        m_queued = nullptr;
        m_note.clear();
        // Fight first every turn; the move list starts where the game's does
        // (it remembers the last move used).
        m_cursor = page == BattlePage::Moves && snap.gameCursor < snap.moveCount ? snap.gameCursor : 0;
    }

    BattleMenuOutput BattleMenu::Update(const BattleSnapshot &snap, u32 pressed)
    {
        BattleMenuOutput out;
        const BattlePage page = PageFor(snap);
        if (page != m_page || snap.menu != m_menu || snap.afterCatch != m_afterCatch)
            Enter(page, snap);
        if (m_stable < 0xFFFF)
            m_stable++;

        if (m_page != BattlePage::Command && m_page != BattlePage::Moves)
            return out;
        out.capture = true;

        if (m_pending && ++m_pendingFrames > PendingFrames)
        {
            // The action gave up (it logs why); let the player choose again.
            m_pending = false;
            m_note = "No answer from the game, try again";
        }
        if (snap.actionRunning || m_pending)
            return out;

        if (m_queued == nullptr)
        {
            if (pressed & (BattleKeys::Up | BattleKeys::Down))
                m_note.clear();
            if (m_page == BattlePage::Command)
            {
                const u8 count = (u8)BattleCommand::Count;
                if (pressed & BattleKeys::Up)
                    m_cursor = StepCursor(m_cursor, count, -1);
                else if (pressed & BattleKeys::Down)
                    m_cursor = StepCursor(m_cursor, count, 1);
                else if (pressed & BattleKeys::A)
                    m_queued = CommandActions[m_cursor];
            }
            else
            {
                if (pressed & BattleKeys::Up)
                    m_cursor = StepCursor(m_cursor, snap.moveCount, -1);
                else if (pressed & BattleKeys::Down)
                    m_cursor = StepCursor(m_cursor, snap.moveCount, 1);
                else if (pressed & BattleKeys::B)
                    m_queued = "battle-moves-back";
                else if ((pressed & BattleKeys::A) && m_cursor < snap.moveCount)
                {
                    // A move without PP isn't sent while another has some;
                    // with none left the game uses Struggle whichever is tapped.
                    bool anyPp = false;
                    for (u8 i = 0; i < snap.moveCount; i++)
                        anyPp = anyPp || snap.moves[i].pp > 0;
                    const MoveRow &row = snap.moves[m_cursor];
                    if (row.pp == 0 && anyPp)
                        m_note = (row.name.empty() ? std::string("That move") : row.name) + " has no PP left";
                    else
                        m_queued = MoveActions[m_cursor];
                }
            }
        }

        const u32 settle = m_page == BattlePage::Command ? CommandSettleFrames : MovesSettleFrames;
        if (m_queued != nullptr && m_stable >= settle)
        {
            out.action = m_queued;
            m_queued = nullptr;
            m_pending = true;
            m_pendingFrames = 0;
        }
        return out;
    }

    bool BattleMenu::Visible(void) const
    {
        if (Prompt())
            return m_stable >= PromptShowFrames;
        return m_page != BattlePage::Hidden && (m_page != BattlePage::Command || m_stable >= ShowFrames);
    }

    bool BattleMenu::Prompt(void) const
    {
        return m_page == BattlePage::Game && m_menu == GameMenu::Other && !m_fromGame && !m_afterCatch;
    }

    Rect BattleMenu::Area(void) const
    {
        switch (m_page)
        {
        case BattlePage::Command: return CommandArea;
        case BattlePage::Moves:   return MovesArea;
        case BattlePage::Game:    return m_afterCatch || Prompt() ? CatchArea : GameArea;
        default:                  return Rect{ 0, 0, 0, 0 };
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

    static void DrawHints(Canvas &canvas, const Rect &row, const std::string &left, const std::string &right)
    {
        DrawText(canvas, left, row, Style(Font::Small, TextDim));
        DrawText(canvas, right, row, Style(Font::Small, TextDim, Align::Right));
    }

    static std::string Number(const char *fmt, unsigned a, unsigned b)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    // A rounded label in the type's colour, as wide as its text.
    static s32 DrawTypePill(Canvas &canvas, s32 x, s32 y, u8 type)
    {
        std::string name = TypeName(type);
        for (char &c : name)
            c = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        const TextStyle style = Style(Font::Small, Rgb(255, 255, 255), Align::Center);
        const s32 w = MeasureText(name, style) + 10;
        const Rect pill = { x, y, w, 12 };
        canvas.FillRoundRect(pill, 5, TypeColor(type));
        DrawText(canvas, name, Rect{ pill.x, pill.y + 1, pill.w, pill.h }, style);
        return w;
    }

    static void DrawCommand(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        DrawPanel(canvas, CommandArea);
        Stack body(Inset(CommandArea, 10, 6, 10, 6), Stack::Column, 3);
        const std::string who = snap.activeName.empty() ? std::string("you") : snap.activeName;
        DrawText(canvas, "What will " + who + " do?", body.Next(20), Style(Font::System, TextMain));
        const Rect rule = body.Next(1);
        canvas.HLine(rule.x, rule.y, rule.w, Divider);

        const Rect footer = body.NextFromEnd(10);
        if (!menu.Note().empty())
            DrawText(canvas, menu.Note(), footer, Style(Font::Small, Danger));
        else
            DrawHints(canvas, footer, "Up/Down: move", "A: choose");

        for (u8 i = 0; i < (u8)BattleCommand::Count; i++)
        {
            const Rect line = body.Next(28);
            const bool on = i == menu.Cursor();
            if (on)
            {
                canvas.FillRoundRect(line, 6, menu.Pending() ? Sent : Highlight);
                DrawIcon(canvas, Icon::ArrowRight, Rect{ line.x + 3, line.y + 6, 16, 16 }, TextMain);
            }
            std::string label = CommandLabel((BattleCommand)i);
            if (on && menu.Pending())
                label += "...";
            DrawText(canvas, label, Inset(line, 24, 0, 6, 0), Style(Font::System, on ? TextMain : TextDim));
        }
    }

    static void DrawMoves(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        DrawPanel(canvas, MovesArea);
        Stack body(Inset(MovesArea, 10, 5, 10, 6), Stack::Column, 2);
        const Rect title = body.Next(20);
        DrawText(canvas, "Fight", title, Style(Font::System, TextMain));
        DrawHints(canvas, Rect{ title.x, title.y + 5, title.w, 10 }, "", "B: back");
        const Rect rule = body.Next(1);
        canvas.HLine(rule.x, rule.y, rule.w, Divider);

        const Rect footer = body.NextFromEnd(10);
        if (!menu.Note().empty())
            DrawText(canvas, menu.Note(), footer, Style(Font::Small, Danger));
        else
            DrawHints(canvas, footer, "Up/Down: move", "A: use");

        for (u8 i = 0; i < 4; i++)
        {
            const Rect line = body.Next(38);
            if (i >= snap.moveCount)
                continue;
            const MoveRow &row = snap.moves[i];
            const bool on = i == menu.Cursor();
            if (on)
                canvas.FillRoundRect(line, 6, menu.Pending() ? Sent : Highlight);

            Stack rows(Inset(line, 8, 1, 8, 3), Stack::Column, 1);
            const Rect top = rows.Next(19);
            const TextStyle pp = Style(Font::System, row.pp == 0 ? Danger : on ? TextMain : TextDim, Align::Right);
            const std::string ppText = Number("PP %u/%u", row.pp, row.maxPp);
            const s32 ppWidth = MeasureText(ppText, pp);
            DrawText(canvas, ppText, Rect{ top.Right() - ppWidth, top.y, ppWidth, top.h }, pp);
            std::string name = row.name.empty() ? Number("Move %u", row.move, 0) : row.name;
            if (on && menu.Pending())
                name += "...";
            DrawText(canvas, name, Rect{ top.x, top.y, top.w - ppWidth - 6, top.h },
                     Style(Font::System, on ? TextMain : TextDim));

            const Rect bottom = rows.Next(12);
            s32 x = bottom.x;
            if (row.type != 0xFF)
                x += DrawTypePill(canvas, x, bottom.y, row.type) + 6;
            const char *effect = EffectLabel(row.effect);
            if (effect[0] != '\0')
                DrawText(canvas, effect, Rect{ x, bottom.y + 2, bottom.Right() - x, 8 },
                         Style(Font::Small, EffectColor(row.effect)));
        }
    }

    static void DrawGame(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = menu.Area();
        DrawPanel(canvas, area);
        Stack body(Inset(area, 10, 5, 10, 6), Stack::Column, 3);
        const Rect title = body.Next(20);
        const Rect line = body.Next(20);
        const Rect hints1 = body.Next(10);
        const Rect hints2 = body.Next(10);
        const TextStyle main = Style(Font::System, TextMain);

        if (snap.afterCatch)
        {
            DrawText(canvas, "Caught!", title, main);
            // A new species gets its Pokédex page first; then the nickname
            // question, where A opens a keyboard on the bottom screen.
            DrawText(canvas, "B: next, no nickname", line, Style(Font::System, Good));
            DrawHints(canvas, hints1, "Nickname? A: yes (keyboard below)", "");
            DrawHints(canvas, hints2, "ZR: show the bottom screen", "");
        }
        else if (snap.menu == GameMenu::Bag)
        {
            DrawText(canvas, std::string("Bag: ") + BattlePocketName(snap.bagPocket), title, main);
            std::string item = snap.bagItem.empty()
                ? (snap.listCount == 0 ? std::string("(empty)") : Number("Item %u of %u", snap.gameCursor + 1u, snap.listCount))
                : snap.bagItem + Number(" x%u", snap.bagItemCount, 0);
            if (snap.bagItemPage)
                item = (snap.bagItem.empty() ? std::string("Use this item") : "Use " + snap.bagItem) + "?";
            DrawText(canvas, item, line, Style(Font::System, Good));
            if (snap.bagItemPage)
            {
                DrawHints(canvas, hints1, "A: use", "B: back");
                DrawHints(canvas, hints2, "Left/Right: other item", "");
            }
            else
            {
                DrawHints(canvas, hints1, "Left/Right: pocket", "Up/Down: item");
                DrawHints(canvas, hints2, "A: open", "B: back");
            }
        }
        else if (snap.menu == GameMenu::Pokemon)
        {
            DrawText(canvas, "Pok\xC3\xA9mon", title, main);
            // The list's cursor is not set for a few frames as it opens.
            std::string mon = !snap.partyName.empty() ? snap.partyName + Number(" Lv%u", snap.partyLevel, 0)
                            : snap.gameCursor < snap.listCount ? Number("Slot %u of %u", snap.gameCursor + 1u, snap.listCount)
                            : std::string("Choose a Pok\xC3\xA9mon");
            if (snap.partyMaxHp != 0)
                mon += Number("  HP %u/%u", snap.partyHp, snap.partyMaxHp);
            DrawText(canvas, mon, line, Style(Font::System, Good));
            DrawHints(canvas, hints1, "D-pad: move", "A: choose");
            DrawHints(canvas, hints2, "", "B: back");
        }
        else if (menu.Prompt())
        {
            DrawText(canvas, "The game asks", title, main);
            DrawText(canvas, "Choices are on the bottom screen", line, Style(Font::System, TextDim));
            DrawHints(canvas, hints1, "Up/Down + A: choose", "B: back");
            DrawHints(canvas, hints2, "ZR: show the bottom screen", "");
        }
        else
        {
            DrawText(canvas, "Game screen", title, main);
            DrawText(canvas, "Buttons go to the game", line, Style(Font::System, TextDim));
            DrawHints(canvas, hints1, "D-pad: move", "A: choose");
            DrawHints(canvas, hints2, "", "B: back");
        }
    }

    void BattleMenu::Draw(Canvas &canvas, const BattleSnapshot &snap) const
    {
        switch (m_page)
        {
        case BattlePage::Command: DrawCommand(canvas, *this, snap); break;
        case BattlePage::Moves:   DrawMoves(canvas, *this, snap); break;
        case BattlePage::Game:    DrawGame(canvas, *this, snap); break;
        default: break;
        }
    }

    std::string BattleMenu::DrawKey(const BattleSnapshot &snap) const
    {
        std::string key = Number("%u %u", (unsigned)m_page, m_cursor) + (m_pending ? "p" : "") + "|" + m_note;
        switch (m_page)
        {
        case BattlePage::Command:
            key += "|" + snap.activeName;
            break;
        case BattlePage::Moves:
            for (u8 i = 0; i < snap.moveCount; i++)
            {
                const MoveRow &row = snap.moves[i];
                key += "|" + row.name + Number(" %u %u", row.move, row.type) + Number(" %u %u", row.effect, row.pp)
                       + Number(" %u", row.maxPp, 0);
            }
            break;
        case BattlePage::Game:
            key += (snap.afterCatch ? "c" : "") + Number("|%u %u", (unsigned)snap.menu, snap.gameCursor) + Number(" %u %u", snap.listCount, snap.bagPocket)
                   + (snap.bagItemPage ? "i" : "") + snap.bagItem + Number(" %u", snap.bagItemCount, 0) + "|"
                   + snap.partyName + Number(" %u %u", snap.partyLevel, snap.partyHp) + Number(" %u", snap.partyMaxHp, 0);
            break;
        default:
            break;
        }
        return key;
    }

    std::string BattleMenu::Status(const BattleSnapshot &snap) const
    {
        static const char *const pages[] = { "hidden", "command", "moves", "game" };
        std::string out = std::string("battle-ui: page ") + pages[(int)m_page];
        if (m_page == BattlePage::Command)
            out += Number(", cursor %u of %u (", m_cursor, (unsigned)BattleCommand::Count)
                   + CommandLabel((BattleCommand)m_cursor) + ")";
        else if (m_page == BattlePage::Moves && m_cursor < snap.moveCount)
        {
            const MoveRow &row = snap.moves[m_cursor];
            out += Number(", cursor %u of %u (", m_cursor, snap.moveCount) + row.name + ", " + TypeName(row.type);
            const char *effect = EffectLabel(row.effect);
            out += std::string(effect[0] ? ", " : "") + effect + Number(", PP %u/%u)", row.pp, row.maxPp);
        }
        else if (m_page == BattlePage::Game)
        {
            static const char *const menus[] = { "none", "command", "moves", "pokemon", "bag", "other" };
            out += snap.afterCatch ? std::string(", after catch") : std::string(", game ") + menus[(int)snap.menu];
            if (Prompt())
                out += Number(", prompt, vt %08X", snap.otherVtable, 0);
            if (snap.menu == GameMenu::Bag)
                out += std::string(", ") + BattlePocketName(snap.bagPocket) + (snap.bagItemPage ? ", item page" : "")
                       + ", " + (snap.bagItem.empty() ? Number("item %u of %u", snap.gameCursor + 1u, snap.listCount) : snap.bagItem);
            else if (snap.menu == GameMenu::Pokemon)
                out += ", " + (!snap.partyName.empty() ? snap.partyName
                               : snap.gameCursor < snap.listCount ? Number("slot %u of %u", snap.gameCursor + 1u, snap.listCount)
                               : std::string("no slot yet"));
        }
        if (m_pending)
            out += ", sent";
        if (!m_note.empty())
            out += ", note: " + m_note;
        return out;
    }
}
}
