// Host tests for the native Party screen (Sources/Screens/PartyMenu.cpp):
// cursor moves, pages, the follow-up decisions, the actions against a fake
// Party screen / its Bags / the teaching screen / summary, and drawing.
// Build and run: make -C tests. With path arguments, also writes previews
// of the party list and the summary there (PPM), and of the menu and the
// teaching question next to the first.

#include "Screens/PartyMenu.hpp"
#include "UI/Layout.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static void TestMove(void)
{
    // 0 1
    // 2 3
    // 4 5
    CHECK(PartyMove(0, 1, 0, 6) == 1);
    CHECK(PartyMove(1, 1, 0, 6) == 1);      // no wrap
    CHECK(PartyMove(1, -1, 0, 6) == 0);
    CHECK(PartyMove(0, -1, 0, 6) == 0);
    CHECK(PartyMove(0, 0, 1, 6) == 2);
    CHECK(PartyMove(3, 0, 1, 6) == 5);
    CHECK(PartyMove(5, 0, 1, 6) == 5);
    CHECK(PartyMove(4, 0, -1, 6) == 2);
    CHECK(PartyMove(1, 0, -1, 6) == 1);
    // Smaller parties: nothing to move to stays.
    CHECK(PartyMove(2, 1, 0, 3) == 2);
    CHECK(PartyMove(1, 0, 1, 3) == 1);
    CHECK(PartyMove(0, 0, 1, 3) == 2);
    CHECK(PartyMove(0, 1, 0, 1) == 0);
    CHECK(PartyMove(7, 1, 0, 6) == 0);      // out of range: back to the first
}

static std::vector<PartyBagItem> Items(int n)
{
    std::vector<PartyBagItem> items;
    for (int i = 0; i < n; i++)
        items.push_back({ (u16)(17 + i), (u16)(i + 1), "Item" });
    return items;
}

static void TestState(void)
{
    const std::vector<PartyBagItem> items = Items(3);
    PartyMenuInput in;
    in.count = 6;
    in.items = &items;
    PartyMenuState s;
    s.Reset(6);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 0);
    // B is the game's on the list (it closes the Party screen), ours elsewhere.
    CHECK((s.Keys() & PartyKey::B) == 0);
    CHECK((s.Keys() & PartyKey::A) != 0 && (s.Keys() & PartyKey::Y) != 0);

    CHECK(s.Press(PartyKey::Right, in).kind == PartyCommand::None && s.Cursor() == 1);
    CHECK(s.Press(PartyKey::Down, in).kind == PartyCommand::None && s.Cursor() == 3);
    CHECK(s.Press(PartyKey::B, in).kind == PartyCommand::None && s.Page() == PartyPage::List);

    // The menu: Take only while holding, Move only with company.
    CHECK(PartyMenuEntries(false, 6).size() == 5);
    CHECK(PartyMenuEntries(true, 6).size() == 6 && PartyMenuEntries(true, 6)[3] == PartyMenuItem::TakeItem);
    CHECK(PartyMenuEntries(false, 1).size() == 4 && PartyMenuEntries(false, 1)[3] == PartyMenuItem::Cancel);

    // A: the menu, starting on Summary; up wraps to Cancel.
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::Menu && s.MenuCursor() == 0);
    CHECK((s.Keys() & PartyKey::B) != 0);
    s.Press(PartyKey::Up, in);
    CHECK(s.MenuCursor() == 4);         // Summary, Use, Give, Move, Cancel
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 3);

    // Summary.
    s.Press(PartyKey::A, in);
    PartyCommand c = s.Press(PartyKey::A, in);
    CHECK(c.kind == PartyCommand::Summary && c.pos == 3 && s.Page() == PartyPage::List);

    // B leaves the menu.
    s.Press(PartyKey::A, in);
    s.Press(PartyKey::B, in);
    CHECK(s.Page() == PartyPage::List);

    // Move from the menu: pick the other one with the D-pad, A swaps.
    s.Press(PartyKey::A, in);
    for (int i = 0; i < 3; i++)
        s.Press(PartyKey::Down, in);
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::Swap && s.SwapFrom() == 3);
    s.Press(PartyKey::Up, in);
    s.Press(PartyKey::Left, in);
    CHECK(s.Cursor() == 0);
    c = s.Press(PartyKey::A, in);
    CHECK(c.kind == PartyCommand::Swap && c.pos == 3 && c.other == 0);
    CHECK(s.Page() == PartyPage::List && s.SwapFrom() == -1);

    // Y on the list starts a move too; A on the same one cancels; B cancels
    // and puts the cursor back.
    s.Press(PartyKey::Y, in);
    CHECK(s.Page() == PartyPage::Swap && s.SwapFrom() == 0);
    CHECK(s.Press(PartyKey::A, in).kind == PartyCommand::None && s.Page() == PartyPage::List);
    s.Press(PartyKey::Y, in);
    s.Press(PartyKey::Right, in);
    CHECK(s.Press(PartyKey::B, in).kind == PartyCommand::None);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 0);
    // Alone in the party: nothing to move.
    PartyMenuState one;
    PartyMenuInput alone = in;
    alone.count = 1;
    one.Reset(1);
    one.Press(PartyKey::Y, alone);
    CHECK(one.Page() == PartyPage::List);

    // Use item: the item list, pockets with L/R or left/right (all four),
    // A uses.
    s.Press(PartyKey::Right, in);
    s.Press(PartyKey::A, in);
    s.Press(PartyKey::Down, in);
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::Items && !s.Giving() && s.Pocket() == PartyPocket::Medicine && s.ItemCursor() == 0);
    s.Press(PartyKey::Up, in);
    CHECK(s.ItemCursor() == 2);     // wraps
    s.Press(PartyKey::R, in);
    CHECK(s.Pocket() == PartyPocket::Items && s.ItemCursor() == 0);
    s.Press(PartyKey::R, in);
    CHECK(s.Pocket() == PartyPocket::TMs);
    s.Press(PartyKey::Right, in);
    CHECK(s.Pocket() == PartyPocket::Berries);
    s.Press(PartyKey::R, in);
    CHECK(s.Pocket() == PartyPocket::Medicine);     // wraps
    s.Press(PartyKey::Left, in);
    CHECK(s.Pocket() == PartyPocket::Berries);
    s.Press(PartyKey::L, in);
    CHECK(s.Pocket() == PartyPocket::TMs);
    s.Press(PartyKey::Down, in);
    c = s.Press(PartyKey::A, in);
    CHECK(c.kind == PartyCommand::UseItem && c.pos == 1 && c.pocket == PartyPocket::TMs && c.item == 1);
    CHECK(s.Page() == PartyPage::List);
    // An empty pocket: A does nothing, B goes back to the menu.
    const std::vector<PartyBagItem> none;
    PartyMenuInput empty = in;
    empty.items = &none;
    s.Press(PartyKey::A, empty);
    s.Press(PartyKey::Down, empty);
    s.Press(PartyKey::A, empty);
    CHECK(s.Press(PartyKey::A, empty).kind == PartyCommand::None && s.Page() == PartyPage::Items);
    s.Press(PartyKey::B, empty);
    CHECK(s.Page() == PartyPage::Menu);
    s.Press(PartyKey::B, empty);

    // Give item: TMs are skipped (they can't be held); a TM pocket left
    // open by Use falls back to Medicine.
    s.Press(PartyKey::A, in);
    s.Press(PartyKey::Down, in);
    s.Press(PartyKey::Down, in);
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::Items && s.Giving() && s.Pocket() == PartyPocket::Medicine);
    s.Press(PartyKey::R, in);
    s.Press(PartyKey::R, in);
    CHECK(s.Pocket() == PartyPocket::Berries);
    s.Press(PartyKey::L, in);
    CHECK(s.Pocket() == PartyPocket::Items);
    c = s.Press(PartyKey::A, in);
    CHECK(c.kind == PartyCommand::GiveItem && c.pos == 1 && c.pocket == PartyPocket::Items && c.item == 0);
    CHECK(PartyNextPocket(PartyPocket::Items, 1, true) == PartyPocket::Berries);
    CHECK(PartyNextPocket(PartyPocket::Berries, -1, true) == PartyPocket::Items);
    CHECK(!PartyPocketAllowed(PartyPocket::TMs, true) && PartyPocketAllowed(PartyPocket::TMs, false));
    CHECK(PartyUseViaRestore(PartyPocket::Medicine) && PartyUseViaRestore(PartyPocket::Berries));
    CHECK(!PartyUseViaRestore(PartyPocket::Items) && !PartyUseViaRestore(PartyPocket::TMs));

    // Take item: only offered while holding; 4th entry then.
    PartyMenuInput holding = in;
    holding.holding = true;
    s.Press(PartyKey::A, holding);
    for (int i = 0; i < 3; i++)
        s.Press(PartyKey::Down, holding);
    c = s.Press(PartyKey::A, holding);
    CHECK(c.kind == PartyCommand::TakeItem && c.pos == 1 && s.Page() == PartyPage::List);
    // Without an item the 4th entry is Move.
    s.Press(PartyKey::A, in);
    for (int i = 0; i < 3; i++)
        s.Press(PartyKey::Down, in);
    s.Press(PartyKey::A, in);
    CHECK(s.Page() == PartyPage::Swap);
    s.Press(PartyKey::B, in);

    // Ask: Up/Down wrap, A answers, B answers the cancel choice if any.
    PartyMenuInput ask = in;
    ask.choices = 2;
    ask.cancelChoice = 1;
    s.Ask();
    CHECK(s.Page() == PartyPage::Ask && s.AskCursor() == 0 && (s.Keys() & PartyKey::B) != 0);
    s.Press(PartyKey::Up, ask);
    CHECK(s.AskCursor() == 1);
    c = s.Press(PartyKey::A, ask);
    CHECK(c.kind == PartyCommand::Answer && c.choice == 1 && s.Page() == PartyPage::List);
    s.Ask();
    c = s.Press(PartyKey::B, ask);
    CHECK(c.kind == PartyCommand::Answer && c.choice == 1);
    ask.cancelChoice = -1;      // the teaching screen: B does nothing
    ask.choices = 5;
    s.Ask();
    CHECK(s.Press(PartyKey::B, ask).kind == PartyCommand::None && s.Page() == PartyPage::Ask);
    s.Press(PartyKey::Up, ask);
    c = s.Press(PartyKey::A, ask);
    CHECK(c.kind == PartyCommand::Answer && c.choice == 4);
    s.Ask();
    s.EndAsk();
    CHECK(s.Page() == PartyPage::List);

    // The game's cursor can set ours; out of range is clamped.
    s.Reset(6);
    s.SetCursor(9, 6);
    CHECK(s.Cursor() == 5);
    s.Reset(2);
    CHECK(s.Cursor() == 1);
}

static void TestStatus(void)
{
    CHECK(std::string(PartyStatusName(0)) == "");
    CHECK(std::string(PartyStatusName(1)) == "PAR");
    CHECK(std::string(PartyStatusName(2)) == "SLP");
    CHECK(std::string(PartyStatusName(3)) == "FRZ");
    CHECK(std::string(PartyStatusName(4)) == "BRN");
    CHECK(std::string(PartyStatusName(5)) == "PSN");   // seen: the game said POISONED
    CHECK(std::string(PartyStatusName(6)) == "?");
    CHECK(std::string(PartyStatusName(1u << 12)) == "?");
}

static void TestPlan(void)
{
    PartyBagPlan p = PlanPartyBag(0, 0, 7, 9);
    CHECK(p.row == 0 && p.dir == 0);
    p = PlanPartyBag(6, 0, 7, 9);
    CHECK(p.row == 6);
    p = PlanPartyBag(7, 0, 7, 9);
    CHECK(p.row == -1 && p.dir == 1);
    p = PlanPartyBag(8, 2, 7, 9);
    CHECK(p.row == 6);
    p = PlanPartyBag(1, 2, 7, 9);
    CHECK(p.row == -1 && p.dir == -1);
    p = PlanPartyBag(9, 0, 7, 9);
    CHECK(p.row == -1 && p.dir == 0);       // not in the list
}

static void TestItemMenu(void)
{
    // The Party screen's Bag (seen): Escape Rope 06 07 11, TM 06 11, Quick
    // Claw (held item) 07 11; the field Bag's Potion 00 03 04 11.
    CHECK(PartyItemMenuEntry({ 0x06, 0x07, 0x11 }, false) == 3);
    CHECK(PartyItemMenuEntry({ 0x06, 0x07, 0x11 }, true) == 2);
    CHECK(PartyItemMenuEntry({ 0x06, 0x11 }, false) == 2);
    CHECK(PartyItemMenuEntry({ 0x06, 0x11 }, true) == 0);
    CHECK(PartyItemMenuEntry({ 0x07, 0x11 }, true) == 2);
    CHECK(PartyItemMenuEntry({ 0x07, 0x11 }, false) == 0);
    CHECK(PartyItemMenuEntry({ 0x00, 0x03, 0x04, 0x11 }, false) == 4);
    CHECK(PartyItemMenuEntry({ 0x00, 0x03, 0x04, 0x11 }, true) == 3);
    CHECK(PartyItemMenuEntry({ 0x12, 0x13 }, false) == 0);
    CHECK(PartyItemMenuEntry({}, false) == 0);
}

// What PartyFollow does over a run of game screens (each held `frames`),
// as the plugin runs it: an action resets the count (the plugin acts, then
// looks again). Returns the actions in order, one letter each: B advance,
// A ask, T teach, L leave the Bag, D done.
static std::string FollowRun(const std::vector<std::pair<PartyGame, int>> &run)
{
    std::string out;
    for (const auto &step : run)
    {
        u32 stable = 0;
        for (int f = 0; f < step.second; f++)
        {
            const PartyFollowDo d = PartyFollow(step.first, ++stable);
            if (d == PartyFollowDo::Wait)
                continue;
            out += d == PartyFollowDo::Advance ? 'B' : d == PartyFollowDo::Ask ? 'A' : d == PartyFollowDo::Teach ? 'T'
                 : d == PartyFollowDo::LeaveBag ? 'L' : 'D';
            stable = 0;
            if (d == PartyFollowDo::Done || d == PartyFollowDo::Ask || d == PartyFollowDo::Teach)
                break;          // the plugin stops following or waits for the player
        }
    }
    return out;
}

static void TestFollow(void)
{
    // Nothing happens before a screen has settled.
    CHECK(PartyFollow(PartyGame::BagMessage, 11) == PartyFollowDo::Wait);
    CHECK(PartyFollow(PartyGame::BagMessage, 12) == PartyFollowDo::Advance);
    CHECK(PartyFollow(PartyGame::BagList, 19) == PartyFollowDo::Wait);
    CHECK(PartyFollow(PartyGame::BagList, 20) == PartyFollowDo::LeaveBag);
    CHECK(PartyFollow(PartyGame::PartyList, 10) == PartyFollowDo::Done);
    // Never a key outside the Bag and the Party screen, however long:
    // a B could stop an evolution.
    for (u32 n : { 1u, 100u, 5000u })
    {
        CHECK(PartyFollow(PartyGame::Other, n) == PartyFollowDo::Wait);
        CHECK(PartyFollow(PartyGame::Gap, n) == PartyFollowDo::Wait);
        CHECK(PartyFollow(PartyGame::TeachPage, n) == PartyFollowDo::Wait);
        CHECK(PartyFollow(PartyGame::BagBusy, n) == PartyFollowDo::Wait);
    }

    // Potion (seen): the HP bar, "HP was restored", the list, out of the
    // Bag, the Party list.
    CHECK(FollowRun({ { PartyGame::BagBusy, 40 }, { PartyGame::BagMessage, 20 }, { PartyGame::BagList, 30 },
                      { PartyGame::Gap, 20 }, { PartyGame::PartyList, 20 } }) == "BLD");
    // Give over a held item (seen): "already holding", the Yes/No question.
    CHECK(FollowRun({ { PartyGame::BagMessage, 20 }, { PartyGame::BagYesNo, 20 } }) == "BA");
    // TM (seen): the teaching screen asks.
    CHECK(FollowRun({ { PartyGame::Gap, 30 }, { PartyGame::Teach, 40 } }) == "T");
    // ... after the answer: its page, messages in the Bag, the list.
    CHECK(FollowRun({ { PartyGame::TeachPage, 30 }, { PartyGame::Gap, 30 }, { PartyGame::BagMessage, 20 },
                      { PartyGame::BagBusy, 10 }, { PartyGame::BagMessage, 20 }, { PartyGame::BagList, 25 },
                      { PartyGame::PartyList, 15 } }) == "BBLD");
    // Ether (not seen, no Ether in the save): which move, as a menu.
    CHECK(FollowRun({ { PartyGame::BagBusy, 10 }, { PartyGame::BagMenu, 10 } }) == "A");
    // Evolution stone / Rare Candy evolution (not seen): another scene
    // plays without a key from the plugin, then the Bag's messages.
    CHECK(FollowRun({ { PartyGame::Gap, 60 }, { PartyGame::Other, 3000 }, { PartyGame::Gap, 60 },
                      { PartyGame::BagMessage, 15 }, { PartyGame::BagList, 21 } }) == "BL");
    // Rare Candy (not seen): level-up message and stats window, a move
    // question (Yes/No), the teaching screen.
    CHECK(FollowRun({ { PartyGame::BagMessage, 13 }, { PartyGame::BagMessage, 13 }, { PartyGame::BagYesNo, 9 } }) == "BBA");
    // Take (seen): a message on the Party screen, B.
    CHECK(FollowRun({ { PartyGame::PartyOther, 16 }, { PartyGame::PartyList, 10 } }) == "BD");
}

// A fake Party screen, its Bags (Restore, Held Item -> Use/Give), the
// teaching screen and the summary. Taps and presses act as on the game
// (docs/PARTY-UI.md): in move mode a tap only moves the cursor and A picks;
// a tap on the target swaps; the Bag keeps its cursor and scroll; the item
// menu grows upwards from Cancel.
struct FakeParty
{
    enum Mode { List, SlotMenu, HeldMenu, PartyMessage, MoveMode, MovePicked, Bag, ItemMenu, YesNo, Message, Teach,
                TeachPage, Status, Closed };

    ActionWorld world;
    Mode mode = List;
    int cursor = 0;
    int order[6] = { 0, 1, 2, 3, 4, 5 };
    int picked = -1;
    int menuFor = -1;           // slot menu / Bag for this position
    int bagCount = 9, bagScroll = 2, bagRow = 4;
    int pocketTaps = 0;
    std::vector<u8> menu = { 0x06, 0x07, 0x11 };   // the item menu's entries
    bool holding = false;       // the Pokémon the Bag is for holds an item
    int usedItem = -1, usedOn = -1, gaveItem = -1, gaveOn = -1, tookFrom = -1;
    int teachRow = -1, forgot = -1;
    bool switched = false;
    u32 frame = 0, prevKeys = 0;
    bool wasTouching = false;
    std::vector<std::string> tapped;
    std::vector<u32> presses;
    struct Change { u32 at; std::string scene; Mode mode; };
    std::vector<Change> pending;

    FakeParty()
    {
        Set(SceneParty, List);
    }

    void Set(const std::string &scene, Mode m)
    {
        world.scene = scene;
        mode = m;
        world.flags = scene == SceneParty && m == List ? WorldPartyList : 0;
    }

    void Later(u32 delay, const std::string &scene, Mode m)
    {
        pending.push_back({ frame + delay, scene, m });
    }

    // Goes to another scene through a loading gap.
    void Go(const std::string &scene, Mode m)
    {
        Later(5, "", m);
        Later(25, scene, m);
    }

    int Abs(void) const { return bagScroll + bagRow; }

    const char *Screen(void) const
    {
        switch (mode)
        {
        case List: case MoveMode: case MovePicked: return "party";
        case SlotMenu: return "party_slot_menu";
        case HeldMenu: return "party_held_menu";
        case Bag: return "bag";
        case ItemMenu: return "party_bag_menu";
        case YesNo: return "bag_yes_no";
        case Teach: return "learn_moves";
        case TeachPage: return "learn_confirm";
        default: return "";
        }
    }

    const LayoutButton *Hit(u16 x, u16 y)
    {
        const std::string screen = Screen();
        if (world.scene.empty() || screen.empty())
            return nullptr;
        for (const LayoutButton &b : LayoutButtons)
        {
            const std::string name = b.name;
            if (name.substr(0, name.find('.')) == screen && x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1)
                return &b;
        }
        return nullptr;
    }

    void Tap(const std::string &name)
    {
        tapped.push_back(name);
        if (name.compare(0, 10, "party.slot") == 0)
        {
            const int pos = name[10] - '1';
            if (mode == List)
            {
                menuFor = cursor = pos;
                Set(SceneParty, SlotMenu);
            }
            else if (mode == MoveMode)
                cursor = pos;
            else if (mode == MovePicked)
            {
                const int t = order[picked];
                order[picked] = order[pos];
                order[pos] = t;
                cursor = pos;
                picked = -1;
                mode = MoveMode;
            }
        }
        else if (name == "party.move_pokemon" && mode == List)
            Set(SceneParty, MoveMode);
        else if (name == "party_slot_menu.summary")
            Go(SceneStatus, Status);
        else if (name == "party_slot_menu.restore")
            Go(SceneBag, Bag);
        else if (name == "party_slot_menu.held_item")
            Set(SceneParty, HeldMenu);
        else if (name == "party_slot_menu.back")
            Set(SceneParty, List);
        else if (name == "party_held_menu.use_give")
            Go(SceneBag, Bag);
        else if (name == "party_held_menu.take")
        {
            tookFrom = menuFor;
            Set(SceneParty, PartyMessage);
        }
        else if (name.compare(0, 11, "bag.pocket_") == 0)
            pocketTaps++;
        else if (name.compare(0, 12, "bag.item_row") == 0)
        {
            const int row = name[12] - '1';
            if (bagScroll + row < bagCount)
            {
                bagRow = row;
                mode = ItemMenu;
            }
        }
        else if (name.compare(0, 26, "party_bag_menu.from_bottom") == 0)
        {
            const int k = (int)menu.size() - (name[26] - '0');
            const u8 command = k >= 0 && k < (int)menu.size() ? menu[k] : 0xFF;
            if (command == 0x06)
            {
                usedItem = Abs();
                usedOn = menuFor;
                mode = Message;
            }
            else if (command == 0x07 && holding)
                mode = YesNo;
            else if (command == 0x07)
            {
                gaveItem = Abs();
                gaveOn = menuFor;
                mode = Bag;
            }
            else if (command == 0x11)
                mode = Bag;
        }
        else if (name == "bag_yes_no.yes" && mode == YesNo)
        {
            switched = true;
            gaveItem = Abs();
            gaveOn = menuFor;
            mode = Message;
        }
        else if (name == "bag_yes_no.no" && mode == YesNo)
            mode = Bag;
        else if (name.compare(0, 15, "learn_moves.mov") == 0 && mode == Teach)
        {
            teachRow = name[16] - '1';
            mode = TeachPage;
        }
        else if (name == "learn_moves.new" && mode == Teach)
        {
            teachRow = 4;
            mode = TeachPage;
        }
        else if (name == "learn_confirm.yes" && mode == TeachPage)
        {
            forgot = teachRow;
            Go(SceneBag, Message);
        }
    }

    void Press(u32 key)
    {
        presses.push_back(key);
        if (key == ActionKeyB)
        {
            switch (mode)
            {
            case List: Go(SceneField, Closed); break;
            case SlotMenu: case MoveMode: case PartyMessage: Set(SceneParty, List); break;
            case HeldMenu: Set(SceneParty, SlotMenu); break;
            case MovePicked: mode = MoveMode; picked = -1; break;
            case Bag: Go(SceneParty, List); break;
            case ItemMenu: case Message: case YesNo: mode = Bag; break;
            case Status: Go(SceneParty, List); break;
            default: break;
            }
        }
        else if (key == 1u && mode == MoveMode)
        {
            picked = cursor;
            mode = MovePicked;
        }
        else if (key == 1u && mode == Message)
            mode = Bag;
        else if ((key == (1u << 6) || key == (1u << 7)) && mode == Bag)
        {
            int abs = Abs() + (key == (1u << 6) ? -1 : 1);
            abs = (abs + bagCount) % bagCount;
            if (abs < bagScroll)
                bagScroll = abs;
            else if (abs >= bagScroll + 7)
                bagScroll = abs - 6;
            bagRow = abs - bagScroll;
        }
    }

    void Feed(const ActionOutput &out)
    {
        if (out.touching && !wasTouching)
            if (const LayoutButton *b = Hit(out.x, out.y))
                Tap(b->name);
        wasTouching = out.touching;
        const u32 pressed = out.keys & ~prevKeys;
        prevKeys = out.keys;
        if (pressed != 0)
            Press(pressed);
        frame++;
        for (size_t i = 0; i < pending.size();)
        {
            if (pending[i].at <= frame)
            {
                Set(pending[i].scene, pending[i].mode);
                pending.erase(pending.begin() + i);
            }
            else
                i++;
        }
        if (world.scene == SceneParty && mode == List)
            world.flags |= WorldPartyList;
    }
};

static bool Run(FakeParty &game, const PartyAction &action)
{
    std::vector<std::string> log;
    ActionRunner runner;
    if (!runner.Start(action.def, game.world, log))
        return false;
    for (u32 i = 0; i < 3000 && runner.Running(); i++)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    const bool ok = !log.empty() && log.back().find(" ok in ") != std::string::npos;
    if (!ok)
        for (const std::string &line : log)
            std::printf("    %s\n", line.c_str());
    return ok;
}

// Brings item `target` of the fake Bag into reach and opens its menu, as
// PartyScreen's Seek does.
static void OpenItem(FakeParty &game, int target, int &steps)
{
    PartyAction a;
    steps = 0;
    for (int guard = 0; guard < 20; guard++)
    {
        const PartyBagPlan plan = PlanPartyBag(target, game.bagScroll, 7, game.bagCount);
        if (plan.row >= 0)
        {
            MakePartyBagRowAction(a, plan.row);
            CHECK(Run(game, a));
            break;
        }
        CHECK(plan.dir != 0);
        MakePartyBagStepAction(a, plan.dir < 0);
        CHECK(Run(game, a));
        steps++;
    }
    CHECK(game.mode == FakeParty::ItemMenu && game.Abs() == target);
}

static void TestActions(void)
{
    // Every tap is a verified button (the menu's 4th and 5th entries from
    // the bottom were never tapped: an Ether's move list would need them).
    PartyAction a;
    MakePartySummaryAction(a, 5);
    CHECK(CheckAction(a.def).empty());
    MakePartySwapAction(a, 0, 5, 2);
    CHECK(CheckAction(a.def).empty());
    for (int p = 0; p < (int)PartyPocket::Count; p++)
    {
        MakePartyRestoreAction(a, 3, (PartyPocket)p);
        CHECK(CheckAction(a.def).empty());
        MakePartyHeldBagAction(a, 3, (PartyPocket)p);
        CHECK(CheckAction(a.def).empty());
    }
    for (int r = 0; r < 7; r++)
    {
        MakePartyBagRowAction(a, r);
        CHECK(CheckAction(a.def).empty());
    }
    for (int k = 1; k <= 5; k++)
    {
        MakePartyBagMenuAction(a, k);
        CHECK(CheckAction(a.def).empty() == (k <= 3));
    }
    for (int r = 0; r < 5; r++)
    {
        MakePartyTeachAction(a, r);
        CHECK(CheckAction(a.def).empty());
    }
    MakePartyTakeAction(a, 0);
    CHECK(CheckAction(a.def).empty());
    MakePartyYesNoAction(a, true);
    CHECK(CheckAction(a.def).empty());
    MakePartyYesNoAction(a, false);
    CHECK(CheckAction(a.def).empty());
    MakePartyMessageAction(a, true);
    CHECK(CheckAction(a.def).empty());
    MakePartyBagStepAction(a, true);
    CHECK(CheckAction(a.def).empty());
    MakePartyBagBackAction(a);
    CHECK(CheckAction(a.def).empty());

    // Summary: slot, Summary, done in Status.
    {
        FakeParty game;
        MakePartySummaryAction(a, 4);
        CHECK(Run(game, a));
        CHECK(game.world.scene == SceneStatus);
        CHECK(game.tapped.size() == 2 && game.tapped[0] == "party.slot5" && game.tapped[1] == "party_slot_menu.summary");
        CHECK(game.presses.empty());
    }
    // Refused off the plain list (e.g. a slot menu up).
    {
        FakeParty game;
        game.Set(SceneParty, FakeParty::SlotMenu);
        MakePartySummaryAction(a, 0);
        CHECK(!Run(game, a));
        CHECK(game.tapped.empty());
    }

    // Swap 1 and 4 with the game's cursor elsewhere: the cursor is tapped
    // there first; exactly one B at the end, back on the list.
    {
        FakeParty game;
        game.cursor = 2;
        MakePartySwapAction(a, 0, 3, game.cursor);
        CHECK(Run(game, a));
        CHECK(game.order[0] == 3 && game.order[3] == 0 && game.order[1] == 1);
        CHECK(game.mode == FakeParty::List && game.world.scene == SceneParty);
        CHECK(game.tapped.size() == 3 && game.tapped[1] == "party.slot1" && game.tapped[2] == "party.slot4");
        CHECK(game.presses.size() == 2 && game.presses[0] == 1u && game.presses[1] == ActionKeyB);
    }
    // ... and with the cursor already there: no tap needed before A.
    {
        FakeParty game;
        game.cursor = 5;
        MakePartySwapAction(a, 5, 1, game.cursor);
        CHECK(Run(game, a));
        CHECK(game.order[5] == 1 && game.order[1] == 5);
        CHECK(game.tapped.size() == 2 && game.tapped[1] == "party.slot2");
    }

    // Use item 8 of 9 (scrolled out of view) on position 2: Restore, the
    // pocket tab, steps until it shows, tap it, Use (3rd from the bottom of
    // Use / Give / Cancel), A through the message, out of the Bag.
    {
        FakeParty game;
        game.bagScroll = 0;
        game.bagRow = 0;
        MakePartyRestoreAction(a, 2, PartyPocket::Medicine);
        CHECK(Run(game, a));
        CHECK(game.world.scene == SceneBag && game.pocketTaps == 1);
        int steps = 0;
        OpenItem(game, 8, steps);
        CHECK(steps == 8);  // the cursor walks to the bottom row (6), then the list scrolls (2)
        MakePartyBagMenuAction(a, PartyItemMenuEntry(game.menu, false));
        CHECK(Run(game, a));
        CHECK(game.usedItem == 8 && game.usedOn == 2 && game.mode == FakeParty::Message);
        MakePartyMessageAction(a, true);
        CHECK(Run(game, a));
        CHECK(game.mode == FakeParty::Bag && game.presses.back() == 1u);
        MakePartyBagBackAction(a);
        CHECK(Run(game, a));
        CHECK(game.world.scene == SceneParty && game.mode == FakeParty::List);
    }
    // An item above the view: steps up, never wrapping.
    {
        FakeParty game;
        game.bagScroll = 2;
        game.bagRow = 3;
        game.Set(SceneBag, FakeParty::Bag);
        for (int guard = 0; guard < 20; guard++)
        {
            const PartyBagPlan plan = PlanPartyBag(0, game.bagScroll, 7, game.bagCount);
            if (plan.row >= 0)
            {
                CHECK(plan.row == 0);
                break;
            }
            CHECK(plan.dir == -1);
            MakePartyBagStepAction(a, true);
            CHECK(Run(game, a));
        }
        CHECK(game.bagScroll == 0);
    }
    // Backing out of the Bag without using anything.
    {
        FakeParty game;
        game.Set(SceneBag, FakeParty::ItemMenu);
        MakePartyBagBackAction(a);
        CHECK(Run(game, a));
        CHECK(game.world.scene == SceneParty && game.mode == FakeParty::List);
        CHECK(game.usedItem == -1);
    }

    // Give (seen: Quick Claw to Torracat): Held Item, Use/Give, the pocket,
    // the row, Give (2nd from the bottom); given at once, back in the list.
    {
        FakeParty game;
        game.bagScroll = 0;
        game.bagRow = 0;
        game.menu = { 0x07, 0x11 };
        MakePartyHeldBagAction(a, 0, PartyPocket::Items);
        CHECK(Run(game, a));
        CHECK(game.tapped.size() == 4 && game.tapped[1] == "party_slot_menu.held_item"
              && game.tapped[2] == "party_held_menu.use_give" && game.tapped[3] == "bag.pocket_items");
        int steps = 0;
        OpenItem(game, 0, steps);
        CHECK(PartyItemMenuEntry(game.menu, false) == 0);   // can't be used
        MakePartyBagMenuAction(a, PartyItemMenuEntry(game.menu, true));
        CHECK(Run(game, a));
        CHECK(game.gaveItem == 0 && game.gaveOn == 0 && game.mode == FakeParty::Bag && !game.switched);
    }
    // ... over a held item: the Yes/No question, Yes switches.
    {
        FakeParty game;
        game.Set(SceneBag, FakeParty::ItemMenu);
        game.menuFor = 1;
        game.holding = true;
        MakePartyBagMenuAction(a, PartyItemMenuEntry(game.menu, true));
        CHECK(Run(game, a));
        CHECK(game.mode == FakeParty::YesNo && game.gaveItem == -1);
        MakePartyYesNoAction(a, true);
        CHECK(Run(game, a));
        CHECK(game.switched && game.gaveOn == 1 && game.mode == FakeParty::Message);
    }
    // ... No keeps it.
    {
        FakeParty game;
        game.Set(SceneBag, FakeParty::YesNo);
        game.holding = true;
        MakePartyYesNoAction(a, false);
        CHECK(Run(game, a));
        CHECK(!game.switched && game.gaveItem == -1 && game.mode == FakeParty::Bag);
    }

    // Take (seen): Held Item, Take, one B through "You received ..."; never
    // a second B (that would close the Party screen).
    {
        FakeParty game;
        MakePartyTakeAction(a, 3);
        CHECK(Run(game, a));
        CHECK(game.tookFrom == 3 && game.mode == FakeParty::List && game.world.scene == SceneParty);
        CHECK(game.presses.size() == 1 && game.presses[0] == ActionKeyB);
        CHECK(game.tapped.size() == 3 && game.tapped[0] == "party.slot4" && game.tapped[2] == "party_held_menu.take");
    }

    // TM (seen: TM01 on Torracat, Lick forgotten): the teaching screen,
    // tap the row, Forget this move; the screen goes, the Bag's messages.
    for (int row = 0; row < 5; row++)
    {
        FakeParty game;
        game.Set(SceneTeach, FakeParty::Teach);
        MakePartyTeachAction(a, row);
        CHECK(Run(game, a));
        CHECK(game.forgot == row && game.world.scene != SceneTeach && game.mode == FakeParty::Message);
        CHECK(game.tapped.size() == 2 && game.tapped[1] == "learn_confirm.yes");
        CHECK(game.presses.empty());
    }
    // A row tap the game didn't see: the action fails (still teaching), the
    // plugin shows the question again.
    {
        FakeParty game;
        game.Set(SceneTeach, FakeParty::TeachPage);
        game.teachRow = 2;
        game.mode = FakeParty::Closed;      // taps hit nothing
        MakePartyTeachAction(a, 0);
        CHECK(!Run(game, a));
        CHECK(game.forgot == -1 && game.world.scene == SceneTeach);
    }
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void WritePpm(const UI::Canvas &canvas, const char *path)
{
    if (path == nullptr)
        return;
    FILE *f = std::fopen(path, "wb");
    if (f == nullptr)
    {
        std::printf("can't write %s\n", path);
        s_failures++;
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", UI::ScreenW, UI::ScreenH);
    for (int y = 0; y < UI::ScreenH; y++)
        for (int x = 0; x < UI::ScreenW; x++)
        {
            const UI::Color c = At(canvas, x, y);
            const u8 rgb[3] = { c.r, c.g, c.b };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("party_menu_test: preview written to %s\n", path);
}

static PartyMonView Mon(const char *name, const char *species, int level, int hp, int maxHp, const char *item)
{
    PartyMonView m;
    m.valid = true;
    m.name = name;
    m.species = species;
    m.level = (u8)level;
    m.gender = (u8)(level % 2);
    m.hp = (u16)hp;
    m.maxHp = (u16)maxHp;
    m.item = item;
    m.itemId = item[0] ? 217 : 0;
    return m;
}

static bool Orange(UI::Color c)
{
    return c.r > 220 && c.g > 60 && c.g < 130 && c.b < 60;
}

// "party-preview.ppm" -> "party-NAME-preview.ppm" next to it.
static std::string Sibling(const char *path, const char *name)
{
    if (path == nullptr)
        return "";
    std::string p = path;
    const size_t at = p.rfind("party-preview");
    return at == std::string::npos ? std::string() : p.substr(0, at) + "party-" + name + "-preview.ppm";
}

static void TestDraw(const char *listPreview, const char *summaryPreview)
{
    // Plates inside the area, not overlapping, clear of the footer.
    for (int i = 0; i < 6; i++)
    {
        const UI::Rect r = PartyCardRect(i);
        CHECK(r.x >= PartyArea.x + 4 && r.Right() <= PartyArea.Right() - 4);
        CHECK(r.y >= PartyArea.y + 4 && r.Bottom() <= PartyArea.Bottom() - 20);
        for (int j = 0; j < i; j++)
            CHECK(UI::Intersect(UI::Inset(r, -3), UI::Inset(PartyCardRect(j), -3)).Empty());
    }

    static u8 pixels[UI::ScreenW * UI::ScreenH * 3];
    UI::Surface surface = { pixels, 3, UI::ScreenW * 3, UI::PixelFormat::BGR8, UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);

    PartyView view;
    view.mons.push_back(Mon("Pichu", "Pichu", 12, 18, 27, ""));
    view.mons.push_back(Mon("Zorua", "Zorua", 14, 35, 35, ""));
    view.mons.push_back(Mon("Pikipek", "Pikipek", 13, 5, 35, ""));
    view.mons.push_back(Mon("Torracat", "Torracat", 17, 51, 51, "Quick Claw"));
    view.mons.push_back(Mon("Ledyba", "Ledyba", 13, 37, 37, "Oran Berry"));
    view.mons.push_back(Mon("Buneary", "Buneary", 12, 37, 37, ""));
    view.mons[2].status = 5;
    view.cursor = 3;
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    // Outside the area untouched; the selected plate has the game's orange
    // frame, the others don't.
    CHECK(At(canvas, 2, 2).g == 200);
    const UI::Rect sel = PartyCardRect(view.cursor);
    CHECK(Orange(At(canvas, sel.x + sel.w / 2, sel.y - 1)) || Orange(At(canvas, sel.x + sel.w / 2, sel.y - 2)));
    const UI::Rect other = PartyCardRect(0);
    CHECK(!Orange(At(canvas, other.x + other.w / 2, other.y - 1)) && !Orange(At(canvas, other.x + other.w / 2, other.y - 2)));
    WritePpm(canvas, listPreview);

    // Keys change with what is shown.
    const std::string key = PartyViewKey(view);
    view.mons[0].hp = 27;
    CHECK(PartyViewKey(view) != key);
    const std::string key2 = PartyViewKey(view);
    view.mons[0].gender = 1;
    CHECK(PartyViewKey(view) != key2);

    // The menu (Take offered: Torracat holds an item), as option pills.
    view.page = PartyPage::Menu;
    view.menuCursor = 3;
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    CHECK(At(canvas, 2, 2).g == 200);
    const std::string menuPath = Sibling(listPreview, "menu");
    WritePpm(canvas, menuPath.empty() ? nullptr : menuPath.c_str());

    view.page = PartyPage::Items;
    view.giving = true;
    view.pocket = PartyPocket::Items;
    view.items = Items(9);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);    // more items than rows
    CHECK(At(canvas, 2, 2).g == 200);

    // The teaching question: four moves and "Don't learn".
    view.page = PartyPage::Ask;
    view.ask.title = "Torracat wants to learn Work Up.\nForget which move?";
    view.ask.moves = true;
    const u16 moves[4] = { 10, 424, 24, 122 };
    const char *const names[4] = { "Scratch", "Fire Fang", "Double Kick", "Lick" };
    for (int i = 0; i < 4; i++)
    {
        PartyChoice c;
        c.move = moves[i];
        c.pp = 30;
        c.label = names[i];
        view.ask.choices.push_back(c);
    }
    view.ask.choices.push_back(PartyChoice{ "Don't learn Work Up" });
    view.askCursor = 3;
    const std::string askKey = PartyViewKey(view);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    CHECK(At(canvas, 2, 2).g == 200);
    const std::string teachPath = Sibling(listPreview, "teach");
    WritePpm(canvas, teachPath.empty() ? nullptr : teachPath.c_str());
    view.askCursor = 4;
    CHECK(PartyViewKey(view) != askKey);

    // A Yes/No question.
    view.ask = PartyAskView();
    view.ask.title = "Zorua is already holding Escape Rope.\nSwitch it for the Quick Claw?";
    view.ask.choices = { PartyChoice{ "Yes" }, PartyChoice{ "No" } };
    view.askCursor = 0;
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    CHECK(At(canvas, 2, 2).g == 200);

    // Busy and a note: the footer message.
    view.page = PartyPage::List;
    view.busy = "Giving Quick Claw to Zorua...";
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    CHECK(At(canvas, 2, 2).g == 200);

    SummaryView sum;
    sum.mon = Mon("Pikipek", "Pikipek", 13, 35, 35, "");
    sum.mon.moves[0] = 64; sum.mon.pp[0] = 35; sum.mon.moveNames[0] = "Peck";
    sum.mon.moves[1] = 45; sum.mon.pp[1] = 40; sum.mon.moveNames[1] = "Growl";
    sum.mon.moves[2] = 497; sum.mon.pp[2] = 15; sum.mon.moveNames[2] = "Echoed Voice";
    sum.mon.moves[3] = 249; sum.mon.pp[3] = 15;
    sum.mon.ability = "Skill Link";
    sum.mon.nature = "Timid";
    sum.pos = 2;
    sum.count = 6;
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawSummary(canvas, sum);
    CHECK(At(canvas, 2, 2).g == 200);
    const std::string skey = SummaryViewKey(sum);
    sum.hidden = true;
    CHECK(SummaryViewKey(sum) != skey);
    WritePpm(canvas, summaryPreview);
}

int main(int argc, char **argv)
{
    TestMove();
    TestState();
    TestStatus();
    TestPlan();
    TestItemMenu();
    TestFollow();
    TestActions();
    TestDraw(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
    if (s_failures != 0)
    {
        std::printf("party_menu_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("party_menu_test: all passed\n");
    return 0;
}
