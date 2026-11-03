#include "Screens/BattleScreen.hpp"

#include <3ds.h>

#include "ActionQueue.hpp"
#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Battle.hpp"
#include "Buttons.hpp"
#include "Party.hpp"
#include "Settings.hpp"
#include "Screens/BattleMenu.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert((u32)BattleMenu::Other == (u32)Screens::GameMenu::Other, "GameMenu mirrors BattleMenu");
    static_assert(Screens::BattleKeys::A == (u32)Key::A && Screens::BattleKeys::DDown == (u32)Key::DPadDown
                  && Screens::BattleKeys::CPadDown == (u32)Key::CPadDown, "battle menu key bits");

    static bool s_enabled = true;
    static Screens::BattleMenu s_menu;
    static Screens::BattleSnapshot s_snap;
    static u32 s_frames = 0;
    static u32 s_captured = 0;      // keys this screen asked InputCapture to hold
    static std::string s_status;    // last logged Status()

    // Names are looked up once per ID (decrypting a message line each time
    // would cost every frame).
    struct CachedName
    {
        u16 id;
        std::string name;
    };
    static std::vector<CachedName> s_moveNames, s_speciesNames;

    // What the OSD callback draws, handed over under s_drawLock: the plugin
    // thread copies the menu and snapshot when what they show changes; the
    // callback re-renders the layer then and copies it every frame.
    static Mutex s_drawLock;
    static Screens::BattleMenu s_drawMenu;
    static Screens::BattleSnapshot s_drawSnap;
    static std::string s_drawKey;
    static volatile bool s_visible = false;
    static bool s_dirty = false;
    static UI::Layer s_layer;
    static UI::Rect s_layerArea = {};
    static u8 s_pixels[230 * 200 * 4];  // the largest page (the move list) in RGBA8

    static const std::string &Cached(std::vector<CachedName> &cache, u16 id, std::string (*lookup)(u16, const BattleState &),
                                     const BattleState &state)
    {
        for (const CachedName &entry : cache)
            if (entry.id == id)
                return entry.name;
        static const std::string none;
        const std::string name = lookup(id, state);
        if (name.empty())
            return none;    // not there yet (or never); try again next time
        if (cache.size() >= 64)
            cache.erase(cache.begin());
        cache.push_back({ id, name });
        return cache.back().name;
    }

    static std::string LookupMove(u16 move, const BattleState &state)
    {
        return MoveName(state, move);
    }

    static std::string LookupSpecies(u16 species, const BattleState &)
    {
        return SpeciesName(species);
    }

    // The Poké Balls tab's highlighted ball: the Items pocket's balls in
    // order (BallsInPocket). The pocket is read when the cursor or the
    // number of balls changes, and about once a second (counts change).
    static void BagBall(Screens::BattleSnapshot &snap)
    {
        static u8 lastCursor = 0xFF, lastCount = 0;
        static u32 calls = 0;
        static std::string name;
        static u16 count = 0;
        if (snap.gameCursor != lastCursor || snap.listCount != lastCount || ++calls % 30 == 0)
        {
            lastCursor = snap.gameCursor;
            lastCount = snap.listCount;
            name.clear();
            count = 0;
            const BagState bag = ReadBag();
            const u32 slots = bag.valid ? bag.capacity[(int)Pocket::Items] : 0;
            std::vector<u16> items(slots), counts(slots);
            for (u32 i = 0; i < slots; i++)
            {
                const BagSlot slot = ReadBagSlot(bag, Pocket::Items, i);
                items[i] = slot.item;
                counts[i] = slot.count;
            }
            u16 balls[32], ballCounts[32];
            const u8 n = Screens::BallsInPocket(items.data(), counts.data(), slots, balls, ballCounts, 32);
            // Only trust it if it lists what the game's tab lists.
            if (n == snap.listCount && snap.gameCursor < n && n <= 32)
            {
                name = ItemName(balls[snap.gameCursor]);
                count = ballCounts[snap.gameCursor];
            }
        }
        snap.bagItem = name;
        snap.bagItemCount = count;
    }

    static void ReadSnapshot(u32 sceneManager)
    {
        Screens::BattleSnapshot snap;
        const BattleState state = ReadBattle(sceneManager);
        snap.inBattle = state.valid;
        if (!state.valid)
        {
            s_snap = snap;
            return;
        }
        snap.menu = (Screens::GameMenu)state.menu;
        snap.otherVtable = state.menu == BattleMenu::Other ? state.menuVtable : 0;
        snap.gameCursor = state.cursor;
        snap.listCount = state.listCount;
        snap.activeName = Cached(s_speciesNames, state.player.species, LookupSpecies, state);

        const BattleMon &mon = state.player;
        snap.moveCount = Screens::BuildMoveRows(snap.moves, mon.moves, mon.pp, mon.maxPp, state.moveType,
                                                state.moveEffect, state.menu == BattleMenu::Moves ? state.listCount : 0);
        for (u8 i = 0; i < snap.moveCount; i++)
            snap.moves[i].name = Cached(s_moveNames, snap.moves[i].move, LookupMove, state);

        if (state.menu == BattleMenu::Bag)
        {
            snap.bagPocket = state.bagPocket;
            snap.bagItemPage = state.bagItemPage;
            if (state.bagPocket == Screens::BattlePocketBalls)
                BagBall(snap);
        }
        else if (state.menu == BattleMenu::Pokemon && state.cursor < 6 && state.cursor < state.partyCount[0])
        {
            const BattleMon &slot = state.party[state.cursor];
            snap.partyName = Cached(s_speciesNames, slot.species, LookupSpecies, state);
            snap.partyLevel = slot.level;
            snap.partyHp = slot.hp;
            snap.partyMaxHp = slot.maxHp;
        }
        s_snap = snap;
    }

    static void SetCapture(u32 keys)
    {
        if (keys == s_captured)
            return;
        if (keys != 0)
            InputCaptureBegin(keys, true, false);
        else
            InputCaptureEnd();
        s_captured = keys;
    }

    void BattleScreenUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        s_frames++;
        // A catch ends in the Pokédex scene (Zukan) with the nickname
        // question; the helper stays up there until the next scene.
        static std::string lastScene;
        static bool afterCatch = false;
        if (!scene.empty() && scene != lastScene)
        {
            afterCatch = scene == SceneDex && lastScene == SceneBattle;
            lastScene = scene;
        }
        const bool inBattle = s_enabled && scene == SceneBattle;
        if (inBattle && s_frames % 2 == 0)
            ReadSnapshot(sceneManager);
        else if (!inBattle)
        {
            s_snap = Screens::BattleSnapshot();
            s_snap.afterCatch = s_enabled && afterCatch && scene == SceneDex;
        }
        s_snap.actionRunning = ActionRunning();
        s_snap.panelKey = KeysToString(GetSettings().panelKey);

        const Screens::BattlePage before = s_menu.Page();
        const u32 beforeFrames = s_menu.Stable();
        const Screens::BattleMenuOutput out = s_menu.Update(s_snap, inBattle ? CaptureRealPressed() : 0);
        if (before == Screens::BattlePage::Command && s_menu.Page() != before && beforeFrames < 120)
            log.push_back(Utils::Format("battle-ui: the game's command menu went after %lu frames", beforeFrames));

        // Logged as the player sees it: the command bar isn't shown during
        // the game's brief command-menu flashes (BattleMenu::ShowFrames).
        const std::string status = s_menu.Visible() ? s_menu.Status(s_snap) : "battle-ui: page hidden";
        if (status != s_status)
        {
            s_status = status;
            log.push_back(status);
        }
        if (out.action != nullptr)
        {
            log.push_back(std::string("battle-ui: chose ") + out.action);
            if (!ActionCommand(out.action, sceneManager, log))
                log.push_back(std::string("battle-ui: no action ") + out.action);
        }

        // An action captures everything while it runs and then restores the
        // request it found (ours), so only change ours between actions.
        if (!ActionRunning())
            SetCapture(out.capture ? Screens::BattleMenuKeys : 0);

        const bool visible = s_menu.Visible();
        const std::string key = visible ? s_menu.DrawKey(s_snap) : std::string();
        if (key != s_drawKey || visible != s_visible)
        {
            s_drawLock.Lock();
            s_drawMenu = s_menu;
            s_drawSnap = s_snap;
            s_drawKey = key;
            s_dirty = true;
            s_visible = visible;
            s_drawLock.Unlock();
        }
    }

    bool BattleScreenOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_visible)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);

        // Never wait for the plugin thread here (this is the game's frame):
        // if it is handing over a new state, show the old one this frame.
        if (!s_drawLock.TryLock())
        {
            if (s_visible)
            {
                const UI::Rect area = s_drawMenu.Area();
                if (!s_layer.Matches(fb) || area.x != s_layerArea.x || area.y != s_layerArea.y
                    || area.w != s_layerArea.w || area.h != s_layerArea.h)
                {
                    s_layer.Setup(fb, area, 10, s_pixels, sizeof(s_pixels));
                    s_layerArea = area;
                    s_dirty = true;
                }
                if (s_dirty)
                {
                    UI::Canvas canvas = s_layer.Begin();
                    s_drawMenu.Draw(canvas, s_drawSnap);
                    s_dirty = false;
                }
            }
            s_drawLock.Unlock();
        }
        if (s_layerArea.w > 0 && s_visible)
            s_layer.Present(fb);
        return true;
    }

    void BattleScreenSetEnabled(bool enabled)
    {
        s_enabled = enabled;
        AutoPanelSetNativeBattle(enabled);
    }

    bool BattleScreenEnabled(void)
    {
        return s_enabled;
    }

    std::string BattleScreenStatus(void)
    {
        return std::string("battle-ui: ") + (s_enabled ? "on" : "off") + Utils::Format(", capture %08lX; ", s_captured)
               + s_menu.Status(s_snap).substr(11);
    }
}
