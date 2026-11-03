// Host tests for the native Bag's pure part (Sources/Screens/BagList.cpp):
// tabs and actions, the list cursor, which menu entries are native, and the
// driver against a small fake of the game's Bag. Build and run: make -C tests.
// With a path argument, also writes a preview of the Bag there as a PPM image.

#include "Screens/BagList.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static void TestTables(void)
{
    CHECK(BagTabFind("medicine") == 1);
    CHECK(BagTabFind("rotom_powers") == 0);
    CHECK(BagTabFind("free_space") == BagTabCount - 1);
    CHECK(BagTabFind("nope") == -1);
    for (int i = 0; i < BagTabCount; i++)
    {
        const LayoutButton *b = FindLayoutButton(BagTabAt(i).button);
        CHECK(b != nullptr && b->verified == LayoutVerified::Tap);
        // Tabs sit left to right in the game's tab order.
        if (i > 0)
            CHECK(b != nullptr && b->tapX > FindLayoutButton(BagTabAt(i - 1).button)->tapX);
    }

    // Every command has an action; all of them may run except the menu
    // positions that were never tapped (2: Move to Free Space, 5: unseen).
    BagCmd cmd;
    std::vector<BagCmd> all;
    for (int i = 0; i < BagTabCount; i++) all.push_back(BagCmd{ BagStep::TapTab, i });
    for (int r = 1; r <= 7; r++) all.push_back(BagCmd{ BagStep::TapRow, r });
    for (int p = 1; p <= 5; p++) all.push_back(BagCmd{ BagStep::TapMenu, p });
    for (int s = 0; s < 6; s++) all.push_back(BagCmd{ BagStep::TapParty, s });
    for (BagStep s : { BagStep::PressUp, BagStep::PressDown, BagStep::TapYes, BagStep::TapNo, BagStep::PressA,
                       BagStep::PressB, BagStep::TapBack })
        all.push_back(BagCmd{ s, 0 });
    for (const BagCmd &c : all)
    {
        const ActionDef *def = BagAction(c);
        CHECK(def != nullptr);
        if (def == nullptr)
            continue;
        CHECK(std::strncmp(def->name, "bag-", 4) == 0);
        CHECK(def->summary != nullptr);
        CHECK(def->needs.scene != nullptr && std::string(def->needs.scene) == SceneBag);
        const std::string bad = CheckAction(*def);
        const bool untapped = c.step == BagStep::TapMenu && (c.arg == 2 || c.arg == 5);
        if (bad.empty() == untapped)
            std::printf("    %s: '%s'\n", def->name, bad.c_str());
        CHECK(bad.empty() != untapped);
    }
    CHECK(BagAction(BagCmd{ BagStep::TapRow, 0 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapRow, 8 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapParty, 6 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapTab, BagTabCount }) == nullptr);
    CHECK(BagAction(cmd) == nullptr);

    CHECK(std::string(BagCommandLabel(BagCommand::Give)) == "Give");
    CHECK(std::string(BagCommandLabel(0x07)) == "Option 0x07");
    CHECK(BagCommandEnabled(BagCommand::Use, BagTabFind("medicine")));
    CHECK(BagCommandEnabled(BagCommand::Use, BagTabFind("berries")));
    CHECK(!BagCommandEnabled(BagCommand::Use, BagTabFind("items")));     // Escape Rope leaves the Bag
    CHECK(!BagCommandEnabled(BagCommand::Use, BagTabFind("tms")));
    CHECK(BagCommandEnabled(BagCommand::Give, BagTabFind("items")));
    CHECK(BagCommandEnabled(BagCommand::Cancel, 0) && BagCommandEnabled(BagCommand::Yes, 0) && BagCommandEnabled(BagCommand::No, 0));
    CHECK(!BagCommandEnabled(BagCommand::FreeSpace, 1));
    CHECK(!BagCommandEnabled(0x07, 1));
}

static void TestCursor(void)
{
    CHECK(BagMoveCursor(0, -1, 9) == 8);
    CHECK(BagMoveCursor(8, 1, 9) == 0);
    CHECK(BagMoveCursor(3, 1, 9) == 4);
    CHECK(BagMoveCursor(5, 1, 0) == 0);
    CHECK(BagListTop(0, 3, 5) == 0);        // short list: never scrolls
    CHECK(BagListTop(0, 7, 9) == 0);        // 8 rows: 0..7 fit
    CHECK(BagListTop(0, 8, 9) == 1);
    CHECK(BagListTop(1, 0, 9) == 0);        // wrapped to the top
    CHECK(BagListTop(0, 8, 20) == 1);
    CHECK(BagListTop(10, 12, 20) == 10);    // still visible: stays
    CHECK(BagListTop(15, 19, 20) == 12);    // never past the end
}

// --- A fake of the game's field Bag ---

struct FakeBag
{
    BagGame g;
    std::vector<std::vector<BagItem>> pockets;
    std::vector<int> cursorOf, scrollOf;    // per pocket, as the game remembers
    int busy = 0;                           // frames of Busy before the next state shows
    BagMode after = BagMode::List;
    std::vector<int> queue;                 // states after each A in a message: modes
    u16 held[6] = {};
    int lastPicked = -1;
    u8 pending = 0;                         // Use or Give chosen in the menu
    bool ignoreTaps = false;
    bool closed = false;
    int commands = 0;
    std::vector<BagCmd> log;

    FakeBag()
    {
        pockets.resize(BagTabCount);
        cursorOf.assign(BagTabCount, 0);
        scrollOf.assign(BagTabCount, 0);
        for (int i = 0; i < 12; i++)
            pockets[1].push_back(BagItem{ (u16)(17 + i), (u16)(i + 1), false });
        pockets[2].push_back(BagItem{ 217, 1, false });     // Quick Claw
        pockets[2].push_back(BagItem{ 78, 3, false });      // Escape Rope
        held[3] = 217;
        Show(1);
        g.mode = BagMode::List;
    }

    void Show(int tab)
    {
        g.tab = tab;
        g.items = pockets[tab];
        g.visible = (int)g.items.size() < 7 ? (int)g.items.size() : 7;
        g.cursorRow = cursorOf[tab];
        g.scroll = scrollOf[tab];
    }

    void Save(void)
    {
        cursorOf[g.tab] = g.cursorRow;
        scrollOf[g.tab] = g.scroll;
    }

    void Move(int delta)
    {
        const int n = (int)g.items.size();
        int index = (g.Index() + delta + n) % n;
        // The game keeps the cursor on row 3 while it can scroll.
        int scroll = index - 3;
        if (scroll < 0) scroll = 0;
        if (scroll > n - g.visible) scroll = n - g.visible;
        g.scroll = scroll;
        g.cursorRow = index - scroll;
        Save();
    }

    void Then(BagMode mode, int frames = 3)
    {
        busy = frames;
        after = mode;
        g.mode = BagMode::Busy;
    }

    void OpenMenu(void)
    {
        const BagItem &it = g.items[g.Index()];
        g.item = it.id;
        g.commands.clear();
        if (g.tab == 1 || it.id == 78)
            g.commands.push_back(BagCommand::Use);
        g.commands.push_back(BagCommand::Give);
        g.commands.push_back(BagCommand::FreeSpace);
        g.commands.push_back(BagCommand::Cancel);
        Then(BagMode::Menu);
    }

    void Apply(const BagCmd &c)
    {
        log.push_back(c);
        commands++;
        if (ignoreTaps)
            return;
        switch (c.step)
        {
        case BagStep::TapTab:
            if (g.mode == BagMode::List)
            {
                Show(c.arg);
                g.scroll = -1;  // slides in
                Then(BagMode::List);
            }
            break;
        case BagStep::TapRow:
            if (g.mode == BagMode::List && c.arg <= g.visible)
            {
                g.cursorRow = c.arg - 1;
                Save();
                OpenMenu();
            }
            break;
        case BagStep::PressUp:
        case BagStep::PressDown:
            if (g.mode == BagMode::List)
                Move(c.step == BagStep::PressUp ? -1 : 1);
            break;
        case BagStep::TapMenu:
        {
            if (g.mode != BagMode::Menu)
                break;
            const int entry = (int)g.commands.size() - c.arg;
            const u8 cmd = g.commands[entry];
            g.commands.clear();
            if (cmd == BagCommand::Cancel)
                Then(BagMode::List);
            else if (cmd == BagCommand::Use || cmd == BagCommand::Give)
            {
                g.partyCursor = 0;
                pending = cmd;
                Then(BagMode::Pick);
            }
            break;
        }
        case BagStep::TapParty:
            if (g.mode != BagMode::Pick)
                break;
            if (g.partyCursor != c.arg)
            {
                g.partyCursor = c.arg;
                break;
            }
            {
                const bool give = pending == BagCommand::Give;
                lastPicked = c.arg;
                // Use: message, then back to the pick. Give: message, then
                // the list; over a held item a question first.
                queue.clear();
                if (give && held[c.arg] != 0)
                    queue = { (int)BagMode::Menu };     // Yes / No after the message
                else
                    queue = { (int)(give ? BagMode::List : BagMode::Pick) };
                if (give && held[c.arg] == 0)
                    held[c.arg] = g.item;
                Then(BagMode::Message);
            }
            break;
        case BagStep::PressA:
            if (g.mode == BagMode::Message && !queue.empty())
            {
                const BagMode next = (BagMode)queue.front();
                queue.erase(queue.begin());
                if (next == BagMode::Menu)
                {
                    g.commands = { BagCommand::Yes, BagCommand::No };
                    Then(BagMode::Menu);
                }
                else
                    Then(next);
            }
            break;
        case BagStep::TapYes:
        case BagStep::TapNo:
            if (g.YesNo())
            {
                if (c.step == BagStep::TapYes)
                    held[lastPicked] = g.item;
                g.commands.clear();
                queue = { (int)BagMode::List };
                Then(BagMode::Message);
            }
            break;
        case BagStep::PressB:
            if (g.mode == BagMode::Menu || g.mode == BagMode::Pick)
            {
                g.commands.clear();
                Then(BagMode::List);
            }
            break;
        case BagStep::TapBack:
            if (g.mode == BagMode::List)
                closed = true;
            break;
        case BagStep::None:
            break;
        }
    }

    // One frame of the game.
    void Frame(void)
    {
        if (closed)
        {
            g = BagGame();
            return;
        }
        if (busy > 0 && --busy == 0)
        {
            g.mode = after;
            if (g.scroll < 0)
                g.scroll = scrollOf[g.tab];
        }
    }
};

static void PrintLog(const std::vector<std::string> &log)
{
    for (const std::string &l : log)
        std::printf("    %s\n", l.c_str());
}

// Runs the driver until it is idle; each command takes `actionFrames`
// frames in the action layer. Returns true if it ended without failure; the
// driver's log is printed if that isn't what the test `expects`.
static bool Run(BagDriver &d, FakeBag &f, bool expects = true, u32 maxFrames = 2000, int actionFrames = 8)
{
    std::vector<std::string> log;
    int running = 0;
    for (u32 i = 0; i < maxFrames && d.Busy(); i++)
    {
        f.Frame();
        const BagCmd cmd = d.Update(f.g, running > 0, log);
        if (running > 0)
            running--;
        if (cmd.step != BagStep::None)
        {
            CHECK(BagAction(cmd) != nullptr);
            f.Apply(cmd);
            running = actionFrames;
        }
    }
    const bool ok = !d.Busy() && d.Failure().empty();
    if (ok != expects)
        PrintLog(log);
    return ok;
}

static int Count(const FakeBag &f, BagStep step)
{
    int n = 0;
    for (const BagCmd &c : f.log)
        n += c.step == step;
    return n;
}

static void TestDriver(void)
{
    // Pocket switch: one tab tap, done once the list has settled.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(2);
        CHECK(Run(d, f));
        CHECK(f.g.tab == 2 && f.commands == 1 && f.log[0] == (BagCmd{ BagStep::TapTab, 2 }));
        const u32 finished = d.Finished();
        d.Tab(2);   // already there: nothing to do
        CHECK(Run(d, f));
        CHECK(f.commands == 1 && d.Finished() == finished + 1);
    }
    // Open an item in the window: one row tap.
    {
        FakeBag f;
        BagDriver d;
        d.Open(4, f.g.items[4].id);
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Menu && f.g.item == 21);
        CHECK(f.commands == 1 && f.log[0] == (BagCmd{ BagStep::TapRow, 5 }));
    }
    // Below the window: down until it is in, then the row.
    {
        FakeBag f;
        BagDriver d;
        d.Open(10, f.g.items[10].id);
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Menu && f.g.item == 27);
        CHECK(Count(f, BagStep::PressDown) > 0 && Count(f, BagStep::PressUp) == 0);
        CHECK(f.log.back().step == BagStep::TapRow);
        // Then back up to the first item: presses up.
        BagDriver d2;
        f.Apply(BagCmd{ BagStep::PressB, 0 });
        for (int i = 0; i < 5; i++) f.Frame();
        f.log.clear();
        d2.Open(0, 17);
        CHECK(Run(d2, f));
        CHECK(f.g.item == 17 && Count(f, BagStep::PressUp) > 0 && Count(f, BagStep::PressDown) == 0);
    }
    // The list changed under the native cursor: refuse, don't open another item.
    {
        FakeBag f;
        BagDriver d;
        d.Open(2, 999);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 0 && d.Failure().find("isn't where it was") != std::string::npos);
    }
    // Use: menu entry 0 of 4 is position 4; the pick taps twice (cursor, then
    // choose) for slot 4, once for the slot under the cursor; messages: A.
    {
        FakeBag f;
        BagDriver d;
        d.Open(0, 17);
        CHECK(Run(d, f));
        d.Choose(0);
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 4 }));
        CHECK(f.g.mode == BagMode::Pick);
        d.Pick(3);
        CHECK(Run(d, f));
        CHECK(Count(f, BagStep::TapParty) == 2 && f.lastPicked == 3);
        CHECK(f.g.mode == BagMode::Message);
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Pick && Count(f, BagStep::PressA) == 1);
        // Slot under the cursor (still 3): one tap.
        d.Pick(3);
        CHECK(Run(d, f));
        CHECK(Count(f, BagStep::TapParty) == 3);
        d.Advance();
        CHECK(Run(d, f));
        d.Cancel();     // B back to the list
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List && f.log.back().step == BagStep::PressB);
    }
    // Give over a held item: message, Yes / No, message, list.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(2);
        CHECK(Run(d, f));
        d.Open(1, 78);
        CHECK(Run(d, f));
        CHECK(f.g.commands.size() == 4);
        d.Choose(1);    // Give: 3rd from the bottom
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 3 }));
        d.Pick(3);      // holds Quick Claw
        CHECK(Run(d, f));
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.YesNo());
        d.Answer(true);
        CHECK(Run(d, f));
        CHECK(f.log.back().step == BagStep::TapYes && f.held[3] == 78);
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List);
    }
    // Cancel from the item menu taps its last entry (Cancel); Close taps back.
    {
        FakeBag f;
        BagDriver d;
        d.Open(1, 18);
        CHECK(Run(d, f));
        d.Cancel();
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 1 }) && f.g.mode == BagMode::List);
        d.Close();
        CHECK(Run(d, f));
        CHECK(f.closed && f.log.back().step == BagStep::TapBack);
    }
    // A game that ignores the taps: a few tries, then a failure, no endless tapping.
    {
        FakeBag f;
        f.ignoreTaps = true;
        BagDriver d;
        d.Tab(3);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 3 && !d.Failure().empty());
        d.Open(0, 17);
        f.commands = 0;
        CHECK(!Run(d, f, false, 5000));
        CHECK(f.commands > 0 && f.commands <= (int)BagDriver::MaxCommands);
    }
    // Not where the goal starts (pick asked for in the list): fails at once.
    {
        FakeBag f;
        BagDriver d;
        d.Pick(2);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 0);
    }
    // The Bag closes under a goal: fails.
    {
        FakeBag f;
        BagDriver d;
        d.Open(0, 17);
        f.closed = true;
        CHECK(!Run(d, f, false));
        CHECK(d.Failure().find("gone") != std::string::npos);
    }
    // Settling: the driver doesn't act on a screen that keeps changing.
    {
        BagGame g;
        g.mode = BagMode::List;
        g.tab = 1;
        g.scroll = 0;
        g.items.push_back(BagItem{ 17, 1, false });
        g.visible = 1;
        BagDriver d;
        d.Tab(2);
        std::vector<std::string> log;
        for (u32 i = 0; i < 50; i++)
        {
            g.cursorRow = (int)(i & 1);
            CHECK(d.Update(g, false, log).step == BagStep::None);
        }
        CHECK(d.Busy());
        // While an action runs, nothing either.
        g.cursorRow = 0;
        for (u32 i = 0; i < 50; i++)
            CHECK(d.Update(g, true, log).step == BagStep::None);
        BagCmd cmd;
        for (u32 i = 0; i < 20 && cmd.step == BagStep::None; i++)
            cmd = d.Update(g, false, log);
        CHECK(cmd == (BagCmd{ BagStep::TapTab, 2 }));
    }
}

static void TestSignature(void)
{
    BagGame a;
    a.mode = BagMode::Menu;
    a.commands = { BagCommand::Use, BagCommand::Cancel };
    BagGame b = a;
    CHECK(a.Signature() == b.Signature());
    b.commands = { BagCommand::Yes, BagCommand::No };
    CHECK(a.Signature() != b.Signature());
    CHECK(b.YesNo() && !a.YesNo());
    b = a;
    b.partyCursor = 2;
    CHECK(a.Signature() != b.Signature());
    BagGame c;
    c.scroll = 3;
    c.cursorRow = 2;
    CHECK(c.Index() == 5);
    c.scroll = -1;
    CHECK(c.Index() == -1);
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void TestDraw(const char *preview)
{
    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);

    BagView view;
    view.mode = BagMode::Menu;
    view.tab = 1;
    const char *names[] = { "Potion", "Paralyze Heal", "Awakening", "Antidote", "Burn Heal", "Ice Heal",
                            "Revive", "Super Potion", "Fresh Water" };
    for (int i = 0; i < 9; i++)
    {
        view.names.push_back(names[i]);
        view.items.push_back(BagItem{ (u16)(17 + i), (u16)(9 - i), i == 4 });
    }
    view.cursor = 8;
    view.top = 1;
    view.commands = { BagCommand::Use, BagCommand::Give, BagCommand::FreeSpace, BagCommand::Cancel };
    view.menuTitle = "Fresh Water";
    const char *mons[] = { "Torracat", "Zorua", "Pikipek", "Pichu", "Ledyba", "Buneary" };
    for (int i = 0; i < 6; i++)
    {
        BagMon m;
        m.name = mons[i];
        m.level = 12 + i;
        m.maxHp = 30 + i;
        m.hp = i == 3 ? 7 : m.maxHp;
        m.held = i == 3 ? "Quick Claw" : "";
        view.party.push_back(m);
    }

    // Every mode draws, and nothing outside the area is touched.
    for (BagMode mode : { BagMode::List, BagMode::Pick, BagMode::Message, BagMode::Busy, BagMode::Menu })
    {
        canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));
        view.mode = mode;
        view.partyCursor = mode == BagMode::Pick ? 3 : -1;
        DrawBag(canvas, view);
        const UI::Color outside = At(canvas, BagArea.x - 1, BagArea.y + 50);
        CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
        CHECK(At(canvas, BagArea.Right() + 1, BagArea.Bottom() - 50).g == 200);
        CHECK(At(canvas, BagArea.x + 40, BagArea.y + 100).g != 200);
    }
    // Empty pocket.
    BagView empty;
    empty.mode = BagMode::List;
    empty.tab = 6;
    DrawBag(canvas, empty);
    DrawBag(canvas, view);

    if (preview != nullptr)
    {
        FILE *f = std::fopen(preview, "wb");
        if (f == nullptr)
        {
            std::printf("can't write %s\n", preview);
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
        std::printf("bag_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestTables();
    TestCursor();
    TestDriver();
    TestSignature();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("bag_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("bag_test: all passed\n");
    return 0;
}
