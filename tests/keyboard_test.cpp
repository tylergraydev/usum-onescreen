// Host tests for the native keyboard's grid, cursor, key presses and actions
// (Sources/Screens/KeyboardGrid.cpp). Build and run: make -C tests. With a
// path argument, also writes a preview of the keyboard there as a PPM image.

#include "Screens/KeyboardGrid.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static int K(const char *name)
{
    const int i = KeyboardFind(name);
    CHECK(i >= 0);
    return i;
}

static bool Overlap(const UI::Rect &a, const UI::Rect &b)
{
    return a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
}

static void TestTable(void)
{
    const std::vector<KeyboardKey> &keys = KeyboardKeys();
    // 45 character keys, Space, Delete, OK, Shift: the game's QWERTY page.
    CHECK(keys.size() == 49);
    int perRow[5] = {};
    for (int i = 0; i < (int)keys.size(); i++)
    {
        const KeyboardKey &k = keys[i];
        CHECK(k.row >= 0 && k.row < 5);
        perRow[k.row]++;
        CHECK(KeyboardFind(k.name) == i);
        // Every key is a layout button verified by a tap.
        const std::string bad = CheckAction(KeyboardAction(i));
        if (!bad.empty())
            std::printf("    %s: %s\n", k.name, bad.c_str());
        CHECK(bad.empty());
        CHECK(std::string(KeyboardAction(i).name) == std::string("keyboard-") + k.name);
        CHECK(KeyboardAction(i).steps.size() == 2 && KeyboardAction(i).steps[0].kind == StepKind::Tap);

        // Game rects: inside the bottom screen, keys don't overlap there or natively.
        const UI::Rect g = KeyboardGameRect(i);
        CHECK(g.w > 0 && g.h > 0 && g.x >= 0 && g.Right() <= 320 && g.y >= 0 && g.Bottom() <= 240);
        const UI::Rect r = KeyboardKeyRect(i);
        CHECK(r.x >= KeyboardArea.x + 4 && r.Right() <= KeyboardArea.Right() - 4);
        CHECK(r.y >= KeyboardArea.y + 4 && r.Bottom() <= KeyboardArea.Bottom() - 4);
        CHECK(r.w >= 20 && r.h >= 20);         // room for one glyph of the 18 px font
        for (int j = 0; j < i; j++)
        {
            CHECK(!Overlap(g, KeyboardGameRect(j)));
            CHECK(!Overlap(r, KeyboardKeyRect(j)));
        }

        // Character keys type something with shift off and on.
        if (k.kind == KeyKind::Char || k.kind == KeyKind::Space)
            CHECK(k.lower != 0 && k.upper != 0);
        else
            CHECK(k.lower == 0 && k.upper == 0);
    }
    CHECK(perRow[0] == 12 && perRow[1] == 11 && perRow[2] == 12 && perRow[3] == 12 && perRow[4] == 2);
    CHECK(KeyboardFind("nope") == -1);
    CHECK(CheckAction(KeyboardToQwerty()).empty());

    // A few keys from the key sweep (docs/KEYBOARD.md).
    CHECK(keys[K("1")].lower == '1' && keys[K("1")].upper == GameMale);
    CHECK(keys[K("2")].upper == GameFemale);
    CHECK(keys[K("0")].upper == ')');
    CHECK(keys[K("e_acute")].lower == 0xE9 && keys[K("e_acute")].upper == 0xC9);
    CHECK(keys[K("apostrophe")].lower == 0x2019);
    CHECK(keys[K("q")].upper == 'Q');
    // Tap points are the measured ones.
    const LayoutButton *q = FindLayoutButton("strinput.q");
    CHECK(q != nullptr && q->tapX == 40 && q->tapY == 109);
    const LayoutButton *ok = FindLayoutButton("strinput.ok");
    CHECK(ok != nullptr && ok->tapX == 260 && ok->tapY == 109);
}

static void TestMove(void)
{
    // Left / right along the row, wrapping.
    CHECK(KeyboardMove(K("q"), 1, 0) == K("w"));
    CHECK(KeyboardMove(K("q"), -1, 0) == K("ok"));
    CHECK(KeyboardMove(K("ok"), 1, 0) == K("q"));
    CHECK(KeyboardMove(K("excl"), -1, 0) == K("delete"));
    CHECK(KeyboardMove(K("quote_close"), 1, 0) == K("z"));
    CHECK(KeyboardMove(K("space"), 1, 0) == K("shift"));
    CHECK(KeyboardMove(K("shift"), 1, 0) == K("space"));

    // Down: the rows step right by half a key; ties go right.
    CHECK(KeyboardMove(K("excl"), 0, 1) == K("q"));
    CHECK(KeyboardMove(K("q"), 0, 1) == K("a"));
    CHECK(KeyboardMove(K("a"), 0, 1) == K("z"));
    CHECK(KeyboardMove(K("z"), 0, 1) == K("space"));
    CHECK(KeyboardMove(K("quote_close"), 0, 1) == K("shift"));
    CHECK(KeyboardMove(K("delete"), 0, 1) == K("ok"));
    CHECK(KeyboardMove(K("ok"), 0, 1) == K("e_acute"));
    // Up: ties go left, so up and down undo each other.
    CHECK(KeyboardMove(K("b"), 0, -1) == K("g"));
    CHECK(KeyboardMove(K("g"), 0, -1) == K("t"));
    CHECK(KeyboardMove(K("t"), 0, 1) == K("g"));
    CHECK(KeyboardMove(K("g"), 0, 1) == K("b"));
    CHECK(KeyboardMove(K("e_acute"), 0, -1) == K("ok"));
    // Wrap between the top row and Space / Shift.
    CHECK(KeyboardMove(K("space"), 0, 1) == K("4"));
    CHECK(KeyboardMove(K("shift"), 0, 1) == K("delete"));
    CHECK(KeyboardMove(K("excl"), 0, -1) == K("space"));
    CHECK(KeyboardMove(K("delete"), 0, -1) == K("shift"));

    // Every key can be reached from every key (no dead ends), and up then
    // down from a character row (not the wide keys) comes back.
    const int n = (int)KeyboardKeys().size();
    for (int from = 0; from < n; from++)
    {
        std::vector<bool> seen(n, false);
        std::vector<int> todo = { from };
        seen[from] = true;
        while (!todo.empty())
        {
            const int at = todo.back();
            todo.pop_back();
            const int next[4] = { KeyboardMove(at, 1, 0), KeyboardMove(at, -1, 0), KeyboardMove(at, 0, 1),
                                  KeyboardMove(at, 0, -1) };
            for (int m : next)
            {
                CHECK(m >= 0 && m < n);
                if (m >= 0 && m < n && !seen[m])
                {
                    seen[m] = true;
                    todo.push_back(m);
                }
            }
        }
        for (int i = 0; i < n; i++)
            CHECK(seen[i]);
    }

    CHECK(KeyboardMove(-1, 1, 0) == 0);
    CHECK(KeyboardMove(99, 0, 1) == 0);
    CHECK(KeyboardMove(K("q"), 0, 0) == K("q"));
}

static void TestText(void)
{
    const u16 name[] = { 'B', 'o', GameMale, 0xE9, 0x201C, 0 };
    CHECK(KeyboardUtf8(name, 5) == "Bo\xE2\x99\x82\xC3\xA9\xE2\x80\x9C");
    CHECK(KeyboardUtf8(name, 2) == "Bo");
    CHECK(KeyboardUtf8(name, 16) == KeyboardUtf8(name, 5));      // stops at the 0
    const u16 female = GameFemale;
    CHECK(KeyboardUtf8(&female, 1) == "\xE2\x99\x80");
    CHECK(KeyboardKeyText(K("q"), false) == "q");
    CHECK(KeyboardKeyText(K("q"), true) == "Q");
    CHECK(KeyboardKeyText(K("3"), true) == "+");
    CHECK(KeyboardKeyText(K("space"), false) == " ");
    CHECK(KeyboardKeyText(K("delete"), false).empty());
    CHECK(KeyboardKeyText(-1, false).empty());
}

static void TestPress(void)
{
    KeyboardGame game;
    game.max = 12;
    game.length = 0;
    CHECK(KeyboardPressKey(K("q"), game, 0, 0) == KeyboardPress::Tap);
    CHECK(KeyboardPressKey(K("delete"), game, 0, 0) == KeyboardPress::Empty);
    CHECK(KeyboardPressKey(K("delete"), game, 1, 0) == KeyboardPress::Tap);       // a queued character
    CHECK(KeyboardPressKey(K("ok"), game, 0, 0) == KeyboardPress::Tap);
    CHECK(KeyboardPressKey(K("shift"), game, 0, 0) == KeyboardPress::Tap);

    // The limit counts queued characters, less queued deletes.
    game.length = 11;
    CHECK(KeyboardPressKey(K("q"), game, 0, 0) == KeyboardPress::Tap);
    CHECK(KeyboardPressKey(K("q"), game, 1, 0) == KeyboardPress::Full);
    CHECK(KeyboardPressKey(K("space"), game, 1, 0) == KeyboardPress::Full);
    CHECK(KeyboardPressKey(K("q"), game, 1, 1) == KeyboardPress::Tap);
    game.length = 12;
    CHECK(KeyboardPressKey(K("e_acute"), game, 0, 0) == KeyboardPress::Full);
    CHECK(KeyboardPressKey(K("delete"), game, 0, 0) == KeyboardPress::Tap);
    CHECK(KeyboardPressKey(K("ok"), game, 0, 0) == KeyboardPress::Tap);
    CHECK(KeyboardPressKey(K("shift"), game, 0, 0) == KeyboardPress::Tap);
    // More deletes queued than there are characters.
    game.length = 1;
    CHECK(KeyboardPressKey(K("delete"), game, 0, 1) == KeyboardPress::Empty);
    CHECK(KeyboardPressKey(-1, game, 0, 0) == KeyboardPress::Empty);
}

// A fake game keyboard: taps on the QWERTY keys type like the game (shift
// on at the start and one-shot, the length limit), Delete and Shift work,
// OK closes it, and the page arrows switch pages.
struct FakeKeyboard
{
    ActionWorld world;
    KeyboardGame game;
    bool closed = false;
    bool wasTouching = false;
    std::vector<std::string> tapped;

    FakeKeyboard()
    {
        world.scene = SceneDex;
        game.max = 12;
        game.shift = true;
    }

    void Feed(const ActionOutput &out)
    {
        if (out.touching && !wasTouching && !closed)
            for (const LayoutButton &b : LayoutButtons)
            {
                const std::string name = b.name;
                const std::string screen = name.substr(0, name.find('.'));
                if (screen != (game.qwerty ? "strinput" : "strinput_grid")
                    || out.x < b.x0 || out.x > b.x1 || out.y < b.y0 || out.y > b.y1)
                    continue;
                tapped.push_back(name);
                const int k = KeyboardFind(name.substr(name.find('.') + 1));
                if (name == "strinput_grid.to_qwerty")
                    game.qwerty = true;
                else if (name == "strinput.to_grid")
                    game.qwerty = false;
                else if (k >= 0)
                {
                    const KeyboardKey &key = KeyboardKeys()[k];
                    if (key.kind == KeyKind::Shift)
                        game.shift = !game.shift;
                    else if (key.kind == KeyKind::Delete && game.length > 0)
                        game.text[--game.length] = 0;
                    else if (key.kind == KeyKind::Ok)
                        closed = true;
                    else if ((key.kind == KeyKind::Char || key.kind == KeyKind::Space) && game.length < game.max)
                    {
                        game.text[game.length++] = game.shift ? key.upper : key.lower;
                        game.shift = false;
                    }
                }
            }
        wasTouching = out.touching;
    }
};

static bool Run(FakeKeyboard &kb, const ActionDef &def)
{
    ActionRunner runner;
    std::vector<std::string> log;
    if (!runner.Start(def, kb.world, log))
        return false;
    for (u32 i = 0; i < 200 && runner.Running(); i++)
        kb.Feed(runner.Update(kb.world, log));
    CHECK(!runner.Running());
    return !log.empty() && log.back().find(" ok in ") != std::string::npos;
}

// Types `keys` (names) as the native keyboard would: a press is refused
// when KeyboardPressKey says so, otherwise its action runs.
static void Type(FakeKeyboard &kb, const std::vector<const char *> &keys)
{
    for (const char *name : keys)
        if (KeyboardPressKey(K(name), kb.game, 0, 0) == KeyboardPress::Tap)
            CHECK(Run(kb, KeyboardAction(K(name))));
}

static void TestActions(void)
{
    // "Bobo": the game starts with shift on, so B is a capital.
    {
        FakeKeyboard kb;
        Type(kb, { "b", "o", "b", "o" });
        CHECK(KeyboardUtf8(kb.game.text, kb.game.length) == "Bobo");
        CHECK(kb.tapped.size() == 4 && kb.tapped[0] == "strinput.b");
        // Shift, then the digit row types symbols; delete; OK closes.
        Type(kb, { "shift", "1", "delete", "delete", "ok" });
        CHECK(KeyboardUtf8(kb.game.text, kb.game.length) == "Bob");
        CHECK(kb.closed);
    }
    // The limit: the 13th character is never sent.
    {
        FakeKeyboard kb;
        std::vector<const char *> keys(13, "a");
        Type(kb, keys);
        CHECK(kb.game.length == 12);
        CHECK(kb.tapped.size() == 12);
        CHECK(KeyboardPressKey(K("a"), kb.game, 0, 0) == KeyboardPress::Full);
    }
    // On the grid page, the way back to QWERTY is one tap.
    {
        FakeKeyboard kb;
        kb.game.qwerty = false;
        CHECK(Run(kb, KeyboardToQwerty()));
        CHECK(kb.game.qwerty);
        CHECK(kb.tapped.size() == 1 && kb.tapped[0] == "strinput_grid.to_qwerty");
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

    KeyboardView view;
    view.cursor = K("g");
    view.game.max = 12;
    view.game.length = 4;
    const u16 bobo[] = { 'B', 'o', 'b', 'o' };
    std::memcpy(view.game.text, bobo, sizeof(bobo));
    view.game.shift = true;
    DrawKeyboard(canvas, view);

    // Nothing outside the keyboard's area is touched (the Layer copies only it).
    const UI::Color outside = At(canvas, KeyboardArea.x - 1, KeyboardArea.y + 50);
    CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
    CHECK(At(canvas, 2, 2).g == 200);
    // The cursor key has the yellow ring just outside it; the others not.
    const UI::Rect sel = KeyboardKeyRect(view.cursor);
    const UI::Color ring = At(canvas, sel.x + sel.w / 2, sel.y - 1);
    CHECK(ring.r > 200 && ring.g > 200 && ring.b < 160);
    const UI::Rect other = KeyboardKeyRect(K("q"));
    const UI::Color noRing = At(canvas, other.x + other.w / 2, other.y - 1);
    CHECK(!(noRing.r > 200 && noRing.g > 200 && noRing.b < 160));
    // Shift on: its key is orange.
    const UI::Rect shift = KeyboardKeyRect(K("shift"));
    const UI::Color sc = At(canvas, shift.x + 3, shift.y + shift.h / 2);
    CHECK(sc.r > 200 && sc.g < 140 && sc.b < 80);

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
        std::printf("keyboard_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestTable();
    TestMove();
    TestText();
    TestPress();
    TestActions();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("keyboard_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("keyboard_test: all passed\n");
    return 0;
}
