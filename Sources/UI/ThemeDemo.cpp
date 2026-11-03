#include "UI/ThemeDemo.hpp"

#include <cstdio>

#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
namespace UI
{
    ThemeDemoData ThemeDemoSample(void)
    {
        // Placeholder words, not game text.
        ThemeDemoData d;
        const struct { const char *name, *type; u8 typeId, category, pp, maxPp; } moves[4] = {
            { "Sample Strike", "Normal", 0, 1, 35, 35 },
            { "Ember Bite", "Fire", 9, 1, 7, 15 },
            { "Twin Kick", "Fighting", 1, 1, 3, 30 },
            { "Shade Lick", "Ghost", 7, 2, 0, 30 },
        };
        for (s32 i = 0; i < 4; i++)
        {
            d.moves[i].name = moves[i].name;
            d.moves[i].typeName = moves[i].type;
            d.moves[i].type = moves[i].typeId;
            d.moves[i].category = moves[i].category;
            d.moves[i].pp = moves[i].pp;
            d.moves[i].maxPp = moves[i].maxPp;
        }
        d.moveCount = 4;
        const struct { const char *name; u8 level; u32 hp, maxHp; } party[6] = {
            { "Alpha", 17, 51, 51 }, { "Bravo", 14, 20, 35 }, { "Charlie", 13, 6, 35 },
            { "Delta", 12, 0, 27 },  { "Echo", 13, 37, 37 },  { "Foxtrot", 12, 30, 37 },
        };
        for (s32 i = 0; i < 6; i++)
        {
            d.party[i].name = party[i].name;
            d.party[i].level = party[i].level;
            d.party[i].hp = party[i].hp;
            d.party[i].maxHp = party[i].maxHp;
        }
        d.partyCount = 6;
        return d;
    }

    static void Backdrop(Canvas &canvas, const Rect &area, Color top, Color bottom)
    {
        for (s32 y = 0; y < area.h; y++)
        {
            const s32 t = area.h > 1 ? y * 255 / (area.h - 1) : 0;
            canvas.HLine(area.x, area.y + y, area.w,
                         Rgb((u8)(top.r + (bottom.r - top.r) * t / 255), (u8)(top.g + (bottom.g - top.g) * t / 255),
                             (u8)(top.b + (bottom.b - top.b) * t / 255)));
        }
    }

    static std::string Format(const char *fmt, u32 a, u32 b)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, (unsigned)a, (unsigned)b);
        return buf;
    }

    static void MovePage(Canvas &canvas, const ThemeDemoData &data, s32 selected)
    {
        const Rect area = ThemeDemoArea;
        Backdrop(canvas, area, Rgb(0x1F, 0x29, 0x47), Rgb(0x50, 0x4A, 0x42));

        // The lead Pokémon in a message window, left.
        const Rect win{ area.x + 4, area.y + 24, 140, 70 };
        Theme::DrawWindow(canvas, win);
        Theme::TextStyle white;
        white.color = Theme::WindowText;
        if (data.partyCount > 0)
        {
            const ThemeDemoMon &mon = data.party[0];
            Theme::DrawText(canvas, mon.name, Rect{ win.x + 10, win.y + 7, win.w - 20, 19 }, white);
            Theme::TextStyle small = white;
            small.size = Theme::TextSize::Small;
            Theme::DrawText(canvas, Format("Lv.%u", mon.level, 0), Rect{ win.x + 10, win.y + 27, 40, 15 }, small);
            small.align = Align::Right;
            Theme::DrawText(canvas, Format("%u/%u", mon.hp, mon.maxHp), Rect{ win.x + 60, win.y + 27, win.w - 70, 15 }, small);
            Theme::DrawHpBar(canvas, Rect{ win.x + 10, win.y + 46, win.w - 20, Theme::GaugeHeight + 2 }, mon.hp, mon.maxHp);
        }
        // Balls and gender signs, left under the window.
        s32 bx = area.x + 6;
        for (u16 ball = 4; ball >= 1; ball--, bx += 34)
            if (!Theme::DrawBall(canvas, bx, area.y + 100, ball))
                break;
        Theme::DrawGender(canvas, area.x + 10, area.y + 140, false);
        Theme::DrawGender(canvas, area.x + 28, area.y + 140, true);
        for (u8 c = 0; c < 3; c++)
            Theme::DrawCategoryIcon(canvas, area.x + 4 + c * 46, area.y + 160, c);

        // Header strip, like the game's "L+A Move info".
        canvas.FillRect(Rect{ area.x + 150, area.y, area.w - 150, 20 }, Rgb(0, 0, 0, 0x90));
        Theme::DrawButtonPrompt(canvas, area.x + 152, area.y, Theme::Button::L, "+");
        Theme::DrawButtonPrompt(canvas, area.x + 152 + Theme::PromptWidth(Theme::Button::L, "+"), area.y,
                                Theme::Button::A, "Move info");

        // Four move buttons, right, as in the game's move list.
        const s32 count = data.moveCount > 0 ? data.moveCount : 1;
        const s32 cursor = ((selected % count) + count) % count;
        for (s32 i = 0; i < data.moveCount; i++)
        {
            const ThemeDemoMove &m = data.moves[i];
            const Rect b{ area.x + 150, area.y + 24 + i * 50, area.w - 152, 46 };
            const Theme::TypeColors c = Theme::TypeColor(m.type);
            Theme::DrawMoveButton(canvas, b, m.type, i == cursor);
            Theme::TextStyle name = white;
            name.outline = c.lineDark;
            Theme::DrawText(canvas, m.name, Rect{ b.x + 22, b.y + 3, b.w - 26, 19 }, name);
            Theme::DrawTypePlate(canvas, Rect{ b.x + 14, b.y + 26, Theme::TypePlateW + 6, Theme::TypePlateH + 2 }, m.type,
                                 m.typeName);
            Theme::TextStyle pp = white;
            pp.color = Theme::PpTextColor(m.pp, m.maxPp);
            pp.outline = c.lineDark;
            Theme::DrawText(canvas, Format("PP %u / %u", m.pp, m.maxPp), Rect{ b.x + 76, b.y + 24, b.w - 80, 19 }, pp);
            if (i == cursor)
                Theme::DrawCursor(canvas, b.x + 12, b.y + 34);
        }

        Theme::DrawButtonPrompt(canvas, area.x + 4, area.y + area.h - 24, Theme::Button::B, "Back");
        Theme::DrawButtonPrompt(canvas, area.x + 4 + Theme::PromptWidth(Theme::Button::B, "Back") + 4, area.y + area.h - 24,
                                Theme::Button::A, "Use");
    }

    static void ListPage(Canvas &canvas, const ThemeDemoData &data, s32 selected)
    {
        const Rect area = ThemeDemoArea;
        Backdrop(canvas, area, Rgb(0xFF, 0xE0, 0x7A), Rgb(0xFA, 0xB8, 0x13));

        // Question window on top.
        const Rect win{ area.x + 4, area.y + 4, area.w - 8, 44 };
        Theme::DrawWindow(canvas, win);
        Theme::TextStyle white;
        white.color = Theme::WindowText;
        const std::string who = data.partyCount > 0 ? data.party[((selected % data.partyCount) + data.partyCount) % data.partyCount].name
                                                    : std::string("?");
        const s32 w = Theme::DrawText(canvas, "Choose an option for ", Rect{ win.x + 10, win.y + 3, win.w - 20, 19 }, white);
        Theme::TextStyle accent = white;
        accent.color = Theme::AccentText;
        Theme::DrawText(canvas, who, Rect{ win.x + 10 + w, win.y + 3, win.w - 20 - w, 19 }, accent);
        Theme::DrawText(canvas, "Text in the game's own font.", Rect{ win.x + 10, win.y + 22, win.w - 20, 19 }, white);

        // Party rows, left (Bag style).
        const s32 n = data.partyCount;
        const s32 cursor = n > 0 ? ((selected % n) + n) % n : 0;
        for (s32 i = 0; i < n; i++)
        {
            const ThemeDemoMon &mon = data.party[i];
            const Rect row{ area.x + 4, area.y + 52 + i * Theme::RowPitch, 180, Theme::RowHeight };
            Theme::DrawRow(canvas, row, Theme::RowKind::List, mon.name, i == cursor);
            if (i == cursor)
                Theme::DrawHighlightFrame(canvas, row, Theme::RowRadius);
            Theme::TextStyle small;
            small.size = Theme::TextSize::Small;
            small.align = Align::Right;
            Theme::DrawText(canvas, Format("Lv.%u", mon.level, 0), Rect{ row.x + 100, row.y + 2, 36, 22 }, small);
            Theme::DrawHpBar(canvas, Rect{ row.x + 140, row.y + 10, 30, Theme::GaugeHeight }, mon.hp, mon.maxHp);
        }

        // Options, right (Party menu style).
        static const char *options[4] = { "Summary", "Swap", "Item", "Back" };
        for (s32 i = 0; i < 4; i++)
            Theme::DrawRow(canvas, Rect{ area.x + 190, area.y + 52 + i * 32, 126, 30 }, Theme::RowKind::Option, options[i],
                           i == (cursor % 4));

        // PP bars at each level, and prompts.
        static const u32 pps[4] = { 30, 12, 5, 0 };
        for (s32 i = 0; i < 4; i++)
            Theme::DrawPpBar(canvas, Rect{ area.x + 194 + i * 31, area.y + 186, 28, Theme::GaugeHeight + 2 }, pps[i], 30);
        s32 x = area.x + 4;
        x += Theme::DrawButtonPrompt(canvas, x, area.y + area.h - 24, Theme::Button::A, "OK") + 4;
        x += Theme::DrawButtonPrompt(canvas, x, area.y + area.h - 24, Theme::Button::B, "Back") + 4;
        x += Theme::DrawButtonPrompt(canvas, x, area.y + area.h - 24, Theme::Button::X, "Sort") + 4;
        Theme::DrawButtonPrompt(canvas, x, area.y + area.h - 24, Theme::Button::Y, "Move");
    }

    void DrawThemeDemo(Canvas &canvas, const ThemeDemoData &data, s32 page, s32 selected)
    {
        if (((page % ThemeDemoPages) + ThemeDemoPages) % ThemeDemoPages == 0)
            MovePage(canvas, data, selected);
        else
            ListPage(canvas, data, selected);
    }
}
}
