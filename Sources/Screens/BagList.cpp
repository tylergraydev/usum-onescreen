#include "Screens/BagList.hpp"

#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using UI::Rgb;

    // HID key bits (same values as CTRPF's Key::).
    static const u32 KeyA = 1u << 0, KeyUp = 1u << 6, KeyDown = 1u << 7;

    const BagTab &BagTabAt(int tab)
    {
        // The game's tab index (Bag param +0x54) runs left to right over the
        // tabs at the bottom of its screen. Use is native where it only asks
        // for a Pokémon (Medicine, Berries); elsewhere it can leave the Bag
        // (Escape Rope), start an evolution or teach a move.
        static const BagTab tabs[BagTabCount] =
        {
            { "rotom_powers", "Rotom",   "bag.pocket_rotom_powers", false },
            { "medicine",     "Meds",    "bag.pocket_medicine",     true  },
            { "items",        "Items",   "bag.pocket_items",        false },
            { "tms",          "TMs",     "bag.pocket_tms",          false },
            { "berries",      "Berry",   "bag.pocket_berries",      true  },
            { "key_items",    "Key",     "bag.pocket_key_items",    false },
            { "free_space",   "Free",    "bag.pocket_free_space",   false },
        };
        return tabs[tab < 0 || tab >= BagTabCount ? 0 : tab];
    }

    int BagTabFind(const std::string &name)
    {
        for (int i = 0; i < BagTabCount; i++)
            if (name == BagTabAt(i).name)
                return i;
        return -1;
    }

    const char *BagCommandLabel(u8 command)
    {
        switch (command)
        {
        case BagCommand::Use:       return "Use";
        case BagCommand::Give:      return "Give";
        case BagCommand::FreeSpace: return "Move to Free Space";
        case BagCommand::Cancel:    return "Cancel";
        case BagCommand::Yes:       return "Yes";
        case BagCommand::No:        return "No";
        }
        static char other[16];
        std::snprintf(other, sizeof(other), "Option 0x%02X", command);
        return other;
    }

    u32 BagGame::Signature(void) const
    {
        // FNV-1a over what the driver looks at.
        u32 h = 2166136261u;
        auto mix = [&h](u32 v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xFF; h *= 16777619u; } };
        mix((u32)mode);
        mix((u32)tab);
        mix((u32)scroll);
        mix((u32)cursorRow);
        mix((u32)items.size());
        mix((u32)partyCursor);
        mix(item);
        for (u8 c : commands)
            mix(c);
        return h;
    }

    // --- Native list cursor ---

    int BagMoveCursor(int cursor, int delta, int count)
    {
        if (count <= 0)
            return 0;
        int c = (cursor + delta) % count;
        return c < 0 ? c + count : c;
    }

    int BagListTop(int top, int cursor, int count, int rows)
    {
        if (count <= rows)
            return 0;
        if (cursor < top)
            top = cursor;
        if (cursor >= top + rows)
            top = cursor - rows + 1;
        if (top > count - rows)
            top = count - rows;
        return top < 0 ? 0 : top;
    }

    bool BagCommandEnabled(u8 command, int tab)
    {
        switch (command)
        {
        case BagCommand::Use:
            return BagTabAt(tab).canUse;
        case BagCommand::Give:
        case BagCommand::Cancel:
        case BagCommand::Yes:
        case BagCommand::No:
            return true;
        }
        return false;
    }

    // --- Actions ---

    // The game's item menu grows upwards from Cancel; Layouts.hpp has its
    // entries by position from the bottom.
    static const int MenuPositions = 5;

    struct BagActionTable
    {
        std::vector<std::string> names, summaries;
        std::vector<ActionDef> defs;
        int tab, row, up, down, menu, party, yes, no, a, b, back;
    };

    static BagActionTable MakeBagActions(void)
    {
        BagActionTable t;
        t.names.reserve(64);
        t.summaries.reserve(64);
        t.defs.reserve(64);
        const ActionCond bag = InScene(SceneBag);
        auto add = [&](const std::string &name, std::vector<ActionStep> steps) {
            t.names.push_back(name);
            t.summaries.push_back(steps[0].Describe());
            t.defs.push_back(ActionDef{ nullptr, nullptr, bag, steps });
            return (int)t.defs.size() - 1;
        };
        // Button names must outlive the definitions too.
        static std::vector<std::string> buttons;
        buttons.reserve(32);
        auto button = [](const std::string &name) { buttons.push_back(name); return buttons.back().c_str(); };

        t.tab = (int)t.defs.size();
        for (int i = 0; i < BagTabCount; i++)
            add(std::string("bag-tab-") + BagTabAt(i).name, { StepTap(BagTabAt(i).button) });
        t.row = (int)t.defs.size();
        for (int r = 1; r <= 7; r++)
            add("bag-row" + std::to_string(r), { StepTap(button("bag.item_row" + std::to_string(r))) });
        t.up = add("bag-up", { StepPress(KeyUp) });
        t.down = add("bag-down", { StepPress(KeyDown) });
        t.menu = (int)t.defs.size();
        for (int p = 1; p <= MenuPositions; p++)
            add("bag-menu-" + std::to_string(p), { StepTap(button("bag_item_menu.from_bottom" + std::to_string(p))) });
        t.party = (int)t.defs.size();
        for (int s = 1; s <= 6; s++)
            add("bag-party" + std::to_string(s), { StepTap(button("bag_party.slot" + std::to_string(s))) });
        t.yes = add("bag-yes", { StepTap("bag_yes_no.yes") });
        t.no = add("bag-no", { StepTap("bag_yes_no.no") });
        t.a = add("bag-a", { StepPress(KeyA) });
        t.b = add("bag-b", { StepPress(ActionKeyB) });
        // Back returns to the X menu (or wherever the Bag was opened from).
        t.back = add("bag-back", { StepTap("bag.back"), StepWaitFor(NotInScene(SceneBag), 180) });

        for (size_t i = 0; i < t.defs.size(); i++)
        {
            t.defs[i].name = t.names[i].c_str();
            t.defs[i].summary = t.summaries[i].c_str();
        }
        return t;
    }

    const ActionDef *BagAction(const BagCmd &cmd)
    {
        static const BagActionTable t = MakeBagActions();
        const int a = cmd.arg;
        switch (cmd.step)
        {
        case BagStep::TapTab:    return a >= 0 && a < BagTabCount ? &t.defs[t.tab + a] : nullptr;
        case BagStep::TapRow:    return a >= 1 && a <= 7 ? &t.defs[t.row + a - 1] : nullptr;
        case BagStep::PressUp:   return &t.defs[t.up];
        case BagStep::PressDown: return &t.defs[t.down];
        case BagStep::TapMenu:   return a >= 1 && a <= MenuPositions ? &t.defs[t.menu + a - 1] : nullptr;
        case BagStep::TapParty:  return a >= 0 && a < 6 ? &t.defs[t.party + a] : nullptr;
        case BagStep::TapYes:    return &t.defs[t.yes];
        case BagStep::TapNo:     return &t.defs[t.no];
        case BagStep::PressA:    return &t.defs[t.a];
        case BagStep::PressB:    return &t.defs[t.b];
        case BagStep::TapBack:   return &t.defs[t.back];
        case BagStep::None:      break;
        }
        return nullptr;
    }

    // --- Driver ---

    const char *BagGoalName(BagGoal goal)
    {
        static const char *const names[] = { "none", "tab", "open", "choose", "pick", "answer", "advance", "cancel", "close" };
        return names[(int)goal];
    }

    void BagDriver::Begin(BagGoal goal)
    {
        m_goal = goal;
        m_frames = 0;
        m_stable = 0;
        m_signature = 0;
        m_sent = false;
        m_commands = 0;
        m_startSignature = 0;
        m_failure.clear();
    }

    void BagDriver::Tab(int tab)          { Begin(BagGoal::Tab); m_arg = tab; }
    void BagDriver::Open(int index, u16 id) { Begin(BagGoal::Open); m_arg = index; m_id = id; }
    void BagDriver::Choose(int entry)     { Begin(BagGoal::Choose); m_arg = entry; }
    void BagDriver::Pick(int slot)        { Begin(BagGoal::Pick); m_arg = slot; }
    void BagDriver::Answer(bool yes)      { Begin(BagGoal::Answer); m_arg = yes ? 1 : 0; }
    void BagDriver::Advance(void)         { Begin(BagGoal::Advance); }
    void BagDriver::Cancel(void)          { Begin(BagGoal::Cancel); }
    void BagDriver::Close(void)           { Begin(BagGoal::Close); }

    void BagDriver::Done(std::vector<std::string> &log)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "bag: %s done in %lu frames, %lu commands", BagGoalName(m_goal),
                      (unsigned long)m_frames, (unsigned long)m_commands);
        log.push_back(line);
        m_goal = BagGoal::None;
        m_finished++;
    }

    void BagDriver::Fail(const std::string &why, std::vector<std::string> &log)
    {
        m_failure = std::string(BagGoalName(m_goal)) + ": " + why;
        log.push_back("bag: " + m_failure + " FAILED");
        m_goal = BagGoal::None;
        m_finished++;
    }

    void BagDriver::Refused(const std::string &why, std::vector<std::string> &log)
    {
        if (m_goal != BagGoal::None)
            Fail("action refused: " + why, log);
    }

    BagCmd BagDriver::Send(BagStep step, int arg)
    {
        m_commands++;
        m_stable = 0;
        m_sent = true;
        BagCmd cmd;
        cmd.step = step;
        cmd.arg = arg;
        return cmd;
    }

    BagCmd BagDriver::Update(const BagGame &g, bool actionRunning, std::vector<std::string> &log)
    {
        if (m_goal == BagGoal::None)
            return BagCmd();
        // While an action runs (ours or not) the screen is changing anyway.
        if (actionRunning)
        {
            m_stable = 0;
            return BagCmd();
        }
        m_sent = false;
        m_frames++;
        const u32 sig = g.Signature();
        if (sig != m_signature)
        {
            m_signature = sig;
            m_stable = 0;
        }
        else
            m_stable++;

        if (m_goal == BagGoal::Close && !g.Valid())
        {
            Done(log);
            return BagCmd();
        }
        if (!g.Valid())
        {
            Fail("the Bag is gone", log);
            return BagCmd();
        }
        if (m_frames > Timeout)
        {
            Fail("timed out", log);
            return BagCmd();
        }
        if (m_commands >= MaxCommands)
        {
            Fail("too many taps and presses", log);
            return BagCmd();
        }
        const u32 settle = (g.mode == BagMode::Message || m_goal == BagGoal::Advance) ? MessageSettleFrames : SettleFrames;
        if (m_stable < settle || g.mode == BagMode::Busy)
            return BagCmd();

        switch (m_goal)
        {
        case BagGoal::Tab:
            if (g.mode != BagMode::List)
                break;
            if (g.tab == m_arg)
            {
                if (g.scroll >= 0)
                    Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("the game stays on another pocket", log);
                return BagCmd();
            }
            return Send(BagStep::TapTab, m_arg);

        case BagGoal::Open:
            if (g.mode == BagMode::Menu && !g.YesNo() && g.item == m_id)
            {
                Done(log);
                return BagCmd();
            }
            if (g.mode != BagMode::List)
                break;
            if (m_arg < 0 || m_arg >= (int)g.items.size() || g.items[m_arg].id != m_id)
            {
                Fail("the item isn't where it was", log);
                return BagCmd();
            }
            if (g.scroll < 0)
                return BagCmd();
            if (m_arg >= g.scroll && m_arg < g.scroll + g.visible && m_arg - g.scroll < 7)
                return Send(BagStep::TapRow, m_arg - g.scroll + 1);
            return Send(m_arg > g.Index() ? BagStep::PressDown : BagStep::PressUp);

        case BagGoal::Choose:
            if (m_commands == 0)
            {
                if (g.mode != BagMode::Menu || m_arg < 0 || m_arg >= (int)g.commands.size())
                    break;
                m_startSignature = sig;
                return Send(BagStep::TapMenu, (int)g.commands.size() - m_arg);
            }
            if (g.mode != BagMode::Menu || sig != m_startSignature)
            {
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("the menu doesn't take the tap", log);
                return BagCmd();
            }
            return Send(BagStep::TapMenu, (int)g.commands.size() - m_arg);

        case BagGoal::Pick:
            if (g.mode != BagMode::Pick)
            {
                if (m_commands == 0)
                    break;
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 4)
            {
                Fail("the party slot doesn't take the tap", log);
                return BagCmd();
            }
            // The first tap on a slot moves the cursor there, a tap on the
            // slot under the cursor chooses it.
            return Send(BagStep::TapParty, m_arg);

        case BagGoal::Answer:
            if (!g.YesNo())
            {
                if (m_commands == 0)
                    break;
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 3)
            {
                Fail("Yes / No doesn't take the tap", log);
                return BagCmd();
            }
            return Send(m_arg ? BagStep::TapYes : BagStep::TapNo);

        case BagGoal::Advance:
            if (g.mode == BagMode::Message)
                return Send(BagStep::PressA);
            Done(log);
            return BagCmd();

        case BagGoal::Cancel:
            if (g.mode == BagMode::List)
            {
                Done(log);
                return BagCmd();
            }
            if (m_commands >= 6)
            {
                Fail("the game doesn't go back to the list", log);
                return BagCmd();
            }
            if (g.mode == BagMode::Message)
                return Send(BagStep::PressA);
            if (g.mode == BagMode::Menu && !g.YesNo() && !g.commands.empty() && g.commands.back() == BagCommand::Cancel)
                return Send(BagStep::TapMenu, 1);
            return Send(BagStep::PressB);

        case BagGoal::Close:
            if (g.mode != BagMode::List)
                break;
            if (m_commands >= 2)
            {
                Fail("the Bag doesn't close", log);
                return BagCmd();
            }
            return Send(BagStep::TapBack);

        case BagGoal::None:
            break;
        }

        static const char *const modes[] = { "none", "list", "menu", "pick", "message", "busy" };
        Fail(std::string("unexpected screen: ") + modes[(int)g.mode], log);
        return BagCmd();
    }

    std::string BagDriver::Status(void) const
    {
        if (m_goal == BagGoal::None)
            return m_failure.empty() ? "idle" : "idle, last failed: " + m_failure;
        char line[96];
        std::snprintf(line, sizeof(line), "%s (%lu frames, %lu commands)", BagGoalName(m_goal),
                      (unsigned long)m_frames, (unsigned long)m_commands);
        return line;
    }

    // --- Drawing ---

    static constexpr UI::Color PanelBg   = Rgb(22, 30, 52);
    static constexpr UI::Color Border    = Rgb(120, 150, 220);
    static constexpr UI::Color Divider   = Rgb(120, 150, 220, 90);
    static constexpr UI::Color TextMain  = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim   = Rgb(150, 165, 200);
    static constexpr UI::Color Selected  = Rgb(255, 236, 120);
    static constexpr UI::Color RowFill   = Rgb(36, 48, 82);
    static constexpr UI::Color RowCursor = Rgb(64, 84, 140);
    static constexpr UI::Color TabFill   = Rgb(44, 58, 98);
    static constexpr UI::Color TabOn     = Rgb(226, 118, 34);
    static constexpr UI::Color NewBadge  = Rgb(232, 72, 60);
    static constexpr UI::Color HpGood    = Rgb(72, 200, 96);
    static constexpr UI::Color HpLow     = Rgb(232, 196, 48);
    static constexpr UI::Color HpBad     = Rgb(232, 72, 60);
    static constexpr UI::Color HeldDot   = Rgb(240, 170, 60);
    static constexpr UI::Color MenuBg    = Rgb(30, 40, 70);

    // Geometry inside BagArea.
    static const int Pad = 8;
    static const int TabsY = BagArea.y + 6, TabH = 24, TabGap = 2;
    static const int ListX = BagArea.x + Pad, ListY = TabsY + TabH + 8, ListW = 214, RowH = 20;
    static const int PartyX = ListX + ListW + 8, PartyW = BagArea.Right() - Pad - PartyX, MonH = 26;
    static const int FooterH = 18, FooterY = BagArea.Bottom() - 6 - FooterH;

    static std::string Count(const BagItem &item)
    {
        return "x" + std::to_string(item.count);
    }

    static void DrawTabs(UI::Canvas &canvas, const BagView &view)
    {
        const int w = (BagArea.w - 2 * Pad - (BagTabCount - 1) * TabGap) / BagTabCount;
        for (int i = 0; i < BagTabCount; i++)
        {
            const UI::Rect r = { BagArea.x + Pad + i * (w + TabGap), TabsY, w, TabH };
            const bool on = i == view.tab;
            const bool going = i == view.wantTab && view.wantTab != view.tab;
            canvas.FillRoundRect(r, 8, on ? TabOn : TabFill);
            if (going)
                canvas.StrokeRoundRect(r, 8, Selected, 2);
            UI::TextStyle style;
            style.color = on ? TextMain : TextDim;
            style.align = UI::Align::Center;
            UI::DrawText(canvas, BagTabAt(i).label, UI::Inset(r, 1, 0, 1, 0), style);
        }
    }

    static void DrawList(UI::Canvas &canvas, const BagView &view)
    {
        const UI::Rect area = { ListX, ListY, ListW, BagListRows * RowH };
        UI::TextStyle name;
        UI::TextStyle count;
        count.align = UI::Align::Right;
        const bool listing = view.mode == BagMode::List && (view.wantTab < 0 || view.wantTab == view.tab);
        if (view.items.empty())
        {
            name.color = TextDim;
            name.align = UI::Align::Center;
            UI::DrawText(canvas, "Nothing in this pocket", area, name);
            return;
        }
        for (int row = 0; row < BagListRows; row++)
        {
            const int i = view.top + row;
            if (i >= (int)view.items.size())
                break;
            const UI::Rect r = { area.x, area.y + row * RowH, area.w, RowH - 2 };
            const bool cursor = i == view.cursor;
            canvas.FillRoundRect(r, 6, cursor ? RowCursor : RowFill);
            if (cursor && listing)
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 2, r.y + 3, 12, 12 }, Selected);
            name.color = count.color = listing || cursor ? TextMain : TextDim;
            const std::string label = i < (int)view.names.size() && !view.names[i].empty()
                                      ? view.names[i] : "#" + std::to_string(view.items[i].id);
            int right = r.Right() - 4;
            if (view.items[i].isNew)
            {
                const UI::Rect badge = { right - 8, r.y + 5, 8, 8 };
                canvas.FillRoundRect(badge, 4, NewBadge);
                right -= 12;
            }
            UI::DrawText(canvas, Count(view.items[i]), UI::Rect{ right - 40, r.y, 40, r.h }, count);
            UI::DrawText(canvas, label, UI::Rect{ r.x + 16, r.y, right - 44 - (r.x + 16), r.h }, name);
        }
        // Scroll marks when the list goes on.
        if (view.top > 0)
            UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ area.Right() + 1, area.y + 4, 6, 8 }, TextMain);
        if (view.top + BagListRows < (int)view.items.size())
            UI::DrawIcon(canvas, UI::Icon::ArrowDown, UI::Rect{ area.Right() + 1, area.Bottom() - 12, 6, 8 }, TextMain);
    }

    static void DrawParty(UI::Canvas &canvas, const BagView &view)
    {
        const bool picking = view.mode == BagMode::Pick;
        for (int i = 0; i < (int)view.party.size() && i < 6; i++)
        {
            const BagMon &mon = view.party[i];
            const UI::Rect r = { PartyX, ListY + i * MonH, PartyW, MonH - 2 };
            const bool cursor = picking && i == view.partyCursor;
            canvas.FillRoundRect(r, 6, cursor ? RowCursor : RowFill);
            if (cursor)
                canvas.StrokeRoundRect(UI::Inset(r, -1), 7, Selected, 2);
            UI::TextStyle style;
            style.color = picking || cursor ? TextMain : TextDim;
            const UI::Rect line = { r.x + 6, r.y, r.w - 12, r.h - 5 };
            if (!mon.held.empty())
                canvas.FillRoundRect(UI::Rect{ r.x + 2, r.y + 3, 4, 4 }, 2, HeldDot);
            if (mon.egg)
            {
                UI::DrawText(canvas, "Egg", line, style);
                continue;
            }
            UI::TextStyle hp = style;
            hp.align = UI::Align::Right;
            const std::string hpText = std::to_string(mon.hp) + "/" + std::to_string(mon.maxHp);
            const int hpW = UI::MeasureText(hpText, hp);
            UI::DrawText(canvas, hpText, line, hp);
            UI::DrawText(canvas, mon.name, UI::Rect{ line.x, line.y, line.w - hpW - 4, line.h }, style);
            // HP bar under the line.
            const UI::Rect bar = { r.x + 6, r.Bottom() - 5, r.w - 12, 3 };
            canvas.FillRect(bar, Divider);
            if (mon.maxHp > 0)
            {
                const int filled = bar.w * mon.hp / mon.maxHp;
                const UI::Color c = mon.hp * 2 > mon.maxHp ? HpGood : mon.hp * 5 > mon.maxHp ? HpLow : HpBad;
                canvas.FillRect(UI::Rect{ bar.x, bar.y, filled, bar.h }, c);
            }
        }
    }

    static void DrawMenu(UI::Canvas &canvas, const BagView &view)
    {
        const int n = (int)view.commands.size();
        const int entryH = 22;
        const int h = 26 + n * entryH + 6;
        const UI::Rect box = { PartyX - 6, FooterY - 4 - h, PartyW + 6, h };
        canvas.FillRoundRect(box, 10, MenuBg);
        canvas.StrokeRoundRect(box, 10, Border, 2);
        UI::TextStyle title;
        title.color = Selected;
        UI::DrawText(canvas, view.menuTitle, UI::Rect{ box.x + 8, box.y + 4, box.w - 16, 20 }, title);
        for (int i = 0; i < n; i++)
        {
            const UI::Rect r = { box.x + 6, box.y + 26 + i * entryH, box.w - 12, entryH - 2 };
            const bool cursor = i == view.menuCursor;
            const bool enabled = BagCommandEnabled(view.commands[i], view.tab);
            if (cursor)
                canvas.FillRoundRect(r, 6, RowCursor);
            UI::TextStyle style;
            style.color = enabled ? TextMain : TextDim;
            if (cursor)
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 2, r.y + 4, 12, 12 }, Selected);
            UI::DrawText(canvas, BagCommandLabel(view.commands[i]), UI::Inset(r, 16, 0, 4, 0), style);
        }
    }

    static std::string Hint(const BagView &view)
    {
        switch (view.mode)
        {
        case BagMode::List:
            return "A Choose   B Close   L/R Pocket";
        case BagMode::Menu:
            return "A OK   B Back";
        case BagMode::Pick:
            return "Which Pokémon?   A OK   B Back";
        case BagMode::Message:
            return "...";
        default:
            return "";
        }
    }

    void DrawBag(UI::Canvas &canvas, const BagView &view)
    {
        canvas.FillRoundRect(BagArea, BagRadius, PanelBg);
        canvas.StrokeRoundRect(BagArea, BagRadius, Border, 2);
        DrawTabs(canvas, view);
        DrawList(canvas, view);
        DrawParty(canvas, view);
        canvas.VLine(PartyX - 4, ListY, BagListRows * RowH, Divider);
        if (view.mode == BagMode::Menu && !view.commands.empty())
            DrawMenu(canvas, view);

        UI::TextStyle footer;
        footer.color = view.busy ? TextMain : TextDim;
        const std::string text = !view.status.empty() ? view.status : Hint(view);
        UI::DrawText(canvas, text, UI::Rect{ ListX, FooterY, BagArea.Right() - Pad - ListX, FooterH }, footer);
    }
}
