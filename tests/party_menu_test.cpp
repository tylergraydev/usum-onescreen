// Host tests for the native Party screen (Sources/Screens/PartyMenu.cpp):
// cursor moves, pages, the actions against a fake Party screen / Restore bag
// / summary, and drawing. Build and run: make -C tests. With path arguments,
// also writes previews of the party list and the summary there (PPM).

#include "Screens/PartyMenu.hpp"

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
    PartyMenuState s;
    s.Reset(6);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 0);
    // B is the game's on the list (it closes the Party screen), ours elsewhere.
    CHECK((s.Keys() & PartyKey::B) == 0);
    CHECK((s.Keys() & PartyKey::A) != 0 && (s.Keys() & PartyKey::Y) != 0);

    CHECK(s.Press(PartyKey::Right, 6, &items).kind == PartyCommand::None && s.Cursor() == 1);
    CHECK(s.Press(PartyKey::Down, 6, &items).kind == PartyCommand::None && s.Cursor() == 3);
    CHECK(s.Press(PartyKey::B, 6, &items).kind == PartyCommand::None && s.Page() == PartyPage::List);

    // A: the menu, starting on Summary; up wraps to Cancel.
    s.Press(PartyKey::A, 6, &items);
    CHECK(s.Page() == PartyPage::Menu && s.MenuCursor() == 0);
    CHECK((s.Keys() & PartyKey::B) != 0);
    s.Press(PartyKey::Up, 6, &items);
    CHECK(s.MenuCursor() == (int)PartyMenuItem::Cancel);
    s.Press(PartyKey::A, 6, &items);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 3);

    // Summary.
    s.Press(PartyKey::A, 6, &items);
    PartyCommand c = s.Press(PartyKey::A, 6, &items);
    CHECK(c.kind == PartyCommand::Summary && c.pos == 3 && s.Page() == PartyPage::List);

    // B leaves the menu.
    s.Press(PartyKey::A, 6, &items);
    s.Press(PartyKey::B, 6, &items);
    CHECK(s.Page() == PartyPage::List);

    // Move from the menu: pick the other one with the D-pad, A swaps.
    s.Press(PartyKey::A, 6, &items);
    s.Press(PartyKey::Down, 6, &items);
    s.Press(PartyKey::Down, 6, &items);
    CHECK(s.MenuCursor() == (int)PartyMenuItem::Move);
    s.Press(PartyKey::A, 6, &items);
    CHECK(s.Page() == PartyPage::Swap && s.SwapFrom() == 3);
    s.Press(PartyKey::Up, 6, &items);
    s.Press(PartyKey::Left, 6, &items);
    CHECK(s.Cursor() == 0);
    c = s.Press(PartyKey::A, 6, &items);
    CHECK(c.kind == PartyCommand::Swap && c.pos == 3 && c.other == 0);
    CHECK(s.Page() == PartyPage::List && s.SwapFrom() == -1);

    // Y on the list starts a move too; A on the same one cancels; B cancels
    // and puts the cursor back.
    s.Press(PartyKey::Y, 6, &items);
    CHECK(s.Page() == PartyPage::Swap && s.SwapFrom() == 0);
    CHECK(s.Press(PartyKey::A, 6, &items).kind == PartyCommand::None && s.Page() == PartyPage::List);
    s.Press(PartyKey::Y, 6, &items);
    s.Press(PartyKey::Right, 6, &items);
    CHECK(s.Press(PartyKey::B, 6, &items).kind == PartyCommand::None);
    CHECK(s.Page() == PartyPage::List && s.Cursor() == 0);
    // Alone in the party: nothing to move.
    PartyMenuState one;
    one.Reset(1);
    one.Press(PartyKey::Y, 1, &items);
    CHECK(one.Page() == PartyPage::List);

    // Use item: the item list, pockets with L/R or left/right, A uses.
    s.Press(PartyKey::Right, 6, &items);
    s.Press(PartyKey::A, 6, &items);
    s.Press(PartyKey::Down, 6, &items);
    s.Press(PartyKey::A, 6, &items);
    CHECK(s.Page() == PartyPage::Items && s.Pocket() == PartyPocket::Medicine && s.ItemCursor() == 0);
    s.Press(PartyKey::Up, 6, &items);
    CHECK(s.ItemCursor() == 2);     // wraps
    s.Press(PartyKey::R, 6, &items);
    CHECK(s.Pocket() == PartyPocket::Berries && s.ItemCursor() == 0);
    s.Press(PartyKey::Left, 6, &items);
    CHECK(s.Pocket() == PartyPocket::Medicine);
    s.Press(PartyKey::Down, 6, &items);
    c = s.Press(PartyKey::A, 6, &items);
    CHECK(c.kind == PartyCommand::UseItem && c.pos == 1 && c.pocket == PartyPocket::Medicine && c.item == 1);
    CHECK(s.Page() == PartyPage::List);
    // An empty pocket: A does nothing, B goes back to the menu.
    const std::vector<PartyBagItem> none;
    s.Press(PartyKey::A, 6, &none);
    s.Press(PartyKey::Down, 6, &none);
    s.Press(PartyKey::A, 6, &none);
    CHECK(s.Press(PartyKey::A, 6, &none).kind == PartyCommand::None && s.Page() == PartyPage::Items);
    s.Press(PartyKey::B, 6, &none);
    CHECK(s.Page() == PartyPage::Menu);

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

// A fake Party screen, Restore bag and summary. Taps and presses act as on
// the game (docs/PARTY-UI.md): in move mode a tap only moves the cursor and
// A picks; a tap on the target swaps; the Bag keeps its cursor and scroll.
struct FakeParty
{
    enum Mode { List, SlotMenu, MoveMode, MovePicked, Bag, ItemMenu, Message, Status, Closed };

    ActionWorld world;
    Mode mode = List;
    int cursor = 0;
    int order[6] = { 0, 1, 2, 3, 4, 5 };
    int picked = -1;
    int menuFor = -1;           // slot menu / Restore for this position
    int bagCount = 9, bagScroll = 2, bagRow = 4;
    int pocketTaps = 0;
    int usedItem = -1, usedOn = -1;
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
        case Bag: return "bag";
        case ItemMenu: return "restore_item_menu";
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
        else if (name == "party_slot_menu.back")
            Set(SceneParty, List);
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
        else if (name == "restore_item_menu.use")
        {
            usedItem = Abs();
            usedOn = menuFor;
            mode = Message;
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
            case SlotMenu: case MoveMode: Set(SceneParty, List); break;
            case MovePicked: mode = MoveMode; picked = -1; break;
            case Bag: Go(SceneParty, List); break;
            case ItemMenu: case Message: mode = Bag; break;
            case Status: Go(SceneParty, List); break;
            default: break;
            }
        }
        else if (key == 1u && mode == MoveMode)
        {
            picked = cursor;
            mode = MovePicked;
        }
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

static void TestActions(void)
{
    // Every tap is a verified button.
    PartyAction a;
    MakePartySummaryAction(a, 5);
    CHECK(CheckAction(a.def).empty());
    MakePartySwapAction(a, 0, 5, 2);
    CHECK(CheckAction(a.def).empty());
    for (int p = 0; p < (int)PartyPocket::Count; p++)
    {
        MakePartyRestoreAction(a, 3, (PartyPocket)p);
        CHECK(CheckAction(a.def).empty());
    }
    for (int r = 0; r < 7; r++)
    {
        MakePartyBagRowAction(a, r);
        CHECK(CheckAction(a.def).empty());
    }
    MakePartyUseAction(a);
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
    // pocket tab, steps until it shows, tap it, Use, back on the list.
    {
        FakeParty game;
        game.bagScroll = 0;
        game.bagRow = 0;
        MakePartyRestoreAction(a, 2, PartyPocket::Medicine);
        CHECK(Run(game, a));
        CHECK(game.world.scene == SceneBag && game.pocketTaps == 1);
        const int target = 8;
        int steps = 0;
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
        CHECK(steps == 8);  // the cursor walks to the bottom row (6), then the list scrolls (2)
        MakePartyUseAction(a);
        CHECK(Run(game, a));
        CHECK(game.usedItem == target && game.usedOn == 2);
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
    m.hp = (u16)hp;
    m.maxHp = (u16)maxHp;
    m.item = item;
    return m;
}

static void TestDraw(const char *listPreview, const char *summaryPreview)
{
    // Cards inside the area, not overlapping.
    for (int i = 0; i < 6; i++)
    {
        const UI::Rect r = PartyCardRect(i);
        CHECK(r.x >= PartyArea.x + 4 && r.Right() <= PartyArea.Right() - 4);
        CHECK(r.y >= PartyArea.y + 4 && r.Bottom() <= PartyArea.Bottom() - 20);
        for (int j = 0; j < i; j++)
            CHECK(UI::Intersect(r, PartyCardRect(j)).Empty());
    }

    static u8 pixels[UI::ScreenW * UI::ScreenH * 3];
    UI::Surface surface = { pixels, 3, UI::ScreenW * 3, UI::PixelFormat::BGR8, UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);

    PartyView view;
    view.mons.push_back(Mon("Pichu", "Pichu", 12, 18, 27, ""));
    view.mons.push_back(Mon("Zorua", "Zorua", 14, 35, 35, ""));
    view.mons.push_back(Mon("Pikipek", "Pikipek", 13, 5, 35, ""));
    view.mons.push_back(Mon("Torracat", "Torracat", 17, 51, 51, ""));
    view.mons.push_back(Mon("Ledyba", "Ledyba", 13, 37, 37, "Quick Claw"));
    view.mons.push_back(Mon("Buneary", "Buneary", 12, 37, 37, ""));
    view.mons[2].status = 5;
    view.cursor = 3;
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);
    // Outside the area untouched; the selected card has the yellow ring.
    CHECK(At(canvas, 2, 2).g == 200);
    const UI::Rect sel = PartyCardRect(view.cursor);
    const UI::Color ring = At(canvas, sel.x + sel.w / 2, sel.y - 2);
    CHECK(ring.r > 200 && ring.g > 200 && ring.b < 160);
    const UI::Rect other = PartyCardRect(0);
    const UI::Color noRing = At(canvas, other.x + other.w / 2, other.y - 2);
    CHECK(!(noRing.r > 200 && noRing.g > 200 && noRing.b < 160));
    WritePpm(canvas, listPreview);

    // Keys change with what is shown.
    const std::string key = PartyViewKey(view);
    view.mons[0].hp = 27;
    CHECK(PartyViewKey(view) != key);
    view.page = PartyPage::Items;
    view.items = Items(9);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(0, 200, 0));
    DrawParty(canvas, view);    // the popup with more items than rows

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
