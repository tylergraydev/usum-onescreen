// Host tests for the native battle menu's pure logic (Sources/Screens/
// BattleMenu.cpp, BattleMenuDraw.cpp): pages follow the game's menu, input,
// settling, PP checks, move rows, effectiveness labels, the native Pokémon
// list, Bag and "Use next Pokémon?" against a fake game, and drawing inside
// each page's area. Build and run: make -C tests.

#include "Screens/BattleMenu.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ActionQueue.hpp"

using namespace OneScreen;
using namespace OneScreen::Screens;

static int s_failures = 0;
static const char *s_previewDir = nullptr;    // argv[1]: write PPM previews there

static void WritePreview(const std::vector<u8> &bgr, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    std::fprintf(f, "P6\n%d %d\n255\n", UI::ScreenW, UI::ScreenH);
    for (size_t i = 0; i + 2 < bgr.size(); i += 3)
    {
        const u8 rgb[3] = { bgr[i + 2], bgr[i + 1], bgr[i] };
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

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

    snap.menu = GameMenu::Bag;  // native pages, with their keys
    CHECK(menu.Update(snap, 0).capture && menu.Page() == BattlePage::Bag);
    snap.menu = GameMenu::Pokemon;
    CHECK(menu.Update(snap, 0).capture && menu.Page() == BattlePage::Party);
    snap.menu = GameMenu::PokemonPage;  // same page
    CHECK(menu.Page() == BattlePage::Party);
    snap.menu = GameMenu::Question;
    snap.question = QuestionNextPokemon;
    CHECK(menu.Update(snap, 0).capture && menu.Page() == BattlePage::Question);
    snap.question = 0x99;   // a question the menu doesn't know: the helper
    CHECK(!menu.Update(snap, 0).capture && menu.Page() == BattlePage::Game);
    snap.question = 0;
    snap.menu = GameMenu::Other;
    CHECK(!menu.Update(snap, 0).capture && menu.Page() == BattlePage::Game);

    // After a catch (Pokédex scene, nickname question): helper, no capture.
    BattleSnapshot caught;
    caught.afterCatch = true;
    BattleMenu after;
    CHECK(!after.Update(caught, BattleKeys::B).capture && after.Page() == BattlePage::Game && after.Visible());
    CHECK(after.Status(caught).find("after catch") != std::string::npos);
    // ...drawn at the top: the question is along the bottom of the screen.
    CHECK(after.Area().Bottom() <= 120 && !after.Area().Empty());

    // Other screens not reached from the Bag / Pokémon list: before the
    // first command menu (the intro's objects) nothing; after it (the
    // battle's questions) a helper at the top, only once it stays.
    BattleMenu intro;
    CHECK(!intro.Update(snap, 0).capture && intro.Prompt() && !intro.Visible());
    Idle(intro, snap, BattleMenu::PromptShowFrames * 2);
    CHECK(!intro.Visible());
    intro.Update(CommandSnap(), 0);
    intro.Update(snap, 0);
    CHECK(!intro.Visible());
    Idle(intro, snap, BattleMenu::PromptShowFrames);
    CHECK(intro.Visible() && intro.Area().Bottom() <= 120);
    CHECK(intro.Status(snap).find("prompt") != std::string::npos);
    // From the Pokémon list (a game screen it opened): at once.
    BattleMenu fromList;
    BattleSnapshot list = snap;
    list.menu = GameMenu::Pokemon;
    fromList.Update(list, 0);
    fromList.Update(snap, 0);
    CHECK(!fromList.Prompt() && fromList.Visible());

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
    CHECK(!lost.Pending() && lost.Note() == "No answer from the game, try again");
    CHECK(lost.Update(snap, BattleKeys::A).action != nullptr);

    // Run the game ignores (trapped by Wrap, seen with Ekans): said so.
    BattleMenu trapped;
    trapped.Update(snap, 0);
    Idle(trapped, snap, BattleMenu::CommandSettleFrames);
    trapped.Update(snap, BattleKeys::DUp);
    CHECK(trapped.Update(snap, BattleKeys::A).action != nullptr);
    Idle(trapped, snap, BattleMenu::PendingFrames + 1);
    CHECK(trapped.Note() == "Can't escape! (trapped?)");
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

    // A refused move (Disable): the list stays, the note says so.
    BattleMenu refused;
    refused.Update(snap, 0);
    Idle(refused, snap, BattleMenu::MovesSettleFrames);
    CHECK(refused.Update(snap, BattleKeys::A).action != nullptr);
    Idle(refused, snap, BattleMenu::PendingFrames + 1);
    CHECK(!refused.Pending() && refused.Note() == "The game refused that move, pick another");

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

static void TestItems(void)
{
    CHECK(IsBallItem(4) && IsBallItem(851) && IsBallItem(492) && !IsBallItem(17) && !IsBallItem(0));
    CHECK(IsHpOnlyItem(17) && IsHpOnlyItem(155) && !IsHpOnlyItem(28) && !IsHpOnlyItem(4));
    for (u8 p = 0; p < BattlePocketCount; p++)
        CHECK(BattlePocketButton(p) != nullptr && FindLayoutButton(BattlePocketButton(p)) != nullptr);
    CHECK(BattlePocketButton(BattlePocketCount) == nullptr);
}

// --- A fake game for the Pokémon list, Bag and question ---

// The game as the menu sees it: taps land after the action (18 frames)
// and change the screen as the real one did in instance w71 (docs/BATTLE-UI.md).
struct FakeGame
{
    BattleSnapshot snap;
    u32 busy = 0;               // frames left of the running step
    std::vector<std::string> taps;
    int switchedTo = -1;        // slot sent in
    int usedOn = -1;            // pick slot an item was used on
    u16 usedItem = 0;
    bool thrown = false, left = false, answered = false;
    int answer = -1;
    u32 gap = 0;                // frames of "no screen" then the command menu between list and page
    int pageFor = -1;
    bool ignoreTaps = false;

    FakeGame()
    {
        snap.inBattle = true;
        snap.activeName = "Torracat";
        const char *names[6] = { "Torracat", "Zorua", "Pikipek", "Pichu", "Ledyba", "Buneary" };
        const u16 hp[6] = { 51, 20, 35, 0, 37, 37 };
        snap.partyCount = 6;
        for (u8 i = 0; i < 6; i++)
        {
            snap.party[i].name = names[i];
            snap.party[i].species = names[i];
            snap.party[i].level = 12;
            snap.party[i].hp = hp[i];
            snap.party[i].maxHp = i == 0 ? 51 : 37;
        }
    }

    void Pocket(u8 pocket)
    {
        snap.bagPocket = pocket;
        static const BattleBagItem pockets[5][4] = {
            { { 958, 2, "Roto Boost" } },
            { { 17, 9, "Potion" }, { 26, 2, "Super Potion" }, { 30, 1, "Fresh Water" }, { 155, 7, "Oran Berry" } },
            { { 22, 5, "Paralyze Heal" }, { 28, 8, "Revive" } },
            { { 4, 37, "Pok\xC3\xA9 Ball" }, { 3, 5, "Great Ball" }, { 14, 1, "Heal Ball" } },
            {},
        };
        static const u8 counts[5] = { 1, 4, 2, 3, 0 };
        snap.bagItemCount = counts[pocket];
        for (u8 i = 0; i < counts[pocket]; i++)
            snap.bagItems[i] = pockets[pocket][i];
    }

    void Apply(const BattleMenuOutput &out)
    {
        if (out.tap == nullptr && out.press == 0)
            return;
        CHECK(busy == 0);
        busy = 18;
        const std::string tap = out.tap ? out.tap : "press";
        if (out.tap != nullptr)
        {
            const LayoutButton *button = FindLayoutButton(out.tap);
            CHECK(button != nullptr);
            taps.push_back(tap);
        }
        else
            taps.push_back("press B");
        if (ignoreTaps)
            return;
        BattleSnapshot &s = snap;
        if (s.menu == GameMenu::Pokemon)
        {
            if (tap.compare(0, 10, "party.slot") == 0)
            {
                pageFor = tap[10] - '1';
                gap = 6;
                s.menu = GameMenu::None;
            }
            else if (tap == "battle_party.back")
            {
                s.menu = GameMenu::Command;
                left = true;
            }
        }
        else if (s.menu == GameMenu::PokemonPage)
        {
            if (tap == "battle_party_page.switch")
            {
                switchedTo = pageFor;
                s.menu = GameMenu::None;
            }
            else if (tap == "battle_party_page.back")
                s.menu = GameMenu::Pokemon;
        }
        else if (s.menu == GameMenu::Bag)
        {
            for (u8 p = 0; p < BattlePocketCount; p++)
                if (tap == BattlePocketButton(p))
                    Pocket(p);
            if (s.bagPick)
            {
                if (tap == "press")
                    s.bagPick = false;
                else if (tap.compare(0, 14, "bag_party.slot") == 0)
                {
                    const u8 slot = (u8)(tap[14] - '1');
                    if (s.bagPickCursor != slot)
                        s.bagPickCursor = slot;     // a tap off the cursor only moves it
                    else
                    {
                        usedOn = slot;
                        usedItem = s.bagItems[s.bagPageIndex].id;
                        s.bagPick = false;
                        s.menu = GameMenu::None;
                    }
                }
            }
            else if (s.bagItemPage)
            {
                if (tap == "battle_bag_item_page.next_item")
                    s.bagPageIndex = (u8)((s.bagPageIndex + 1) % s.bagItemCount);
                else if (tap == "battle_bag_item_page.prev_item")
                    s.bagPageIndex = (u8)((s.bagPageIndex + s.bagItemCount - 1) % s.bagItemCount);
                else if (tap == "battle_bag_item_page.back")
                    s.bagItemPage = false;
                else if (tap == "battle_bag_item_page.use")
                {
                    s.bagItemPage = false;
                    if (IsBallItem(s.bagItems[s.bagPageIndex].id))
                    {
                        thrown = true;
                        usedItem = s.bagItems[s.bagPageIndex].id;
                        s.menu = GameMenu::None;
                    }
                    else
                    {
                        s.bagPick = true;
                        s.bagPickCursor = 0;
                    }
                }
            }
            else if (tap == "bag.item_row1" && s.bagItemCount > 0)
            {
                s.bagItemPage = true;
                s.bagPageIndex = 0;
            }
            else if (tap == "battle_bag.back")
            {
                s.menu = GameMenu::Command;
                left = true;
            }
        }
        else if (s.menu == GameMenu::Question)
        {
            answered = true;
            answer = tap == "battle_ask.first" ? 0 : tap == "battle_ask.second" ? 1 : -1;
            s.menu = answer == 0 ? GameMenu::Pokemon : GameMenu::None;
        }
    }

    // One frame: the menu, then the game.
    void Frame(BattleMenu &menu, u32 pressed = 0)
    {
        snap.actionRunning = busy > 0;
        const BattleMenuOutput out = menu.Update(snap, pressed);
        if (busy > 0)
            busy--;
        if (gap > 0 && --gap == 0)
            snap.menu = GameMenu::PokemonPage;
        else if (gap == 3)
            snap.menu = GameMenu::Command;  // the game's command menu, for a moment
        Apply(out);
    }

    void Run(BattleMenu &menu, u32 frames)
    {
        for (u32 i = 0; i < frames; i++)
            Frame(menu);
    }
};

static void TestParty(void)
{
    // Pokémon from the command bar: the list, cursor on the first that could
    // come in; A, Switch: tap the slot, then Switch on its page, through the
    // game's gap between them.
    FakeGame game;
    BattleMenu menu;
    game.snap.menu = GameMenu::Command;
    game.Run(menu, BattleMenu::CommandSettleFrames);
    menu.Update(game.snap, BattleKeys::DDown);
    menu.Update(game.snap, BattleKeys::DDown);
    const BattleMenuOutput out = menu.Update(game.snap, BattleKeys::A);
    CHECK(out.action != nullptr && std::strcmp(out.action, "battle-pokemon") == 0);
    game.snap.menu = GameMenu::None;
    game.Run(menu, 3);
    game.snap.menu = GameMenu::Pokemon;
    game.Frame(menu);
    CHECK(menu.Page() == BattlePage::Party && !menu.Forced() && menu.Cursor() == 1);
    CHECK(menu.Status(game.snap).find("Zorua") != std::string::npos);

    // In battle, fainted, and moves: Right/Left in the row, Down a row.
    game.Frame(menu, BattleKeys::DLeft);
    game.Frame(menu, BattleKeys::A);
    CHECK(menu.Note() == "Torracat is already in battle!" && !menu.PopupOpen());
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::DRight);
    CHECK(menu.Cursor() == 3);
    game.Frame(menu, BattleKeys::A);
    CHECK(menu.Note() == "Pichu has no energy left to battle!");
    game.Frame(menu, BattleKeys::DUp);
    CHECK(menu.Cursor() == 1 && menu.Note().empty());
    game.Frame(menu, BattleKeys::A);
    CHECK(menu.PopupOpen() && menu.PopupCursor() == 0);
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::A);   // Cancel
    CHECK(!menu.PopupOpen() && !menu.Busy());
    game.Frame(menu, BattleKeys::A);
    game.Frame(menu, BattleKeys::A);   // Switch
    CHECK(menu.Busy());
    game.Run(menu, 200);
    CHECK(game.switchedTo == 1);
    CHECK(game.taps.size() == 2 && game.taps[0] == "party.slot2" && game.taps[1] == "battle_party_page.switch");
    CHECK(menu.Page() == BattlePage::Hidden && !menu.Busy());

    // B goes back (the list's back button).
    FakeGame back;
    BattleMenu backMenu;
    back.snap.menu = GameMenu::Command;
    back.Run(backMenu, BattleMenu::CommandSettleFrames);
    backMenu.Update(back.snap, BattleKeys::DDown);
    backMenu.Update(back.snap, BattleKeys::DDown);
    backMenu.Update(back.snap, BattleKeys::A);
    back.snap.menu = GameMenu::Pokemon;
    back.Frame(backMenu, BattleKeys::B);
    back.Run(backMenu, 100);
    CHECK(back.left && back.taps.size() == 1 && back.taps[0] == "battle_party.back");

    // After a faint (no Pokémon command): no way back, B says so.
    FakeGame forced;
    BattleMenu forcedMenu;
    forced.snap.party[0].hp = 0;
    forced.snap.menu = GameMenu::Pokemon;
    forced.Frame(forcedMenu);
    CHECK(forcedMenu.Forced());
    forced.Frame(forcedMenu, BattleKeys::B);
    CHECK(!forcedMenu.Busy() && forcedMenu.Note() == "Choose a Pok\xC3\xA9mon to send out");
    CHECK(forcedMenu.Status(forced.snap).find("forced") != std::string::npos);

    // A game that never answers: the goal gives up with a note.
    FakeGame deaf;
    BattleMenu deafMenu;
    deaf.ignoreTaps = true;
    deaf.snap.menu = GameMenu::Pokemon;
    deaf.Frame(deafMenu);
    deaf.Frame(deafMenu, BattleKeys::A);
    deaf.Frame(deafMenu, BattleKeys::A);
    deaf.Run(deafMenu, BattleMenu::GoalFrames + 10);
    CHECK(!deafMenu.Busy() && deafMenu.Note() == "The game didn't answer, try again");
    CHECK(deaf.taps.size() <= 3);
}

static void TestBag(void)
{
    FakeGame game;
    BattleMenu menu;
    game.snap.menu = GameMenu::Bag;
    game.Pocket(0);
    game.Frame(menu);
    CHECK(menu.Page() == BattlePage::Bag && menu.BagCursor() == 0);

    // Right: the HP/PP tab (a tap on it).
    game.Frame(menu, BattleKeys::DRight);
    CHECK(menu.WantPocket() == 1);
    game.Run(menu, 120);
    CHECK(game.snap.bagPocket == 1 && !menu.Busy() && menu.WantPocket() == -1);
    CHECK(game.taps.back() == "battle_bag.pocket_hp_pp");
    // L wraps to the left of it: Rotom Powers; R back.
    game.Frame(menu, BattleKeys::L);
    game.Run(menu, 120);
    CHECK(game.snap.bagPocket == 0);
    game.Frame(menu, BattleKeys::R);
    game.Run(menu, 120);
    CHECK(game.snap.bagPocket == 1);

    // Fresh Water (the third): its page through the arrows, Use, then
    // "Which Pokémon...?": native pick; full HP and fainted are refused,
    // Zorua (20/37) gets it with two taps (the first only moves the cursor).
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::DDown);
    CHECK(menu.BagCursor() == 2);
    game.taps.clear();
    game.Frame(menu, BattleKeys::A);
    game.Run(menu, 300);
    CHECK(game.snap.bagPick && !menu.Busy());
    CHECK(game.taps.size() == 4 && game.taps[0] == "bag.item_row1" && game.taps[1] == "battle_bag_item_page.next_item"
          && game.taps[3] == "battle_bag_item_page.use");
    // The cursor starts on the first Pokémon it would help (Zorua).
    CHECK(menu.Status(game.snap).find("pick 1 of 6 (Zorua 20/37)") != std::string::npos);
    game.Frame(menu, BattleKeys::DUp);
    game.Frame(menu, BattleKeys::A);
    CHECK(menu.Note() == "It won't have any effect." && !menu.Busy());
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::DDown);
    game.Frame(menu, BattleKeys::A);   // Pichu, fainted
    CHECK(menu.Note() == "It won't have any effect.");
    game.Frame(menu, BattleKeys::DUp);
    game.Frame(menu, BattleKeys::DUp);
    CHECK(menu.PickCursor() == 1);
    game.taps.clear();
    game.Frame(menu, BattleKeys::A);
    CHECK(menu.Busy() && menu.BusyText() == "Using Fresh Water on Zorua...");
    game.Run(menu, 200);
    CHECK(game.usedOn == 1 && game.usedItem == 30);
    CHECK(game.taps.size() == 2 && game.taps[0] == "bag_party.slot2" && game.taps[1] == "bag_party.slot2");
    CHECK(menu.Page() == BattlePage::Hidden);

    // A ball: the Poké Balls tab, Great Ball (second), thrown.
    FakeGame ball;
    BattleMenu ballMenu;
    ball.snap.menu = GameMenu::Bag;
    ball.Pocket(1);
    ball.Frame(ballMenu);
    ball.Frame(ballMenu, BattleKeys::DRight);
    ball.Run(ballMenu, 100);
    ball.Frame(ballMenu, BattleKeys::DRight);
    ball.Run(ballMenu, 100);
    CHECK(ball.snap.bagPocket == BattlePocketBalls);
    ball.Frame(ballMenu, BattleKeys::CPadDown);
    ball.Frame(ballMenu, BattleKeys::A);
    CHECK(ballMenu.BusyText() == "Throwing Great Ball...");
    ball.Run(ballMenu, 300);
    CHECK(ball.thrown && ball.usedItem == 3);

    // B from the pick goes back to the list; B there leaves the Bag.
    FakeGame out;
    BattleMenu outMenu;
    out.snap.menu = GameMenu::Bag;
    out.Pocket(1);
    out.snap.bagPick = true;
    out.snap.bagPickCursor = 0;
    out.snap.bagPageIndex = 0;
    out.Frame(outMenu);
    out.Run(outMenu, BattleMenu::ListSettleFrames);
    out.Frame(outMenu, BattleKeys::B);
    out.Run(outMenu, 100);
    CHECK(!out.snap.bagPick && out.snap.menu == GameMenu::Bag && out.taps.back() == "press B");
    out.Frame(outMenu, BattleKeys::B);
    out.Run(outMenu, 100);
    CHECK(out.left && out.taps.back() == "battle_bag.back");

    // The Bag changing under a choice: nothing is used.
    FakeGame changed;
    BattleMenu changedMenu;
    changed.snap.menu = GameMenu::Bag;
    changed.Pocket(3);
    changed.Frame(changedMenu);
    changed.Frame(changedMenu, BattleKeys::A);
    changed.snap.bagItems[0].id = 3;
    changed.Run(changedMenu, 200);
    CHECK(!changed.thrown && changedMenu.Note() == "The Bag changed, choose again");

    // Scrolling: the window follows the cursor, BagRows at a time.
    FakeGame many;
    BattleMenu manyMenu;
    many.snap.menu = GameMenu::Bag;
    many.snap.bagItemCount = 9;
    for (u8 i = 0; i < 9; i++)
        many.snap.bagItems[i] = { (u16)(17 + i), 1, "Item" };
    many.Frame(manyMenu);
    for (int i = 0; i < 7; i++)
        many.Frame(manyMenu, BattleKeys::DDown);
    CHECK(manyMenu.BagCursor() == 7 && manyMenu.BagTop() == 7 - BattleMenu::BagRows + 1);
    many.Frame(manyMenu, BattleKeys::DUp);
    many.Frame(manyMenu, BattleKeys::DDown);
    many.Frame(manyMenu, BattleKeys::DDown);
    many.Frame(manyMenu, BattleKeys::DDown);   // wraps to the top
    CHECK(manyMenu.BagCursor() == 0 && manyMenu.BagTop() == 0);
}

static void TestQuestion(void)
{
    // "Use next Pokémon?": Use Next Pokémon opens the (forced) list.
    FakeGame game;
    BattleMenu menu;
    game.snap.menu = GameMenu::Question;
    game.snap.question = QuestionNextPokemon;
    game.Frame(menu);
    CHECK(menu.Page() == BattlePage::Question && menu.Visible());
    game.Frame(menu, BattleKeys::A);
    game.Run(menu, 100);
    CHECK(game.answered && game.answer == 0 && menu.Page() == BattlePage::Party && menu.Forced());

    // Run: the second entry.
    FakeGame run;
    BattleMenu runMenu;
    run.snap.menu = GameMenu::Question;
    run.snap.question = QuestionNextPokemon;
    run.Frame(runMenu);
    run.Frame(runMenu, BattleKeys::DDown);
    CHECK(runMenu.Status(run.snap).find("(Run)") != std::string::npos);
    run.Frame(runMenu, BattleKeys::A);
    run.Run(runMenu, 100);
    CHECK(run.answer == 1);
}

// The target choice (never seen in the game: no double or SOS battle in
// the save): a game screen right after a move is the target screen; its
// buttons are only measured, so a choice says to use the bottom screen.
static void TestTarget(void)
{
    BattleSnapshot snap = MovesSnap();
    BattleMenu menu;
    menu.Update(snap, 0);
    Idle(menu, snap, BattleMenu::MovesSettleFrames);
    CHECK(menu.Update(snap, BattleKeys::A).action != nullptr);
    BattleSnapshot gap = snap;
    gap.menu = GameMenu::None;
    Idle(menu, gap, 20);
    BattleSnapshot target = snap;
    target.menu = GameMenu::Other;
    target.otherVtable = 0x10C000;
    const BattleMenuOutput out = menu.Update(target, 0);
    CHECK(menu.Page() == BattlePage::Target && out.capture && menu.Visible());
    CHECK(menu.Area().y >= 36 && menu.Area().x >= 166);
    menu.Update(target, BattleKeys::DRight);
    CHECK(menu.Cursor() == 1);
    menu.Update(target, BattleKeys::DDown);
    CHECK(menu.Cursor() == 3);
    CHECK(menu.Status(target).find("Bottom right") != std::string::npos);
    Idle(menu, target, BattleMenu::ListSettleFrames + BattleMenu::StepSettleFrames);
    menu.Update(target, BattleKeys::A);
    BattleMenuOutput step;
    for (u32 i = 0; i < 60 && step.tap == nullptr; i++)
        step = menu.Update(target, 0);
    CHECK(step.tap == nullptr && !menu.Busy());
    CHECK(menu.Note() == "Not tried in the game yet: use the bottom screen");
    for (u8 i = 0; i < TargetCount; i++)
        CHECK(FindLayoutButton(TargetButton(i)) != nullptr);
    CHECK(FindLayoutButton("battle_target.back") != nullptr);

    // The same screen with no move sent lately (an intro object, a prompt):
    // the helper, as before.
    BattleMenu other;
    other.Update(target, 0);
    CHECK(other.Page() == BattlePage::Game);
    Idle(menu, snap, 1);
}

// Every step the menu can send is a known, verified button (one, Run,
// is measured only: docs/LAYOUTS.md).
static void TestButtons(void)
{
    const char *buttons[] = { "party.slot1", "party.slot6", "battle_party.back", "battle_party_page.switch",
                              "battle_party_page.back", "bag.item_row1", "battle_bag_item_page.use",
                              "battle_bag_item_page.next_item", "battle_bag_item_page.prev_item",
                              "battle_bag_item_page.back", "battle_bag.back", "bag_party.slot1", "bag_party.slot6",
                              "battle_ask.first" };
    for (const char *name : buttons)
    {
        const LayoutButton *b = FindLayoutButton(name);
        CHECK(b != nullptr && b->verified == LayoutVerified::Tap);
    }
    CHECK(FindLayoutButton("battle_ask.second") != nullptr);
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

    FakeGame fake;
    BattleSnapshot snaps[6] = { CommandSnap(), MovesSnap(), fake.snap, fake.snap, fake.snap, fake.snap };
    snaps[2].menu = GameMenu::Bag;
    fake.Pocket(BattlePocketBalls);
    snaps[2].bagPocket = BattlePocketBalls;
    snaps[2].bagItemCount = fake.snap.bagItemCount;
    for (u8 i = 0; i < fake.snap.bagItemCount; i++)
        snaps[2].bagItems[i] = fake.snap.bagItems[i];
    snaps[3].menu = GameMenu::Bag;
    snaps[3].bagPick = true;
    snaps[3].bagPickCursor = 2;
    snaps[4].menu = GameMenu::Pokemon;
    snaps[5].menu = GameMenu::Question;
    snaps[5].question = QuestionNextPokemon;
    const char *previews[6] = { "battle-command", "battle-moves", "battle-bag", "battle-pick", "battle-party",
                                "battle-question" };
    for (int n = 0; n < 6; n++)
    {
        const BattleSnapshot &snap = snaps[n];
        std::fill(pixels.begin(), pixels.end(), 0);
        BattleMenu menu;
        menu.Update(snap, 0);
        const UI::Rect area = menu.Area();
        CHECK(!area.Empty());
        CHECK(area.Right() <= UI::ScreenW && area.Bottom() <= UI::ScreenH);
        // The command bar, moves and question leave the HP boxes clear.
        if (n == 0 || n == 1 || n == 5)
            CHECK(area.y >= 36 && area.x >= 166);
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
        if (outside != 0)
            std::printf("%s: %u pixels outside Area()\n", previews[n], outside);
        CHECK(outside == 0);
        if (s_previewDir != nullptr)
            WritePreview(pixels, std::string(s_previewDir) + "/" + previews[n] + ".ppm");
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


static bool SameRect(const UI::Rect &a, const UI::Rect &b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void TestLayouts(void)
{
    for (BattleLayout layout : { BattleLayout::Classic, BattleLayout::Bar, BattleLayout::Rail })
    {
        BattleMenu menu;
        CHECK(menu.Layout() == BattleLayout::Classic);
        menu.SetLayout(layout); // works before entering battle
        CHECK(menu.Layout() == layout);
        BattleSnapshot snap = CommandSnap();
        Idle(menu, snap, BattleMenu::CommandSettleFrames);
        const std::string firstStatus = menu.Status(snap);
        CHECK(firstStatus == "battle-ui: page command, cursor 0 of 4 (Fight)");
        menu.Update(snap, BattleKeys::DDown);
        menu.Update(snap, BattleKeys::CPadDown);
        CHECK(menu.Cursor() == 2); // native-wild-battle: down, down = Pokemon
        menu.Update(snap, BattleKeys::DUp);
        menu.Update(snap, BattleKeys::CPadUp);
        menu.Update(snap, BattleKeys::DUp);
        CHECK(menu.Cursor() == 3); // up wraps to Run
        menu.Update(snap, BattleKeys::DDown);
        menu.Update(snap, BattleKeys::DLeft);
        CHECK(menu.Cursor() == (layout == BattleLayout::Bar ? 3 : 0));
        menu.Update(snap, BattleKeys::CPadRight);
        CHECK(menu.Cursor() == 0);
        if (layout == BattleLayout::Bar)
        {
            for (u8 i = 1; i <= 4; i++)
            {
                menu.Update(snap, BattleKeys::DRight);
                CHECK(menu.Cursor() == i % 4);
            }
        }
        const char *fight = menu.Update(snap, BattleKeys::A).action;
        CHECK(fight != nullptr && std::strcmp(fight, "battle-fight") == 0);

        for (u8 count = 1; count <= 4; count++)
        {
            BattleMenu moves;
            moves.SetLayout(layout);
            snap = MovesSnap();
            snap.moveCount = count;
            Idle(moves, snap, BattleMenu::MovesSettleFrames);
            for (u8 i = 1; i <= count; i++)
            {
                moves.Update(snap, BattleKeys::DDown);
                CHECK(moves.Cursor() == i % count);
            }
            moves.Update(snap, BattleKeys::DUp);
            CHECK(moves.Cursor() == count - 1);
            moves.Update(snap, BattleKeys::DDown);
            moves.Update(snap, BattleKeys::DRight);
            const u8 right = layout == BattleLayout::Bar && count > 1 ? 1 : 0;
            CHECK(moves.Cursor() == right);
            moves.Update(snap, BattleKeys::DLeft);
            CHECK(moves.Cursor() == 0);
            if (layout == BattleLayout::Bar && count >= 3)
            {
                moves.Update(snap, BattleKeys::DDown);
                moves.Update(snap, BattleKeys::DDown);
                moves.Update(snap, BattleKeys::CPadRight);
                CHECK(moves.Cursor() == (count == 4 ? 3 : 2));
                moves.Update(snap, BattleKeys::CPadLeft);
                CHECK(moves.Cursor() == 2);
            }
            const BattleMenuOutput back = moves.Update(snap, BattleKeys::B);
            CHECK(back.action != nullptr && std::strcmp(back.action, "battle-moves-back") == 0);
        }
        // Changing a visible menu preserves its selection and scenario log,
        // changes the draw key and area, and survives leaving/reentering battle.
        BattleMenu changing;
        changing.Update(CommandSnap(), 0);
        changing.Update(CommandSnap(), BattleKeys::DDown);
        const std::string key = changing.DrawKey(CommandSnap());
        const std::string status = changing.Status(CommandSnap());
        const UI::Rect area = changing.Area();
        changing.SetLayout(layout);
        CHECK((changing.DrawKey(CommandSnap()) == key) == (layout == BattleLayout::Classic));
        CHECK(SameRect(changing.Area(), area) == (layout == BattleLayout::Classic));
        CHECK(changing.Status(CommandSnap()) == status && changing.Cursor() == 1);
        changing.Update(BattleSnapshot{}, 0);
        changing.Update(CommandSnap(), 0);
        CHECK(changing.Layout() == layout);
    }
}

// Full frames show the protected HUD regions in context. Check every pixel
// outside Area() against its original background, without clipping Draw().
static void TestLayoutDrawing(void)
{
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * 3, 0);
    UI::Surface surface = {};
    surface.origin = pixels.data();
    surface.format = UI::PixelFormat::BGR8;
    surface.stepX = 3;
    surface.stepY = UI::ScreenW * 3;
    surface.width = UI::ScreenW;
    surface.height = UI::ScreenH;
    UI::Canvas canvas(surface);
    for (BattleLayout layout : { BattleLayout::Classic, BattleLayout::Bar, BattleLayout::Rail })
    {
        for (bool moves : { false, true })
        {
            BattleSnapshot snap = moves ? MovesSnap() : CommandSnap();
            // Only the new composition previews use these effects; keep the
            // established Classic preview fixtures byte-for-byte unchanged.
            if (layout != BattleLayout::Classic)
            {
                snap.moves[1].effect = 2;
                snap.moves[2].effect = 3;
                snap.moves[3].effect = 1;
            }
            BattleMenu menu;
            menu.SetLayout(layout);
            Idle(menu, snap, BattleMenu::CommandSettleFrames);
            const UI::Rect area = menu.Area();
            CHECK(area.x >= 0 && area.y >= 0 && area.Right() <= 400 && area.Bottom() <= 240);
            CHECK(UI::Intersect(area, UI::Rect{ 240, 0, 160, 36 }).Empty());
            CHECK(UI::Intersect(area, UI::Rect{ 0, 180, 166, 60 }).Empty());
            for (u8 cursor = 0; cursor < 6; cursor++)
            {
                if (cursor == 4 && moves)
                {
                    snap.moves[0].pp = 0;
                    snap.moves[0].effect = 0; // exercise the no-effect X
                    CHECK(menu.Update(snap, BattleKeys::A).action == nullptr);
                    CHECK(!menu.Note().empty());
                }
                if (cursor == 5)
                {
                    if (moves)
                        for (MoveRow &row : snap.moves) row.pp = 0;
                    CHECK(menu.Update(snap, BattleKeys::A).action != nullptr);
                    CHECK(menu.Pending());
                }
                canvas.FillRect(UI::Rect{ 0, 0, 400, 240 }, UI::Rgb(105, 124, 125));
                canvas.FillRoundRect(UI::Rect{ 240, 2, 158, 32 }, 5, UI::Rgb(230, 236, 223));
                canvas.FillRect(UI::Rect{ 258, 23, 116, 5 }, UI::Rgb(60, 180, 70));
                canvas.FillRoundRect(UI::Rect{ 4, 182, 158, 34 }, 5, UI::Rgb(230, 236, 223));
                canvas.FillRect(UI::Rect{ 30, 204, 112, 5 }, UI::Rgb(60, 180, 70));
                canvas.FillRect(UI::Rect{ 4, 220, 158, 16 }, UI::Rgb(55, 60, 54));
                const std::vector<u8> before = pixels;
                menu.Draw(canvas, snap);
                u32 outside = 0;
                s32 firstX = -1, firstY = -1;
                for (s32 y = 0; y < 240; y++)
                    for (s32 x = 0; x < 400; x++)
                        if (x < area.x || x >= area.Right() || y < area.y || y >= area.Bottom())
                        {
                            const size_t p = (y * 400 + x) * 3;
                            if (std::memcmp(&pixels[p], &before[p], 3) != 0)
                            {
                                outside++;
                                if (firstX < 0) { firstX = x; firstY = y; }
                            }
                        }
                if (outside)
                    std::printf("layout %u moves %u cursor %u: %u pixels outside, first %d,%d\n",
                                (unsigned)layout, moves ? 1u : 0u, cursor, outside, firstX, firstY);
                CHECK(outside == 0);
                if (cursor == 0 && layout != BattleLayout::Classic && s_previewDir != nullptr)
                {
                    const std::string name = layout == BattleLayout::Bar ? "battle-bar-" : "battle-rail-";
                    WritePreview(pixels, std::string(s_previewDir) + "/" + name + (moves ? "moves" : "command") + ".ppm");
                }
                menu.Update(snap, BattleKeys::DDown);
            }
        }
        // All other pages keep exactly the same area and pixels.
        FakeGame fake;
        BattleSnapshot other[7] = { fake.snap, fake.snap, fake.snap, {}, CommandSnap(), fake.snap, CommandSnap() };
        other[0].menu = GameMenu::Pokemon;
        other[1].menu = GameMenu::Bag;
        other[2].menu = GameMenu::Question;
        other[2].question = QuestionNextPokemon;
        other[3].afterCatch = true;
        other[4].menu = GameMenu::Other;
        other[5].menu = GameMenu::Bag;
        other[5].bagPick = true;
        other[5].bagPickCursor = 1;
        other[6].menu = GameMenu::Other;
        for (const BattleSnapshot &snap : other)
        {
            BattleMenu classic, styled;
            styled.SetLayout(layout);
            if (&snap == &other[6])
            {
                const BattleSnapshot moves = MovesSnap();
                Idle(classic, moves, BattleMenu::MovesSettleFrames);
                Idle(styled, moves, BattleMenu::MovesSettleFrames);
                classic.Update(moves, BattleKeys::A);
                styled.Update(moves, BattleKeys::A);
            }
            classic.Update(snap, 0);
            styled.Update(snap, 0);
            if (&snap == &other[6])
                CHECK(classic.Page() == BattlePage::Target && styled.Page() == BattlePage::Target);
            CHECK(SameRect(classic.Area(), styled.Area()));
            canvas.FillRect(UI::Rect{ 0, 0, 400, 240 }, UI::Rgb(0, 0, 0));
            classic.Draw(canvas, snap);
            const std::vector<u8> expected = pixels;
            canvas.FillRect(UI::Rect{ 0, 0, 400, 240 }, UI::Rgb(0, 0, 0));
            styled.Draw(canvas, snap);
            CHECK(pixels == expected);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
        s_previewDir = argv[1];
    TestPages();
    TestCommand();
    TestMoves();
    TestMoveRows();
    TestLabels();
    TestItems();
    TestParty();
    TestBag();
    TestQuestion();
    TestTarget();
    TestButtons();
    TestDrawing();
    TestLayouts();
    TestLayoutDrawing();
    if (s_failures != 0)
    {
        std::printf("battle_menu_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("battle_menu_test: ok\n");
    return 0;
}
