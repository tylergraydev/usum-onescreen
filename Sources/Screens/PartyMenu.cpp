#include "Screens/PartyMenu.hpp"

#include <cstdio>

#include "UI/GameAssets.hpp"
#include "UI/Layout.hpp"
#include "UI/Theme.hpp"
#include "UI/Motion.hpp"

namespace OneScreen
{
    using UI::Rgb;
    namespace Theme = UI::Theme;

    const char *const SceneTeach = "WazaOshie";

    // HID key bits (same values as CTRPF's Key::), for the actions.
    static const u32 KeyA = 1u << 0;
    static const u32 KeyUp = 1u << 6, KeyDown = 1u << 7;

    // The game's item menu entries (docs/BAG.md). The Party screen's Bag
    // has its own Use 0x06 and Give 0x07 (Escape Rope: 06 07 11; TM: 06 11),
    // the field Bag Use 0x00 and Give 0x03.
    static const u8 CmdGive = 0x03, CmdPartyGive = 0x07, CmdFreeSpace = 0x04, CmdCancel = 0x11, CmdYes = 0x12,
                    CmdNo = 0x13;

    const char *PartyStatusName(u32 status)
    {
        static const char *const names[] = { "", "PAR", "SLP", "FRZ", "BRN", "PSN" };
        return status < 6 ? names[status] : "?";
    }

    const char *PartyPocketName(PartyPocket pocket)
    {
        static const char *const names[] = { "Medicine", "Items", "TMs", "Berries" };
        return (u32)pocket < (u32)PartyPocket::Count ? names[(u32)pocket] : "?";
    }

    const char *PartyPocketButton(PartyPocket pocket)
    {
        static const char *const buttons[] = { "bag.pocket_medicine", "bag.pocket_items", "bag.pocket_tms",
                                               "bag.pocket_berries" };
        return buttons[(u32)pocket < (u32)PartyPocket::Count ? (u32)pocket : 0];
    }

    bool PartyPocketAllowed(PartyPocket pocket, bool give)
    {
        return (u32)pocket < (u32)PartyPocket::Count && !(give && pocket == PartyPocket::TMs);
    }

    PartyPocket PartyNextPocket(PartyPocket pocket, int dir, bool give)
    {
        const int n = (int)PartyPocket::Count;
        int p = (int)pocket;
        for (int i = 0; i < n; i++)
        {
            p = (p + (dir < 0 ? n - 1 : 1)) % n;
            if (PartyPocketAllowed((PartyPocket)p, give))
                return (PartyPocket)p;
        }
        return pocket;
    }

    bool PartyUseViaRestore(PartyPocket pocket)
    {
        return pocket == PartyPocket::Medicine || pocket == PartyPocket::Berries;
    }

    const char *PartyMenuLabel(PartyMenuItem item)
    {
        static const char *const labels[] = { "Summary", "Use item", "Give item", "Take item", "Move", "Cancel" };
        return (u32)item < (u32)PartyMenuItem::Count ? labels[(u32)item] : "?";
    }

    std::vector<PartyMenuItem> PartyMenuEntries(bool holding, int count)
    {
        std::vector<PartyMenuItem> entries = { PartyMenuItem::Summary, PartyMenuItem::UseItem, PartyMenuItem::GiveItem };
        if (holding)
            entries.push_back(PartyMenuItem::TakeItem);
        if (count > 1)
            entries.push_back(PartyMenuItem::Move);
        entries.push_back(PartyMenuItem::Cancel);
        return entries;
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

    void PartyMenuState::Ask(int cursor)
    {
        m_page = PartyPage::Ask;
        m_ask = cursor < 0 ? 0 : cursor;
    }

    void PartyMenuState::EndAsk(void)
    {
        if (m_page == PartyPage::Ask)
            m_page = PartyPage::List;
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

    PartyCommand PartyMenuState::Press(u32 keys, const PartyMenuInput &in)
    {
        PartyCommand cmd;
        const int count = in.count;
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
            const std::vector<PartyMenuItem> entries = PartyMenuEntries(in.holding, count);
            const int n = (int)entries.size();
            if (m_menu >= n)
                m_menu = n - 1;
            if (dy != 0)
                m_menu = (m_menu + dy + n) % n;
            else if (keys & PartyKey::B)
                m_page = PartyPage::List;
            else if (keys & PartyKey::A)
            {
                switch (entries[m_menu])
                {
                case PartyMenuItem::Summary:
                    m_page = PartyPage::List;
                    cmd.kind = PartyCommand::Summary;
                    cmd.pos = m_cursor;
                    break;
                case PartyMenuItem::UseItem:
                case PartyMenuItem::GiveItem:
                    m_give = entries[m_menu] == PartyMenuItem::GiveItem;
                    if (!PartyPocketAllowed(m_pocket, m_give))
                        m_pocket = PartyPocket::Medicine;
                    m_page = PartyPage::Items;
                    m_item = 0;
                    break;
                case PartyMenuItem::TakeItem:
                    m_page = PartyPage::List;
                    cmd.kind = PartyCommand::TakeItem;
                    cmd.pos = m_cursor;
                    break;
                case PartyMenuItem::Move:
                    m_page = PartyPage::Swap;
                    m_swapFrom = m_cursor;
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
            const int n = in.items != nullptr ? (int)in.items->size() : 0;
            if (dy != 0 && n > 0)
                m_item = (m_item + dy + n) % n;
            else if ((keys & (PartyKey::L | PartyKey::R)) || dx != 0)
            {
                m_pocket = PartyNextPocket(m_pocket, (keys & PartyKey::L) || dx < 0 ? -1 : 1, m_give);
                m_item = 0;
            }
            else if (keys & PartyKey::B)
                m_page = PartyPage::Menu;
            else if ((keys & PartyKey::A) && n > 0)
            {
                if (m_item >= n)
                    m_item = n - 1;
                cmd.kind = m_give ? PartyCommand::GiveItem : PartyCommand::UseItem;
                cmd.pos = m_cursor;
                cmd.pocket = m_pocket;
                cmd.item = m_item;
                m_page = PartyPage::List;
            }
            break;
        }

        case PartyPage::Ask:
        {
            const int n = in.choices;
            if (dy != 0 && n > 0)
                m_ask = (m_ask + dy + n) % n;
            else if ((keys & PartyKey::A) && n > 0)
            {
                cmd.kind = PartyCommand::Answer;
                cmd.choice = m_ask < n ? m_ask : n - 1;
                cmd.pos = m_cursor;
                m_page = PartyPage::List;
            }
            else if ((keys & PartyKey::B) && in.cancelChoice >= 0)
            {
                cmd.kind = PartyCommand::Answer;
                cmd.choice = in.cancelChoice;
                cmd.pos = m_cursor;
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
    static const char *const MenuButtons[5] =
    {
        "party_bag_menu.from_bottom1", "party_bag_menu.from_bottom2", "party_bag_menu.from_bottom3",
        "party_bag_menu.from_bottom4", "party_bag_menu.from_bottom5",
    };
    static const char *const TeachButtons[5] =
    {
        "learn_moves.move1", "learn_moves.move2", "learn_moves.move3", "learn_moves.move4", "learn_moves.new",
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

    // Slot menu -> Held Item: its submenu (Use/Give, Take, Move, Back).
    static void OpenHeldMenu(std::vector<ActionStep> &s, int pos)
    {
        OpenSlotMenu(s, pos);
        s.push_back(StepTap("party_slot_menu.held_item"));
        s.push_back(StepWait(15));
    }

    // Into the Bag, then the pocket's tab (the Bag reopens on the pocket it
    // showed last).
    static void EnterBag(std::vector<ActionStep> &s, PartyPocket pocket)
    {
        s.push_back(StepWaitFor(InScene(SceneBag), 180));
        s.push_back(StepWait(30));      // the Bag slides in
        s.push_back(StepTap(PartyPocketButton(pocket)));
        s.push_back(StepWait(20));
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
        EnterBag(s, pocket);
    }

    void MakePartyHeldBagAction(PartyAction &out, int pos, PartyPocket pocket)
    {
        Begin(out, "party-held-bag", "Party: tap the slot, Held Item, Use/Give, the pocket's tab", PartyList());
        std::vector<ActionStep> &s = out.def.steps;
        OpenHeldMenu(s, pos);
        s.push_back(StepTap("party_held_menu.use_give"));
        EnterBag(s, pocket);
    }

    void MakePartyTakeAction(PartyAction &out, int pos)
    {
        Begin(out, "party-take", "Party: tap the slot, Held Item, Take, B through the message", PartyList());
        std::vector<ActionStep> &s = out.def.steps;
        OpenHeldMenu(s, pos);
        s.push_back(StepTap("party_held_menu.take"));
        // "You received the X from Y." stays until B (the bar under it
        // doesn't read as the plain list meanwhile).
        s.push_back(StepWait(40));
        const s8 press = (s8)s.size();
        s.push_back(StepPress(ActionKeyB));
        // Only pressed again if the list didn't come back: one B too many
        // would close the Party screen.
        s.push_back(StepWaitFor(PartyList(), 60).Retry(press, 1));
    }

    void MakePartyBagRowAction(PartyAction &out, int row)
    {
        Begin(out, "party-bag-row", "Party's Bag: tap a shown row (item menu)", InScene(SceneBag));
        out.def.steps.push_back(StepTap(RowButtons[row < 0 ? 0 : row > 6 ? 6 : row]));
        out.def.steps.push_back(StepWait(20));
    }

    void MakePartyBagStepAction(PartyAction &out, bool up)
    {
        Begin(out, up ? "party-bag-up" : "party-bag-down", "Party's Bag: one D-pad step", InScene(SceneBag));
        out.def.steps.push_back(StepPress(up ? KeyUp : KeyDown));
        out.def.steps.push_back(StepWait(10));
    }

    void MakePartyBagTabAction(PartyAction &out, PartyPocket pocket)
    {
        Begin(out, "party-bag-tab", "Party's Bag: tap the pocket's tab", InScene(SceneBag));
        out.def.steps.push_back(StepTap(PartyPocketButton(pocket)));
        out.def.steps.push_back(StepWait(20));
    }

    void MakePartyBagMenuAction(PartyAction &out, int fromBottom)
    {
        const int i = fromBottom < 1 ? 0 : fromBottom > 5 ? 4 : fromBottom - 1;
        char name[32];
        std::snprintf(name, sizeof(name), "party-bag-menu%d", i + 1);
        Begin(out, name, "Party's Bag: tap a menu entry (counted from the bottom)", InScene(SceneBag));
        out.def.steps.push_back(StepTap(MenuButtons[i]));
        out.def.steps.push_back(StepWait(20));
    }

    void MakePartyYesNoAction(PartyAction &out, bool yes)
    {
        Begin(out, yes ? "party-bag-yes" : "party-bag-no", "Party's Bag: answer the question", InScene(SceneBag));
        out.def.steps.push_back(StepTap(yes ? "bag_yes_no.yes" : "bag_yes_no.no"));
        out.def.steps.push_back(StepWait(20));
    }

    void MakePartyMessageAction(PartyAction &out, bool bag)
    {
        Begin(out, bag ? "party-message-a" : "party-message-b", "A / B through a message",
              InScene(bag ? SceneBag : SceneParty));
        out.def.steps.push_back(StepPress(bag ? KeyA : ActionKeyB));
        out.def.steps.push_back(StepWait(15));
    }

    void MakePartyTeachAction(PartyAction &out, int row)
    {
        const int i = row < 0 ? 0 : row > 4 ? 4 : row;
        char name[32];
        std::snprintf(name, sizeof(name), i == 4 ? "party-teach-skip" : "party-teach-forget%d", i + 1);
        Begin(out, name, "Teaching screen: tap the row, then Forget this move / Give up", InScene(SceneTeach));
        std::vector<ActionStep> &s = out.def.steps;
        s.push_back(StepTap(TeachButtons[i]));
        s.push_back(StepWait(30));      // the move's page slides in
        s.push_back(StepTap("learn_confirm.yes"));
        // The screen goes; the Bag prints the result.
        s.push_back(StepWaitFor(NotInScene(SceneTeach), 240));
    }

    // B until the Bag is gone (a message, then the list), then the list.
    static void LeaveBag(std::vector<ActionStep> &s)
    {
        const s8 press = (s8)s.size();
        s.push_back(StepPress(ActionKeyB));
        s.push_back(StepWaitFor(NotInScene(SceneBag), 45).Retry(press, 4));
        s.push_back(StepWaitFor(PartyList(), 180));
    }

    void MakePartyBagBackAction(PartyAction &out)
    {
        Begin(out, "party-bag-back", "Party's Bag: B back to Party", InScene(SceneBag));
        LeaveBag(out.def.steps);
    }

    int PartyItemMenuEntry(const std::vector<u8> &commands, bool give)
    {
        const int n = (int)commands.size();
        for (int k = 0; k < n; k++)
        {
            const u8 c = commands[k];
            const bool isGive = c == CmdGive || c == CmdPartyGive;
            const bool match = give ? isGive : !isGive && c != CmdFreeSpace && c != CmdCancel && c != CmdYes && c != CmdNo;
            if (match)
                return n - k;
        }
        return 0;
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

    PartyFollowDo PartyFollow(PartyGame game, u32 stable)
    {
        // Frames each kind of screen must hold before acting: messages and
        // lists can flicker between two messages.
        switch (game)
        {
        case PartyGame::PartyList:  return stable >= 10 ? PartyFollowDo::Done : PartyFollowDo::Wait;
        case PartyGame::PartyOther: return stable >= 15 ? PartyFollowDo::Advance : PartyFollowDo::Wait;
        case PartyGame::BagList:    return stable >= 20 ? PartyFollowDo::LeaveBag : PartyFollowDo::Wait;
        case PartyGame::BagMessage: return stable >= 12 ? PartyFollowDo::Advance : PartyFollowDo::Wait;
        case PartyGame::BagYesNo:
        case PartyGame::BagMenu:    return stable >= 8 ? PartyFollowDo::Ask : PartyFollowDo::Wait;
        case PartyGame::Teach:      return stable >= 20 ? PartyFollowDo::Teach : PartyFollowDo::Wait;
        default:                    return PartyFollowDo::Wait;
        }
    }

    // --- Drawing ---

    // Party and summary backdrop: the game's green waves, as a gradient.
    static constexpr UI::Color BackTop     = Rgb(0x9A, 0xE8, 0x5A);
    static constexpr UI::Color BackBottom  = Rgb(0x2E, 0xB0, 0x3C);
    static constexpr UI::Color BackLine    = Rgb(0x14, 0x6E, 0x1C);
    static constexpr UI::Color DimVeil     = Rgb(0x00, 0x00, 0x00, 0x60);
    static constexpr UI::Color SwapColor   = Rgb(0x11, 0xB6, 0xFF);
    static constexpr UI::Color StatusBg    = Rgb(0xB0, 0x48, 0xC4);
    static constexpr UI::Color LabelBar    = Rgb(0x14, 0x6E, 0x1C);   // summary label column
    static constexpr UI::Color ValueTop    = Rgb(0xE2, 0xFA, 0xB8);
    static constexpr UI::Color ValueBottom = Rgb(0xC6, 0xF8, 0x8D);
    static constexpr UI::Color MoveFill    = Rgb(0xF2, 0xFF, 0xF2);
    static constexpr UI::Color DimText     = Rgb(0x50, 0x60, 0x50);

    static const int Pad = 6, ColGap = 6, RowGap = 4, FooterH = Theme::PromptHeight, FooterGap = 4;
    static const int CardW = (PartyArea.w - 2 * Pad - ColGap) / 2;
    static const int CardH = (PartyArea.h - 2 * Pad - FooterH - FooterGap - 2 * RowGap) / 3;

    UI::Rect PartyCardRect(int pos)
    {
        if (pos < 0 || pos > 5)
            pos = 0;
        // The right column sits a little lower, as on the game's screen.
        return UI::Rect{ PartyArea.x + Pad + (pos % 2) * (CardW + ColGap),
                         PartyArea.y + Pad + (pos / 2) * (CardH + RowGap) + (pos % 2) * 4, CardW, CardH - 4 };
    }

    static UI::Rect FooterRect(void)
    {
        return UI::Rect{ PartyArea.x + Pad, PartyArea.Bottom() - Pad - FooterH, PartyArea.w - 2 * Pad, FooterH };
    }

    static std::string Format(const char *fmt, int a, int b = 0)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, a, b);
        return buf;
    }

    static void Backdrop(UI::Canvas &canvas, const UI::Rect &area)
    {
        canvas.FillRoundRect(area, PartyRadius, BackLine);
        const UI::Rect in = UI::Inset(area, 2);
        for (int y = 0; y < in.h; y++)
        {
            const int t = in.h > 1 ? y * 255 / (in.h - 1) : 0;
            const UI::Color c = Rgb((u8)(BackTop.r + (BackBottom.r - BackTop.r) * t / 255),
                                    (u8)(BackTop.g + (BackBottom.g - BackTop.g) * t / 255),
                                    (u8)(BackTop.b + (BackBottom.b - BackTop.b) * t / 255));
            // Rounded corners: skip the corner pixels outside the radius.
            int inset = 0;
            const int r = PartyRadius - 2;
            const int dy = y < r ? r - y : y >= in.h - r ? y - (in.h - r - 1) : 0;
            while (inset < r && (r - inset) * (r - inset) + dy * dy > r * r)
                inset++;
            if (dy == 0)
                inset = 0;
            canvas.HLine(in.x + inset, in.y + y, in.w - 2 * inset, c);
        }
        // A few soft diagonal bands for the game's waves.
        // Their round ends extend past the endpoints; keep them inside the backdrop.
        const UI::Rect clip = canvas.GetClip();
        canvas.SetClip(UI::Intersect(clip, in));
        for (int k = 0; k < 6; k++)
            canvas.Line((float)(in.x + k * 70), (float)in.Bottom(), (float)(in.x + k * 70 + 90), (float)in.y, 10.0f,
                        Rgb(0xFF, 0xFF, 0xFF, 0x14));
        canvas.SetClip(clip);
    }

    static Theme::TextStyle Style(Theme::TextSize size, UI::Color color, UI::Align align = UI::Align::Left)
    {
        Theme::TextStyle s;
        s.size = size;
        s.color = color;
        s.align = align;
        return s;
    }

    // A status badge ending at `right`; returns its left edge.
    static int DrawStatus(UI::Canvas &canvas, u32 status, int right, int y, int h)
    {
        const char *label = PartyStatusName(status);
        if (label[0] == '\0')
            return right;
        const Theme::TextStyle style = Style(Theme::TextSize::Small, Theme::WindowText, UI::Align::Center);
        const int w = Theme::MeasureText(label, Theme::TextSize::Small) + 8;
        const UI::Rect badge = { right - w, y, w, h };
        canvas.FillRoundRect(badge, h / 2, StatusBg);
        Theme::DrawText(canvas, label, badge, style);
        return badge.x;
    }

    // A plate as on the game's Party screen: a green list-row plate, name
    // and gender, Lv., the HP bar with its numbers, the held item.
    static void DrawCard(UI::Canvas &canvas, const PartyMonView &mon, const UI::Rect &r, bool selected, bool source, unsigned slot,
                         const UI::Rect &cursor, bool deferHighlight)
    {
        Theme::DrawRowBackground(canvas, r, Theme::RowKind::List);
        if (source)
            canvas.StrokeRoundRect(UI::Inset(r, -2), Theme::RowRadius + 2, SwapColor, 3);
        if (selected && !deferHighlight)
        {
            Theme::DrawHighlightFrame(canvas, UI::Inset(cursor, -1), Theme::RowRadius + 1);
            Theme::DrawHighlightFrame(canvas, UI::Inset(cursor, -3), Theme::RowRadius + 3);
        }

        const Theme::TextStyle text = Style(Theme::TextSize::Body, Theme::ListText);
        const Theme::TextStyle small = Style(Theme::TextSize::Small, Theme::ListText);
        const int line = Theme::LineHeight(Theme::TextSize::Body);
        const int smallLine = Theme::LineHeight(Theme::TextSize::Small);
        const UI::Rect inner = UI::Inset(r, 14, 4, 12, 4);

        if (!mon.valid || mon.egg)
        {
            Theme::DrawText(canvas, mon.valid ? std::string("Egg") : std::string("?"),
                            UI::Rect{ inner.x, inner.y, inner.w, line }, text);
            return;
        }

        // Line 1: name and gender (as the game: the gender sign at the
        // right end).
        const int haW = mon.hiddenAbility ? Theme::MeasureText("HA", Theme::TextSize::Small) + 6 : 0;
        const int nameRoom = inner.w - 16 - (mon.shiny ? 14 : 0) - (haW ? haW + 4 : 0);
        const int nameW = Theme::DrawText(canvas, mon.name, UI::Rect{ inner.x, inner.y, nameRoom, line }, text);
        if (mon.shiny)
            Theme::DrawShiny(canvas, inner.x + nameW + 2, inner.y + (line - 10) / 2);
        if (haW)
        {
            const UI::Rect pill = { inner.Right() - 20 - haW, inner.y + 3, haW, line - 6 };
            canvas.FillRoundRect(pill, 3, Theme::AccentText);
            Theme::DrawText(canvas, "HA", pill, Style(Theme::TextSize::Small, Theme::WindowLine, UI::Align::Center));
        }
        if (mon.gender < 2)
            Theme::DrawGender(canvas, inner.Right() - 12, inner.y + (line - 12) / 2, mon.gender == 1);

        // Line 2: Lv. (and status), the HP bar.
        const int y2 = inner.y + line;
        const std::string lv = Format("Lv.%d", mon.level);
        int x2 = inner.x + Theme::DrawText(canvas, lv, UI::Rect{ inner.x, y2, inner.w / 2, smallLine }, small) + 6;
        const char *status = PartyStatusName(mon.status);
        if (status[0] != '\0')
        {
            const int w = Theme::MeasureText(status, Theme::TextSize::Small) + 8;
            DrawStatus(canvas, mon.status, x2 + w, y2 + 1, smallLine - 2);
            x2 += w + 4;
        }
        unsigned identity = 2166136261u;
        for (unsigned char c : mon.name) identity = (identity ^ c) * 16777619u;
        for (unsigned char c : mon.species) identity = (identity ^ c) * 16777619u;
        identity = (identity ^ mon.gender) * 16777619u;
        const int hpValue = UI::Motion::Gauge(UI::Motion::Screen::Party, slot, mon.hp, mon.maxHp, identity);
        Theme::DrawHpBar(canvas, UI::Rect{ x2, y2 + (smallLine - Theme::GaugeHeight - 3) / 2, inner.Right() - x2,
                                           Theme::GaugeHeight + 3 }, hpValue, mon.maxHp);

        // Line 3: held item, HP numbers.
        const int y3 = y2 + smallLine;
        const std::string hp = Format("%d/%d", mon.hp, mon.maxHp);
        const int hpW = Theme::MeasureText(hp, Theme::TextSize::Small);
        Theme::DrawText(canvas, hp, UI::Rect{ inner.Right() - hpW, y3, hpW, smallLine }, small);
        Theme::DrawText(canvas, mon.item.empty() ? std::string("No item") : mon.item,
                        UI::Rect{ inner.x, y3, inner.w - hpW - 6, smallLine },
                        Style(Theme::TextSize::Small, mon.item.empty() ? DimText : Theme::ListText));
    }

    // The game's question window: white text on the dark window, up to two
    // lines ('\n' between).
    static void DrawQuestion(UI::Canvas &canvas, const UI::Rect &r, const std::string &text)
    {
        Theme::DrawWindow(canvas, r);
        const size_t nl = text.find('\n');
        // The game's text is narrower than the system font: a line that
        // doesn't fit goes down to the small size.
        const int room = r.w - 2 * Theme::WindowPad - 4;
        const bool fits = Theme::MeasureText(text.substr(0, nl), Theme::TextSize::Body) <= room
                          && (nl == std::string::npos || Theme::MeasureText(text.substr(nl + 1), Theme::TextSize::Body) <= room);
        const Theme::TextSize size = fits ? Theme::TextSize::Body : Theme::TextSize::Small;
        const Theme::TextStyle white = Style(size, Theme::WindowText);
        const int line = Theme::LineHeight(size);
        const int lines = nl == std::string::npos ? 1 : 2;
        int y = r.y + (r.h - lines * line) / 2;
        Theme::DrawText(canvas, text.substr(0, nl), UI::Rect{ r.x + Theme::WindowPad + 2, y, r.w - 2 * Theme::WindowPad - 4, line },
                        white);
        if (lines == 2)
            Theme::DrawText(canvas, text.substr(nl + 1),
                            UI::Rect{ r.x + Theme::WindowPad + 2, y + line, r.w - 2 * Theme::WindowPad - 4, line }, white);
    }

    // Option pills stacked from `top`, the cursor on `cursor`.
    static void DrawOptions(UI::Canvas &canvas, int x, int top, int w, const std::vector<std::string> &labels, int cursor)
    {
        for (int i = 0; i < (int)labels.size(); i++)
            Theme::DrawRow(canvas, UI::Rect{ x, top + i * Theme::RowPitch, w, Theme::OptionHeight },
                           Theme::RowKind::Option, labels[i], i == cursor);
    }

    static std::string MoveName(u16 move, const std::string &known)
    {
        if (!known.empty())
            return known;
        const UI::GameAssets *assets = UI::GetGameAssets();
        const std::string name = assets != nullptr ? assets->MoveName(move) : std::string();
        return name.empty() ? Format("Move %d", move) : name;
    }

    // A move as a light row: type plate, name, PP (max from the move table
    // when it is loaded).
    static void DrawMoveRow(UI::Canvas &canvas, const UI::Rect &r, u16 move, const std::string &name, u8 pp, bool selected)
    {
        Theme::DrawRowBackground(canvas, r, Theme::RowKind::List);
        const UI::GameAssets *assets = UI::GetGameAssets();
        const UI::MoveInfo info = assets != nullptr ? assets->Move(move) : UI::MoveInfo();
        int x = r.x + Theme::RowTextInset;
        if (info.valid)
        {
            Theme::DrawTypePlate(canvas, UI::Rect{ x, r.y + (r.h - Theme::TypePlateH) / 2, Theme::TypePlateW, Theme::TypePlateH },
                                 info.type, Theme::Upper(assets->TypeName(info.type)));
            x += Theme::TypePlateW + 6;
        }
        const std::string ppText = info.valid && info.pp ? Format("%d/%d", pp, info.pp) : Format("PP %d", pp);
        const int ppW = Theme::MeasureText("88/88", Theme::TextSize::Small) + 4;
        Theme::TextStyle ppStyle = Style(Theme::TextSize::Small, Theme::ListText, UI::Align::Right);
        Theme::DrawText(canvas, ppText, UI::Rect{ r.Right() - 12 - ppW, r.y, ppW, r.h }, ppStyle);
        Theme::DrawText(canvas, name, UI::Rect{ x, r.y, r.Right() - 16 - ppW - x, r.h },
                        Style(Theme::TextSize::Body, Theme::ListText));
        if (selected)
            Theme::DrawCursor(canvas, r.x + 14, r.y + r.h / 2);
    }

    static void DrawPrompts(UI::Canvas &canvas, const std::vector<std::pair<Theme::Button, std::string>> &prompts)
    {
        const UI::Rect f = FooterRect();
        int x = f.x;
        for (const auto &p : prompts)
            x += Theme::DrawButtonPrompt(canvas, x, f.y, p.first, p.second) + 4;
    }

    // A message along the bottom, in the game's window.
    static void DrawFooterMessage(UI::Canvas &canvas, const std::string &text)
    {
        const UI::Rect f = FooterRect();
        const UI::Rect r = { f.x, f.Bottom() - 26, f.w, 26 };
        Theme::DrawWindow(canvas, r);
        Theme::DrawText(canvas, text, UI::Inset(r, Theme::WindowPad + 2, 0, Theme::WindowPad, 0),
                        Style(Theme::TextSize::Small, Theme::WindowText));
    }

    static const int ItemRowsShown = 5;

    void DrawParty(UI::Canvas &canvas, const PartyView &view)
    {
        Backdrop(canvas, PartyArea);

        const bool popup = view.page == PartyPage::Menu || view.page == PartyPage::Items || view.page == PartyPage::Ask;
        const bool selection = view.cursor >= 0 && view.cursor < (int)view.mons.size() && view.cursor < 6
                               && view.busy.empty() && view.page != PartyPage::Ask;
        const UI::Rect target = selection ? PartyCardRect(view.cursor) : UI::Rect{};
        const UI::Rect cursor = selection ? UI::Motion::Cursor(UI::Motion::Screen::Party, target) : target;
        const bool deferHighlight = selection && (cursor.x != target.x || cursor.y != target.y);
        for (int i = 0; i < (int)view.mons.size() && i < 6; i++)
            DrawCard(canvas, view.mons[i], PartyCardRect(i), i == view.cursor && view.busy.empty() && view.page != PartyPage::Ask,
                     view.page == PartyPage::Swap && i == view.swapFrom, (unsigned)i, cursor, deferHighlight);
        // While moving, draw the same two frames after the static cards so
        // later cards cannot paint over the travelling highlight.
        if (deferHighlight)
        {
            Theme::DrawHighlightFrame(canvas, UI::Inset(cursor, -1), Theme::RowRadius + 1);
            Theme::DrawHighlightFrame(canvas, UI::Inset(cursor, -3), Theme::RowRadius + 3);
        }
        if (view.mons.empty())
            Theme::DrawText(canvas, "Reading the party...", PartyCardRect(0),
                            Style(Theme::TextSize::Body, Theme::ListText, UI::Align::Center));
        if (popup || !view.busy.empty())
            canvas.FillRoundRect(UI::Inset(PartyArea, 2), PartyRadius - 2, DimVeil);

        const std::string name = view.cursor >= 0 && view.cursor < (int)view.mons.size()
                                 ? view.mons[view.cursor].name : std::string();
        const bool holding = view.cursor >= 0 && view.cursor < (int)view.mons.size() && view.mons[view.cursor].itemId != 0;
        const int qx = PartyArea.x + Pad, qw = PartyArea.w - 2 * Pad;
        const UI::Rect question = { qx, PartyArea.y + Pad, qw, 48 };

        if (view.page == PartyPage::Menu)
        {
            // As the game's: the question on top, the options down the
            // right, touching (28 px pitch), no prompts under them.
            const UI::Rect q = { qx, question.y, qw, 44 };
            DrawQuestion(canvas, q, "What would you like to do\nwith " + name + "?");
            std::vector<std::string> labels;
            for (PartyMenuItem item : PartyMenuEntries(holding, (int)view.mons.size()))
                labels.push_back(PartyMenuLabel(item));
            const int w = 160;
            DrawOptions(canvas, PartyArea.Right() - Pad - w, q.Bottom() + 4, w, labels, view.menuCursor);
        }
        else if (view.page == PartyPage::Items)
        {
            DrawQuestion(canvas, question, std::string(view.giving ? "Give " : "Use ") + "an item " +
                                           (view.giving ? "to " : "on ") + name + "\n< " +
                                           PartyPocketName(view.pocket) + " >");
            int first = view.itemCursor - ItemRowsShown / 2;
            if (first > (int)view.items.size() - ItemRowsShown)
                first = (int)view.items.size() - ItemRowsShown;
            if (first < 0)
                first = 0;
            const int w = 230, x = PartyArea.Right() - Pad - w;
            int y = question.Bottom() + 4;
            if (view.items.empty())
                Theme::DrawRow(canvas, UI::Rect{ x, y, w, Theme::RowHeight }, Theme::RowKind::List, "(nothing here)", false);
            for (int i = first; i < (int)view.items.size() && i < first + ItemRowsShown; i++, y += Theme::RowPitch)
            {
                const UI::Rect row = { x, y, w, Theme::RowHeight };
                Theme::DrawRow(canvas, row, Theme::RowKind::List, view.items[i].name, i == view.itemCursor);
                Theme::DrawText(canvas, Format("x%d", view.items[i].count), UI::Rect{ row.x, row.y, row.w - 14, row.h },
                                Style(Theme::TextSize::Small, Theme::ListText, UI::Align::Right));
            }
            if (first > 0)
                Theme::DrawText(canvas, "^", UI::Rect{ x - 14, question.Bottom() + 4, 12, 19 },
                                Style(Theme::TextSize::Body, Theme::WindowText));
            if (first + ItemRowsShown < (int)view.items.size())
                Theme::DrawText(canvas, "v", UI::Rect{ x - 14, y - 20, 12, 19 }, Style(Theme::TextSize::Body, Theme::WindowText));
        }
        else if (view.page == PartyPage::Ask)
        {
            const bool two = view.ask.title.find('\n') != std::string::npos;
            const UI::Rect q = { qx, PartyArea.y + Pad, qw, two ? 48 : 30 };
            DrawQuestion(canvas, q, Theme::FormatPanelHint(view.ask.title));
            if (view.ask.moves)
            {
                const int w = 300, x = PartyArea.Right() - Pad - w, h = 28;
                for (int i = 0; i < (int)view.ask.choices.size(); i++)
                {
                    const PartyChoice &c = view.ask.choices[i];
                    const UI::Rect row = { x, q.Bottom() + 4 + i * (h + 2) + (i == 4 ? 4 : 0), w, h };
                    if (c.move != 0)
                        DrawMoveRow(canvas, row, c.move, c.label, c.pp, i == view.askCursor);
                    else
                        Theme::DrawRow(canvas, row, Theme::RowKind::Option, c.label, i == view.askCursor);
                }
            }
            else
            {
                std::vector<std::string> labels;
                for (const PartyChoice &c : view.ask.choices)
                    labels.push_back(c.label);
                const int w = 160;
                DrawOptions(canvas, PartyArea.Right() - Pad - w, q.Bottom() + 8, w, labels, view.askCursor);
            }
        }

        if (!view.busy.empty())
            DrawFooterMessage(canvas, view.busy);
        else if (!view.note.empty() && view.page == PartyPage::List)
            DrawFooterMessage(canvas, Theme::FormatPanelHint(view.note));
        else
            switch (view.page)
            {
            case PartyPage::List:
                DrawPrompts(canvas, { { Theme::Button::A, "Choose" }, { Theme::Button::Y, "Move" }, { Theme::Button::B, "Close" } });
                break;
            case PartyPage::Menu:
                break;
            case PartyPage::Swap:
                DrawFooterMessage(canvas, "Move " + (view.swapFrom >= 0 && view.swapFrom < (int)view.mons.size()
                                                     ? view.mons[view.swapFrom].name : std::string())
                                          + Theme::FormatPromptText(" where?   A here, B cancel"));
                break;
            case PartyPage::Items:
                DrawPrompts(canvas, { { Theme::Button::A, view.giving ? "Give" : "Use" }, { Theme::Button::L, "" }, { Theme::Button::R, "Pocket" },
                                      { Theme::Button::B, "Back" } });
                break;
            case PartyPage::Ask:
                // The move list reaches the bottom (and B isn't an answer there).
                if (!view.ask.moves && !view.ask.choices.empty())
                    DrawPrompts(canvas, { { Theme::Button::A, "Choose" }, { Theme::Button::B, view.ask.choices.back().label } });
                break;
            }
    }

    // A summary row: the label in a dark green bar, the value on light green.
    static void DrawInfoRow(UI::Canvas &canvas, const UI::Rect &r, const std::string &label, const std::string &value,
                            UI::Color valueColor)
    {
        const int labelW = 62;
        canvas.FillRoundRect(r, 4, ValueBottom);
        canvas.FillRoundRect(UI::Rect{ r.x, r.y, r.w, r.h / 2 }, 4, ValueTop);
        canvas.FillRoundRect(UI::Rect{ r.x, r.y, labelW, r.h }, 4, LabelBar);
        Theme::DrawText(canvas, label, UI::Rect{ r.x + 6, r.y, labelW - 8, r.h }, Style(Theme::TextSize::Small, Theme::WindowText));
        const Theme::TextSize size = Theme::MeasureText(value, Theme::TextSize::Body) <= r.w - labelW - 10
                                     ? Theme::TextSize::Body : Theme::TextSize::Small;
        Theme::DrawText(canvas, value, UI::Rect{ r.x + labelW + 6, r.y, r.w - labelW - 10, r.h }, Style(size, valueColor));
    }

    const char *JudgeWord(u8 iv, bool hyperTrained)
    {
        if (hyperTrained) return "Hyper trained";
        if (iv == 31) return "Best";
        if (iv == 30) return "Fantastic";
        if (iv >= 26) return "Very good";
        if (iv >= 16) return "Pretty good";
        if (iv >= 1) return "Decent";
        return "No good";
    }

    static void DrawJudge(UI::Canvas &canvas, const PartyMonView &mon, int x, int y, int w)
    {
        static const char *const stats[6] = { "HP", "Atk", "Def", "SpA", "SpD", "Spe" };
        const int labelW = Theme::MeasureText("SpA", Theme::TextSize::Small) + 12;
        const int evW = Theme::MeasureText("252", Theme::TextSize::Small) + 8;
        const int barW = 46, gap = 6;
        const int barX = x + w - barW - gap;
        const int evX = barX - gap - evW;
        const int ivX = x + labelW + gap;
        Theme::DrawText(canvas, "Judge / IV", UI::Rect{ ivX, y, evX - ivX, 18 },
                        Style(Theme::TextSize::Small, Theme::WindowText));
        Theme::DrawText(canvas, "EV / 252", UI::Rect{ evX, y, x + w - evX, 18 },
                        Style(Theme::TextSize::Small, Theme::WindowText));
        y += 18;
        int total = 0;
        for (int i = 0; i < 6; i++, y += 19)
        {
            const UI::Rect r = { x, y, w, 18 };
            canvas.FillRoundRect(r, 4, ValueBottom);
            canvas.FillRoundRect(UI::Rect{ x, y, labelW, r.h }, 4, LabelBar);
            Theme::DrawText(canvas, stats[i], UI::Rect{ x + 5, y, labelW - 6, r.h },
                            Style(Theme::TextSize::Small, Theme::WindowText));
            Theme::DrawText(canvas, Format("%d ", mon.ivs[i]) + JudgeWord(mon.ivs[i], mon.hyperTrained[i]),
                            UI::Rect{ ivX, y, evX - gap - ivX, r.h }, Style(Theme::TextSize::Small, Theme::AccentText));
            Theme::DrawText(canvas, Format("%d", mon.evs[i]), UI::Rect{ evX, y, evW, r.h },
                            Style(Theme::TextSize::Small, Theme::AccentText, UI::Align::Right));
            // Gauge geometry is the theme's, with a fixed per-stat EV limit.
            Theme::DrawHpBar(canvas, UI::Rect{ barX, y + 4, barW, Theme::GaugeHeight + 3 }, mon.evs[i], 252);
            total += mon.evs[i];
        }
        Theme::DrawText(canvas, Format("EV total: %d / 510", total), UI::Rect{ x + 4, y, w - 8, 18 },
                        Style(Theme::TextSize::Small, Theme::WindowText));
    }

    void DrawSummary(UI::Canvas &canvas, const SummaryView &view)
    {
        Backdrop(canvas, PartyArea);
        const PartyMonView &mon = view.mon;
        const Theme::TextStyle white = Style(Theme::TextSize::Body, Theme::WindowText);
        const Theme::TextStyle whiteSmall = Style(Theme::TextSize::Small, Theme::WindowText);
        const int x = PartyArea.x + Pad, w = PartyArea.w - 2 * Pad;
        int y = PartyArea.y + Pad;

        // Header window: name, gender, species, Lv., position.
        const UI::Rect head = { x, y, w, 32 };
        Theme::DrawWindow(canvas, head);
        const UI::Rect hin = UI::Inset(head, Theme::WindowPad + 2, 0, Theme::WindowPad + 2, 0);
        const std::string where = Format("%d/%d", view.pos + 1, view.count);
        const int whereW = Theme::MeasureText(where, Theme::TextSize::Small);
        Theme::DrawText(canvas, where, UI::Rect{ hin.Right() - whereW, hin.y, whereW, hin.h }, whiteSmall);
        int hx = hin.x;
        hx += Theme::DrawText(canvas, mon.valid ? (mon.egg ? std::string("Egg") : mon.name) : std::string("?"),
                              UI::Rect{ hx, hin.y, mon.shiny && !mon.egg ? 136 : 150, hin.h }, white);
        if (mon.valid && !mon.egg && mon.shiny)
        {
            Theme::DrawShiny(canvas, hx + 2, hin.y + (hin.h - 10) / 2);
            hx += 14;
        }
        if (mon.valid && !mon.egg && mon.gender < 2)
        {
            Theme::DrawGender(canvas, hx + 4, hin.y + (hin.h - 12) / 2, mon.gender == 1);
            hx += 18;
        }
        if (mon.valid && !mon.egg)
        {
            hx += 8;
            hx += Theme::DrawText(canvas, Format("Lv.%d", mon.level), UI::Rect{ hx, hin.y, 60, hin.h }, whiteSmall);
            if (mon.species != mon.name)
                Theme::DrawText(canvas, mon.species, UI::Rect{ hx + 10, hin.y, hin.Right() - whereW - 10 - hx - 10, hin.h },
                                Style(Theme::TextSize::Small, Theme::AccentText));
        }
        y = head.Bottom() + 6;
        if (!mon.valid || mon.egg)
        {
            DrawPrompts(canvas, { { Theme::Button::X, view.judge ? "Details" : "Judge" },
                                  { Theme::Button::Y, "Game page" }, { Theme::Button::B, "Back" } });
            return;
        }
        if (view.judge)
        {
            DrawJudge(canvas, mon, x, y, w);
            DrawPrompts(canvas, { { Theme::Button::X, "Details" }, { Theme::Button::Y, "Game page" }, { Theme::Button::B, "Back" } });
            return;
        }

        // Left: HP, status, item, ability, nature.
        const int leftW = 196, rowH = 24, gap = 4;
        {
            const UI::Rect r = { x, y, leftW, rowH };
            canvas.FillRoundRect(r, 4, ValueBottom);
            canvas.FillRoundRect(UI::Rect{ r.x, r.y, 62, r.h }, 4, LabelBar);
            Theme::DrawText(canvas, "HP", UI::Rect{ r.x + 6, r.y, 30, r.h }, whiteSmall);
            DrawStatus(canvas, mon.status, r.x + 60, r.y + 4, r.h - 8);
            Theme::DrawText(canvas, Format("%d/%d", mon.hp, mon.maxHp), UI::Rect{ r.x + 66, r.y, r.w - 72, r.h },
                            Style(Theme::TextSize::Body, Theme::AccentText, UI::Align::Right));
            Theme::DrawHpBar(canvas, UI::Rect{ r.x, r.Bottom() + 2, leftW, Theme::GaugeHeight + 3 }, mon.hp, mon.maxHp);
        }
        y += rowH + Theme::GaugeHeight + 3 + 2 + gap;
        const std::string rows[3][2] =
        {
            { "Item", mon.item.empty() ? std::string("None") : mon.item },
            { "Ability", mon.ability.empty() ? std::string("-") : mon.ability },
            { "Nature", mon.nature.empty() ? std::string("-") : mon.nature },
        };
        for (const auto &row : rows)
        {
            const bool ha = mon.hiddenAbility && row[0] == "Ability";
            const int pillW = ha ? Theme::MeasureText("HA", Theme::TextSize::Small) + 6 : 0;
            DrawInfoRow(canvas, UI::Rect{ x, y, leftW - (ha ? pillW + 2 : 0), rowH }, row[0], row[1], Theme::AccentText);
            if (ha)
            {
                const int px = x + leftW - pillW;
                canvas.FillRoundRect(UI::Rect{ px, y + 3, pillW, rowH - 6 }, 6, LabelBar);
                Theme::DrawText(canvas, "HA", UI::Rect{ px, y + 3, pillW, rowH - 6 },
                                Style(Theme::TextSize::Small, Theme::WindowText, UI::Align::Center));
            }
            y += rowH + gap;
        }

        // Right: the four moves with type plate, category icon and PP.
        const UI::GameAssets *assets = UI::GetGameAssets();
        const int mx = x + leftW + 8, mw = w - leftW - 8, mh = 40;
        int my = PartyArea.y + Pad + 32 + 6;
        for (int i = 0; i < 4; i++, my += mh + 3)
        {
            const UI::Rect r = { mx, my, mw, mh };
            canvas.FillRoundRect(r, 8, BackLine);
            canvas.FillRoundRect(UI::Inset(r, 2), 6, MoveFill);
            if (mon.moves[i] == 0)
            {
                Theme::DrawText(canvas, "-", UI::Inset(r, 12, 0, 8, 0), Style(Theme::TextSize::Body, DimText));
                continue;
            }
            const UI::MoveInfo info = assets != nullptr ? assets->Move(mon.moves[i]) : UI::MoveInfo();
            const int top = r.y + 3, bottom = r.y + 21;
            int nx = r.x + 8;
            if (info.valid)
            {
                Theme::DrawTypePlate(canvas, UI::Rect{ nx, top + 2, Theme::TypePlateW, Theme::TypePlateH }, info.type,
                                     Theme::Upper(assets->TypeName(info.type)));
                nx += Theme::TypePlateW + 6;
            }
            Theme::DrawText(canvas, MoveName(mon.moves[i], mon.moveNames[i]), UI::Rect{ nx, top, r.Right() - 8 - nx, 19 },
                            Style(Theme::TextSize::Body, Theme::ListText));
            if (info.valid)
                Theme::DrawCategoryIcon(canvas, r.x + 8 + (Theme::TypePlateW - 42) / 2, bottom - 1, info.category);
            const std::string pp = info.valid && info.pp ? Format("PP %d/%d", mon.pp[i], info.pp) : Format("PP %d", mon.pp[i]);
            Theme::DrawText(canvas, pp, UI::Rect{ r.x + 8, bottom, r.w - 16, 17 },
                            Style(Theme::TextSize::Small, Theme::AccentText, UI::Align::Right));
        }

        // Keep the details prompts below the left column: short labels, so
        // they don't run into the fourth move's category / PP.
        DrawPrompts(canvas, { { Theme::Button::X, "Judge" }, { Theme::Button::Y, "Game" }, { Theme::Button::B, "Back" } });
    }

    static std::string MonKey(const PartyMonView &m)
    {
        std::string k = m.valid ? "v" : "-";
        k += m.egg ? "e" : "";
        k += m.shiny ? "*" : "";
        k += m.hiddenAbility ? "ha" : "";
        for (int i = 0; i < 6; i++)
            k += Format("/%d:%d", m.ivs[i], m.evs[i]) + (m.hyperTrained[i] ? "t" : "");
        k += m.name + "|" + m.species + "|" + m.item + "|" + m.ability + "|" + m.nature;
        k += Format("|%d|%d", m.level, m.hp) + Format("|%d|%d", m.maxHp, (int)m.status) + Format("|%d|%d", m.gender, m.itemId);
        for (int i = 0; i < 4; i++)
            k += Format("|%d:%d", m.moves[i], m.pp[i]) + m.moveNames[i];
        return k;
    }

    std::string PartyViewKey(const PartyView &v)
    {
        std::string k = Format("%d/%d", (int)v.page, v.cursor) + Format("/%d/%d", v.menuCursor, v.swapFrom)
                      + Format("/%d/%d", (int)v.pocket, v.itemCursor) + Format("/%d/%d", v.giving, v.askCursor)
                      + "/" + v.busy + "/" + v.note + "/" + v.ask.title + (v.ask.moves ? "m" : "");
        for (const PartyMonView &m : v.mons)
            k += "#" + MonKey(m);
        for (const PartyBagItem &i : v.items)
            k += Format("@%d:%d", i.id, i.count) + i.name;
        for (const PartyChoice &c : v.ask.choices)
            k += Format("?%d:%d", c.move, c.pp) + c.label;
        return k;
    }

    std::string SummaryViewKey(const SummaryView &v)
    {
        return Format("%d/%d", v.pos, v.count) + (v.hidden ? "h" : "s") + (v.judge ? "j" : "d") + MonKey(v.mon);
    }
}
