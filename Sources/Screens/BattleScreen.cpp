#include "Screens/BattleScreen.hpp"


#include <3ds.h>

#include "ActionQueue.hpp"
#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Battle.hpp"
#include "Buttons.hpp"
#include "Party.hpp"
#include "PartyData.hpp"
#include "Settings.hpp"
#include "Screens/BattleMenu.hpp"
#include "Screens/MoveLearn.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert((u32)BattleMenu::Other == (u32)Screens::GameMenu::Other, "GameMenu mirrors BattleMenu");
    static_assert(Screens::BattleKeys::A == (u32)Key::A && Screens::BattleKeys::DDown == (u32)Key::DPadDown
                  && Screens::BattleKeys::CPadDown == (u32)Key::CPadDown, "battle menu key bits");

    static bool s_enabled = true;
    static bool s_layoutLog = false;
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
    static std::vector<CachedName> s_moveNames, s_speciesNames, s_itemNames;

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
    // A single step of the Pokémon list, Bag or question (one tap or press,
    // then a short wait); kept here while the action layer runs it.
    static ActionDef s_step = { nullptr, nullptr, {}, {} };
    static const char *const Owner = "battle";  // UI::ScreenBuffer

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

    static std::string LookupItem(u16 item, const BattleState &)
    {
        return ItemName(item);
    }

    // Nicknames, held items and status come from the party (RE3), by the
    // battler record's party index; read when the party or Bag page opens
    // and about once a second while it shows (decoding six Pokémon).
    struct PartyExtra
    {
        bool valid = false;
        std::string nickname, item;
        u32 status = 0;
        bool egg = false;
    };
    static PartyExtra s_extra[6];

    static void ReadPartyExtra(u32 sceneManager)
    {
        const PartyState party = ReadParty(sceneManager);
        for (u32 i = 0; i < 6; i++)
        {
            PartyExtra &extra = s_extra[i];
            extra = PartyExtra();
            if (!party.valid || i >= party.count || party.slots[i].mon.form == Pk7Form::Invalid)
                continue;
            const Pk7 &mon = party.slots[i].mon;
            char nick[64];
            Utf16ToUtf8(mon.nickname, 13, nick, sizeof(nick));
            extra.valid = true;
            extra.nickname = nick;
            extra.item = mon.heldItem ? ItemName(mon.heldItem) : std::string();
            extra.status = mon.status;
            extra.egg = mon.isEgg;
        }
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
        snap.otherVtable = state.menu == BattleMenu::Other || state.menu == BattleMenu::Question ? state.menuVtable : 0;
        snap.question = state.question;
        snap.gameCursor = state.cursor;
        snap.listCount = state.listCount;
        snap.activeName = Cached(s_speciesNames, state.player.species, LookupSpecies, state);

        const BattleMon &mon = state.player;
        snap.moveCount = Screens::BuildMoveRows(snap.moves, mon.moves, mon.pp, mon.maxPp, state.moveType,
                                                state.moveEffect, state.menu == BattleMenu::Moves ? state.listCount : 0);
        for (u8 i = 0; i < snap.moveCount; i++)
        {
            Screens::MoveRow &row = snap.moves[i];
            row.name = Cached(s_moveNames, row.move, LookupMove, state);
            // Details always come from the loaded move table; list type and
            // effectiveness still take precedence over the table's type.
            const UI::MoveInfo info = UI::AssetMove(row.move);
            if (info.valid)
            {
                if (row.type == 0xFF)
                    row.type = info.type;
                row.power = info.power;
                row.accuracy = info.accuracy;
                row.category = info.category;
            }
            if (row.type != 0xFF)
                row.typeName = UI::AssetTypeName(row.type);
        }

        // The battle party, battle order, with the party's nicknames.
        const bool lists = state.menu == BattleMenu::Pokemon || state.menu == BattleMenu::PokemonPage
                           || state.menu == BattleMenu::Bag;
        static bool listsBefore = false;
        static u32 extraFrames = 0;
        if (lists && (!listsBefore || ++extraFrames % 30 == 0))
            ReadPartyExtra(sceneManager);
        listsBefore = lists;
        snap.partyCount = state.partyCount[0] < 6 ? state.partyCount[0] : 6;
        for (u8 i = 0; i < snap.partyCount; i++)
        {
            const BattleMon &mon = state.party[i];
            Screens::BattlePartyMon &out = snap.party[i];
            out.species = Cached(s_speciesNames, mon.species, LookupSpecies, state);
            out.name = out.species;
            out.level = mon.level;
            out.hp = mon.hp;
            out.maxHp = mon.maxHp;
            if (mon.partyIndex < 6 && s_extra[mon.partyIndex].valid)
            {
                const PartyExtra &extra = s_extra[mon.partyIndex];
                if (!extra.nickname.empty())
                    out.name = extra.nickname;
                out.item = extra.item;
                out.status = extra.status;
                out.egg = extra.egg;
            }
        }

        if (state.menu == BattleMenu::Bag)
        {
            snap.bagPocket = state.bagPocket;
            snap.bagItemPage = state.bagItemPage;
            snap.bagPick = state.bagPick;
            snap.bagPickCursor = state.bagPickCursor;
            snap.bagPageIndex = state.bagPageIndex;
            snap.bagItemCount = state.bagItemCount;
            for (u8 i = 0; i < state.bagItemCount; i++)
            {
                snap.bagItems[i].id = state.bagItems[i].item;
                snap.bagItems[i].count = state.bagItems[i].count;
                snap.bagItems[i].name = Cached(s_itemNames, state.bagItems[i].item, LookupItem, state);
            }
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
        if (s_layoutLog)
        {
            const BattleLayout layout = s_menu.Layout();
            log.push_back(std::string("battle-ui: layout ") + (layout == BattleLayout::Bar ? "bar"
                          : layout == BattleLayout::Rail ? "rail" : "classic"));
            s_layoutLog = false;
        }
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

        // A Pokémon wants a fifth move: the move-learning screen takes it,
        // and the "the game asks" helper stays away.
        const u32 pressed = inBattle ? CaptureRealPressed() : 0;
        const MoveLearnFrame learn = MoveLearnUpdate(sceneManager, inBattle, pressed, log);
        if (learn.asked && (s_snap.menu == Screens::GameMenu::Other || s_snap.menu == Screens::GameMenu::Question))
        {
            s_snap.menu = Screens::GameMenu::None;
            s_snap.otherVtable = 0;
        }

        const Screens::BattlePage before = s_menu.Page();
        const u32 beforeFrames = s_menu.Stable();
        const Screens::BattleMenuOutput out = s_menu.Update(s_snap, pressed);
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
        else if (out.tap != nullptr || out.press != 0)
        {
            // One step of the Pokémon list, Bag or question: the menu looks
            // at the game again once it is done.
            s_step.name = "battle-step";
            s_step.summary = "battle: one tap or press for the native menu";
            s_step.needs = InScene(SceneBattle);
            s_step.steps.clear();
            if (out.tap != nullptr)
                s_step.steps.push_back(StepTap(out.tap));
            else
                s_step.steps.push_back(StepPress(out.press));
            s_step.steps.push_back(StepWait(10));
            log.push_back(std::string("battle-ui: step ") + (out.tap != nullptr ? out.tap : "press B"));
            if (!ActionStart(s_step, sceneManager, log))
                log.push_back("battle-ui: step refused");
        }

        // An action captures everything while it runs and then restores the
        // request it found (ours), so only change ours between actions.
        if (!ActionRunning())
            SetCapture(out.capture ? out.keys : learn.capture);

        const bool visible = s_menu.Visible();
        const std::string key = visible ? s_menu.DrawKey(s_snap) : std::string();
        if (visible != s_visible)
            log.push_back(UI::ScreenBuffer::HeapLog(visible ? "battle shown" : "battle hidden"));
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
        const bool learnDrew = MoveLearnOnScreen(screen);
        if (!screen.IsTop)
            return learnDrew;
        if (!s_visible)
        {
            UI::ScreenBuffer::GiveBack(Owner);
            return learnDrew;
        }
        bool fresh = false;
        u8 *pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
        if (pixels == nullptr)
            return learnDrew;
        if (fresh)
            s_layerArea = UI::Rect{};   // set up again before anything is presented
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
                    s_layer.Setup(fb, area, s_drawMenu.Radius(), pixels, UI::ScreenBuffer::Bytes);
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
        // True only if the CPU wrote to the frame (see UI::Layer::Present).
        const bool battleDrew = s_layerArea.w > 0 && s_visible && s_layer.Present(fb);
        return learnDrew | battleDrew;
    }

    void BattleScreenSetEnabled(bool enabled)
    {
        s_enabled = enabled;
        AutoPanelSetNativeBattle(enabled);
    }

    void BattleScreenSetLayout(BattleLayout layout)
    {
        s_menu.SetLayout(layout);
        s_layoutLog = true;
        // DrawKey hands the updated menu to the OSD callback on the next
        // update; its existing area comparison rebuilds the layer there.
    }

    BattleLayout BattleScreenLayout(void)
    {
        return s_menu.Layout();
    }

    bool BattleScreenEnabled(void)
    {
        return s_enabled;
    }

    std::string BattleScreenStatus(void)
    {
        return std::string("battle-ui: ") + (s_enabled ? "on" : "off") + Utils::Format(", capture %08lX; ", s_captured)
               + s_menu.Status(s_snap).substr(11) + "; " + MoveLearnStatus();
    }
}
