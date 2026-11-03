// Host tests for the native X menu's grid, cursor and actions
// (Sources/Screens/XMenuGrid.cpp). Build and run: make -C tests. With a path
// argument, also writes a preview of the menu there as a PPM image.

#include "Screens/XMenuGrid.hpp"

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

static void TestTable(void)
{
    const std::vector<XMenuEntry> &entries = XMenuEntries();
    CHECK(entries.size() == 12);
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
        if (std::string(e.name) == "qr_scanner")
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

        // Cells sit inside the menu and don't overlap.
        const UI::Rect r = XMenuCellRect(i);
        CHECK(r.x >= XMenuArea.x + 4 && r.Right() <= XMenuArea.Right() - 4);
        CHECK(r.y >= XMenuArea.y + 4 && r.Bottom() <= XMenuArea.Bottom() - 4);
        for (int j = 0; j < i; j++)
        {
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
                    Later(12, SceneField, WorldXMenu | WorldXMenuPage2);
                else if (name == "xmenu_page2.prev_page")
                    Later(12, SceneField, WorldXMenu);
                else if (name == "field.map")
                {
                    Later(10, "", 0);
                    Later(30, SceneTownMap, 0);
                }
                else if (name == "xmenu.quick_link" || name == "xmenu.festival_plaza")
                    Later(15, SceneField, 0);
                else
                    for (const XMenuEntry &e : XMenuEntries())
                        if (e.button != nullptr && name == e.button)
                        {
                            Later(10, "", 0);
                            Later(30, e.opens, 0);
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
            CHECK(e.opens == nullptr ? game.world.scene == SceneField && !(game.world.flags & WorldXMenu)
                                     : game.world.scene == e.opens);
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

    // Nothing outside the menu's area is touched (the Layer copies only it).
    const UI::Color outside = At(canvas, XMenuArea.x - 1, XMenuArea.y + 50);
    CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
    CHECK(At(canvas, 2, 2).g == 200);
    // The selected cell has the yellow ring just outside it; the others not.
    const UI::Rect sel = XMenuCellRect(view.cursor);
    const UI::Color ring = At(canvas, sel.x + sel.w / 2, sel.y - 2);
    CHECK(ring.r > 200 && ring.g > 200 && ring.b < 160);
    const UI::Rect other = XMenuCellRect(E("pokemon"));
    const UI::Color noRing = At(canvas, other.x + other.w / 2, other.y - 2);
    CHECK(!(noRing.r > 200 && noRing.g > 200 && noRing.b < 160));

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
    TestMove();
    TestFlipPage();
    TestCursorMemory();
    TestActions();
    TestNative();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("xmenu_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("xmenu_test: all passed\n");
    return 0;
}
