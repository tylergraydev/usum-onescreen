// Host tests for the action sequencing in Sources/ActionQueue.cpp, against a
// small fake of the game's bottom screens. Build and run: make -C tests.

#include "ActionQueue.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static bool Inside(const LayoutButton &b, u16 x, u16 y)
{
    return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1;
}

// A fake game: reacts to a new touch (the frame touching starts) or a new
// key press the way the real screens do, after a delay, passing through the
// (none) scene on scene changes. `ignoreTaps` drops that many taps first
// (a battle intro, Rotom's chat).
struct FakeGame
{
    ActionWorld world;
    bool wasTouching = false;
    u32 prevKeys = 0;
    u32 frame = 0;
    u32 ignoreTaps = 0;
    u32 exitDelay = 10;                 // Pokédex back -> (none)
    bool rotomChat = false;             // Rotom's face ignores taps
    u32 taps = 0, xPresses = 0, bPresses = 0;
    std::vector<std::string> tapped;    // layout names of the buttons hit

    // A pending change: at frame `at`, become `scene` / `flags`.
    struct Change { u32 at; std::string scene; u32 flags; };
    std::vector<Change> pending;

    void Go(const std::string &scene, u32 flags, u32 delay)
    {
        pending.push_back({ frame + delay, "", 0 });
        pending.push_back({ frame + delay + 20, scene, flags });
    }

    void Overlay(u32 flags, u32 delay)
    {
        pending.push_back({ frame + delay, world.scene, flags });
    }

    const char *Hit(u16 x, u16 y)
    {
        for (const LayoutButton &b : LayoutButtons)
        {
            if (!Inside(b, x, y))
                continue;
            const std::string name = b.name;
            const std::string screen = name.substr(0, name.find('.'));
            if ((world.scene == SceneField && !(world.flags & WorldXMenu) && screen == "field")
                || (world.scene == SceneField && (world.flags & WorldXMenu) && screen == "xmenu")
                || (world.scene == SceneDex && screen == "pokedex_top")
                || (world.scene == SceneParty && screen == "party")
                || (world.scene == SceneBag && screen == "bag")
                || (world.scene == SceneTownMap && screen == "town_map")
                || (world.scene == SceneBattle && screen == "battle_command"))
                return b.name;
        }
        return nullptr;
    }

    void OnTap(const std::string &name)
    {
        if (name == "field.rotom_face")
        {
            if (!rotomChat)
                Go(SceneDex, 0, 10);
        }
        else if (name == "xmenu.pokedex")          Go(SceneDex, 0, 10);
        else if (name == "xmenu.pokemon")          Go(SceneParty, 0, 10);
        else if (name == "xmenu.bag")              Go(SceneBag, 0, 10);
        else if (name == "xmenu.back")             Overlay(0, 8);
        else if (name == "party.back" || name == "bag.back")
        {
            // The X menu shows ~20 frames after FieldRo is back.
            Go(SceneField, 0, 10);
            pending.push_back({ frame + 10 + 20 + 25, SceneField, WorldXMenu });
        }
        else if (name == "pokedex_top.back")       Go(SceneField, 0, exitDelay);
        else if (name == "battle_command.run")
        {
            Overlay(0, 5);
            Go(SceneField, 0, 300);
        }
    }

    void Feed(const ActionOutput &out)
    {
        if (out.touching && !wasTouching && !world.scene.empty())
        {
            taps++;
            if (ignoreTaps > 0)
                ignoreTaps--;
            else if (const char *name = Hit(out.x, out.y))
            {
                tapped.push_back(name);
                OnTap(name);
            }
        }
        wasTouching = out.touching;

        const u32 pressed = out.keys & ~prevKeys;
        prevKeys = out.keys;
        if ((pressed & ActionKeyX) && world.scene == SceneField)
        {
            xPresses++;
            Overlay(world.flags ^ WorldXMenu, 12);
        }
        if ((pressed & ActionKeyB) && world.scene == SceneField && (world.flags & WorldXMenu))
        {
            bPresses++;
            Overlay(0, 8);
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

// Runs `action` against `game` until it finishes (or `limit` frames).
static bool Run(FakeGame &game, const char *action, std::vector<std::string> &log, u32 limit = 5000)
{
    ActionRunner runner;
    const ActionDef *def = FindAction(action);
    CHECK(def != nullptr);
    if (def == nullptr || !runner.Start(*def, game.world, log))
        return false;
    for (u32 i = 0; i < limit && runner.Running(); i++)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    return !log.empty() && log.back().find(" ok in ") != std::string::npos;
}

static bool LogHas(const std::vector<std::string> &log, const char *text)
{
    for (const std::string &line : log)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}

static void PrintLog(const std::vector<std::string> &log)
{
    for (const std::string &line : log)
        std::printf("    %s\n", line.c_str());
}

static void TestTables(void)
{
    const char *names[] = { "open-pokedex", "open-party", "open-bag", "battle-run", "close" };
    for (const char *name : names)
    {
        const ActionDef *def = FindAction(name);
        CHECK(def != nullptr);
        if (def != nullptr)
            CHECK(CheckAction(*def).empty());
    }
    CHECK(FindAction("open-nothing") == nullptr);

    const LayoutButton *pokemon = FindLayoutButton("xmenu.pokemon");
    CHECK(pokemon != nullptr && pokemon->tapX == 88 && pokemon->tapY == 48);
    CHECK(FindLayoutButton("xmenu.nothing") == nullptr);

    // Never tap a button that wasn't verified by a tap (Save, item Use).
    ActionDef save = { "save", "", AnyScene(), { StepTap("xmenu.save") } };
    CHECK(CheckAction(save).find("never verified") != std::string::npos);
    ActionDef unknown = { "x", "", AnyScene(), { StepTap("xmenu.nothing") } };
    CHECK(CheckAction(unknown).find("unknown button") != std::string::npos);
    std::vector<std::string> log;
    ActionRunner runner;
    CHECK(!runner.Start(save, ActionWorld(), log));
    CHECK(!runner.Running());

    // Every back button BackOut uses exists and was tapped.
    const char *scenes[] = { SceneDex, SceneParty, SceneBag, SceneTownMap };
    for (const char *scene : scenes)
    {
        const LayoutButton *b = FindLayoutButton(BackButtonFor(scene, 0));
        CHECK(b != nullptr && b->verified == LayoutVerified::Tap);
    }
    CHECK(BackButtonFor(SceneField, WorldXMenu) == nullptr);
    CHECK(BackKeysFor(SceneField, WorldXMenu) == ActionKeyB);
    CHECK(BackKeysFor(SceneDex, 0) == 0);
    CHECK(BackButtonFor(SceneField, 0) == nullptr && BackKeysFor(SceneField, 0) == 0);
    CHECK(BackButtonFor(SceneBattle, 0) == nullptr);
}

static void TestConditions(void)
{
    ActionWorld w;
    w.scene = SceneField;
    w.flags = WorldXMenu;
    CHECK(AnyScene().Matches(w));
    CHECK(InScene(SceneField).Matches(w));
    CHECK(!InScene(SceneDex).Matches(w));
    CHECK(NotInScene(SceneDex).Matches(w));
    CHECK(!NotInScene(SceneField).Matches(w));
    CHECK(InScene(SceneField).With(WorldXMenu).Matches(w));
    CHECK(!InScene(SceneField).Without(WorldXMenu).Matches(w));
    w.scene = "";
    CHECK(!InScene(SceneField).Matches(w));
    CHECK(NotInScene(SceneBattle).Matches(w));
}

static void TestOpenParty(void)
{
    // From the field with the X menu closed: X once, then the Pokémon pill.
    FakeGame game;
    game.world.scene = SceneField;
    std::vector<std::string> log;
    CHECK(Run(game, "open-party", log));
    CHECK(game.world.scene == SceneParty);
    CHECK(game.xPresses == 1);
    CHECK(game.tapped.size() == 1 && game.tapped[0] == "xmenu.pokemon");

    // With the X menu already up (its cursor anywhere): no X, just the tap.
    FakeGame open;
    open.world.scene = SceneField;
    open.world.flags = WorldXMenu;
    log.clear();
    CHECK(Run(open, "open-bag", log));
    CHECK(open.world.scene == SceneBag);
    CHECK(open.xPresses == 0);
    CHECK(open.tapped.size() == 1 && open.tapped[0] == "xmenu.bag");
}

static void TestOpenPokedex(void)
{
    FakeGame game;
    game.world.scene = SceneField;
    std::vector<std::string> log;
    CHECK(Run(game, "open-pokedex", log));
    CHECK(game.world.scene == SceneDex);

    // One ignored tap is retried; two fail with a timeout.
    FakeGame missed;
    missed.world.scene = SceneField;
    missed.ignoreTaps = 1;
    log.clear();
    CHECK(Run(missed, "open-pokedex", log));
    CHECK(missed.taps == 2);
    CHECK(LogHas(log, "retry 1"));

    // Rotom chatting: the face is ignored, so the X menu's Pokédex is used.
    FakeGame chat;
    chat.world.scene = SceneField;
    chat.rotomChat = true;
    log.clear();
    CHECK(Run(chat, "open-pokedex", log));
    CHECK(chat.world.scene == SceneDex);
    CHECK(chat.xPresses == 1);
    CHECK(chat.tapped.size() == 3 && chat.tapped[2] == "xmenu.pokedex");
    CHECK(LogHas(log, "going on"));

    // Nothing answers at all: fails at the last wait.
    FakeGame dead;
    dead.world.scene = SceneField;
    dead.ignoreTaps = 100;
    log.clear();
    CHECK(!Run(dead, "open-pokedex", log));
    CHECK(dead.taps == 4);
    CHECK(LogHas(log, "FAILED at step 7/7"));
    CHECK(LogHas(log, "timeout"));

    // Not from the X menu or another scene.
    FakeGame dex;
    dex.world.scene = SceneDex;
    log.clear();
    CHECK(!Run(dex, "open-pokedex", log));
    CHECK(LogHas(log, "refused: needs FieldRo -x-menu, now Zukan"));
    CHECK(dex.taps == 0);
}

static void TestBattleRun(void)
{
    // The command menu shows after the intro; the first tap is lost.
    FakeGame game;
    game.world.scene = SceneBattle;
    game.Overlay(WorldBattleCommand, 400);
    game.ignoreTaps = 1;
    std::vector<std::string> log;
    CHECK(Run(game, "battle-run", log));
    CHECK(game.world.scene == SceneField);
    CHECK(game.taps == 2);
    CHECK(game.tapped.size() == 1 && game.tapped[0] == "battle_command.run");
    CHECK(LogHas(log, "retry 1 from step 1"));

    FakeGame field;
    field.world.scene = SceneField;
    log.clear();
    CHECK(!Run(field, "battle-run", log));
    CHECK(LogHas(log, "refused"));
}

static void TestClose(void)
{
    // Party -> X menu -> field: two back taps.
    FakeGame game;
    game.world.scene = SceneParty;
    std::vector<std::string> log;
    CHECK(Run(game, "close", log));
    CHECK(game.world.scene == SceneField && game.world.flags == 0);
    CHECK(game.tapped.size() == 1 && game.tapped[0] == "party.back");
    CHECK(game.bPresses == 1);

    // The Pokédex keeps its scene for ~75 frames after back: one tap only.
    FakeGame dex;
    dex.world.scene = SceneDex;
    dex.exitDelay = 75;
    log.clear();
    CHECK(Run(dex, "close", log));
    CHECK(dex.world.scene == SceneField);
    CHECK(dex.taps == 1);

    // Already in the field: nothing to tap.
    FakeGame field;
    field.world.scene = SceneField;
    log.clear();
    CHECK(Run(field, "close", log));
    CHECK(field.taps == 0);

    // A screen without a known back button fails instead of guessing.
    FakeGame other;
    other.world.scene = "TrainerPass";
    log.clear();
    CHECK(!Run(other, "close", log));
    CHECK(LogHas(log, "no back button known"));
    CHECK(other.taps == 0);

    // A back button that does nothing: gives up after BackOutMaxTaps.
    FakeGame stuck;
    stuck.world.scene = SceneTownMap;   // the fake ignores town_map.back
    log.clear();
    CHECK(!Run(stuck, "close", log));
    CHECK(stuck.taps == ActionRunner::BackOutMaxTaps);
    CHECK(LogHas(log, "still not in the field"));

    FakeGame battle;
    battle.world.scene = SceneBattle;
    log.clear();
    CHECK(!Run(battle, "close", log));
    CHECK(LogHas(log, "refused"));
}

static void TestRunnerState(void)
{
    // Busy while running; Cancel stops at once and reports.
    ActionRunner runner;
    ActionWorld world;
    world.scene = SceneField;
    std::vector<std::string> log;
    CHECK(runner.Start(*FindAction("open-party"), world, log));
    CHECK(!runner.Start(*FindAction("open-bag"), world, log));
    CHECK(LogHas(log, "refused: open-party is running"));
    const ActionOutput first = runner.Update(world, log);
    CHECK(first.keys == ActionKeyX && !first.touching);
    CHECK(runner.Status().find("running open-party, step 1/4") != std::string::npos);
    runner.Cancel(log);
    CHECK(!runner.Running());
    CHECK(LogHas(log, "cancelled"));
    const ActionOutput after = runner.Update(world, log);
    CHECK(after.keys == 0 && !after.touching);
    CHECK(runner.Status().find("idle, last: open-party FAILED") != std::string::npos);

    // A press is held for ActionHoldFrames, then released for the gap.
    ActionDef press = { "press", "", AnyScene(), { StepPress(ActionKeyB), StepPress(ActionKeyB) } };
    CHECK(runner.Start(press, world, log));
    u32 held = 0, edges = 0, prev = 0;
    while (runner.Running())
    {
        const ActionOutput out = runner.Update(world, log);
        held += out.keys ? 1 : 0;
        edges += (out.keys & ~prev) ? 1 : 0;
        prev = out.keys;
    }
    CHECK(held == 2 * ActionHoldFrames);
    CHECK(edges == 2);
}

int main(void)
{
    TestTables();
    TestConditions();
    TestOpenParty();
    TestOpenPokedex();
    TestBattleRun();
    TestClose();
    TestRunnerState();
    if (s_failures != 0)
    {
        std::printf("action_queue_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("action_queue_test: ok\n");
    (void)PrintLog;
    return 0;
}
