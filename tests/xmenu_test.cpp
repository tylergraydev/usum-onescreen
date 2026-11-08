// Host tests for the native X menu's grid, cursor and actions
// (Sources/Screens/XMenuGrid.cpp). Build and run: make -C tests. With a path
// argument, also writes a preview of the menu there as a PPM image.

#include "hires_screen_smoke.hpp"
#include "Screens/XMenuGrid.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static int E(const char *name)
{
    const int i = XMenuFind(name);
    CHECK(i >= 0);
    return i;
}

static unsigned AllBut(const char *name)
{
    return ~(1u << E(name));
}

static void TestCloseAction(void)
{
    // The Map entry's hand-over to the native map: B, then the field without
    // the X menu; allowed only from the open X menu.
    const ActionDef &def = XMenuCloseAction();
    CHECK(CheckAction(def).empty());
    CHECK(std::string(def.name) == "xmenu-close");
    CHECK(def.steps.size() == 2);
    CHECK(def.steps.size() == 2 && def.steps[0].kind == StepKind::Press && def.steps[0].keys == ActionKeyB);
    CHECK(def.steps.size() == 2 && def.steps[1].kind == StepKind::WaitFor);
    ActionWorld menu, field;
    menu.scene = field.scene = SceneField;
    menu.flags = WorldXMenu;
    CHECK(def.needs.Matches(menu) && !def.needs.Matches(field));
    CHECK(def.steps.size() == 2 && def.steps[1].until.Matches(field) && !def.steps[1].until.Matches(menu));
}

static void TestTable(void)
{
    const std::vector<XMenuEntry> &entries = XMenuEntries();
    CHECK(entries.size() == 14);
    CHECK(entries.size() <= 32);    // enabled masks are one word

    for (int i = 0; i < (int)entries.size(); i++)
    {
        const XMenuEntry &e = entries[i];
        if (e.page >= 0)
        {
            CHECK(XMenuAtSlot(e.page, e.slot) == i);
            CHECK(e.button != nullptr && FindLayoutButton(e.button) != nullptr);
            // The native grid mirrors the game's pages: page p is columns 2p, 2p+1.
            CHECK(e.col == e.page * 2 + e.slot % 2 && e.row == e.slot / 2);
        }
        // Every action is allowed except QR Scanner, whose tap isn't
        // verified (Azahar has no camera and hangs in it).
        const std::string bad = CheckAction(XMenuAction(i));
        if (std::string(e.name) == "qr_scanner" || !XMenuVisible(i))
            CHECK(!bad.empty());
        else
        {
            if (!bad.empty())
                std::printf("    %s: %s\n", e.name, bad.c_str());
            CHECK(bad.empty());
        }
        CHECK(std::string(XMenuAction(i).name) == std::string("xmenu-") + e.name);
        // Entries with a native screen (Screens/SubScreens.hpp).
        const std::string n = e.name;
        CHECK(XMenuOpensNative(i) == (n == "options" || n == "trainer_passport" || n == "battle_video"));

        if (!XMenuVisible(i))
        {
            CHECK(XMenuCellRect(i).Empty());
            continue;
        }
        // Cells sit inside the menu and don't overlap.
        const UI::Rect r = XMenuCellRect(i);
        CHECK(r.x >= XMenuArea.x + 4 && r.Right() <= XMenuArea.Right() - 4);
        CHECK(r.y >= XMenuArea.y + 4 && r.Bottom() <= XMenuArea.Bottom() - 4);
        for (int j = 0; j < i; j++)
        {
            if (!XMenuVisible(j))
                continue;
            const UI::Rect o = XMenuCellRect(j);
            CHECK(!(r.x < o.Right() && o.x < r.Right() && r.y < o.Bottom() && o.y < r.Bottom()));
        }
    }
    CHECK(XMenuAtSlot(1, 4) == -1);     // page 2's two empty slots
    CHECK(XMenuAtSlot(1, 5) == -1);
    CHECK(XMenuFind("nope") == -1);
}

static void TestMove(void)
{
    const unsigned all = ~0u;
    // Left / right stay in the row, across the page gap, no wrap.
    CHECK(XMenuMove(E("pokemon"), 1, 0, all) == E("pokedex"));
    CHECK(XMenuMove(E("pokedex"), 1, 0, all) == E("pokemon_refresh"));
    CHECK(XMenuMove(E("pokemon_refresh"), 1, 0, all) == E("qr_scanner"));
    CHECK(XMenuMove(E("qr_scanner"), 1, 0, all) == E("qr_scanner"));
    CHECK(XMenuMove(E("pokemon"), -1, 0, all) == E("pokemon"));
    CHECK(XMenuMove(E("festival_plaza"), 1, 0, all) == E("trainer_passport"));
    CHECK(XMenuMove(E("trainer_passport"), 1, 0, all) == E("trainer_passport"));   // page 2 row 3 is empty
    CHECK(XMenuMove(E("map"), 1, 0, all) == E("save"));
    CHECK(XMenuMove(E("save"), -1, 0, all) == E("map"));

    // Up / down: columns first, then the bottom row.
    CHECK(XMenuMove(E("pokemon"), 0, 1, all) == E("bag"));
    CHECK(XMenuMove(E("bag"), 0, 1, all) == E("festival_plaza"));
    CHECK(XMenuMove(E("festival_plaza"), 0, 1, all) == E("map"));
    CHECK(XMenuMove(E("trainer_passport"), 0, 1, all) == E("map"));
    CHECK(XMenuMove(E("battle_video"), 0, 1, all) == E("save"));     // skips the empty slots
    CHECK(XMenuMove(E("options"), 0, 1, all) == E("save"));
    CHECK(XMenuMove(E("map"), 0, 1, all) == E("map"));
    CHECK(XMenuMove(E("map"), 0, -1, all) == E("festival_plaza"));
    CHECK(XMenuMove(E("save"), 0, -1, all) == E("battle_video"));
    CHECK(XMenuMove(E("pokedex"), 0, -1, all) == E("pokedex"));
    CHECK(XMenuMove(E("options"), 0, -1, all) == E("qr_scanner"));

    // Disabled entries don't take the cursor.
    CHECK(XMenuMove(E("pokemon_refresh"), 1, 0, AllBut("qr_scanner")) == E("pokemon_refresh"));
    CHECK(XMenuMove(E("options"), 0, -1, AllBut("qr_scanner")) == E("pokemon_refresh"));
    CHECK(XMenuMove(E("pokemon"), 0, 1, AllBut("bag")) == E("festival_plaza"));

    // Bad input stays in range.
    CHECK(XMenuMove(-1, 1, 0, all) == 0);
    CHECK(XMenuMove(99, 0, 1, all) == 0);
}

static void TestFlipPage(void)
{
    const unsigned all = ~0u;
    CHECK(XMenuFlipPage(E("bag"), 1, all) == E("battle_video"));
    CHECK(XMenuFlipPage(E("battle_video"), 0, all) == E("bag"));
    CHECK(XMenuFlipPage(E("options"), 0, all) == E("quick_link"));
    CHECK(XMenuFlipPage(E("festival_plaza"), 1, all) == E("battle_video"));      // slot 5 is empty
    CHECK(XMenuFlipPage(E("trainer_passport"), 1, all) == E("options"));
    CHECK(XMenuFlipPage(E("pokedex"), 1, AllBut("qr_scanner")) == E("pokemon_refresh"));
    CHECK(XMenuFlipPage(E("pokedex"), 0, all) == E("pokedex"));
    CHECK(XMenuFlipPage(E("map"), 1, all) == E("map"));
}

static void TestCursorMemory(void)
{
    XMenuCursor c;
    CHECK(c.Index() == 0);
    // First reading: follow the game.
    CHECK(c.FromGame(1, 2));
    CHECK(c.Index() == E("battle_video"));
    // The same reading again changes nothing, even after our own moves.
    c.Set(E("map"));
    CHECK(!c.FromGame(1, 2));
    CHECK(c.Index() == E("map"));
    // The game's cursor moved (a choice on the bottom screen): follow it.
    CHECK(c.FromGame(0, 0));
    CHECK(c.Index() == E("pokemon"));
    // Opening an entry moves the game's cursor there too.
    c.Chosen(E("options"));
    CHECK(!c.FromGame(1, 3));
    CHECK(c.Index() == E("options"));
    // Save / Map don't move the game's cursor.
    c.Chosen(E("save"));
    CHECK(!c.FromGame(1, 3));
    CHECK(c.Index() == E("save"));
    // A reading of a slot we don't know is remembered but moves nothing.
    CHECK(!c.FromGame(1, 4));
    CHECK(c.Index() == E("save"));
}

// A fake X menu: taps on the page arrows flip the page, taps on an entry
// open it, Y and B close the menu, the field map opens the Town Map.
struct FakeMenu
{
    ActionWorld world;
    bool wasTouching = false;
    u32 prevKeys = 0;
    u32 frame = 0;
    std::vector<std::string> tapped;
    std::vector<u32> presses;
    struct Change { u32 at; std::string scene; u32 flags; };
    std::vector<Change> pending;

    FakeMenu(bool page2)
    {
        world.scene = SceneField;
        world.flags = WorldXMenu | (page2 ? WorldXMenuPage2 : 0);
    }

    void Later(u32 delay, const std::string &scene, u32 flags)
    {
        pending.push_back({ frame + delay, scene, flags });
    }

    const LayoutButton *Hit(u16 x, u16 y)
    {
        const bool menu = world.scene == SceneField && (world.flags & WorldXMenu);
        const std::string screen = !menu ? "field" : (world.flags & WorldXMenuPage2) ? "xmenu_page2" : "xmenu";
        if (world.scene != SceneField)
            return nullptr;
        for (const LayoutButton &b : LayoutButtons)
        {
            const std::string name = b.name;
            if (menu && name.find(".slot") == std::string::npos
                && name != "xmenu.next_page" && name != "xmenu_page2.prev_page")
                continue;
            if (name.substr(0, name.find('.')) == screen && x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1)
                return &b;
        }
        return nullptr;
    }

    void Feed(const ActionOutput &out)
    {
        if (out.touching && !wasTouching)
            if (const LayoutButton *b = Hit(out.x, out.y))
            {
                const std::string name = b->name;
                tapped.push_back(name);
                if (name == "xmenu.next_page")
                {
                    Later(3, SceneField, 0);   // probe drops while the page slides
                    Later(12, SceneField, WorldXMenu | WorldXMenuPage2);
                }
                else if (name == "xmenu_page2.prev_page")
                {
                    Later(3, SceneField, 0);
                    Later(12, SceneField, WorldXMenu);
                }
                else if (name == "field.map")
                {
                    Later(10, "", 0);
                    Later(30, SceneTownMap, 0);
                }
                else
                    for (const XMenuEntry &e : XMenuEntries())
                        if (e.button != nullptr && name == e.button)
                        {
                            if (e.opens == nullptr)
                                Later(15, std::string(e.name) == "poke_pelago" ? "main:0064787C" : SceneField, 0);
                            else
                            {
                                Later(10, "", 0);
                                Later(30, e.opens, 0);
                            }
                        }
            }
        wasTouching = out.touching;

        const u32 pressed = out.keys & ~prevKeys;
        prevKeys = out.keys;
        if (pressed != 0)
        {
            presses.push_back(pressed);
            if (world.flags & WorldXMenu)
                Later(8, SceneField, 0);
        }

        frame++;
        for (size_t i = 0; i < pending.size();)
        {
            if (pending[i].at <= frame)
            {
                world.scene = pending[i].scene;
                world.flags = pending[i].flags;
                pending.erase(pending.begin() + i);
            }
            else
                i++;
        }
    }
};

static bool Open(FakeMenu &game, const char *name, std::vector<std::string> &log)
{
    ActionRunner runner;
    if (!runner.Start(XMenuAction(E(name)), game.world, log))
        return false;
    for (u32 i = 0; i < 2000 && runner.Running(); i++)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    return !log.empty() && log.back().find(" ok in ") != std::string::npos;
}

static void PrintLog(const std::vector<std::string> &log)
{
    for (const std::string &line : log)
        std::printf("    %s\n", line.c_str());
}

static void TestNative(void)
{
    // Only the Pokédex has a native screen behind it (Screens/Pokedex.hpp).
    for (int i = 0; i < (int)XMenuEntries().size(); i++)
        CHECK((XMenuNativeScreen(i) != nullptr) == (i == E("pokedex")));
    CHECK(std::string(XMenuNativeScreen(E("pokedex"))) == "pokedex");
    CHECK(XMenuNativeScreen(-1) == nullptr);
    CHECK(XMenuNativeScreen(99) == nullptr);
}

static void TestActions(void)
{
    // Every grid entry from either page: one page tap only when needed.
    for (const XMenuEntry &e : XMenuEntries())
    {
        if (e.page < 0 || std::string(e.name) == "qr_scanner")
            continue;
        for (int startPage = 0; startPage < 2; startPage++)
        {
            FakeMenu game(startPage == 1);
            std::vector<std::string> log;
            const bool ok = Open(game, e.name, log);
            CHECK(ok);
            const size_t want = startPage == e.page ? 1 : 2;
            CHECK(game.tapped.size() == want);
            CHECK(!game.tapped.empty() && game.tapped.back() == e.button);
            CHECK(game.presses.empty());
            CHECK(e.opens == nullptr ? !(game.world.flags & WorldXMenu) : game.world.scene == e.opens);
            if (!ok)
                PrintLog(log);
        }
    }

    // Save: Y only, no taps; the game shows its own save dialog.
    {
        FakeMenu game(false);
        std::vector<std::string> log;
        CHECK(Open(game, "save", log));
        CHECK(game.tapped.empty());
        CHECK(game.presses.size() == 1 && game.presses[0] == (1u << 11));
    }

    // Map: B closes the X menu, then the field map is tapped.
    {
        FakeMenu game(true);
        std::vector<std::string> log;
        CHECK(Open(game, "map", log));
        CHECK(game.presses.size() == 1 && game.presses[0] == ActionKeyB);
        CHECK(game.tapped.size() == 1 && game.tapped[0] == "field.map");
        CHECK(game.world.scene == SceneTownMap);
    }

    // Refused unless the X menu is up.
    {
        FakeMenu game(false);
        game.world.flags = 0;
        std::vector<std::string> log;
        CHECK(!Open(game, "pokemon", log));
        CHECK(game.tapped.empty());
    }

    // A page arrow the game ignores is retried, then the action fails
    // rather than tapping an entry on the wrong page.
    {
        FakeMenu game(true);
        game.world.flags = WorldXMenu | WorldXMenuPage2;
        ActionRunner runner;
        std::vector<std::string> log;
        CHECK(runner.Start(XMenuAction(E("bag")), game.world, log));
        u32 taps = 0;
        bool wasTouching = false;
        for (u32 i = 0; i < 2000 && runner.Running(); i++)
        {
            const ActionOutput out = runner.Update(game.world, log);
            if (out.touching && !wasTouching)
            {
                taps++;
                CHECK(out.x == 16 && out.y == 12);      // only ever the page arrow
            }
            wasTouching = out.touching;
        }
        CHECK(!runner.Running());
        CHECK(taps == 3);
        CHECK(!log.empty() && log.back().find("FAILED") != std::string::npos);
    }
}

static XMenuGameLayout MoonLayout(void)
{
    XMenuGameLayout layout;
    for (int id = 0; id < 12; id++)
        layout.ids[id] = (u8)id;
    layout.unlocked = 0xFFF;
    layout.read = true;
    return layout;
}

static void TestLayouts(void)
{
    const ActionDef *quick = &XMenuAction(E("quick_link"));
    XMenuGameLayout moon = MoonLayout();
    const u32 version = XMenuLayoutVersion();
    CHECK(XMenuSetLayout(moon));
    CHECK(XMenuLayoutVersion() == version + 1);
    CHECK(!XMenuSetLayout(moon));
    CHECK(&XMenuAction(E("quick_link")) == quick);
    const char *names[] = { "pokemon", "pokedex", "bag", "photo_album", "quick_link", "festival_plaza",
                            "trainer_passport", "poke_pelago", "pokemon_refresh", "qr_scanner", "battle_video", "options" };
    for (int i = 0; i < 12; i++)
    {
        const XMenuEntry &e = XMenuEntries()[E(names[i])];
        CHECK(e.page == i / 6 && e.slot == i % 6);
        CHECK(e.col == (i / 6) * 2 + i % 2 && e.row == i % 6 / 2);
        CHECK(XMenuAtSlot(i / 6, i % 6) == E(names[i]));
    }
    TestActions();
    {
        FakeMenu game(false);
        std::vector<std::string> log;
        CHECK(Open(game, "quick_link", log));
        CHECK(game.tapped.size() == 1 && game.tapped[0] == "xmenu.slot5");
    }
    {
        FakeMenu game(false);
        std::vector<std::string> log;
        CHECK(Open(game, "options", log));
        CHECK(game.tapped.size() == 2 && game.tapped[0] == "xmenu.next_page"
              && game.tapped[1] == "xmenu_page2.slot6");
    }

    XMenuCursor cursor;
    cursor.Chosen(E("poke_pelago"));
    std::swap(moon.ids[6], moon.ids[7]);
    CHECK(XMenuSetLayout(moon));
    CHECK(XMenuAtSlot(1, 0) == E("poke_pelago"));
    CHECK(XMenuAtSlot(1, 1) == E("trainer_passport"));
    TestActions();
    moon.unlocked &= ~(1u << 7);
    CHECK(XMenuSetLayout(moon));
    CHECK(!XMenuVisible(E("poke_pelago")));
    CHECK(XMenuAtSlot(1, 0) == -1);
    CHECK(XMenuEntries()[E("poke_pelago")].page == -1);
    CHECK(!CheckAction(XMenuAction(E("poke_pelago"))).empty());
    CHECK(XMenuVisible(cursor.Index()) && cursor.Index() != E("poke_pelago"));
    CHECK(XMenuMove(E("trainer_passport"), -1, 0, ~0u) != E("poke_pelago"));

    XMenuGameLayout qr = MoonLayout();
    std::swap(qr.ids[0], qr.ids[9]);
    CHECK(XMenuSetLayout(qr));
    CHECK(XMenuEntries()[E("qr_scanner")].page == 0 && XMenuEntries()[E("qr_scanner")].slot == 0);
    CHECK(CheckAction(XMenuAction(E("qr_scanner"))).find("qr_scanner not available") != std::string::npos);

    moon.ids[0] = moon.ids[1];
    CHECK(XMenuSetLayout(moon));
    CHECK(!XMenuCurrentLayout().read);
    CHECK(XMenuAtSlot(0, 3) == E("quick_link"));
    moon = MoonLayout();
    moon.ids[0] = 12;
    XMenuSetLayout(moon);
    CHECK(!XMenuCurrentLayout().read && XMenuAtSlot(1, 4) == -1);

    // Definitions built on the default layout resolve a later layout at run time.
    FakeMenu game(false);
    ActionRunner runner;
    std::vector<std::string> log;
    CHECK(runner.Start(*quick, game.world, log));
    XMenuSetLayout(MoonLayout());
    while (runner.Running() && game.frame < 1000)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    CHECK(game.tapped.size() == 1 && game.tapped[0] == "xmenu.slot5");
    XMenuSetLayout(XMenuGameLayout());
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
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));

    XMenuView view;
    view.cursor = E("battle_video");
    view.enabled = AllBut("qr_scanner");
    DrawXMenu(canvas, view);
    CHECK(HiResScreenSmoke::Run(XMenuArea, XMenuRadius, [&](UI::Canvas &hi) { DrawXMenu(hi, view); }));

    // Nothing outside the menu's area is touched (the Layer copies only it).
    const UI::Color outside = At(canvas, XMenuArea.x - 1, XMenuArea.y + 50);
    CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
    CHECK(At(canvas, 2, 2).g == 200);
    // As in the game, the selected cell is marked by the red cursor at its
    // left end; the others not.
    const auto red = [](UI::Color c) { return c.r > 200 && c.g < 60 && c.b < 60; };
    const UI::Rect sel = XMenuCellRect(view.cursor);
    CHECK(red(At(canvas, sel.x + 2, sel.y + sel.h / 2)));
    const UI::Rect other = XMenuCellRect(E("pokemon"));
    CHECK(!red(At(canvas, other.x + 2, other.y + other.h / 2)));

    if (preview != nullptr)
    {
        // P6 PPM: small and needs no library; tools/ can turn it into PNG.
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
        std::printf("xmenu_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestTable();
    TestCloseAction();
    TestMove();
    TestFlipPage();
    TestCursorMemory();
    TestActions();
    TestNative();
    TestLayouts();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    XMenuSetLayout(MoonLayout());
    std::string moonPreview;
    if (argc > 1)
    {
        moonPreview = argv[1];
        const size_t dot = moonPreview.rfind('.');
        moonPreview.insert(dot == std::string::npos ? moonPreview.size() : dot, "-moon");
    }
    TestDraw(argc > 1 ? moonPreview.c_str() : nullptr);
    XMenuSetLayout(XMenuGameLayout());
    if (s_failures != 0)
    {
        std::printf("xmenu_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("xmenu_test: all passed\n");
    return 0;
}
