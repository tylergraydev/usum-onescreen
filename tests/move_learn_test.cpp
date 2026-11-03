// Host tests for native move learning (Sources/Screens/MoveLearnMenu.cpp,
// Sources/MoveData.cpp): the move data pack, the screen's pages and keys,
// its actions against a fake of the game's question and forget screen, and
// drawing inside its area. Build and run: make -C tests.
//
//   move_learn_test [preview.ppm] [a/0/1/1 GARC]
// writes the move list as a picture and, given the user's own move data
// GARC, checks a few real moves (skipped when missing, as in CI).

#include "MoveData.hpp"
#include "Screens/BattleMenu.hpp"
#include "Screens/MoveLearnMenu.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace OneScreen;
using namespace OneScreen::Screens;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

// --- Move data ---

static void Put16(std::vector<u8> &b, size_t at, u32 v) { b[at] = (u8)v; b[at + 1] = (u8)(v >> 8); }
static void Put32(std::vector<u8> &b, size_t at, u32 v) { Put16(b, at, v & 0xFFFF); Put16(b, at + 2, v >> 16); }

static std::vector<u8> MakePack(u32 count)
{
    const u32 header = 4 + (count + 1) * 4;
    std::vector<u8> pack(header + count * 0x28, 0);
    pack[0] = 'W';
    pack[1] = 'D';
    Put16(pack, 2, count);
    for (u32 i = 0; i <= count; i++)
        Put32(pack, 4 + i * 4, header + i * 0x28);
    for (u32 i = 0; i < count; i++)
    {
        u8 *r = &pack[header + i * 0x28];
        r[0] = (u8)(i % 18);    // type
        r[2] = (u8)(i % 3);     // category
        r[3] = (u8)(i * 5);     // power
        r[4] = 100;             // accuracy
        r[5] = (u8)(5 + i);     // PP
    }
    return pack;
}

static void TestMovePack(void)
{
    std::vector<MoveInfo> moves;
    std::string error;
    CHECK(ParseMovePack(MakePack(10), moves, error));
    CHECK(moves.size() == 10);
    CHECK(moves[7].known && moves[7].type == 7 && moves[7].category == 1 && moves[7].power == 35
          && moves[7].accuracy == 100 && moves[7].pp == 12);

    std::vector<u8> bad = MakePack(3);
    bad[0] = 'X';
    CHECK(!ParseMovePack(bad, moves, error) && moves.empty());
    bad = MakePack(3);
    Put32(bad, 8, 0xFFFF);  // record 1 starts past the end
    CHECK(!ParseMovePack(bad, moves, error) && moves.empty());
    bad = MakePack(3);
    bad.resize(10);         // offsets cut off
    CHECK(!ParseMovePack(bad, moves, error));
    CHECK(std::strcmp(MoveCategoryName(1), "Physical") == 0 && std::strcmp(MoveCategoryName(9), "?") == 0);
}

// The user's own GARC: entry 0 is the pack. Values from the game's move
// pages (Splash, Baby-Doll Eyes) and battle (Scratch, Fire Fang).
static void TestRealMoveData(const char *path)
{
    FILE *f = path ? std::fopen(path, "rb") : nullptr;
    if (f == nullptr)
    {
        std::printf("move_learn_test: no move data GARC, real check skipped\n");
        return;
    }
    std::vector<u8> garc;
    u8 buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        garc.insert(garc.end(), buf, buf + n);
    std::fclose(f);
    const RomfsRead read = [&garc](uint64_t offset, void *dst, u32 size) {
        if (offset + size > garc.size())
            return false;
        std::memcpy(dst, &garc[offset], size);
        return true;
    };
    uint64_t offset = 0;
    u32 size = 0;
    std::string error;
    CHECK(GarcEntry(read, 0, 0, offset, size, error));
    std::vector<u8> pack(garc.begin() + offset, garc.begin() + offset + size);
    std::vector<MoveInfo> moves;
    CHECK(ParseMovePack(pack, moves, error));
    CHECK(moves.size() == 729);
    if (moves.size() < 729)
        return;
    CHECK(moves[10].type == 0 && moves[10].category == 1 && moves[10].power == 40 && moves[10].pp == 35);   // Scratch
    CHECK(moves[424].type == 9 && moves[424].power == 65 && moves[424].accuracy == 95 && moves[424].pp == 15); // Fire Fang
    CHECK(moves[150].category == 0 && moves[150].power == 0 && moves[150].pp == 40);                          // Splash
    CHECK(moves[608].type == 17 && moves[608].category == 0 && moves[608].accuracy == 100 && moves[608].pp == 30); // Baby-Doll Eyes
    std::printf("move_learn_test: real move data checked (%u moves)\n", (unsigned)moves.size());
}

// --- The screen ---

static LearnMove Move(u16 id, const char *name, u8 type, u8 category, u8 power, u8 pp)
{
    LearnMove m;
    m.move = id;
    m.name = name;
    m.type = type;
    m.category = category;
    m.power = power;
    m.accuracy = 100;
    m.pp = pp;
    m.maxPp = pp;
    return m;
}

static LearnSnapshot AskSnap(void)
{
    LearnSnapshot snap;
    snap.stage = LearnStage::Ask;
    snap.monName = "Buneary";
    snap.moves[0] = Move(111, "Defense Curl", 0, 0, 0, 40);
    snap.moves[1] = Move(150, "Splash", 0, 0, 0, 40);
    snap.moves[2] = Move(1, "Pound", 0, 1, 40, 35);
    snap.moves[3] = Move(193, "Foresight", 0, 0, 0, 40);
    snap.moveCount = 4;
    snap.newMove = Move(608, "Baby-Doll Eyes", 17, 0, 0, 30);
    return snap;
}

static LearnOutput Idle(MoveLearnMenu &menu, const LearnSnapshot &snap, u32 frames)
{
    LearnOutput last;
    for (u32 i = 0; i < frames; i++)
        last = menu.Update(snap, 0);
    return last;
}

static void TestPages(void)
{
    MoveLearnMenu menu;
    LearnSnapshot none;
    CHECK(!menu.Update(none, LearnKeys::A).capture);
    CHECK(menu.Page() == LearnPage::Hidden && menu.Area().Empty());

    // The question shows only once it has settled.
    const LearnSnapshot snap = AskSnap();
    CHECK(Idle(menu, snap, MoveLearnMenu::ShowFrames - 2).capture);    // held, not shown yet
    CHECK(menu.Page() == LearnPage::Hidden);
    CHECK(Idle(menu, snap, 2).capture);
    CHECK(menu.Page() == LearnPage::Choose && menu.Cursor() == 0);

    // Up from the first move wraps to "don't learn"; down goes back round.
    menu.Update(snap, LearnKeys::DUp);
    CHECK(menu.Cursor() == LearnSkipRow);
    menu.Update(snap, LearnKeys::CPadDown);
    CHECK(menu.Cursor() == 0);
    menu.Update(snap, LearnKeys::DDown);
    CHECK(menu.Cursor() == 1);
    CHECK(menu.Status(snap) == "move-learn: page choose, game ask, Buneary wants Baby-Doll Eyes, cursor 1 (Splash)");

    // B jumps to "don't learn"; A asks first, B there goes back.
    menu.Update(snap, LearnKeys::B);
    CHECK(menu.Cursor() == LearnSkipRow && menu.Page() == LearnPage::Choose);
    menu.Update(snap, LearnKeys::DDown);
    CHECK(menu.Cursor() == 0);
    menu.Update(snap, LearnKeys::DDown);
    CHECK(menu.Update(snap, LearnKeys::A).action == nullptr);
    CHECK(menu.Page() == LearnPage::Confirm);
    CHECK(LearnQuestion(snap, menu.Cursor()) == "Forget Splash and learn Baby-Doll Eyes?");
    menu.Update(snap, LearnKeys::B);
    CHECK(menu.Page() == LearnPage::Choose && menu.Cursor() == 1);

    // A, A: the action for that row.
    menu.Update(snap, LearnKeys::A);
    const LearnOutput out = menu.Update(snap, LearnKeys::A);
    CHECK(out.action == MoveLearnAction(1));
    CHECK(out.action != nullptr && std::strcmp(out.action->name, "learn-forget2") == 0);
    CHECK(menu.Page() == LearnPage::Working);
    // The game keeps its screen up a while; nothing is sent twice.
    CHECK(Idle(menu, snap, 100).action == nullptr);
    menu.ActionDone(true);
    CHECK(menu.Page() == LearnPage::Done);
    CHECK(menu.Result() == "Buneary forgot Splash and learned Baby-Doll Eyes");
    // Still asked (the game is slow to close): it doesn't come back.
    Idle(menu, snap, MoveLearnMenu::DoneFrames + 5);
    CHECK(menu.Page() == LearnPage::Hidden);
    Idle(menu, snap, 100);
    CHECK(menu.Page() == LearnPage::Hidden);

    // The game moves on; a second Pokémon asks: it shows again.
    Idle(menu, none, 10);
    Idle(menu, snap, MoveLearnMenu::ShowFrames);
    CHECK(menu.Page() == LearnPage::Choose && menu.Cursor() == 0);
}

static void TestSkipAndFailure(void)
{
    MoveLearnMenu menu;
    LearnSnapshot snap = AskSnap();
    Idle(menu, snap, MoveLearnMenu::ShowFrames);
    menu.Update(snap, LearnKeys::B);
    CHECK(LearnQuestion(snap, menu.Cursor()) == "Don't learn Baby-Doll Eyes?");
    menu.Update(snap, LearnKeys::A);
    const LearnOutput out = menu.Update(snap, LearnKeys::A);
    CHECK(out.action != nullptr && std::strcmp(out.action->name, "learn-skip") == 0);
    // It failed: back to the list with a note, and it can be tried again.
    menu.ActionDone(false);
    CHECK(menu.Page() == LearnPage::Choose && !menu.Note().empty());
    menu.Update(snap, LearnKeys::A);
    CHECK(menu.Update(snap, LearnKeys::A).action != nullptr);
    menu.ActionDone(true);
    CHECK(menu.Result() == "Buneary did not learn Baby-Doll Eyes");

    // No keys while an action runs; the game answered another way (the
    // panel): the screen goes.
    MoveLearnMenu other;
    Idle(other, snap, MoveLearnMenu::ShowFrames);
    snap.actionRunning = true;
    CHECK(other.Update(snap, LearnKeys::DDown).capture);
    CHECK(other.Cursor() == 0);
    snap.actionRunning = false;
    snap.stage = LearnStage::List;
    CHECK(other.Update(snap, 0).capture && other.Page() == LearnPage::Choose);
    snap.stage = LearnStage::None;
    CHECK(!other.Update(snap, LearnKeys::A).capture && other.Page() == LearnPage::Hidden);

    // An action that never ends frees the screen.
    MoveLearnMenu stuck;
    snap.stage = LearnStage::Ask;
    Idle(stuck, snap, MoveLearnMenu::ShowFrames);
    stuck.Update(snap, LearnKeys::A);
    stuck.Update(snap, LearnKeys::A);
    Idle(stuck, snap, MoveLearnMenu::WorkingFrames + 2);
    CHECK(stuck.Page() == LearnPage::Choose && !stuck.Note().empty());

    // Fewer than four moves can't happen here, but the rows still skip
    // the empty ones.
    MoveLearnMenu three;
    snap.moveCount = 3;
    Idle(three, snap, MoveLearnMenu::ShowFrames);
    three.Update(snap, LearnKeys::DUp);
    CHECK(three.Cursor() == LearnSkipRow);
    three.Update(snap, LearnKeys::DUp);
    CHECK(three.Cursor() == 2);
}

// A fake of the game's three screens: what each tap does.
struct FakeLearn
{
    ActionWorld world;
    bool wasTouching = false;
    u32 frame = 0, lag = 0;
    u32 ignoreFirst = 0;            // taps to ignore (a screen still sliding in)
    std::string confirmFor, done;   // the row the page is about; what was confirmed
    struct Change { u32 at; u32 flags; };
    std::vector<Change> pending;

    void Feed(const ActionOutput &out)
    {
        frame++;
        for (size_t i = 0; i < pending.size();)
            if (pending[i].at <= frame)
            {
                world.flags = pending[i].flags;
                pending.erase(pending.begin() + i);
            }
            else
                i++;
        if (out.touching && !wasTouching)
            Tap(out.x, out.y);
        wasTouching = out.touching;
    }

    void Tap(u16 x, u16 y)
    {
        const char *hit = nullptr;
        for (const LayoutButton &b : LayoutButtons)
            if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1 && std::strncmp(b.name, "learn_", 6) == 0)
            {
                const std::string name = b.name;
                const bool ask = name.compare(0, 10, "learn_ask.") == 0, list = name.compare(0, 12, "learn_moves.") == 0;
                if ((ask && world.flags == WorldLearnAsk) || (list && world.flags == WorldLearnList)
                    || (!ask && !list && world.flags == WorldLearnConfirm))
                    hit = b.name;
            }
        if (hit == nullptr)
            return;
        if (ignoreFirst > 0)
        {
            ignoreFirst--;
            return;
        }
        const std::string name = hit;
        if (name == "learn_ask.forget")
            pending.push_back({ frame + 40, WorldLearnList });
        else if (name.compare(0, 12, "learn_moves.") == 0)
        {
            confirmFor = name.substr(12);
            pending.push_back({ frame + 15, WorldLearnConfirm });
        }
        else if (name == "learn_confirm.yes")
        {
            done = confirmFor;
            pending.push_back({ frame + 30, 0 });
        }
    }
};

static bool RunLearn(FakeLearn &game, u8 row, std::vector<std::string> &log)
{
    const ActionDef *def = MoveLearnAction(row);
    CHECK(def != nullptr);
    ActionRunner runner;
    if (def == nullptr || !runner.Start(*def, game.world, log))
        return false;
    for (u32 i = 0; i < 3000 && runner.Running(); i++)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    return !log.empty() && log.back().find(" ok in ") != std::string::npos;
}

static void TestActions(void)
{
    for (u8 row = 0; row <= LearnSkipRow; row++)
    {
        const ActionDef *def = MoveLearnAction(row);
        CHECK(def != nullptr && CheckAction(*def).empty());
    }
    CHECK(MoveLearnAction(LearnSkipRow + 1) == nullptr);

    static const char *const rows[] = { "move1", "move2", "move3", "move4", "new" };
    for (u8 row = 0; row <= LearnSkipRow; row++)
    {
        FakeLearn game;
        game.world.scene = SceneBattle;
        game.world.flags = WorldLearnAsk;
        std::vector<std::string> log;
        CHECK(RunLearn(game, row, log));
        CHECK(game.done == rows[row]);
    }

    // From the forget screen itself (the question already answered).
    FakeLearn list;
    list.world.scene = SceneBattle;
    list.world.flags = WorldLearnList;
    std::vector<std::string> log;
    CHECK(RunLearn(list, 2, log));
    CHECK(list.done == "move3");

    // Taps lost while the screens slide in are tried again.
    FakeLearn slow;
    slow.world.scene = SceneBattle;
    slow.world.flags = WorldLearnAsk;
    slow.ignoreFirst = 1;
    log.clear();
    CHECK(RunLearn(slow, 0, log));
    CHECK(slow.done == "move1");

    // Out of battle it doesn't start.
    FakeLearn field;
    field.world.scene = SceneField;
    ActionRunner runner;
    log.clear();
    CHECK(!runner.Start(*MoveLearnAction(0), field.world, log));
}

static void WritePpm(const std::vector<u8> &pixels, const char *path)
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
    for (size_t i = 0; i < pixels.size(); i += 3)
    {
        const u8 rgb[3] = { pixels[i + 2], pixels[i + 1], pixels[i] };  // BGR8 -> RGB
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

// Every page draws inside its own area and nowhere else; the list (with a
// grey backdrop) is written as a preview.
static void TestDrawing(const char *preview)
{
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * 3, 0);
    UI::Surface surface = {};
    surface.origin = pixels.data();
    surface.format = UI::PixelFormat::BGR8;
    surface.stepX = 3;
    surface.stepY = UI::ScreenW * 3;
    surface.width = UI::ScreenW;
    surface.height = UI::ScreenH;

    const LearnSnapshot snap = AskSnap();
    const u32 keys[][2] = { { 0, 0 }, { LearnKeys::DDown, LearnKeys::A }, { LearnKeys::A, LearnKeys::A } };
    for (int page = 0; page < 4; page++)
    {
        std::fill(pixels.begin(), pixels.end(), 0);
        MoveLearnMenu menu;
        Idle(menu, snap, MoveLearnMenu::ShowFrames);
        if (page >= 1)
        {
            menu.Update(snap, keys[1][0]);
            menu.Update(snap, keys[1][1]);   // confirm
        }
        if (page >= 2)
            menu.Update(snap, LearnKeys::A);    // working
        if (page >= 3)
            menu.ActionDone(true);              // done
        const UI::Rect area = menu.Area();
        CHECK(!area.Empty());
        CHECK(area.y >= 36 && area.x >= 156 && area.Right() <= UI::ScreenW && area.Bottom() <= 194);
        UI::Canvas canvas(surface);
        menu.Draw(canvas, snap);
        u32 inside = 0, outside = 0;
        for (s32 y = 0; y < UI::ScreenH; y++)
            for (s32 x = 0; x < UI::ScreenW; x++)
            {
                const u8 *p = &pixels[(y * UI::ScreenW + x) * 3];
                const bool lit = p[0] | p[1] | p[2];
                const bool in = x >= area.x && x < area.Right() && y >= area.y && y < area.Bottom();
                (in ? inside : outside) += lit ? 1 : 0;
            }
        CHECK(inside > 1000);
        CHECK(outside == 0);
    }

    MoveLearnMenu menu;
    Idle(menu, snap, MoveLearnMenu::ShowFrames);
    menu.Update(snap, LearnKeys::DDown);
    for (size_t i = 0; i < pixels.size(); i++)
        pixels[i] = 70;
    UI::Canvas canvas(surface);
    menu.Draw(canvas, snap);
    WritePpm(pixels, preview);

    // The draw key changes with what is shown, and only then.
    const std::string key = menu.DrawKey(snap);
    CHECK(menu.DrawKey(snap) == key);
    LearnSnapshot changed = snap;
    changed.moves[2].pp = 34;
    CHECK(menu.DrawKey(changed) != key);
    CHECK(LearnPowerText(0) == "-" && LearnPowerText(40) == "40");
    CHECK(LearnAccuracyText(101) == "-" && LearnAccuracyText(95) == "95");
}

int main(int argc, char **argv)
{
    TestMovePack();
    TestRealMoveData(argc > 2 ? argv[2] : nullptr);
    TestPages();
    TestSkipAndFailure();
    TestActions();
    TestDrawing(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("move_learn_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("move_learn_test: all passed\n");
    return 0;
}
