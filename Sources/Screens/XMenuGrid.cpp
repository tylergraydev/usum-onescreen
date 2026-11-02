#include "Screens/XMenuGrid.hpp"

#include <cstdlib>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using UI::Rgb;

    // HID key bit for Y (same value as CTRPF's Key::Y): Save in the X menu.
    static const u32 XMenuKeyY = 1u << 11;

    const std::vector<XMenuEntry> &XMenuEntries(void)
    {
        // Page 1 then page 2 in the game's slot order, then the two extras.
        // Scenes from docs/OFFSETS.md; Quick Link and Festival Plaza stay in
        // FieldRo (the X menu just goes away).
        static const std::vector<XMenuEntry> entries =
        {
            { "pokemon",          "Pokémon",  "",         0, 0, "xmenu.pokemon",               SceneParty,          0, 0, 1, Rgb(46, 160, 84) },
            { "pokedex",          "Pokédex",  "",         0, 1, "xmenu.pokedex",               SceneDex,            1, 0, 1, Rgb(214, 58, 78) },
            { "bag",              "Bag",      "",         0, 2, "xmenu.bag",                   SceneBag,            0, 1, 1, Rgb(226, 118, 34) },
            { "quick_link",       "Quick",    "Link",     0, 3, "xmenu.quick_link",            nullptr,             1, 1, 1, Rgb(40, 104, 214) },
            { "festival_plaza",   "Festival", "Plaza",    0, 4, "xmenu.festival_plaza",        nullptr,             0, 2, 1, Rgb(28, 150, 128) },
            { "trainer_passport", "Trainer",  "Passport", 0, 5, "xmenu.trainer_passport",      "TrainerPass",       1, 2, 1, Rgb(56, 92, 200) },
            { "pokemon_refresh",  "Pokémon",  "Refresh",  1, 0, "xmenu_page2.pokemon_refresh", "Kawaigari",         2, 0, 1, Rgb(196, 146, 24) },
            { "qr_scanner",       "QR",       "Scanner",  1, 1, "xmenu_page2.qr_scanner",      "QRReader",          3, 0, 1, Rgb(104, 64, 196) },
            { "battle_video",     "Battle",   "Video",    1, 2, "xmenu_page2.battle_video",    "BattleVideoPlayer", 2, 1, 1, Rgb(34, 120, 200) },
            { "options",          "Options",  "",         1, 3, "xmenu_page2.options",         "Config",            3, 1, 1, Rgb(186, 48, 150) },
            { "map",              "Map",      "",        -1, -1, nullptr,                      SceneTownMap,        0, 3, 2, Rgb(52, 132, 150) },
            { "save",             "Save",     "",        -1, -1, nullptr,                      nullptr,             2, 3, 2, Rgb(30, 160, 214) },
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
        static const char *const native[] = { "options", "trainer_passport" };
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

    // --- Drawing ---

    static constexpr UI::Color PanelBg   = Rgb(22, 30, 52);
    static constexpr UI::Color Border    = Rgb(120, 150, 220);
    static constexpr UI::Color Divider   = Rgb(120, 150, 220, 90);
    static constexpr UI::Color TextMain  = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim   = Rgb(150, 165, 200);
    static constexpr UI::Color Selected  = Rgb(255, 236, 120);

    // Grid geometry inside XMenuArea: 4 columns (a wider gap between the
    // pages), 3 rows of pills, then the bottom row and a footer line.
    static const int GridX = XMenuArea.x + 10;
    static const int GridY = XMenuArea.y + 10;
    static const int CellW = 82, CellH = 44;
    static const int ColGap = 6, PageGap = 16, RowGap = 6;
    static const int BottomGap = 8, BottomH = 30;
    static const int FooterGap = 6, FooterH = 18;

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

    static void DrawCell(UI::Canvas &canvas, const XMenuEntry &e, const UI::Rect &r, bool selected, bool enabled,
                         bool dim)
    {
        UI::Color fill = enabled ? e.color : Mix(e.color, Rgb(70, 74, 86), 190);
        if (dim && !selected)
            fill = Mix(fill, PanelBg, 140);
        canvas.FillRoundRect(r, 12, fill);
        if (selected)
        {
            canvas.StrokeRoundRect(UI::Inset(r, -3), 14, Selected, 3);
            UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 3, r.y + r.h / 2 - 6, 12, 12 }, Selected);
        }

        UI::TextStyle style;
        style.color = enabled ? TextMain : TextDim;
        style.align = UI::Align::Center;
        const UI::Rect text = UI::Inset(r, 12, 0, 6, 0);
        const int line = UI::LineHeight(style);
        if (e.line2[0] == '\0')
            UI::DrawText(canvas, e.line1, text, style);
        else
        {
            const int top = r.y + (r.h - 2 * line) / 2;
            UI::DrawText(canvas, e.line1, UI::Rect{ text.x, top, text.w, line }, style);
            UI::DrawText(canvas, e.line2, UI::Rect{ text.x, top + line, text.w, line }, style);
        }
    }

    void DrawXMenu(UI::Canvas &canvas, const XMenuView &view)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        canvas.FillRoundRect(XMenuArea, XMenuRadius, PanelBg);
        canvas.StrokeRoundRect(XMenuArea, XMenuRadius, Border, 2);

        // Between the game's two pages.
        const int divX = ColX(2) - PageGap / 2;
        canvas.VLine(divX, GridY + 2, 3 * CellH + 2 * RowGap - 4, Divider);

        // Slots the game leaves empty (page 2's last row on this save).
        for (int page = 0; page < 2; page++)
            for (int slot = 0; slot < 6; slot++)
                if (XMenuAtSlot(page, slot) < 0)
                    canvas.StrokeRoundRect(UI::Rect{ ColX(page * 2 + slot % 2), GridY + slot / 2 * (CellH + RowGap),
                                                     CellW, CellH }, 12, Divider, 1);

        for (int i = 0; i < (int)entries.size(); i++)
            DrawCell(canvas, entries[i], XMenuCellRect(i), i == view.cursor, Enabled(view.enabled, i),
                     view.opening >= 0);

        const UI::Rect last = XMenuCellRect(XMenuFind("save"));
        const UI::Rect footer = { GridX, last.Bottom() + FooterGap, ColX(3) + CellW - GridX, FooterH };
        UI::TextStyle hint;
        hint.color = TextDim;
        std::string text = "A Open    B Close    Y Save    L/R Page";
        if (view.opening >= 0 && view.opening < (int)entries.size())
        {
            const XMenuEntry &e = entries[view.opening];
            text = std::string("Opening ") + e.line1 + (e.line2[0] ? " " : "") + e.line2 + "...";
            hint.color = TextMain;
        }
        else if (view.cursor >= 0 && view.cursor < (int)entries.size() && !Enabled(view.enabled, view.cursor))
            text = "Not available here: use the bottom screen (panel)";
        else if (view.cursor >= 0 && view.cursor < (int)entries.size() && entries[view.cursor].opens != nullptr
                 && !XMenuOpensNative(view.cursor))
            text = "A Open (bottom screen: L+R)    B Close    Y Save";
        UI::DrawText(canvas, text, footer, hint);
    }
}
