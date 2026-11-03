#include "Screens/PartyMenu.hpp"

#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using UI::Rgb;

    // HID key bit for A (same value as CTRPF's Key::A), for the actions.
    static const u32 KeyA = 1u << 0;
    static const u32 KeyUp = 1u << 6, KeyDown = 1u << 7;

    const char *PartyStatusName(u32 status)
    {
        static const char *const names[] = { "", "PAR", "SLP", "FRZ", "BRN", "PSN" };
        return status < 6 ? names[status] : "?";
    }

    const char *PartyPocketName(PartyPocket pocket)
    {
        return pocket == PartyPocket::Berries ? "Berries" : "Medicine";
    }

    const char *PartyPocketButton(PartyPocket pocket)
    {
        return pocket == PartyPocket::Berries ? "bag.pocket_berries" : "bag.pocket_medicine";
    }

    const char *PartyMenuLabel(PartyMenuItem item)
    {
        static const char *const labels[] = { "Summary", "Use item", "Move", "Cancel" };
        return (u32)item < (u32)PartyMenuItem::Count ? labels[(u32)item] : "?";
    }

    int PartyMove(int from, int dx, int dy, int count)
    {
        if (count <= 0)
            return 0;
        if (from < 0 || from >= count)
            return 0;
        int to = from;
        if (dx != 0)
        {
            const int col = from % 2;
            if ((dx > 0 && col == 0) || (dx < 0 && col == 1))
                to = from + (dx > 0 ? 1 : -1);
        }
        else if (dy != 0)
            to = from + 2 * (dy > 0 ? 1 : -1);
        return to >= 0 && to < count ? to : from;
    }

    // --- State ---

    void PartyMenuState::Reset(int count)
    {
        m_page = PartyPage::List;
        m_swapFrom = -1;
        SetCursor(m_cursor, count);
    }

    void PartyMenuState::SetCursor(int pos, int count)
    {
        m_cursor = count <= 0 ? 0 : pos < 0 ? 0 : pos >= count ? count - 1 : pos;
    }

    u32 PartyMenuState::Keys(void) const
    {
        const u32 common = PartyKey::A | PartyKey::Up | PartyKey::Down | PartyKey::Left | PartyKey::Right
                         | PartyKey::X | PartyKey::Y | PartyKey::L | PartyKey::R | PartyKey::Start;
        return m_page == PartyPage::List ? common : common | PartyKey::B;
    }

    static int Direction(u32 keys, int &dx, int &dy)
    {
        dx = dy = 0;
        if (keys & PartyKey::Up)         dy = -1;
        else if (keys & PartyKey::Down)  dy = 1;
        else if (keys & PartyKey::Left)  dx = -1;
        else if (keys & PartyKey::Right) dx = 1;
        return dx != 0 || dy != 0;
    }

    PartyCommand PartyMenuState::Press(u32 keys, int count, const std::vector<PartyBagItem> *items)
    {
        PartyCommand cmd;
        if (count <= 0)
            return cmd;
        if (m_cursor >= count)
            m_cursor = count - 1;
        int dx, dy;
        const bool moved = Direction(keys, dx, dy);

        switch (m_page)
        {
        case PartyPage::List:
            if (moved)
                m_cursor = PartyMove(m_cursor, dx, dy, count);
            else if (keys & PartyKey::A)
            {
                m_page = PartyPage::Menu;
                m_menu = 0;
            }
            else if ((keys & PartyKey::Y) && count > 1)
            {
                m_page = PartyPage::Swap;
                m_swapFrom = m_cursor;
            }
            break;

        case PartyPage::Menu:
        {
            const int n = (int)PartyMenuItem::Count;
            if (dy != 0)
                m_menu = (m_menu + dy + n) % n;
            else if (keys & PartyKey::B)
                m_page = PartyPage::List;
            else if (keys & PartyKey::A)
            {
                switch ((PartyMenuItem)m_menu)
                {
                case PartyMenuItem::Summary:
                    m_page = PartyPage::List;
                    cmd.kind = PartyCommand::Summary;
                    cmd.pos = m_cursor;
                    break;
                case PartyMenuItem::UseItem:
                    m_page = PartyPage::Items;
                    m_item = 0;
                    break;
                case PartyMenuItem::Move:
                    if (count > 1)
                    {
                        m_page = PartyPage::Swap;
                        m_swapFrom = m_cursor;
                    }
                    break;
                default:
                    m_page = PartyPage::List;
                    break;
                }
            }
            break;
        }

        case PartyPage::Swap:
            if (moved)
                m_cursor = PartyMove(m_cursor, dx, dy, count);
            else if (keys & PartyKey::B)
            {
                m_page = PartyPage::List;
                m_cursor = m_swapFrom;
                m_swapFrom = -1;
            }
            else if (keys & (PartyKey::A | PartyKey::Y))
            {
                if (m_cursor != m_swapFrom)
                {
                    cmd.kind = PartyCommand::Swap;
                    cmd.pos = m_swapFrom;
                    cmd.other = m_cursor;
                }
                m_page = PartyPage::List;
                m_swapFrom = -1;
            }
            break;

        case PartyPage::Items:
        {
            const int n = items != nullptr ? (int)items->size() : 0;
            if (dy != 0 && n > 0)
                m_item = (m_item + dy + n) % n;
            else if ((keys & (PartyKey::L | PartyKey::R)) || dx != 0)
            {
                m_pocket = m_pocket == PartyPocket::Medicine ? PartyPocket::Berries : PartyPocket::Medicine;
                m_item = 0;
            }
            else if (keys & PartyKey::B)
                m_page = PartyPage::Menu;
            else if ((keys & PartyKey::A) && n > 0)
            {
                if (m_item >= n)
                    m_item = n - 1;
                cmd.kind = PartyCommand::UseItem;
                cmd.pos = m_cursor;
                cmd.pocket = m_pocket;
                cmd.item = m_item;
                m_page = PartyPage::List;
            }
            break;
        }
        }
        return cmd;
    }

    // --- Actions ---

    static const char *const SlotButtons[6] =
    {
        "party.slot1", "party.slot2", "party.slot3", "party.slot4", "party.slot5", "party.slot6",
    };
    static const char *const RowButtons[7] =
    {
        "bag.item_row1", "bag.item_row2", "bag.item_row3", "bag.item_row4",
        "bag.item_row5", "bag.item_row6", "bag.item_row7",
    };

    static const char *Slot(int pos)
    {
        return SlotButtons[pos < 0 ? 0 : pos > 5 ? 5 : pos];
    }

    static void Begin(PartyAction &out, const std::string &name, const std::string &summary, const ActionCond &needs)
    {
        out.name = name;
        out.summary = summary;
        out.def.name = out.name.c_str();
        out.def.summary = out.summary.c_str();
        out.def.needs = needs;
        out.def.steps.clear();
    }

    static ActionCond PartyList(void)
    {
        return InScene(SceneParty).With(WorldPartyList);
    }

    // Tap a slot and wait for its menu ("What would you like to do with
    // ...?"), which dims the bottom bar.
    static void OpenSlotMenu(std::vector<ActionStep> &s, int pos)
    {
        s.push_back(StepTap(Slot(pos)));
        s.push_back(StepWaitFor(InScene(SceneParty).Without(WorldPartyList), 30).Retry(0, 1));
        s.push_back(StepWait(12));      // the menu slides in
    }

    void MakePartySummaryAction(PartyAction &out, int pos)
    {
        Begin(out, "party-summary", "Party: tap the slot, Summary", PartyList());
        std::vector<ActionStep> &s = out.def.steps;
        OpenSlotMenu(s, pos);
        s.push_back(StepTap("party_slot_menu.summary"));
        s.push_back(StepWaitFor(InScene(SceneStatus), 180));
    }

    void MakePartySwapAction(PartyAction &out, int from, int to, int gameCursor)
    {
        Begin(out, "party-swap", "Party: Y button, cursor to the first, A, tap the second, B", PartyList());
        std::vector<ActionStep> &s = out.def.steps;
        s.push_back(StepTap("party.move_pokemon"));
        // "Which Pokémon do you want to move?" covers the bar.
        s.push_back(StepWaitFor(InScene(SceneParty).Without(WorldPartyList), 30).Retry(0, 1));
        s.push_back(StepWait(10));
        // In move mode a tap only moves the game's cursor; A picks.
        if (gameCursor != from)
        {
            s.push_back(StepTap(Slot(from)));
            s.push_back(StepWait(8));
        }
        s.push_back(StepPress(KeyA));
        s.push_back(StepWait(12));
        // "Where do you want to move it to?": a tap swaps at once.
        s.push_back(StepTap(Slot(to)));
        s.push_back(StepWait(45));
        // Back to "Which Pokémon...?"; B leaves move mode. No retry: a B
        // too many would close the Party screen.
        s.push_back(StepPress(ActionKeyB));
        s.push_back(StepWaitFor(PartyList(), 90));
    }

    void MakePartyRestoreAction(PartyAction &out, int pos, PartyPocket pocket)
    {
        Begin(out, "party-restore", "Party: tap the slot, Restore, the pocket's tab", PartyList());
        std::vector<ActionStep> &s = out.def.steps;
        OpenSlotMenu(s, pos);
        s.push_back(StepTap("party_slot_menu.restore"));
        s.push_back(StepWaitFor(InScene(SceneBag), 180));
        s.push_back(StepWait(30));      // the Bag slides in
        // The Bag reopens on the pocket it showed last.
        s.push_back(StepTap(PartyPocketButton(pocket)));
        s.push_back(StepWait(15));
    }

    void MakePartyBagRowAction(PartyAction &out, int row)
    {
        Begin(out, "party-bag-row", "Restore bag: tap a shown row (item menu)", InScene(SceneBag));
        out.def.steps.push_back(StepTap(RowButtons[row < 0 ? 0 : row > 6 ? 6 : row]));
        out.def.steps.push_back(StepWait(20));
    }

    void MakePartyBagStepAction(PartyAction &out, bool up)
    {
        Begin(out, up ? "party-bag-up" : "party-bag-down", "Restore bag: one D-pad step", InScene(SceneBag));
        out.def.steps.push_back(StepPress(up ? KeyUp : KeyDown));
        out.def.steps.push_back(StepWait(10));
    }

    // B until the Bag is gone (a message, then the list), then the list.
    static void LeaveBag(std::vector<ActionStep> &s)
    {
        const s8 press = (s8)s.size();
        s.push_back(StepPress(ActionKeyB));
        s.push_back(StepWaitFor(NotInScene(SceneBag), 45).Retry(press, 4));
        s.push_back(StepWaitFor(PartyList(), 180));
    }

    void MakePartyUseAction(PartyAction &out)
    {
        Begin(out, "party-use", "Restore bag: Use this item, B through the message, B back to Party",
              InScene(SceneBag));
        std::vector<ActionStep> &s = out.def.steps;
        s.push_back(StepTap("restore_item_menu.use"));
        s.push_back(StepWait(60));      // the effect and its message
        s.push_back(StepPress(ActionKeyB));
        s.push_back(StepWait(30));
        LeaveBag(s);
    }

    void MakePartyBagBackAction(PartyAction &out)
    {
        Begin(out, "party-bag-back", "Restore bag: B back to Party", InScene(SceneBag));
        LeaveBag(out.def.steps);
    }

    PartyBagPlan PlanPartyBag(int target, int scroll, int rows, int count)
    {
        PartyBagPlan plan;
        if (target < 0 || target >= count || rows <= 0)
            return plan;
        if (target >= scroll && target < scroll + rows)
            plan.row = target - scroll;
        else
            plan.dir = target < scroll ? -1 : 1;
        return plan;
    }

    // --- Drawing ---

    static constexpr UI::Color PanelBg   = Rgb(22, 30, 52);
    static constexpr UI::Color CardBg    = Rgb(38, 52, 86);
    static constexpr UI::Color CardDim   = Rgb(30, 38, 62);
    static constexpr UI::Color Border    = Rgb(120, 150, 220);
    static constexpr UI::Color TextMain  = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim   = Rgb(150, 165, 200);
    static constexpr UI::Color Selected  = Rgb(255, 236, 120);
    static constexpr UI::Color SwapColor = Rgb(110, 220, 255);
    static constexpr UI::Color BarBack   = Rgb(16, 20, 34);
    static constexpr UI::Color HpGreen   = Rgb(64, 214, 96);
    static constexpr UI::Color HpYellow  = Rgb(240, 200, 40);
    static constexpr UI::Color HpRed     = Rgb(232, 64, 56);
    static constexpr UI::Color StatusBg  = Rgb(176, 72, 196);
    static constexpr UI::Color MenuBg    = Rgb(30, 42, 72);

    static const int Pad = 8, ColGap = 6, RowGap = 5, FooterH = 18, FooterGap = 6;
    static const int CardW = (PartyArea.w - 2 * Pad - ColGap) / 2;
    static const int CardH = (PartyArea.h - 2 * Pad - FooterH - FooterGap - 2 * RowGap) / 3;

    UI::Rect PartyCardRect(int pos)
    {
        if (pos < 0 || pos > 5)
            pos = 0;
        return UI::Rect{ PartyArea.x + Pad + (pos % 2) * (CardW + ColGap),
                         PartyArea.y + Pad + (pos / 2) * (CardH + RowGap), CardW, CardH };
    }

    static UI::Rect FooterRect(void)
    {
        return UI::Rect{ PartyArea.x + Pad, PartyArea.Bottom() - Pad - FooterH, PartyArea.w - 2 * Pad, FooterH };
    }

    static UI::Color HpColor(u16 hp, u16 maxHp)
    {
        if (maxHp == 0 || hp * 2 > maxHp)
            return HpGreen;
        return hp * 5 > maxHp ? HpYellow : HpRed;
    }

    static void DrawHpBar(UI::Canvas &canvas, const UI::Rect &r, u16 hp, u16 maxHp)
    {
        canvas.FillRoundRect(r, r.h / 2, BarBack);
        if (maxHp == 0 || hp == 0)
            return;
        const int w = (int)((r.w - 2) * (u32)hp / maxHp);
        if (w > 0)
            canvas.FillRoundRect(UI::Rect{ r.x + 1, r.y + 1, w < r.h - 2 ? r.h - 2 : w, r.h - 2 }, (r.h - 2) / 2,
                                 HpColor(hp, maxHp));
    }

    // A status badge ending at `right`; returns its left edge.
    static int DrawStatus(UI::Canvas &canvas, u32 status, int right, int y, int h)
    {
        const char *label = PartyStatusName(status);
        if (label[0] == '\0')
            return right;
        UI::TextStyle style;
        style.font = UI::Font::Small;
        style.color = TextMain;
        style.align = UI::Align::Center;
        const int w = UI::MeasureText(label, style) + 8;
        const UI::Rect badge = { right - w, y, w, h };
        canvas.FillRoundRect(badge, 4, StatusBg);
        UI::DrawText(canvas, label, badge, style);
        return badge.x;
    }

    static std::string Format(const char *fmt, int a, int b = 0)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    static void DrawCard(UI::Canvas &canvas, const PartyMonView &mon, const UI::Rect &r, bool selected, bool source,
                         bool dim)
    {
        canvas.FillRoundRect(r, 10, dim && !selected ? CardDim : CardBg);
        if (source)
            canvas.StrokeRoundRect(UI::Inset(r, -2), 12, SwapColor, 2);
        if (selected)
            canvas.StrokeRoundRect(UI::Inset(r, -3), 13, Selected, 3);

        UI::TextStyle main;
        main.color = TextMain;
        UI::TextStyle dimStyle;
        dimStyle.color = TextDim;
        const int line = UI::LineHeight(main);
        const UI::Rect inner = UI::Inset(r, 10, 1, 8, 1);

        if (!mon.valid)
        {
            UI::DrawText(canvas, "?", UI::Rect{ inner.x, inner.y, inner.w, line }, dimStyle);
            return;
        }
        if (mon.egg)
        {
            UI::DrawText(canvas, "Egg", UI::Rect{ inner.x, inner.y, inner.w, line }, main);
            return;
        }

        // Line 1: name and level.
        UI::TextStyle level = dimStyle;
        level.align = UI::Align::Right;
        const std::string lv = Format("Lv.%d", mon.level);
        const int lvW = UI::MeasureText(lv, level);
        UI::DrawText(canvas, lv, UI::Rect{ inner.Right() - lvW, inner.y, lvW, line }, level);
        UI::DrawText(canvas, mon.name, UI::Rect{ inner.x, inner.y, inner.w - lvW - 6, line }, main);

        // Line 2: HP bar and numbers.
        const int y2 = inner.y + line;
        UI::TextStyle hpText = main;
        hpText.align = UI::Align::Right;
        const std::string hp = Format("%d/%d", mon.hp, mon.maxHp);
        const int hpW = UI::MeasureText("888/888", hpText);
        DrawHpBar(canvas, UI::Rect{ inner.x, y2 + line / 2 - 3, inner.w - hpW - 8, 7 }, mon.hp, mon.maxHp);
        UI::DrawText(canvas, hp, UI::Rect{ inner.Right() - hpW, y2, hpW, line }, hpText);

        // Line 3: held item and status.
        const int y3 = y2 + line;
        const int left = DrawStatus(canvas, mon.status, inner.Right(), y3 + 3, line - 6);
        UI::DrawText(canvas, mon.item.empty() ? std::string("No item") : mon.item,
                     UI::Rect{ inner.x, y3, left - inner.x - 4, line }, mon.item.empty() ? dimStyle : main);
    }

    // A popup list on the right: `title` then the entries, `cursor` marked.
    static void DrawPopup(UI::Canvas &canvas, const UI::Rect &r, const std::string &title,
                          const std::vector<std::string> &rows, int first, int shown, int cursor)
    {
        canvas.FillRoundRect(r, 10, MenuBg);
        canvas.StrokeRoundRect(r, 10, Border, 2);
        UI::TextStyle main;
        main.color = TextMain;
        UI::TextStyle head;
        head.color = Selected;
        const int line = UI::LineHeight(main);
        int y = r.y + 6;
        if (!title.empty())
        {
            UI::DrawText(canvas, title, UI::Rect{ r.x + 12, y, r.w - 24, line }, head);
            y += line + 2;
        }
        if (rows.empty())
        {
            UI::TextStyle dim = main;
            dim.color = TextDim;
            UI::DrawText(canvas, "(nothing here)", UI::Rect{ r.x + 22, y, r.w - 30, line }, dim);
            return;
        }
        for (int i = first; i < (int)rows.size() && i < first + shown; i++, y += line + 2)
        {
            const UI::Rect row = { r.x + 6, y, r.w - 12, line + 2 };
            if (i == cursor)
            {
                canvas.FillRoundRect(row, 6, Rgb(255, 236, 120, 60));
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ row.x + 2, row.y + row.h / 2 - 5, 10, 10 },
                             Selected);
            }
            UI::DrawText(canvas, rows[i], UI::Rect{ row.x + 16, row.y + 1, row.w - 20, line }, main);
        }
        if (first > 0)
            UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ r.Right() - 18, r.y + 6, 10, 10 }, TextDim);
        if (first + shown < (int)rows.size())
            UI::DrawIcon(canvas, UI::Icon::ArrowDown, UI::Rect{ r.Right() - 18, r.Bottom() - 16, 10, 10 }, TextDim);
    }

    static const int ItemRowsShown = 7;

    void DrawParty(UI::Canvas &canvas, const PartyView &view)
    {
        canvas.FillRoundRect(PartyArea, PartyRadius, PanelBg);
        canvas.StrokeRoundRect(PartyArea, PartyRadius, Border, 2);

        const bool popup = view.page == PartyPage::Menu || view.page == PartyPage::Items;
        const bool dim = popup || !view.busy.empty();
        for (int i = 0; i < (int)view.mons.size() && i < 6; i++)
            DrawCard(canvas, view.mons[i], PartyCardRect(i), i == view.cursor && view.busy.empty(),
                     view.page == PartyPage::Swap && i == view.swapFrom, dim);
        if (view.mons.empty())
        {
            UI::TextStyle dimStyle;
            dimStyle.color = TextDim;
            dimStyle.align = UI::Align::Center;
            UI::DrawText(canvas, "Reading the party...", PartyCardRect(0), dimStyle);
        }

        const std::string name = view.cursor >= 0 && view.cursor < (int)view.mons.size()
                                 ? view.mons[view.cursor].name : std::string();
        if (view.page == PartyPage::Menu)
        {
            std::vector<std::string> rows;
            for (int i = 0; i < (int)PartyMenuItem::Count; i++)
                rows.push_back(PartyMenuLabel((PartyMenuItem)i));
            const int line = 20;
            const UI::Rect r = { PartyArea.Right() - Pad - 150, PartyArea.y + 40, 150, 10 + line * 5 + 4 };
            DrawPopup(canvas, r, name, rows, 0, 4, view.menuCursor);
        }
        else if (view.page == PartyPage::Items)
        {
            std::vector<std::string> rows;
            for (const PartyBagItem &item : view.items)
                rows.push_back(item.name + Format("  x%d", item.count));
            int first = view.itemCursor - ItemRowsShown / 2;
            if (first > (int)rows.size() - ItemRowsShown)
                first = (int)rows.size() - ItemRowsShown;
            if (first < 0)
                first = 0;
            const UI::Rect r = { PartyArea.Right() - Pad - 210, PartyArea.y + Pad, 210,
                                 PartyArea.h - 2 * Pad - FooterH - FooterGap };
            DrawPopup(canvas, r, std::string("< ") + PartyPocketName(view.pocket) + " >  for " + name, rows, first,
                      ItemRowsShown, view.itemCursor);
        }

        UI::TextStyle hint;
        hint.color = TextDim;
        std::string text;
        switch (view.page)
        {
        case PartyPage::List:  text = "A Choose   Y Move   B Close"; break;
        case PartyPage::Menu:  text = "A Choose   B Back"; break;
        case PartyPage::Swap:  text = "Move " + (view.swapFrom >= 0 && view.swapFrom < (int)view.mons.size()
                                                 ? view.mons[view.swapFrom].name : std::string()) + " where?   A Here   B Cancel"; break;
        case PartyPage::Items: text = "A Use   L/R Pocket   B Back"; break;
        }
        if (!view.busy.empty())
        {
            text = view.busy;
            hint.color = TextMain;
        }
        else if (!view.note.empty() && view.page == PartyPage::List)
        {
            text = view.note;
            hint.color = TextMain;
        }
        UI::DrawText(canvas, text, FooterRect(), hint);
    }

    void DrawSummary(UI::Canvas &canvas, const SummaryView &view)
    {
        canvas.FillRoundRect(PartyArea, PartyRadius, PanelBg);
        canvas.StrokeRoundRect(PartyArea, PartyRadius, Border, 2);
        const PartyMonView &mon = view.mon;

        UI::TextStyle large;
        large.font = UI::Font::Large;
        large.color = TextMain;
        UI::TextStyle main;
        main.color = TextMain;
        UI::TextStyle dim;
        dim.color = TextDim;
        UI::TextStyle right = dim;
        right.align = UI::Align::Right;
        const int line = UI::LineHeight(main);
        const int x = PartyArea.x + 14, w = PartyArea.w - 28;
        int y = PartyArea.y + 8;

        // Header: name, species, level, position.
        const std::string where = Format("%d/%d", view.pos + 1, view.count);
        const int whereW = UI::MeasureText(where, right);
        UI::DrawText(canvas, where, UI::Rect{ x + w - whereW, y + 3, whereW, line }, right);
        const int nameW = UI::DrawText(canvas, mon.valid ? mon.name : std::string("?"),
                                       UI::Rect{ x, y, w - whereW - 80, UI::LineHeight(large) }, large);
        if (mon.valid && !mon.egg && mon.species != mon.name)
            UI::DrawText(canvas, "(" + mon.species + ")",
                         UI::Rect{ x + nameW + 8, y + 3, w - whereW - nameW - 16, line }, dim);
        y += UI::LineHeight(large) + 2;
        if (!mon.valid || mon.egg)
            return;

        // Level, HP, status.
        UI::DrawText(canvas, Format("Lv.%d", mon.level), UI::Rect{ x, y, 60, line }, main);
        const std::string hp = Format("HP %d/%d", mon.hp, mon.maxHp);
        const int hpW = UI::MeasureText("HP 888/888", main);
        const int statusLeft = DrawStatus(canvas, mon.status, x + w, y + 3, line - 6);
        UI::DrawText(canvas, hp, UI::Rect{ statusLeft - 8 - hpW, y, hpW, line }, main);
        const int barX = x + 64;
        DrawHpBar(canvas, UI::Rect{ barX, y + line / 2 - 3, statusLeft - 8 - hpW - 8 - barX, 7 }, mon.hp, mon.maxHp);
        y += line + 6;

        // Item, ability, nature.
        const int labelW = 86;
        const std::string rows[3][2] =
        {
            { "Item", mon.item.empty() ? std::string("None") : mon.item },
            { "Ability", mon.ability.empty() ? std::string("-") : mon.ability },
            { "Nature", mon.nature.empty() ? std::string("-") : mon.nature },
        };
        for (const auto &row : rows)
        {
            UI::DrawText(canvas, row[0], UI::Rect{ x, y, labelW, line }, dim);
            UI::DrawText(canvas, row[1], UI::Rect{ x + labelW, y, w - labelW, line }, main);
            y += line;
        }
        y += 4;
        canvas.HLine(x, y, w, Rgb(120, 150, 220, 90));
        y += 4;

        // Moves.
        for (int i = 0; i < 4; i++)
        {
            if (mon.moves[i] == 0)
            {
                UI::DrawText(canvas, "-", UI::Rect{ x + 8, y, w, line }, dim);
                y += line;
                continue;
            }
            const std::string name = mon.moveNames[i].empty() ? Format("Move %d", mon.moves[i]) : mon.moveNames[i];
            UI::DrawText(canvas, name, UI::Rect{ x + 8, y, w - 90, line }, main);
            const std::string pp = Format("PP %d", mon.pp[i]);
            UI::DrawText(canvas, pp, UI::Rect{ x + w - 80, y, 80, line }, right);
            y += line;
        }

        UI::TextStyle hint = dim;
        UI::DrawText(canvas, "Up/Down Pokemon   Y Game   B Back", FooterRect(), hint);
    }

    static std::string MonKey(const PartyMonView &m)
    {
        std::string k = m.valid ? "v" : "-";
        k += m.egg ? "e" : "";
        k += m.name + "|" + m.species + "|" + m.item + "|" + m.ability + "|" + m.nature;
        k += Format("|%d|%d", m.level, m.hp) + Format("|%d|%d", m.maxHp, (int)m.status);
        for (int i = 0; i < 4; i++)
            k += Format("|%d:%d", m.moves[i], m.pp[i]) + m.moveNames[i];
        return k;
    }

    std::string PartyViewKey(const PartyView &v)
    {
        std::string k = Format("%d/%d", (int)v.page, v.cursor) + Format("/%d/%d", v.menuCursor, v.swapFrom)
                      + Format("/%d/%d", (int)v.pocket, v.itemCursor) + "/" + v.busy + "/" + v.note;
        for (const PartyMonView &m : v.mons)
            k += "#" + MonKey(m);
        for (const PartyBagItem &i : v.items)
            k += Format("@%d:%d", i.id, i.count) + i.name;
        return k;
    }

    std::string SummaryViewKey(const SummaryView &v)
    {
        return Format("%d/%d", v.pos, v.count) + (v.hidden ? "h" : "s") + MonKey(v.mon);
    }
}
