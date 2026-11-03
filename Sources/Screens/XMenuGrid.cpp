#include "Screens/XMenuGrid.hpp"

#include <algorithm>
#include <cstdlib>

#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using UI::Rgb;

    // HID key bit for Y (same value as CTRPF's Key::Y): Save in the X menu.
    static const u32 XMenuKeyY = 1u << 11;

    const std::vector<XMenuEntry> &XMenuEntries(void)
    {
        // Page 1 then page 2 in the game's slot order, then the two extras.
        // Colours: page 1 and Save measured on the game's pills
        // (docs/VISUAL-STYLE.md), the others picked to match.
        // Scenes from docs/OFFSETS.md; Quick Link and Festival Plaza stay in
        // FieldRo (the X menu just goes away).
        static const std::vector<XMenuEntry> entries =
        {
            { "pokemon",          "Pokémon",  "",         0, 0, "xmenu.pokemon",               SceneParty,          0, 0, 1, Rgb(0, 168, 96) },
            { "pokedex",          "Pokédex",  "",         0, 1, "xmenu.pokedex",               SceneDex,            1, 0, 1, Rgb(232, 44, 64) },
            { "bag",              "Bag",      "",         0, 2, "xmenu.bag",                   SceneBag,            0, 1, 1, Rgb(244, 124, 20) },
            { "quick_link",       "Quick",    "Link",     0, 3, "xmenu.quick_link",            nullptr,             1, 1, 1, Rgb(16, 72, 224) },
            { "festival_plaza",   "Festival", "Plaza",    0, 4, "xmenu.festival_plaza",        nullptr,             0, 2, 1, Rgb(16, 164, 170) },
            { "trainer_passport", "Trainer",  "Passport", 0, 5, "xmenu.trainer_passport",      "TrainerPass",       1, 2, 1, Rgb(24, 104, 232) },
            { "pokemon_refresh",  "Pokémon",  "Refresh",  1, 0, "xmenu_page2.pokemon_refresh", "Kawaigari",         2, 0, 1, Rgb(224, 150, 20) },
            { "qr_scanner",       "QR",       "Scanner",  1, 1, "xmenu_page2.qr_scanner",      "QRReader",          3, 0, 1, Rgb(120, 64, 210) },
            { "battle_video",     "Battle",   "Video",    1, 2, "xmenu_page2.battle_video",    "BattleVideoPlayer", 2, 1, 1, Rgb(20, 128, 224) },
            { "options",          "Options",  "",         1, 3, "xmenu_page2.options",         "Config",            3, 1, 1, Rgb(206, 48, 160) },
            { "map",              "Map",      "",        -1, -1, nullptr,                      SceneTownMap,        0, 3, 2, Rgb(30, 150, 160) },
            { "save",             "Save",     "",        -1, -1, nullptr,                      nullptr,             2, 3, 2, Rgb(17, 182, 255) },
        };
        return entries;
    }

    int XMenuFind(const std::string &name)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        for (size_t i = 0; i < entries.size(); i++)
            if (name == entries[i].name)
                return (int)i;
        return -1;
    }

    int XMenuAtSlot(int page, int slot)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].page == page && entries[i].slot == slot && slot >= 0)
                return (int)i;
        return -1;
    }

    static bool Enabled(unsigned enabled, int i)
    {
        return ((enabled >> i) & 1u) != 0;
    }

    int XMenuMove(int from, int dx, int dy, unsigned enabled)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        if (from < 0 || from >= (int)entries.size())
            return 0;
        const XMenuEntry &f = entries[from];
        // Centres in half columns, so spans stay whole numbers.
        const int fx = 2 * f.col + f.span;
        int best = -1;
        int bestScore = 0;
        for (int i = 0; i < (int)entries.size(); i++)
        {
            if (i == from || !Enabled(enabled, i))
                continue;
            const XMenuEntry &c = entries[i];
            const int cx = 2 * c.col + c.span;
            int score;
            if (dx != 0)
            {
                const int d = (cx - fx) * dx;
                if (c.row != f.row || d <= 0)
                    continue;
                score = d;
            }
            else
            {
                const int d = (c.row - f.row) * dy;
                if (dy == 0 || d <= 0)
                    continue;
                const bool overlap = c.col < f.col + f.span && f.col < c.col + c.span;
                score = (overlap ? 0 : 1000) + d * 100 + std::abs(cx - fx);
            }
            if (best < 0 || score < bestScore)
            {
                best = i;
                bestScore = score;
            }
        }
        return best < 0 ? from : best;
    }

    int XMenuFlipPage(int from, int page, unsigned enabled)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        if (from < 0 || from >= (int)entries.size() || entries[from].page < 0 || entries[from].page == page)
            return from;
        const XMenuEntry &f = entries[from];
        int best = -1, bestScore = 0;
        for (int i = 0; i < (int)entries.size(); i++)
        {
            const XMenuEntry &c = entries[i];
            if (c.page != page || !Enabled(enabled, i))
                continue;
            const int score = std::abs(c.slot / 2 - f.slot / 2) * 10 + std::abs(c.slot % 2 - f.slot % 2);
            if (best < 0 || score < bestScore)
            {
                best = i;
                bestScore = score;
            }
        }
        return best < 0 ? from : best;
    }

    bool XMenuCursor::FromGame(int page, int slot)
    {
        if (page == m_gamePage && slot == m_gameSlot)
            return false;
        m_gamePage = page;
        m_gameSlot = slot;
        const int index = XMenuAtSlot(page, slot);
        if (index < 0 || index == m_index)
            return false;
        m_index = index;
        return true;
    }

    void XMenuCursor::Chosen(int index)
    {
        m_index = index;
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        if (index >= 0 && index < (int)entries.size() && entries[index].page >= 0)
        {
            m_gamePage = entries[index].page;
            m_gameSlot = entries[index].slot;
        }
    }

    bool XMenuOpensNative(int index)
    {
        static const char *const native[] = { "options", "trainer_passport", "battle_video" };
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        if (index < 0 || index >= (int)entries.size())
            return false;
        for (const char *name : native)
            if (std::string(entries[index].name) == name)
                return true;
        return false;
    }

    static std::vector<ActionDef> MakeXMenuActions(void)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        const ActionCond menu = InScene(SceneField).With(WorldXMenu);
        const ActionCond gone = InScene(SceneField).Without(WorldXMenu);

        // Names and summaries must outlive the definitions.
        static std::vector<std::string> names, summaries;
        names.reserve(entries.size());
        summaries.reserve(entries.size());

        std::vector<ActionDef> list;
        for (const XMenuEntry &e : entries)
        {
            names.push_back(std::string("xmenu-") + e.name);
            ActionDef def = { names.back().c_str(), nullptr, menu, {} };
            std::vector<ActionStep> &s = def.steps;
            if (e.page >= 0)
            {
                // Turn the game's page if it is on the other one (it reopens
                // where it was left). The arrow slides the page in: wait for
                // the probe, then a little more, before tapping the entry.
                if (e.page == 0)
                {
                    s.push_back(StepTap("xmenu_page2.prev_page").SkipIf(AnyScene().Without(WorldXMenuPage2)));
                    s.push_back(StepWaitFor(menu.Without(WorldXMenuPage2), 45).Retry(0, 2));
                }
                else
                {
                    s.push_back(StepTap("xmenu.next_page").SkipIf(AnyScene().With(WorldXMenuPage2)));
                    s.push_back(StepWaitFor(menu.With(WorldXMenuPage2), 45).Retry(0, 2));
                }
                s.push_back(StepWait(10));
                s.push_back(StepTap(e.button));
                // Entries that open a scene take up to ~2 s; no re-tap (a
                // second tap would land on the new screen).
                s.push_back(e.opens != nullptr ? StepWaitFor(InScene(e.opens), 240) : StepWaitFor(gone, 120));
                summaries.push_back(std::string("X menu: page ") + (e.page == 0 ? "1" : "2") + ", tap " + e.button);
            }
            else if (std::string(e.name) == "save")
            {
                // Y opens the save dialog ("Would you like to save...?"); the
                // player answers it on the bottom screen.
                s.push_back(StepPress(XMenuKeyY));
                s.push_back(StepWaitFor(gone, 60));
                summaries.push_back("X menu: Y (save dialog)");
            }
            else
            {
                // Map: close the X menu with B (AutoPanel follows B), wait for
                // Rotom's screen, tap the map.
                s.push_back(StepPress(ActionKeyB));
                s.push_back(StepWaitFor(gone, 60));
                s.push_back(StepWait(30));
                s.push_back(StepTap("field.map"));
                s.push_back(StepWaitFor(InScene(SceneTownMap), 150).Retry(3, 1));
                summaries.push_back("B, then tap field.map");
            }
            list.push_back(def);
        }
        for (size_t i = 0; i < list.size(); i++)
            list[i].summary = summaries[i].c_str();
        return list;
    }

    const ActionDef &XMenuAction(int index)
    {
        static const std::vector<ActionDef> actions = MakeXMenuActions();
        return actions[index < 0 || index >= (int)actions.size() ? 0 : index];
    }

    const char *XMenuNativeScreen(int index)
    {
        return index >= 0 && index == XMenuFind("pokedex") ? "pokedex" : nullptr;
    }

    const ActionDef &XMenuCloseAction(void)
    {
        static const ActionDef def = {
            "xmenu-close", "X menu: B (close)", InScene(SceneField).With(WorldXMenu),
            { StepPress(ActionKeyB), StepWaitFor(InScene(SceneField).Without(WorldXMenu), 60) } };
        return def;
    }

    // --- Drawing ---

    // The game's own X menu (docs/VISUAL-STYLE.md, measured on its bottom
    // screen): a yellow sun background, lighter in the middle; entries are
    // coloured pills with a white rim and a dark line; the red arrow cursor
    // marks the selection; Save is a blue bar with the Y button. Text and
    // icons come from UI::Theme, which uses the game's font and icons once
    // they are loaded and its own shapes and the system font before.
    namespace Theme = UI::Theme;

    static constexpr UI::Color SunEdge   = Rgb(0xE2, 0xC9, 0x24);   // the background at the screen's edge
    static constexpr UI::Color SunMid    = Rgb(0xF5, 0xE5, 0x69);
    static constexpr UI::Color SunCentre = Rgb(0xFF, 0xFF, 0xC4);   // around the middle (#FFF7C4 .. #FFFFC7)
    static constexpr UI::Color SunRay    = Rgb(0xFF, 0xF4, 0x8C, 0x70);
    static constexpr UI::Color Frame     = Rgb(0xA0, 0x8B, 0x36);   // the dark gold of the sun's rings
    static constexpr UI::Color PillRim   = Rgb(0xFB, 0xFB, 0xFF);
    static constexpr UI::Color LabelDark = Rgb(0x25, 0x25, 0x25);   // the game's labels under its pills
    static constexpr UI::Color Disabled  = Rgb(0x8A, 0x86, 0x7A);

    // Grid geometry inside XMenuArea: 4 columns (a wider gap between the
    // pages), 3 rows of pills, then the bottom row and the prompts.
    static const int GridX = XMenuArea.x + 10;
    static const int GridY = XMenuArea.y + 10;
    static const int CellW = 82, CellH = 44;
    static const int ColGap = 6, PageGap = 16, RowGap = 6;
    static const int BottomGap = 8, BottomH = 30;
    static const int FooterGap = 6, FooterH = Theme::PromptHeight;
    static const int PillRadius = 14;

    static int ColX(int col)
    {
        return GridX + col * (CellW + ColGap) + (col >= 2 ? PageGap - ColGap : 0);
    }

    UI::Rect XMenuCellRect(int index)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        const XMenuEntry &e = entries[index < 0 || index >= (int)entries.size() ? 0 : index];
        const int x = ColX(e.col);
        const int w = ColX(e.col + e.span - 1) + CellW - x;
        if (e.row < 3)
            return UI::Rect{ x, GridY + e.row * (CellH + RowGap), w, CellH };
        return UI::Rect{ x, GridY + 3 * CellH + 2 * RowGap + BottomGap, w, BottomH };
    }

    static UI::Color Mix(UI::Color a, UI::Color b, int t)
    {
        return Rgb((u8)(a.r + (b.r - a.r) * t / 255), (u8)(a.g + (b.g - a.g) * t / 255),
                   (u8)(a.b + (b.b - a.b) * t / 255), a.a);
    }

    static UI::Color Alpha(UI::Color c, u8 a)
    {
        return Rgb(c.r, c.g, c.b, a);
    }

    // The sun: edge colour to a light middle in rings, with rays between.
    static void DrawBackground(UI::Canvas &canvas)
    {
        const UI::Rect &a = XMenuArea;
        canvas.FillRoundRect(a, XMenuRadius, Frame);
        canvas.FillRoundRect(UI::Inset(a, 2), XMenuRadius - 2, SunEdge);
        canvas.SetClip(UI::Inset(a, 2));
        const int cx = a.x + a.w / 2, cy = a.y + a.h / 2 - 8;
        static const int Rings = 10;
        for (int i = 0; i < Rings; i++)
        {
            const int r = 176 - i * 16;
            const UI::Color c = i < Rings / 2 ? Mix(SunEdge, SunMid, 255 * (i + 1) / (Rings / 2))
                                              : Mix(SunMid, SunCentre, 255 * (i + 1 - Rings / 2) / (Rings / 2));
            canvas.FillRoundRect(UI::Rect{ cx - r, cy - r, 2 * r, 2 * r }, r, c);
        }
        // Twelve rays, as on the game's sun.
        for (int i = 0; i < 12; i += 2)
        {
            static const float Dirs[12][2] = { { 1.f, 0.f }, { .866f, .5f }, { .5f, .866f }, { 0.f, 1.f },
                                               { -.5f, .866f }, { -.866f, .5f }, { -1.f, 0.f }, { -.866f, -.5f },
                                               { -.5f, -.866f }, { 0.f, -1.f }, { .5f, -.866f }, { .866f, -.5f } };
            const float *d0 = Dirs[i], *d1 = Dirs[(i + 1) % 12];
            canvas.FillTriangle((float)cx, (float)cy, cx + d0[0] * 260, cy + d0[1] * 260, cx + d1[0] * 260,
                                cy + d1[1] * 260, SunRay);
        }
        canvas.ResetClip();
    }

    // A pill in the entry's colour: white rim, dark line, light upper half.
    static void DrawPill(UI::Canvas &canvas, const UI::Rect &r, UI::Color color)
    {
        const int radius = std::min<int>(PillRadius, r.h / 2);
        canvas.FillRoundRect(r, radius, PillRim);
        canvas.FillRoundRect(UI::Inset(r, 2), radius - 2, Mix(color, Rgb(0, 0, 0), 150));
        const UI::Rect fill = UI::Inset(r, 4);
        canvas.FillRoundRect(fill, radius - 4, Mix(color, Rgb(0, 0, 0), 30));
        canvas.FillRoundRect(UI::Rect{ fill.x + 2, fill.y + 1, fill.w - 4, fill.h / 2 }, radius - 5,
                             Mix(color, Rgb(255, 255, 255), 70));
        canvas.HLine(fill.x + radius - 4, fill.y + fill.h - 3, fill.w - 2 * (radius - 4), Mix(color, Rgb(0, 0, 0), 90));
    }

    // White capitals with a dark outline in the entry's colour, one or two
    // lines, at the game's size if they fit, else smaller. The system font
    // (no game font loaded) is wider: there, labels that don't fit in
    // capitals keep their own case.
    static void DrawLabel(UI::Canvas &canvas, const XMenuEntry &e, const UI::Rect &r, UI::Color color)
    {
        const bool two = e.line2[0] != '\0';
        const UI::Rect box = UI::Inset(r, 6, 3, 6, 4);
        Theme::TextStyle style;
        style.color = Rgb(255, 255, 255);
        style.outline = Mix(color, Rgb(0, 0, 0), 170);
        style.align = UI::Align::Center;
        std::string line1, line2;
        const auto fits = [&](bool upper, Theme::TextSize size)
        {
            line1 = upper ? Theme::Upper(e.line1) : std::string(e.line1);
            line2 = upper ? Theme::Upper(e.line2) : std::string(e.line2);
            style.size = size;
            return Theme::MeasureText(line1, size) <= box.w && Theme::MeasureText(line2, size) <= box.w
                   && (two ? 2 : 1) * Theme::LineHeight(size) <= box.h + 4;
        };
        if (!fits(true, Theme::TextSize::Body) && !fits(true, Theme::TextSize::Small)
            && !fits(false, Theme::TextSize::Body))
            fits(false, Theme::TextSize::Small);
        const int line = Theme::LineHeight(style.size);
        if (!two)
            Theme::DrawText(canvas, line1, box, style);
        else
        {
            const int top = box.y + (box.h - 2 * line) / 2;
            Theme::DrawText(canvas, line1, UI::Rect{ box.x, top, box.w, line }, style);
            Theme::DrawText(canvas, line2, UI::Rect{ box.x, top + line, box.w, line }, style);
        }
    }

    static void DrawCell(UI::Canvas &canvas, const XMenuEntry &e, const UI::Rect &r, bool selected, bool enabled,
                         bool dim)
    {
        UI::Color color = e.color;
        if (!enabled)
            color = Mix(color, Disabled, 200);
        DrawPill(canvas, r, color);
        DrawLabel(canvas, e, r, color);
        // Save shows the Y button at its left end, as in the game.
        if (std::string(e.name) == "save")
            Theme::DrawButtonPrompt(canvas, r.x + 6, r.y + (r.h - Theme::PromptHeight) / 2, Theme::Button::Y, "");
        if (dim && !selected)
            canvas.FillRoundRect(r, std::min<int>(PillRadius, r.h / 2), Alpha(SunMid, 150));
        // The game marks the selected entry only with its cursor, its tip
        // just inside the pill's left end.
        if (selected)
            Theme::DrawCursor(canvas, r.x + 8, r.y + r.h / 2);
    }

    // Entry `index` opens a screen that only shows on the bottom screen (the
    // panel, L+R): a scene no native screen takes over. Party, Pok�dex, the
    // Bag and the map have their own (Screens/*), as do XMenuOpensNative's.
    static bool OpensOnBottom(int index)
    {
        const XMenuEntry &e = XMenuEntries()[index];
        const std::string name = e.name;
        return e.opens != nullptr && !XMenuOpensNative(index) && XMenuNativeScreen(index) == nullptr
               && name != "pokemon" && name != "bag" && name != "map";
    }

    // The prompts row: button chips, or one line of text in a window.
    static void DrawFooter(UI::Canvas &canvas, const UI::Rect &footer, const XMenuView &view)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        const bool valid = view.cursor >= 0 && view.cursor < (int)entries.size();
        std::string message;
        if (view.opening >= 0 && view.opening < (int)entries.size())
        {
            const XMenuEntry &e = entries[view.opening];
            message = std::string("Opening ") + e.line1 + (e.line2[0] ? " " : "") + e.line2 + "...";
        }
        else if (valid && !Enabled(view.enabled, view.cursor))
            message = "Not available here: use the bottom screen (panel)";
        if (!message.empty())
        {
            const UI::Rect window = UI::Inset(footer, 0, -3, 0, -3);
            Theme::DrawWindow(canvas, window);
            Theme::TextStyle text;
            text.size = Theme::TextSize::Small;
            text.color = Theme::WindowText;
            text.align = UI::Align::Center;
            Theme::DrawText(canvas, message, UI::Inset(window, Theme::WindowPad, 0, Theme::WindowPad, 0), text);
            return;
        }
        const bool bottom = valid && OpensOnBottom(view.cursor);
        struct Chip { Theme::Button button; const char *label; };
        const Chip chips[] = { { Theme::Button::A, bottom ? "Open (L+R)" : "Open" }, { Theme::Button::B, "Close" },
                               { Theme::Button::Y, "Save" }, { Theme::Button::L, "" }, { Theme::Button::R, "Page" } };
        int width = 0;
        for (const Chip &c : chips)
            width += Theme::PromptWidth(c.button, c.label) + 4;
        int x = footer.x + std::max<int>(0, (footer.w - width + 4) / 2);
        for (const Chip &c : chips)
            x += Theme::DrawButtonPrompt(canvas, x, footer.y, c.button, c.label) + (c.button == Theme::Button::L ? 2 : 4);
    }

    void DrawXMenu(UI::Canvas &canvas, const XMenuView &view)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        DrawBackground(canvas);

        // Between the game's two pages.
        const int divX = ColX(2) - PageGap / 2;
        canvas.VLine(divX, GridY + 4, 3 * CellH + 2 * RowGap - 8, Alpha(Frame, 140));

        // Slots the game leaves empty (page 2's last row on this save).
        for (int page = 0; page < 2; page++)
            for (int slot = 0; slot < 6; slot++)
                if (XMenuAtSlot(page, slot) < 0)
                    canvas.StrokeRoundRect(UI::Rect{ ColX(page * 2 + slot % 2), GridY + slot / 2 * (CellH + RowGap),
                                                     CellW, CellH }, PillRadius, Alpha(Frame, 120), 2);

        for (int i = 0; i < (int)entries.size(); i++)
            if (i != view.cursor)
                DrawCell(canvas, entries[i], XMenuCellRect(i), false, Enabled(view.enabled, i), view.opening >= 0);
        // The selected one last: its cursor reaches past the pill's left end.
        if (view.cursor >= 0 && view.cursor < (int)entries.size())
            DrawCell(canvas, entries[view.cursor], XMenuCellRect(view.cursor), true,
                     Enabled(view.enabled, view.cursor), view.opening >= 0);

        const UI::Rect last = XMenuCellRect(XMenuFind("save"));
        DrawFooter(canvas, UI::Rect{ GridX, last.Bottom() + FooterGap, ColX(3) + CellW - GridX, FooterH }, view);
    }
}
