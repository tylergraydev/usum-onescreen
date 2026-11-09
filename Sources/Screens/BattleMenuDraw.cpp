// Drawing of the native battle menu (Screens/BattleMenu.hpp) in the game's
// look (UI/Theme.hpp, docs/VISUAL-STYLE.md): the command bar as option
// pills, the move list as the game's type-coloured move buttons with type
// plates and PP, the Pokémon list as the native Party screen's
// (Screens/PartyMenu.hpp DrawParty), the Bag as Bag-style rows with tabs,
// and the battle's question as a window with two options. Selection is the
// game's red cursor only.

#include "Screens/BattleMenu.hpp"

#include <cstdio>
#include <initializer_list>

#include "Screens/PartyMenu.hpp"
#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
namespace Screens
{
    using namespace UI;

    // Where each page sits on the top screen (400x240). The game draws the
    // opponent's HP box across the top right (y < 34), the player's across
    // the bottom left (x < 132, y > 192) and battle messages along the bottom
    // only while no menu is up (the menu is hidden then), so the command bar
    // and the move list stay right of x 166 and below y 36. The Pokémon list
    // and the Bag cover the scene, as the game's own do on the bottom screen.
    static constexpr Rect CommandArea  = { 222, 48, 174, 188 };
    static constexpr Rect MovesArea    = { 186, 36, 210, 202 };
    static constexpr Rect BarCommandArea = { 170, 166, 226, 70 };
    static constexpr Rect BarMovesArea   = { 170, 74, 226, 164 };
    static constexpr Rect RailCommandArea = { 202, 52, 194, 186 };
    static constexpr Rect RailMovesArea  = { 170, 38, 226, 200 };
    static constexpr Rect QuestionArea = { 186, 60, 210, 118 };
    static constexpr Rect GameArea     = { 166, 158, 230, 78 };
    // After a catch the Pokédex scene asks its question along the bottom,
    // and so do the battle's other questions: their helper goes at the top.
    static constexpr Rect CatchArea    = { 166, 8, 230, 78 };
    static constexpr Rect BagArea      = { 12, 8, 376, 224 };

    Rect BattleDescriptionArea(BattleLayout layout)
    {
        const Rect area = layout == BattleLayout::Bar ? BarMovesArea
                          : layout == BattleLayout::Rail ? RailMovesArea : MovesArea;
        return Inset(area, 8, 25, 8, 25);
    }

    static std::string Format(const char *fmt, unsigned a, unsigned b = 0)
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    static Theme::TextStyle White(Theme::TextSize size = Theme::TextSize::Body, Align align = Align::Left)
    {
        Theme::TextStyle style;
        style.size = size;
        style.color = Theme::WindowText;
        style.align = align;
        return style;
    }

    // A row of button prompts from the left at (x, y); returns the x after.
    static s32 Prompts(Canvas &canvas, s32 x, s32 y, std::initializer_list<std::pair<Theme::Button, const char *>> list)
    {
        for (const auto &p : list)
            x += Theme::DrawButtonPrompt(canvas, x, y, p.first, p.second) + 4;
        return x;
    }

    // A one-line footer: the note (or what is being done) in a window, else
    // nothing.
    static bool Notice(Canvas &canvas, const Rect &r, const std::string &text)
    {
        if (text.empty())
            return false;
        Theme::DrawWindow(canvas, r);
        Theme::DrawText(canvas, text, Inset(r, Theme::WindowPad, 0, Theme::WindowPad, 0), White(Theme::TextSize::Small));
        return true;
    }

    static std::string Footer(const BattleMenu &menu)
    {
        return menu.Busy() ? menu.BusyText() : menu.Note();
    }

    // --- Command bar ---

    static void DrawCommand(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = CommandArea;
        Theme::DrawWindow(canvas, area);
        const Rect head = { area.x, area.y + 2, area.w, 28 };
        const std::string who = snap.activeName.empty() ? std::string("you") : snap.activeName;
        Theme::DrawText(canvas, "What will " + who + " do?", Inset(head, Theme::WindowPad, 2, Theme::WindowPad, 2), White());

        for (u8 i = 0; i < (u8)BattleCommand::Count; i++)
        {
            const Rect pill = { area.x + 8, head.Bottom() + 6 + i * 32, area.w - 16, Theme::OptionHeight + 2 };
            std::string label = CommandLabel((BattleCommand)i);
            if (i == menu.Cursor() && menu.Pending())
                label += "...";
            Theme::DrawRow(canvas, pill, Theme::RowKind::Option, label, i == menu.Cursor());
        }
        const Rect foot = { area.x + 4, area.Bottom() - Theme::PromptHeight - 6, area.w - 8, Theme::PromptHeight + 2 };
        if (!Notice(canvas, foot, menu.Note()))
        {
            if (snap.activeShiny || snap.activeHiddenAbility || snap.foeShiny || snap.foeHiddenAbility)
            {
                UI::TextStyle hint;
                hint.font = Font::Small;
                hint.color = Theme::WindowText;
                UI::DrawText(canvas, "A Choose", Rect{ foot.x + 4, foot.y + 1, area.w - 106, foot.h }, hint);
            }
            else
                Prompts(canvas, area.x + 8, foot.y + 1, { { Theme::Button::A, "Choose" } });
        }
    }

    // --- Move list ---

    static void CompactText(Canvas &canvas, const std::string &text, const Rect &box,
                            Color color, Align align = Align::Left);
    static void DrawMoveDetails(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap, const Rect &area);

    static void DrawMoves(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = MovesArea;
        Theme::DrawWindow(canvas, area);
        const s32 top = area.y + 4;
        for (u8 i = 0; i < snap.moveCount && i < 4; i++)
        {
            const MoveRow &row = snap.moves[i];
            const u8 type = row.type < Theme::TypeCount ? row.type : 0;
            const Rect b = { area.x + 6, top + i * 36, area.w - 12, 34 };
            const bool on = i == menu.Cursor();
            Theme::DrawMoveButton(canvas, b, type, on);

            // The effectiveness at the right of the name line (the game puts
            // it under the PP; there is no room for both here).
            const char *effect = EffectLabel(row.effect);
            s32 effectW = 0;
            if (effect[0] != '\0')
            {
                effectW = Theme::MeasureText(effect, Theme::TextSize::Small) + 4;
                if (effectW > b.w / 2)
                    effectW = b.w / 2;
                CompactText(canvas, effect, Rect{ b.Right() - effectW - 6, b.y + 1, effectW, 15 },
                            row.effect == 2 ? Theme::OptionGlint : Theme::WindowText, Align::Right);
            }
            std::string label = row.name.empty() ? Format("Move %u", row.move) : row.name;
            if (on && menu.Pending())
                label += "...";
            CompactText(canvas, label, Rect{ b.x + 22, b.y + 1, b.w - 32 - effectW, 15 }, Theme::WindowText);

            const std::string typeName = Theme::Upper(row.typeName.empty() ? TypeName(type) : row.typeName);
            const Rect plate = { b.x + 14, b.y + 17, Theme::TypePlateW + 6, Theme::TypePlateH + 2 };
            Theme::DrawTypePlate(canvas, plate, type, "");
            CompactText(canvas, typeName, Inset(plate, 3, 0, 3, 0), Theme::WindowText, Align::Center);
            CompactText(canvas, Format("PP %u / %u", row.pp, row.maxPp),
                        Rect{ b.x + 78, b.y + 17, b.w - 84, 16 }, Theme::PpTextColor(row.pp, row.maxPp));
            if (on)
                Theme::DrawCursor(canvas, b.x + 12, b.y + 26);
        }
        DrawMoveDetails(canvas, menu, snap, area);
        const Rect foot = { area.x + 4, area.Bottom() - 24, area.w - 8, Theme::PromptHeight + 2 };
        if (!Notice(canvas, foot, menu.Note()))
            CompactText(canvas, "A Use  B Back  L Info", foot, Theme::WindowText);
    }

    // --- Pokémon list ---

    // Compact regions keep every label readable even without the game font.
    // Prefer the game's small text; the 5x7 fallback is for narrow/short boxes.
    static void CompactText(Canvas &canvas, const std::string &text, const Rect &box,
                            Color color, Align align)
    {
        std::string shown = text;
        if (!Theme::UsingGameFont())
        {
            // The bitmap fallback is ASCII; keep Pokemon readable there.
            size_t dash;
            while ((dash = shown.find("\xE2\x80\x94")) != std::string::npos)
                shown.replace(dash, 3, "-");
            size_t accent;
            while ((accent = shown.find("\xC3\xA9")) != std::string::npos)
                shown.replace(accent, 2, "e");
        }
        if (Theme::MeasureText(shown, Theme::TextSize::Small) <= box.w
            && Theme::LineHeight(Theme::TextSize::Small) <= box.h)
        {
            Theme::TextStyle style = White(Theme::TextSize::Small, align);
            style.color = color;
            if (color.r || color.g || color.b)
                style.outline = Theme::WindowLine;
            Theme::DrawText(canvas, shown, box, style);
        }
        else
        {
            // The measured game text may also need the ASCII fallback.
            size_t dash;
            while ((dash = shown.find("\xE2\x80\x94")) != std::string::npos)
                shown.replace(dash, 3, "-");
            UI::TextStyle style;
            style.font = Font::Small;
            style.color = color;
            style.align = align;
            UI::DrawText(canvas, shown, box, style);
        }
    }

    // Reserve 30 px above the existing prompts. Both text lines use the
    // measured game Small font when it fits, otherwise CompactText's 5x7.
    static void DrawMoveDetails(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap, const Rect &area)
    {
        const MoveRow empty;
        const MoveRow &row = menu.Cursor() < snap.moveCount && menu.Cursor() < 4 ? snap.moves[menu.Cursor()] : empty;
        const Rect strip = { area.x + 6, area.Bottom() - 54, area.w - 12, 30 };
        if (row.category < 3)
            Theme::DrawCategoryIcon(canvas, strip.x, strip.y + 5, row.category);
        const Rect text = { strip.x + 46, strip.y, strip.w - 46, 15 };
        CompactText(canvas, MoveDetailsText(row), text, Theme::WindowText);
        CompactText(canvas, BattleCategoryName(row.category), Rect{ text.x, text.y + 15, text.w, 15 }, Theme::WindowText);
    }

    // Slants lean right at the top, with both tips inside r. These are
    // opaque shapes on the window: a Layer presents its entire rectangle.
    static void Slant(Canvas &canvas, const Rect &r, Color color)
    {
        const s32 skew = 8;
        canvas.FillRect(Rect{ r.x + skew, r.y, r.w - 2 * skew, r.h }, color);
        const float x = (float)r.x, y = (float)r.y, right = (float)r.Right(), bottom = (float)r.Bottom();
        canvas.FillTriangle(x, bottom, x + skew, y, x + skew, bottom, color);
        canvas.FillTriangle(right - skew, y, right, y, right - skew, bottom, color);
    }

    static Color CommandColor(u8 index)
    {
        static constexpr Color colors[] = { Theme::BattleFight, Theme::BattleBag,
                                            Theme::BattlePokemon, Theme::BattleRun };
        return colors[index];
    }

    static void DrawNewCommand(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = menu.Area();
        const bool bar = menu.Layout() == BattleLayout::Bar;
        Theme::DrawWindow(canvas, area);
        const std::string who = snap.activeName.empty() ? "you" : snap.activeName;
        const std::string caption = menu.Note().empty() ? "What will " + who + " do?" : menu.Note();
        CompactText(canvas, caption, Rect{ area.x + 6, area.y + 3, area.w - 12, 17 }, Theme::WindowText);
        const s32 widths[] = { 48, 40, 80, 44 };
        s32 x = area.x + 3;
        for (u8 i = 0; i < 4; i++)
        {
            const bool on = i == menu.Cursor();
            Rect pill = bar ? Rect{ x, 204, widths[i], 29 }
                            : Rect{ area.x + 20 - 5 * i, area.y + 28 + 33 * i, area.w - 24, 30 };
            if (bar)
            {
                canvas.FillRoundRect(pill, 12, Theme::OptionLine);
                canvas.FillRoundRect(Inset(pill, 2), 10, CommandColor(i));
                if (on)
                    Theme::DrawHighlightFrame(canvas, pill, 12);
            }
            else
            {
                Slant(canvas, pill, on ? Theme::Highlight : Theme::OptionLine);
                Slant(canvas, Inset(pill, 2), Theme::OptionTop);
                Slant(canvas, Rect{ pill.x + 3, pill.y + 3, 26, pill.h - 6 }, CommandColor(i));
                if (on)
                    Theme::DrawCursor(canvas, pill.x + 18, pill.y + pill.h / 2);
            }
            std::string label = CommandLabel((BattleCommand)i);
            if (on && menu.Pending())
                label += "...";
            CompactText(canvas, label, Rect{ pill.x + (bar ? 3 : 31), pill.y + (bar ? 7 : 4),
                        pill.w - (bar ? 6 : 43), bar ? 15 : pill.h - 8 }, Theme::OptionText, Align::Center);
            x += widths[i] + 3;
        }
        if (!bar)
        {
            const bool markers = snap.activeShiny || snap.activeHiddenAbility || snap.foeShiny || snap.foeHiddenAbility;
            CompactText(canvas, "A Choose", Rect{ area.x + 12, area.Bottom() - 20,
                        markers ? area.w - 110 : area.w - 24, 16 }, Theme::WindowText);
        }
    }

    static void EffectMarker(Canvas &canvas, const Rect &r, u8 effect)
    {
        const float x = (float)r.x, y = (float)r.y, right = (float)r.Right(), bottom = (float)r.Bottom();
        if (effect == 2)
            canvas.FillTriangle(x, bottom, (x + right) / 2, y, right, bottom, Theme::OptionGlint);
        else if (effect == 3)
            canvas.FillTriangle(x, y, right, y, (x + right) / 2, bottom, Theme::WindowText);
        else if (effect == 0)
        {
            canvas.Line(x + 1, y + 1, right - 1, bottom - 1, 2, Theme::WindowText);
            canvas.Line(right - 1, y + 1, x + 1, bottom - 1, 2, Theme::WindowText);
        }
    }

    static void DrawNewMoves(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = menu.Area();
        const bool bar = menu.Layout() == BattleLayout::Bar;
        Theme::DrawWindow(canvas, area);
        for (u8 i = 0; i < snap.moveCount && i < 4; i++)
        {
            const MoveRow &row = snap.moves[i];
            const u8 type = row.type < Theme::TypeCount ? row.type : 0;
            const Theme::TypeColors colors = Theme::TypeColor(type);
            const bool on = i == menu.Cursor();
            const Rect b = bar ? Rect{ area.x + 4 + (i % 2) * 110, area.y + 4 + (i / 2) * 54, 108, 52 }
                               : Rect{ area.x + 14 - i * 3, area.y + 4 + i * 35, area.w - 18, 35 };
            if (bar)
            {
                Theme::DrawMoveButton(canvas, b, type, false);
                if (on)
                    Theme::DrawHighlightFrame(canvas, b, 6);
            }
            else
            {
                Slant(canvas, b, on ? Theme::Highlight : colors.lineDark);
                Slant(canvas, Inset(b, 2), colors.dark);
                Slant(canvas, Rect{ b.x + 3, b.y + 2, b.w - 6, 19 }, colors.light);
                if (on)
                    Theme::DrawCursor(canvas, b.x + 12, b.y + 12);
            }
            const s32 inset = bar ? 5 : 24;
            std::string label = row.name.empty() ? Format("Move %u", row.move) : row.name;
            if (on && menu.Pending())
                label += "...";
            CompactText(canvas, label, Rect{ b.x + inset, b.y + 1, b.w - inset - 6, 15 }, Theme::WindowText);
            const std::string typeName = Theme::Upper(row.typeName.empty() ? TypeName(type) : row.typeName);
            const Rect plate = { b.x + inset, b.y + (bar ? 19 : 16), 58, bar ? 14 : 11 };
            Theme::DrawTypePlate(canvas, plate, type, "");
            CompactText(canvas, typeName, Inset(plate, 3, 0, 3, 0), Theme::WindowText, Align::Center);
            const Rect pp = bar ? Rect{ b.x + 5, b.y + 35, b.w - 24, 13 }
                                : Rect{ b.x + 88, b.y + 16, b.w - 100, 11 };
            CompactText(canvas, Format("PP %u/%u", row.pp, row.maxPp), pp, Theme::PpTextColor(row.pp, row.maxPp));
            if (bar)
                EffectMarker(canvas, Rect{ b.Right() - 16, b.y + 36, 10, 10 }, row.effect);
            else
                CompactText(canvas, EffectLabel(row.effect), Rect{ b.x + inset, b.y + 27, b.w - inset - 12, 8 },
                            row.effect == 2 ? Theme::OptionGlint : Theme::WindowText);
        }
        DrawMoveDetails(canvas, menu, snap, area);
        const Rect foot = { area.x + 5, area.Bottom() - 19, area.w - 10, 13 };
        if (menu.Note().empty())
            CompactText(canvas, "A Use  B Back  L Info", foot, Theme::WindowText);
        else
            CompactText(canvas, menu.Note(), foot, Theme::WindowText);
    }

    // --- Pok?mon list ---

    static void DrawPartyPage(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        PartyView view;
        for (u8 i = 0; i < snap.partyCount && i < 6; i++)
        {
            const BattlePartyMon &m = snap.party[i];
            PartyMonView mon;
            mon.valid = true;
            mon.egg = m.egg;
            mon.shiny = m.shiny;
            mon.hiddenAbility = m.hiddenAbility;
            mon.name = m.name;
            mon.species = m.species;
            mon.level = m.level;
            mon.hp = m.hp;
            mon.maxHp = m.maxHp;
            mon.status = m.status;
            mon.item = m.item;
            view.mons.push_back(mon);
        }
        view.page = PartyPage::List;
        view.cursor = menu.Cursor();
        view.busy = menu.BusyText();
        if (!menu.Note().empty())
            view.note = menu.Note();
        else if (menu.Forced())
            view.note = "Choose a Pok\xC3\xA9mon to send out   A Choose";
        else
            view.note = "A Switch   B Back";
        DrawParty(canvas, view);

        if (menu.PopupOpen() && menu.Cursor() < snap.partyCount)
        {
            const Rect win = { PartyArea.Right() - 182, PartyArea.y + 52, 170, 104 };
            Theme::DrawWindow(canvas, win);
            Theme::DrawText(canvas, snap.party[menu.Cursor()].name,
                            Rect{ win.x + Theme::WindowPad, win.y + 4, win.w - 2 * Theme::WindowPad, 19 }, White());
            static const char *const entries[] = { "Switch", "Cancel" };
            for (u8 i = 0; i < 2; i++)
                Theme::DrawRow(canvas, Rect{ win.x + 8, win.y + 30 + i * 34, win.w - 16, Theme::OptionHeight + 2 },
                               Theme::RowKind::Option, entries[i], i == menu.PopupCursor());
        }
    }

    // --- Bag ---

    static void DrawBagPage(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = BagArea;
        canvas.FillRoundRect(area, 12, Rgb(0xFA, 0xB8, 0x13));
        canvas.StrokeRoundRect(area, 12, Theme::WindowLine, 2);

        // Pocket tabs along the top; the open one is a window, the others
        // light pills.
        static const char *const shortNames[] = { "Rotom", "HP/PP", "Status", "Balls", "Battle" };
        const s32 tabW = (area.w - 16) / BattlePocketCount;
        const s32 want = menu.WantPocket();
        for (u8 p = 0; p < BattlePocketCount; p++)
        {
            const Rect tab = { area.x + 8 + p * tabW, area.y + 6, tabW - 4, 24 };
            const bool open = p == snap.bagPocket;
            if (open)
            {
                Theme::DrawWindow(canvas, tab);
                Theme::DrawText(canvas, shortNames[p], Inset(tab, 2, 0, 2, 0), White(Theme::TextSize::Small, Align::Center));
            }
            else
            {
                Theme::DrawRowBackground(canvas, tab, Theme::RowKind::Option);
                Theme::TextStyle dark;
                dark.size = Theme::TextSize::Small;
                dark.align = Align::Center;
                dark.color = Theme::OptionText;
                Theme::DrawText(canvas, shortNames[p], Inset(tab, 2, 0, 2, 2), dark);
            }
            if ((s32)p == want)
                Theme::DrawHighlightFrame(canvas, tab, 6);
        }

        // The game's question while it asks for a Pokémon, else the pocket's
        // name.
        const Rect title = { area.x + 8, area.y + 34, area.w - 16, 20 };
        Theme::DrawWindow(canvas, title);
        Theme::DrawText(canvas, snap.bagPick ? std::string("Which Pok\xC3\xA9mon do you want to use this item on?")
                                             : std::string(BattlePocketName(snap.bagPocket)),
                        Inset(title, Theme::WindowPad, 0, Theme::WindowPad, 0), White(Theme::TextSize::Small));

        const s32 listY = title.Bottom() + 4;
        if (snap.bagPick)
        {
            // The party column, battle order (as the game's).
            for (u8 i = 0; i < snap.partyCount && i < 6; i++)
            {
                const BattlePartyMon &m = snap.party[i];
                const Rect r = { area.x + 8, listY + i * 24, area.w - 16, 22 };
                Theme::DrawRowBackground(canvas, r, Theme::RowKind::List);
                Theme::TextStyle text;
                text.color = Theme::ListText;
                Theme::DrawText(canvas, m.name, Rect{ r.x + Theme::RowTextInset, r.y + 1, 140, 19 }, text);
                text.size = Theme::TextSize::Small;
                Theme::DrawText(canvas, Format("Lv. %u", m.level), Rect{ r.x + 170, r.y + 2, 50, 17 }, text);
                Theme::DrawHpBar(canvas, Rect{ r.x + 222, r.y + 7, 70, 8 }, m.hp, m.maxHp);
                text.align = Align::Right;
                Theme::DrawText(canvas, Format("%u/%u", m.hp, m.maxHp), Rect{ r.x + 290, r.y + 2, r.w - 296, 17 }, text);
                if (i == menu.PickCursor())
                    Theme::DrawCursor(canvas, r.x + 14, r.y + 10);
            }
        }
        else
        {
            for (u8 row = 0; row < BattleMenu::BagRows; row++)
            {
                const u8 i = (u8)(menu.BagTop() + row);
                if (i >= snap.bagItemCount)
                    break;
                const BattleBagItem &item = snap.bagItems[i];
                const Rect r = { area.x + 8, listY + row * Theme::RowPitch, area.w - 16, Theme::RowHeight };
                Theme::DrawRowBackground(canvas, r, Theme::RowKind::List);
                s32 textX = r.x + Theme::RowTextInset;
                if (IsBallItem(item.id) && Theme::DrawBall(canvas, r.x + 22, r.y - 3, item.id))
                    textX += 34;
                Theme::TextStyle text;
                text.color = Theme::ListText;
                Theme::DrawText(canvas, item.name.empty() ? Format("Item %u", item.id) : item.name,
                                Rect{ textX, r.y + 2, r.Right() - 70 - textX, 19 }, text);
                text.align = Align::Right;
                Theme::DrawText(canvas, Format("\xC3\x97 %u", item.count), Rect{ r.Right() - 70, r.y + 2, 60, 19 }, text);
                if (i == menu.BagCursor())
                    Theme::DrawCursor(canvas, r.x + 14, r.y + (r.h - 2) / 2);
            }
            if (snap.bagItemCount == 0)
                Theme::DrawText(canvas, "No items in this pocket", Rect{ area.x + 8, listY + 4, area.w - 16, 19 },
                                White(Theme::TextSize::Body, Align::Center));
        }

        const Rect foot = { area.x + 8, area.Bottom() - Theme::PromptHeight - 6, area.w - 16, Theme::PromptHeight + 2 };
        if (!Notice(canvas, foot, Footer(menu)))
        {
            if (snap.bagPick)
                Prompts(canvas, foot.x, foot.y + 1, { { Theme::Button::A, "Use on it" }, { Theme::Button::B, "Back" } });
            else
                Prompts(canvas, foot.x, foot.y + 1, { { Theme::Button::A, "Use" }, { Theme::Button::B, "Back" },
                                                      { Theme::Button::L, "" }, { Theme::Button::R, "Pocket" } });
        }
    }

    // --- The battle's question ---

    static void DrawQuestion(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &)
    {
        const Rect area = QuestionArea;
        Theme::DrawWindow(canvas, area);
        const Rect head = { area.x, area.y + 2, area.w, 26 };
        Theme::DrawText(canvas, "Use next Pok\xC3\xA9mon?", Inset(head, Theme::WindowPad, 2, Theme::WindowPad, 2), White());
        static const char *const entries[] = { "Use Next Pok\xC3\xA9mon", "Run" };
        for (u8 i = 0; i < 2; i++)
            Theme::DrawRow(canvas, Rect{ area.x + 8, head.Bottom() + 4 + i * 32, area.w - 16, Theme::OptionHeight + 2 },
                           Theme::RowKind::Option, entries[i], i == menu.Cursor());
        const Rect foot = { area.x + 4, area.Bottom() - Theme::PromptHeight - 6, area.w - 8, Theme::PromptHeight + 2 };
        if (!Notice(canvas, foot, Footer(menu)))
            Prompts(canvas, area.x + 8, foot.y + 1, { { Theme::Button::A, "Choose" } });
    }

    // --- The target choice ---

    static void DrawTarget(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = MovesArea;
        Theme::DrawWindow(canvas, area);
        const Rect head = { area.x, area.y + 2, area.w, 26 };
        Theme::DrawText(canvas, "Choose a target", Inset(head, Theme::WindowPad, 2, Theme::WindowPad, 2), White());
        Theme::DrawText(canvas, "As on the bottom screen (" + snap.panelKey + ")",
                        Rect{ area.x + Theme::WindowPad, head.Bottom(), area.w - 2 * Theme::WindowPad, 16 },
                        White(Theme::TextSize::Small));
        const s32 cellW = (area.w - 24) / 2;
        for (u8 i = 0; i < TargetCount; i++)
        {
            const Rect cell = { area.x + 8 + (i % 2) * (cellW + 8), head.Bottom() + 22 + (i / 2) * 56, cellW, 48 };
            Theme::DrawRow(canvas, cell, Theme::RowKind::Option, TargetLabel(i), i == menu.Cursor());
        }
        const Rect foot = { area.x + 4, area.Bottom() - Theme::PromptHeight - 6, area.w - 8, Theme::PromptHeight + 2 };
        if (!Notice(canvas, foot, Footer(menu)))
            Prompts(canvas, foot.x, foot.y + 1, { { Theme::Button::A, "Choose" }, { Theme::Button::B, "Back" } });
    }

    // --- Helpers for the game's own screens ---

    static void DrawGame(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        const Rect area = menu.Area();
        Theme::DrawWindow(canvas, area);
        Stack body(Inset(area, Theme::WindowPad, 4, Theme::WindowPad, 4), Stack::Column, 0);
        const Rect title = body.Next(19);
        const Rect line = body.Next(19);
        const Rect hint1 = body.Next(15);
        const Rect hint2 = body.Next(15);
        Theme::TextStyle accent = White();
        accent.color = Theme::AccentText;
        const Theme::TextStyle small = White(Theme::TextSize::Small);
        if (snap.afterCatch)
        {
            Theme::DrawText(canvas, "Caught!", title, White());
            // A new species gets its Pokédex page first; then the nickname
            // question, where A opens a keyboard on the bottom screen.
            Theme::DrawText(canvas, "B: next, no nickname", line, accent);
            Theme::DrawText(canvas, "Nickname? A: yes (keyboard below)", hint1, small);
            Theme::DrawText(canvas, snap.panelKey + ": show the bottom screen", hint2, small);
        }
        else if (menu.Prompt())
        {
            Theme::DrawText(canvas, "The game asks", title, White());
            Theme::DrawText(canvas, "The choices are below", line, accent);
            Theme::DrawText(canvas, "Up/Down + A: choose   B: back", hint1, small);
            Theme::DrawText(canvas, snap.panelKey + ": show the bottom screen", hint2, small);
        }
        else
        {
            Theme::DrawText(canvas, "Game screen", title, White());
            Theme::DrawText(canvas, "Buttons go to the game", line, accent);
            Theme::DrawText(canvas, "D-pad: move   A: choose   B: back", hint1, small);
            Theme::DrawText(canvas, snap.panelKey + ": show the bottom screen", hint2, small);
        }
    }

    Rect BattleMenu::Area(void) const
    {
        switch (m_page)
        {
        case BattlePage::Command:  return m_layout == BattleLayout::Bar ? BarCommandArea
                                        : m_layout == BattleLayout::Rail ? RailCommandArea : CommandArea;
        case BattlePage::Moves:    return m_layout == BattleLayout::Bar ? BarMovesArea
                                        : m_layout == BattleLayout::Rail ? RailMovesArea : MovesArea;
        case BattlePage::Party:    return PartyArea;
        case BattlePage::Bag:      return BagArea;
        case BattlePage::Question: return QuestionArea;
        case BattlePage::Target:   return MovesArea;
        case BattlePage::Game:     return m_afterCatch || Prompt() ? CatchArea : GameArea;
        default:                   return Rect{ 0, 0, 0, 0 };
        }
    }

    s32 BattleMenu::Radius(void) const
    {
        return m_page == BattlePage::Party ? PartyRadius : m_page == BattlePage::Bag ? 12 : Theme::WindowRadius;
    }

    static void DrawBattlerMarkers(Canvas &canvas, const BattleMenu &menu, const BattleSnapshot &snap)
    {
        if (!menu.Note().empty()) return; // never draw over a refusal/status note
        const Rect area = menu.Area();
        const s32 y = menu.Layout() == BattleLayout::Bar ? 187 : area.Bottom() - 17;
        s32 x = area.Right() - 94;
        for (u8 side = 0; side < 2; side++, x += 44)
        {
            const bool shiny = side == 0 ? snap.activeShiny : snap.foeShiny;
            const bool ha = side == 0 ? snap.activeHiddenAbility : snap.foeHiddenAbility;
            if (!shiny && !ha) continue;
            CompactText(canvas, side == 0 ? "P" : "F", Rect{ x, y, 8, 10 }, Theme::WindowText);
            if (shiny) Theme::DrawShiny(canvas, x + 9, y);
            if (ha)
            {
                const Rect pill = { x + 22, y, 20, 10 };
                canvas.FillRoundRect(pill, 3, Theme::AccentText);
                CompactText(canvas, "HA", pill, Theme::WindowLine, Align::Center);
            }
        }
    }

    void BattleMenu::Draw(Canvas &canvas, const BattleSnapshot &snap) const
    {
        if (m_page == BattlePage::Moves && m_description)
        {
            Theme::DrawWindow(canvas, Area());
            const MoveRow empty;
            const MoveRow &row = m_cursor < snap.moveCount && m_cursor < 4 ? snap.moves[m_cursor] : empty;
            const Rect body = BattleDescriptionArea(m_layout);
            CompactText(canvas, row.name, Rect{ body.x, Area().y + 5, body.w, 16 }, Theme::WindowText);
            UI::TextStyle text;
            text.font = Font::Small;
            text.color = Theme::WindowText;
            const auto lines = WrapText(row.description.empty() ? "Description unavailable" : row.description, body.w, text);
            const s32 pitch = LineHeight(text) + 2;
            for (size_t i = 0; i < lines.size() && (s32)(i + 1) * pitch <= body.h; i++)
                UI::DrawText(canvas, lines[i], Rect{ body.x, body.y + (s32)i * pitch, body.w, pitch }, text);
            CompactText(canvas, "L/B Close  Up/Down Move", Rect{ body.x, Area().Bottom() - 20, body.w, 14 }, Theme::WindowText);
            return;
        }
        switch (m_page)
        {
        case BattlePage::Command:
            if (m_layout == BattleLayout::Classic) DrawCommand(canvas, *this, snap);
            else DrawNewCommand(canvas, *this, snap);
            DrawBattlerMarkers(canvas, *this, snap);
            break;
        case BattlePage::Moves:
            if (m_layout == BattleLayout::Classic) DrawMoves(canvas, *this, snap);
            else DrawNewMoves(canvas, *this, snap);
            break;
        case BattlePage::Party:    DrawPartyPage(canvas, *this, snap); break;
        case BattlePage::Bag:      DrawBagPage(canvas, *this, snap); break;
        case BattlePage::Question: DrawQuestion(canvas, *this, snap); break;
        case BattlePage::Target:   DrawTarget(canvas, *this, snap); break;
        case BattlePage::Game:     DrawGame(canvas, *this, snap); break;
        default: break;
        }
    }

    std::string BattleMenu::DrawKey(const BattleSnapshot &snap) const
    {
        std::string key = Format("%u %u", (unsigned)m_page, m_cursor) + (m_pending ? "p" : "") + "|" + m_note + "|"
                          + m_busyText;
        key += Format("|layout %u", (unsigned)m_layout) + (m_description ? "|description" : "");
        key += Format("|markers %u %u", snap.activeShiny, snap.activeHiddenAbility)
               + Format(" %u %u", snap.foeShiny, snap.foeHiddenAbility);
        switch (m_page)
        {
        case BattlePage::Command:
            key += "|" + snap.activeName;
            break;
        case BattlePage::Moves:
            for (u8 i = 0; i < snap.moveCount; i++)
            {
                const MoveRow &row = snap.moves[i];
                key += "|" + row.name + row.typeName + row.description + Format(" %u %u", row.move, row.type)
                       + Format(" %u %u", row.effect, row.pp) + Format(" %u", row.maxPp)
                       + Format(" %u %u", row.power, row.accuracy) + Format(" %u", row.category);
            }
            break;
        case BattlePage::Party:
        case BattlePage::Bag:
            key += Format("|%u %u", m_popup ? 1u + m_popupCursor : 0u, m_forced ? 1u : 0u)
                   + Format(" %u %u", m_bagCursor, m_bagTop) + Format(" %u %u", (unsigned)(m_wantPocket + 1), m_pickCursor)
                   + Format(" %u %u", snap.bagPocket, snap.bagPick ? 1u : 0u);
            for (u8 i = 0; i < snap.partyCount; i++)
            {
                const BattlePartyMon &m = snap.party[i];
                key += "|" + m.name + m.item + Format(" %u %u", m.level, m.hp) + Format(" %u %u", m.maxHp, m.status)
                       + Format(" %u %u", m.shiny, m.hiddenAbility);
            }
            if (m_page == BattlePage::Bag)
                for (u8 i = 0; i < snap.bagItemCount; i++)
                    key += "|" + snap.bagItems[i].name + Format(" %u %u", snap.bagItems[i].id, snap.bagItems[i].count);
            break;
        case BattlePage::Game:
            key += (m_afterCatch ? "c" : "") + Format("|%u %u", (unsigned)snap.menu, Prompt() ? 1u : 0u) + snap.panelKey;
            break;
        default:
            break;
        }
        return key;
    }
}
}
