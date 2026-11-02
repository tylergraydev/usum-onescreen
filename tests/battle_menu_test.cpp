// Host tests for the native battle menu's pure logic (Sources/Screens/
// BattleMenu.cpp): pages follow the game's menu, input, settling, PP checks,
// move rows, effectiveness labels, the Poké Balls tab, and drawing inside
// each page's area. Build and run: make -C tests.

#include "Screens/BattleMenu.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace OneScreen;
using namespace OneScreen::Screens;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static BattleSnapshot CommandSnap(void)
{
    BattleSnapshot snap;
    snap.inBattle = true;
    snap.menu = GameMenu::Command;
    snap.activeName = "Torracat";
    return snap;
}

static BattleSnapshot MovesSnap(void)
{
    BattleSnapshot snap = CommandSnap();
    snap.menu = GameMenu::Moves;
    const u16 moves[4] = { 10, 424, 24, 122 };
    const u8 pp[4] = { 35, 15, 30, 30 }, maxPp[4] = { 35, 15, 30, 30 };
    const u8 type[4] = { 0, 9, 1, 7 }, effect[4] = { 1, 1, 2, 0 };
    snap.moveCount = BuildMoveRows(snap.moves, moves, pp, maxPp, type, effect, 4);
    const char *names[4] = { "Scratch", "Fire Fang", "Double Kick", "Lick" };
    for (u8 i = 0; i < snap.moveCount; i++)
        snap.moves[i].name = names[i];
    return snap;
}

// Runs `frames` frames with no input; returns the first action asked for.
static const char *Idle(BattleMenu &menu, const BattleSnapshot &snap, u32 frames)
{
    const char *action = nullptr;
    for (u32 i = 0; i < frames; i++)
    {
        const BattleMenuOutput out = menu.Update(snap, 0);
        if (action == nullptr)
            action = out.action;
    }
    return action;
}

static void TestPages(void)
{
    BattleMenu menu;
    BattleSnapshot snap;
    CHECK(!menu.Update(snap, BattleKeys::A).capture);
    CHECK(menu.Page() == BattlePage::Hidden);
    CHECK(menu.Area().Empty());

    snap.inBattle = true;   // intro, messages: nothing to choose
    CHECK(!menu.Update(snap, 0).capture);
    CHECK(menu.Page() == BattlePage::Hidden);

    snap = CommandSnap();
    CHECK(menu.Update(snap, 0).capture);
    CHECK(menu.Page() == BattlePage::Command && menu.Cursor() == 0);

    snap.menu = GameMenu::Bag;  // the game's own screen: buttons go to it
    CHECK(!menu.Update(snap, BattleKeys::A).capture);
    CHECK(menu.Page() == BattlePage::Game);
    snap.menu = GameMenu::Pokemon;
    CHECK(!menu.Update(snap, 0).capture && menu.Page() == BattlePage::Game);
    snap.menu = GameMenu::Other;
    CHECK(!menu.Update(snap, 0).capture && menu.Page() == BattlePage::Game);

    // After a catch (Pokédex scene, nickname question): helper, no capture.
    BattleSnapshot caught;
    caught.afterCatch = true;
    BattleMenu after;
    CHECK(!after.Update(caught, BattleKeys::B).capture && after.Page() == BattlePage::Game && after.Visible());
    CHECK(after.Status(caught).find("after catch") != std::string::npos);

    // Other screens get the helper only after the game's Bag / Pokémon list.
    BattleMenu intro;
    CHECK(!intro.Update(snap, 0).capture && intro.Page() == BattlePage::Hidden);

    // The command bar shows after ShowFrames (the game flashes its command
    // menu between a move and the turn).
    BattleMenu shown;
    const BattleSnapshot command = CommandSnap();
    shown.Update(command, 0);
    CHECK(!shown.Visible());
    Idle(shown, command, BattleMenu::ShowFrames);
    CHECK(shown.Visible());

    snap = MovesSnap();
    snap.gameCursor = 2;    // the game's list remembers the last move
    menu.Update(snap, 0);
    CHECK(menu.Page() == BattlePage::Moves && menu.Cursor() == 2);
}

static void TestCommand(void)
{
    BattleMenu menu;
    const BattleSnapshot snap = CommandSnap();
    menu.Update(snap, 0);
    menu.Update(snap, BattleKeys::DDown);
    CHECK(menu.Cursor() == 1);
    menu.Update(snap, BattleKeys::CPadDown);
    CHECK(menu.Cursor() == 2);
    menu.Update(snap, BattleKeys::DDown);
    menu.Update(snap, BattleKeys::DDown);   // wraps
    CHECK(menu.Cursor() == 0);
    menu.Update(snap, BattleKeys::DUp);
    CHECK(menu.Cursor() == 3);

    // A before the game's menu has settled waits for it, then sends once.
    CHECK(menu.Update(snap, BattleKeys::A).action == nullptr);
    const char *action = Idle(menu, snap, BattleMenu::CommandSettleFrames);
    CHECK(action != nullptr && std::strcmp(action, "battle-flee") == 0);
    CHECK(menu.Pending());
    // While the choice is on its way, input is ignored and nothing is resent.
    CHECK(menu.Update(snap, BattleKeys::DUp).action == nullptr);
    CHECK(menu.Cursor() == 3);
    CHECK(Idle(menu, snap, 50) == nullptr);

    // The game's menu goes (the run message) and comes back: a fresh menu.
    BattleSnapshot busy = snap;
    busy.menu = GameMenu::None;
    menu.Update(busy, 0);
    CHECK(menu.Page() == BattlePage::Hidden && !menu.Pending());
    menu.Update(snap, 0);
    CHECK(menu.Page() == BattlePage::Command && menu.Cursor() == 0);

    // Each command's action, after the menu has settled.
    const char *expect[] = { "battle-fight", "battle-bag", "battle-pokemon", "battle-flee" };
    for (u8 i = 0; i < 4; i++)
    {
        BattleMenu m;
        m.Update(snap, 0);
        Idle(m, snap, BattleMenu::CommandSettleFrames);
        for (u8 k = 0; k < i; k++)
            m.Update(snap, BattleKeys::DDown);
        const BattleMenuOutput out = m.Update(snap, BattleKeys::A);
        CHECK(out.action != nullptr && std::strcmp(out.action, expect[i]) == 0);
    }

    // An action running (the player's choice, or a test command) blocks input.
    BattleMenu m;
    BattleSnapshot running = snap;
    running.actionRunning = true;
    Idle(m, running, 40);
    CHECK(m.Update(running, BattleKeys::A).action == nullptr);
    CHECK(Idle(m, snap, 5) == nullptr);

    // A choice the game never answers is given up on.
    BattleMenu lost;
    lost.Update(snap, 0);
    Idle(lost, snap, BattleMenu::CommandSettleFrames);
    CHECK(lost.Update(snap, BattleKeys::A).action != nullptr);
    Idle(lost, snap, BattleMenu::PendingFrames + 1);
    CHECK(!lost.Pending() && !lost.Note().empty());
    CHECK(lost.Update(snap, BattleKeys::A).action != nullptr);
}

static void TestMoves(void)
{
    BattleSnapshot snap = MovesSnap();
    BattleMenu menu;
    menu.Update(snap, 0);
    Idle(menu, snap, BattleMenu::MovesSettleFrames);
    menu.Update(snap, BattleKeys::DDown);
    const BattleMenuOutput out = menu.Update(snap, BattleKeys::A);
    CHECK(out.action != nullptr && std::strcmp(out.action, "battle-move2") == 0);

    // B goes back to the commands.
    BattleMenu back;
    back.Update(snap, 0);
    Idle(back, snap, BattleMenu::MovesSettleFrames);
    const BattleMenuOutput b = back.Update(snap, BattleKeys::B);
    CHECK(b.action != nullptr && std::strcmp(b.action, "battle-moves-back") == 0);

    // A move without PP isn't sent while another has some.
    snap.moves[0].pp = 0;
    BattleMenu nopp;
    nopp.Update(snap, 0);
    Idle(nopp, snap, BattleMenu::MovesSettleFrames);
    CHECK(nopp.Update(snap, BattleKeys::A).action == nullptr);
    CHECK(nopp.Note() == "Scratch has no PP left");
    nopp.Update(snap, BattleKeys::DDown);
    CHECK(nopp.Note().empty());

    // With no PP anywhere the game uses Struggle: the tap goes through.
    for (u8 i = 0; i < 4; i++)
        snap.moves[i].pp = 0;
    BattleMenu struggle;
    struggle.Update(snap, 0);
    Idle(struggle, snap, BattleMenu::MovesSettleFrames);
    CHECK(struggle.Update(snap, BattleKeys::A).action != nullptr);

    // Fewer moves: the cursor stays on them.
    BattleSnapshot two = MovesSnap();
    two.moveCount = 2;
    BattleMenu m;
    m.Update(two, 0);
    m.Update(two, BattleKeys::DDown);
    m.Update(two, BattleKeys::DDown);
    CHECK(m.Cursor() == 0);
}

static void TestMoveRows(void)
{
    MoveRow rows[4];
    const u16 moves[4] = { 33, 43, 0, 0 };
    const u8 pp[4] = { 35, 29, 0, 0 }, maxPp[4] = { 35, 30, 0, 0 };
    const u8 type[4] = { 0, 0, 0, 0 }, effect[4] = { 3, 1, 0, 0 };
    rows[3].name = "stale";
    CHECK(BuildMoveRows(rows, moves, pp, maxPp, type, effect, 2) == 2);
    CHECK(rows[0].move == 33 && rows[0].pp == 35 && rows[0].effect == 3 && rows[0].type == 0);
    CHECK(rows[1].move == 43 && rows[1].pp == 29 && rows[1].maxPp == 30);
    CHECK(rows[3].name.empty() && rows[3].move == 0);

    // Without the game's list, type and effectiveness are unknown.
    CHECK(BuildMoveRows(rows, moves, pp, maxPp, type, effect, 0) == 2);
    CHECK(rows[0].type == 0xFF && rows[0].effect == 0xFF);
    CHECK(std::strcmp(TypeName(rows[0].type), "?") == 0);
}

static void TestLabels(void)
{
    CHECK(std::strcmp(TypeName(0), "Normal") == 0);
    CHECK(std::strcmp(TypeName(9), "Fire") == 0);
    CHECK(std::strcmp(TypeName(17), "Fairy") == 0);
    CHECK(std::strcmp(EffectLabel(0), "No effect") == 0);
    CHECK(std::strcmp(EffectLabel(1), "") == 0);
    CHECK(std::strcmp(EffectLabel(2), "Super effective") == 0);
    CHECK(std::strcmp(EffectLabel(3), "Not very effective") == 0);
    CHECK(std::strcmp(EffectLabel(0xFF), "") == 0);
    CHECK(std::strcmp(BattlePocketName(BattlePocketBalls), "Pok\xC3\xA9 Balls") == 0);
    CHECK(StepCursor(0, 4, -1) == 3 && StepCursor(3, 4, 1) == 0 && StepCursor(0, 0, 1) == 0);
}

static void TestBalls(void)
{
    // The Items pocket of the test save, in order (docs/RE3-party-bag.md).
    const u16 items[] = { 217, 78, 4, 3, 86, 58, 57, 14, 87, 245, 12 };
    const u16 counts[] = { 1, 3, 37, 5, 1, 1, 1, 1, 1, 1, 0 };
    u16 outItems[8], outCounts[8];
    const u8 n = BallsInPocket(items, counts, 11, outItems, outCounts, 8);
    CHECK(n == 3);
    CHECK(outItems[0] == 4 && outCounts[0] == 37);
    CHECK(outItems[1] == 3 && outItems[2] == 14);
    CHECK(IsBallItem(851) && IsBallItem(492) && !IsBallItem(17) && !IsBallItem(0));
}

// Every page draws inside its own area and nowhere else.
static void TestDrawing(void)
{
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * 3, 0);
    UI::Surface surface = {};
    surface.origin = pixels.data();
    surface.format = UI::PixelFormat::BGR8;
    surface.stepX = 3;
    surface.stepY = UI::ScreenW * 3;
    surface.width = UI::ScreenW;
    surface.height = UI::ScreenH;

    BattleSnapshot snaps[3] = { CommandSnap(), MovesSnap(), CommandSnap() };
    snaps[2].menu = GameMenu::Bag;
    snaps[2].bagPocket = BattlePocketBalls;
    snaps[2].bagItem = "Pok\xC3\xA9 Ball";
    snaps[2].bagItemCount = 37;
    for (const BattleSnapshot &snap : snaps)
    {
        std::fill(pixels.begin(), pixels.end(), 0);
        BattleMenu menu;
        menu.Update(snap, 0);
        const UI::Rect area = menu.Area();
        CHECK(!area.Empty());
        CHECK(area.y >= 36 && area.x >= 166 && area.Right() <= UI::ScreenW && area.Bottom() <= UI::ScreenH);
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

    // The draw key changes with what is shown, and only then.
    BattleSnapshot snap = MovesSnap();
    BattleMenu menu;
    menu.Update(snap, 0);
    const std::string key = menu.DrawKey(snap);
    CHECK(menu.DrawKey(snap) == key);
    snap.moves[1].pp = 14;
    CHECK(menu.DrawKey(snap) != key);
}

int main(void)
{
    TestPages();
    TestCommand();
    TestMoves();
    TestMoveRows();
    TestLabels();
    TestBalls();
    TestDrawing();
    if (s_failures != 0)
    {
        std::printf("battle_menu_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("battle_menu_test: ok\n");
    return 0;
}
