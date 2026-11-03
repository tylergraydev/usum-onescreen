#include "Screens/MoveLearn.hpp"

#include <3ds.h>

#include "ActionQueue.hpp"
#include "Actions.hpp"
#include "Battle.hpp"
#include "MoveData.hpp"
#include "Party.hpp"
#include "Scene.hpp"
#include "Settings.hpp"
#include "Screens/MoveLearnMenu.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"
#include "UI/ScreenBuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Found and checked as described in docs/MOVE-LEARN.md (USUM v1.2).
    // The question: a two-entry list object of the Battle CRO on top of the
    // scene stack; the message it answers sits in the battle view's message
    // work (view +0x230 -> +0x100: u32 ?, u16 message, u16 ?, u32 args...).
    static const u32 VtAsk = 0x10B3B0;          // Battle CRO
    static const u32 AskCount = 0xB0;           // u32, 2
    static const u32 ViewMessage = 0x230, MessageParam = 0x100;
    static const u16 MessageForget = 0x2F;      // "Should an old move be forgotten and replaced with X?"
    // The forget screen: WazaOshie CRO object, somewhere on the stack.
    static const u32 VtWaza = 0x76D0;           // WazaOshie CRO
    static const u32 WazaParam = 0xB0;          // -> u8 party index +1, u16 new move +8
    static const u32 WazaPage = 0xB5;           // u8 1 while a move's page (Forget / Give up) is up
    static const u32 ParamPokeId = 0x01, ParamMove = 0x08;
    // Battler record: the party index it came from.
    static const u32 MonPokeId = 0x19;
    static const u32 SceneView = 0x94, ManagerSlots = 8;
    static const u16 MoveCount = 729;

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        if (address != 0)
            Process::Read32(address, v);
        return v;
    }

    struct LearnRead
    {
        Screens::LearnStage stage = Screens::LearnStage::None;
        u8 pokeId = 0xFF;
        u16 newMove = 0;
    };

    // What of the move learning the bottom screen shows, from the scene
    // stack; cheap enough for every frame.
    static LearnRead ReadLearn(u32 sceneManager)
    {
        LearnRead out;
        const u32 manager = Read32(sceneManager);
        const u32 scene = Read32(manager);
        const u32 battle = scene ? CroModuleOf(Read32(scene)) : 0;
        if (battle == 0 || CroModuleName(battle) != "Battle")
            return out;
        u32 top = 0, waza = 0;
        for (u32 i = 0; i < ManagerSlots; i++)
        {
            const u32 object = Read32(manager + i * 4);
            if (object == 0)
                break;
            top = object;
            const u32 vtable = Read32(object);
            const u32 module = CroModuleOf(vtable);
            if (module != 0 && module != battle && vtable == module + VtWaza && CroModuleName(module) == "WazaOshie")
                waza = object;
        }
        if (waza != 0)
        {
            const u32 param = Read32(waza + WazaParam);
            const u16 move = param ? Read16(param + ParamMove) : 0;
            if (move == 0 || move >= MoveCount)
                return out;
            out.stage = Read8(waza + WazaPage) == 1 ? Screens::LearnStage::Confirm : Screens::LearnStage::List;
            out.pokeId = Read8(param + ParamPokeId);
            out.newMove = move;
            return out;
        }
        if (top == 0 || Read32(top) != battle + VtAsk || Read32(top + AskCount) != 2)
            return out;
        const u32 message = Read32(Read32(scene + SceneView) + ViewMessage);
        if (message == 0 || !Process::CheckAddress(message + MessageParam, MEMPERM_READ)
            || Read16(message + MessageParam + 4) != MessageForget)
            return out;
        const u32 pokeId = Read32(message + MessageParam + 8), move = Read32(message + MessageParam + 12);
        if (pokeId >= 6 || move == 0 || move >= MoveCount)
            return out;
        out.stage = Screens::LearnStage::Ask;
        out.pokeId = (u8)pokeId;
        out.newMove = (u16)move;
        return out;
    }

    u32 MoveLearnWorldFlags(u32 sceneManager)
    {
        switch (ReadLearn(sceneManager).stage)
        {
        case Screens::LearnStage::Ask:     return WorldLearnAsk;
        case Screens::LearnStage::List:    return WorldLearnList;
        case Screens::LearnStage::Confirm: return WorldLearnConfirm;
        default:                           return 0;
        }
    }

    // --- Move data from the game's RomFS (read once) ---

    static std::vector<MoveInfo> s_moveData;
    static bool s_moveDataTried = false;

    static void LoadMoveData(std::vector<std::string> &log)
    {
        if (s_moveDataTried)
            return;
        s_moveDataTried = true;
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        Handle handle = 0;
        const Result res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        if (R_FAILED(res))
        {
            log.push_back(Utils::Format("move-learn: move data: can't open the RomFS (0x%08lX)", (u32)res));
            return;
        }
        const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
            u32 got = 0;
            return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
        };
        std::string error;
        const bool ok = ReadMoveData(read, s_moveData, error);
        FSFILE_Close(handle);
        if (!ok)
        {
            s_moveData.clear();
            log.push_back("move-learn: " + error);
        }
        else
            log.push_back(Utils::Format("move-learn: move data: %u moves from %s", s_moveData.size(), MoveDataGarc));
    }

    // --- The screen ---

    static Screens::MoveLearnMenu s_menu;
    static Screens::LearnSnapshot s_snap;
    static std::string s_status;
    static bool s_acting = false;   // our action was started and hasn't ended yet

    static Mutex s_drawLock;
    static Screens::MoveLearnMenu s_drawMenu;
    static Screens::LearnSnapshot s_drawSnap;
    static std::string s_drawKey;
    static volatile bool s_visible = false;
    static bool s_dirty = false;
    static UI::Layer s_layer;
    static UI::Rect s_layerArea = {};
    static bool s_drawnGameFont = false;    // the game font was loaded when last drawn
    static const char *const Owner = "movelearn";   // UI::ScreenBuffer

    static Screens::LearnMove MakeMove(const BattleState &state, u16 move, u8 pp, u8 maxPp)
    {
        Screens::LearnMove out;
        out.move = move;
        out.name = MoveName(state, move);
        out.pp = pp;
        out.maxPp = maxPp;
        if (move < s_moveData.size() && s_moveData[move].known)
        {
            const MoveInfo &info = s_moveData[move];
            out.type = info.type;
            out.category = info.category;
            out.power = info.power;
            out.accuracy = info.accuracy;
        }
        return out;
    }

    // The snapshot is rebuilt only when what the game asks about changes
    // (names cost a message decrypt each).
    static void ReadSnapshot(u32 sceneManager, const LearnRead &read, std::vector<std::string> &log)
    {
        static u8 lastPokeId = 0xFF;
        static u16 lastMove = 0;
        s_snap.stage = read.stage;
        if (read.stage == Screens::LearnStage::None)
        {
            lastPokeId = 0xFF;
            lastMove = 0;
            return;
        }
        if (read.pokeId == lastPokeId && read.newMove == lastMove && !s_snap.monName.empty())
            return;
        LoadMoveData(log);
        const BattleState state = ReadBattle(sceneManager);
        Screens::LearnSnapshot snap;
        snap.stage = read.stage;
        snap.panelKey = s_snap.panelKey;
        for (u32 slot = 0; slot < 6 && slot < state.partyCount[0]; slot++)
        {
            const BattleMon &mon = state.party[slot];
            if (mon.address == 0 || Read8(mon.address + MonPokeId) != read.pokeId)
                continue;
            snap.monName = SpeciesName(mon.species);
            for (u8 i = 0; i < 4 && mon.moves[i] != 0; i++)
                snap.moves[snap.moveCount++] = MakeMove(state, mon.moves[i], mon.pp[i], mon.maxPp[i]);
            break;
        }
        const u8 pp = read.newMove < s_moveData.size() ? s_moveData[read.newMove].pp : 0;
        snap.newMove = MakeMove(state, read.newMove, pp, pp);
        s_snap = snap;
        lastPokeId = read.pokeId;
        lastMove = read.newMove;
        std::string moves;
        for (u8 i = 0; i < snap.moveCount; i++)
            moves += (i ? ", " : "") + snap.moves[i].name;
        log.push_back(Utils::Format("move-learn: party %u ", read.pokeId) + (snap.monName.empty() ? "?" : snap.monName)
                      + " wants " + snap.newMove.name + Utils::Format(" (%u); knows ", read.newMove) + moves);
    }

    MoveLearnFrame MoveLearnUpdate(u32 sceneManager, bool inBattle, u32 pressed, std::vector<std::string> &log)
    {
        MoveLearnFrame frame;
        // Read every 2 frames, like the battle menu.
        static u32 frames = 0;
        static LearnRead read;
        if (!inBattle)
            read = LearnRead();
        else if (frames++ % 2 == 0 || s_acting)
            read = ReadLearn(sceneManager);
        s_snap.panelKey = KeysToString(GetSettings().panelKey);
        ReadSnapshot(sceneManager, read, log);
        const bool running = ActionRunning();
        s_snap.actionRunning = running;

        if (s_acting && !running)
        {
            // The action's last step waits for the game to leave all three
            // screens; still on one means it gave up.
            s_acting = false;
            const bool ok = read.stage == Screens::LearnStage::None;
            s_menu.ActionDone(ok);
            if (ok)
                log.push_back("move-learn: done, " + s_menu.Result());
        }

        const Screens::LearnOutput out = s_menu.Update(s_snap, inBattle ? pressed : 0);
        const std::string status = s_menu.Status(s_snap);
        if (status != s_status)
        {
            s_status = status;
            log.push_back(status);
        }
        if (out.action != nullptr)
        {
            log.push_back(std::string("move-learn: chose ") + out.action->name);
            if (ActionStart(*out.action, sceneManager, log))
                s_acting = true;
            else
                s_menu.ActionDone(false);
        }

        frame.asked = read.stage != Screens::LearnStage::None || s_menu.Visible();
        frame.capture = out.capture ? Screens::LearnCaptureKeys : 0;

        const bool visible = s_menu.Visible();
        const std::string key = visible ? s_menu.DrawKey(s_snap) : std::string();
        if (visible && !s_visible)
            UI::RequestAssets();   // the game's font and icons; normally loaded at the first field already
        if (visible != s_visible)
            log.push_back(UI::ScreenBuffer::HeapLog(visible ? "movelearn shown" : "movelearn hidden"));
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
        return frame;
    }

    bool MoveLearnOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
            return false;
        if (!s_visible)
        {
            UI::ScreenBuffer::GiveBack(Owner);
            return false;
        }
        bool fresh = false;
        u8 *pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
        if (pixels == nullptr)
            return false;
        if (fresh)
            s_layerArea = UI::Rect{};   // set up again before anything is presented
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        // Never wait for the plugin thread here (this is the game's frame).
        // CTRPF: TryLock() is true on failure.
        if (!s_drawLock.TryLock())
        {
            if (s_visible)
            {
                const UI::Rect area = s_drawMenu.Area();
                if (!s_layer.Matches(fb) || area.x != s_layerArea.x || area.y != s_layerArea.y
                    || area.w != s_layerArea.w || area.h != s_layerArea.h)
                {
                    s_layer.Setup(fb, area, UI::Theme::WindowRadius, pixels, UI::ScreenBuffer::Bytes);

                    s_layerArea = area;
                    s_dirty = true;
                }
                // Also once the game's font and icons have loaded.
                const bool gameFont = UI::Theme::UsingGameFont();
                if (s_dirty || gameFont != s_drawnGameFont)
                {
                    UI::Canvas canvas = s_layer.Begin();
                    s_drawMenu.Draw(canvas, s_drawSnap);
                    s_dirty = false;
                    s_drawnGameFont = gameFont;
                }
            }
            s_drawLock.Unlock();
        }
        if (s_layerArea.w > 0 && s_visible)
            s_layer.Present(fb);
        return true;
    }

    std::string MoveLearnStatus(void)
    {
        return s_menu.Status(s_snap) + (s_moveData.empty() ? ", no move data yet" : "");
    }
}
