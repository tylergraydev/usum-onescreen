#include "PcAnywhere.hpp"

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "Buttons.hpp"
#include "PcAnywhereRules.hpp"
#include "Scene.hpp"
#include "Settings.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2 (Ultra Sun and Ultra Moon; docs/PC-ANYWHERE.md).
    static const u32 GameManagerGlobal = 0x6A59C4;   // -> GameSys::GameManager
    static const u32 GmEventManager    = 0x20;       // gm + 0x20 -> GameEventManager
    static const u32 GmField           = 0x24;       // gm + 0x24 -> field object (0 when closed)

    // A game function found by its first words near its Ultra Sun address
    // (Ultra Moon's GameEventManager::Main is 4 bytes later). `words` = how
    // many of the four must match (a BL in the 4th word moves between builds).
    struct GameFunction
    {
        const char *name;
        u32 sunAddress;
        u32 prologue[4];
        u32 words;
    };

    // GameEventManager::Main(manager): runs the top event, once per frame.
    static const GameFunction EventMain = { "GameEventManager::Main", 0x404350,
        { 0xE92D4070, 0xE1A04000, 0xE5900000, 0xE3500000 }, 4 };
    // EventAppCall's start (gm, app type, flag) -> event, already pushed: the
    // X menu's way of running an app with the field closed and reopened.
    static const GameFunction AppCallStart = { "EventAppCall start", 0x37F54C,
        { 0xE92D40F8, 0xE1A04001, 0xE1A07002, 0xE5906020 }, 4 };
    // BoxEvent's start (gm) -> event, already pushed: the PC script's Box.
    static const GameFunction BoxEventStart = { "BoxEvent start", 0x2DE91C,
        { 0xE92D41F0, 0xE5905020, 0xE1A00005, 0 }, 3 };

    // EventAppCall (vtable 0x64BCD0) fields set after the start.
    static const u32 AppCallVtable = 0x64BCD0;
    static const u32 AppCallBetween = 0x30;   // void (*)(void *) called between field close and reopen
    // BoxEvent (vtable 0x647F58).
    static const u32 BoxEventVtable = 0x647F58;
    static const u32 BoxMode = 0x54;          // u8: 0 = the PC (as the script's Box command sets)
    static const u32 BoxListener = 0xB4;      // listener for the result: none

    // App type for EventAppCall: one its own app switch (0x37F110) doesn't
    // know, so it starts nothing itself; the Box comes from the callback.
    static const u32 AppTypeNone = 0;

    static const u32 BusyFrames = 30;         // game-thread frames to wait for the event manager to empty
    static const u32 OpenTimeout = 60 * 20;   // plugin frames to wait for the Box scene

    static Hook s_eventHook;
    static u32 s_eventMain = 0, s_appCallStart = 0, s_boxEventStart = 0;
    static bool s_ready = false;
    static std::string s_startReport;

    enum : u32 { StateIdle, StatePending, StateStarted, StateRefused, StateOpen };
    enum : u32 { LateNone, LateManager, LateBusy, LateScene, LateField, LateStart };

    // Written by the plugin thread while idle, read by the game thread while pending.
    static volatile u32 s_state = StateIdle;
    static volatile u32 s_sceneObject = 0, s_fieldObject = 0, s_fieldVtable = 0, s_sceneManager = 0;
    // Written by the game thread.
    static volatile u32 s_late = LateNone, s_waited = 0;
    static volatile u32 s_appEvent = 0, s_boxEvent = 0, s_boxCalls = 0;

    // Plugin thread only.
    static std::string s_source;
    static u32 s_openFrames = 0, s_seenBoxCalls = 0, s_comboSeen = 0, s_comboKey = 0;
    static bool s_sawBox = false;
    static u32 s_opened = 0, s_refused = 0;

    static u32 Word(u32 address)
    {
        u32 value = 0;
        if (address == 0 || !Process::Read32(address, value))
            return 0;
        return value;
    }

    static u32 FindFunction(const GameFunction &f)
    {
        for (u32 delta = 0; delta <= 0x40; delta += 4)
            for (int sign = 0; sign < (delta ? 2 : 1); sign++)
            {
                const u32 at = sign ? f.sunAddress - delta : f.sunAddress + delta;
                u32 i = 0;
                while (i < f.words && Word(at + i * 4) == f.prologue[i])
                    i++;
                if (i == f.words)
                    return at;
            }
        return 0;
    }

    // Called by EventAppCall between closing the field and reopening it
    // (game thread, inside the event manager). Starts the Box on top of it;
    // EventAppCall waits until the Box ends, then reopens the field.
    static void BetweenCallback(void *)
    {
        const u32 gm = Word(GameManagerGlobal);
        u32 box = 0;
        if (gm != 0)
            box = ((u32 (*)(u32))s_boxEventStart)(gm);
        if (box != 0)
        {
            *(volatile u8 *)(box + BoxMode) = 0;
            *(volatile u32 *)(box + BoxListener) = 0;
        }
        s_boxEvent = box;
        s_boxCalls = s_boxCalls + 1;
    }

    static void Refuse(u32 why)
    {
        s_late = why;
        __sync_synchronize();
        s_state = StateRefused;
    }

    // Game thread, at the start of the event manager's frame, while a
    // request is pending. Rechecks what the plugin thread saw and starts
    // EventAppCall only with no event running.
    static void TryStart(u32 manager)
    {
        const u32 gm = Word(GameManagerGlobal);
        if (gm == 0 || Word(gm + GmEventManager) != manager || Word(gm + GmField) == 0)
            return Refuse(LateManager);
        const u32 mgr = Word(s_sceneManager);
        if (mgr == 0 || Word(mgr) != s_sceneObject)
            return Refuse(LateScene);
        if (Word(s_fieldObject) != s_fieldVtable || Word(s_fieldObject + 4) != 0)
            return Refuse(LateField);
        if (Word(manager) != 0 || Word(manager + 8) != 0)
        {
            s_waited = s_waited + 1;
            if (s_waited > BusyFrames)
                Refuse(LateBusy);
            return;
        }

        const u32 event = ((u32 (*)(u32, u32, u32))s_appCallStart)(gm, AppTypeNone, 0);
        if (event == 0 || Word(event) != AppCallVtable || Word(manager) != event)
            return Refuse(LateStart);
        *(volatile u32 *)(event + AppCallBetween) = (u32)&BetweenCallback;
        s_appEvent = event;
        __sync_synchronize();
        s_state = StateStarted;
    }

    static u32 EventMainHook(u32 manager)
    {
        if (s_state == StatePending)
            TryStart(manager);
        return HookContext::GetCurrent().OriginalFunction<u32>(manager);
    }

    bool StartPcAnywhere(std::string &report)
    {
        s_eventMain = FindFunction(EventMain);
        s_appCallStart = FindFunction(AppCallStart);
        s_boxEventStart = FindFunction(BoxEventStart);
        const bool vtables = Word(AppCallVtable + 0x10) == 0x39268C && Word(BoxEventVtable + 0x10) == 0x2DEC0C;
        report = Utils::Format("pc: event main %08lX, app call %08lX, box event %08lX, vtables %s",
                               s_eventMain, s_appCallStart, s_boxEventStart, vtables ? "ok" : "MISMATCH");
        if (s_eventMain == 0 || s_appCallStart == 0 || s_boxEventStart == 0 || !vtables)
        {
            report += "; PC anywhere off";
            s_startReport = report;
            return false;
        }
        s_eventHook.InitializeForMitm(s_eventMain, (u32)EventMainHook);
        const HookResult result = s_eventHook.Enable();
        report += Utils::Format("; hook -> %d", (int)result);
        s_ready = result == HookResult::Success;
        s_startReport = report;
        return s_ready;
    }

    // True if a CRO whose name starts with `prefix` is loaded (walks the
    // module list from `module`: header +0x88 next, +0x8C previous).
    static bool CroLoaded(u32 module, const char *prefix)
    {
        for (int dir = 0; dir < 2; dir++)
        {
            u32 m = module;
            for (int i = 0; i < 48 && m != 0; i++)
            {
                if (CroModuleName(m).compare(0, std::string(prefix).size(), prefix) == 0)
                    return true;
                const u32 next = Word(m + (dir == 0 ? 0x88 : 0x8C));
                if (next == module)
                    break;
                m = next;
            }
        }
        return false;
    }

    static std::string LateReason(u32 why)
    {
        switch (why)
        {
        case LateManager: return "the field closed";
        case LateBusy:    return "a script or event is running";
        case LateScene:   return "the scene changed";
        case LateField:   return "the field is busy";
        case LateStart:   return "the game didn't start the event";
        default:          return "unknown";
        }
    }

    void PcAnywhereRequest(u32 sceneManager, const char *source, std::vector<std::string> &log)
    {
        if (!s_ready)
        {
            s_refused++;
            log.push_back(std::string("pc: refused (not available: ") + s_startReport + ")");
            return;
        }

        PcWorld w;
        w.pending = s_state != StateIdle;
        w.scene = SceneModule(sceneManager);
        w.actionRunning = ActionRunning();
        u32 fieldVtable = 0, fieldObject = 0, sceneObject = 0;
        if (w.scene == "FieldRo")
        {
            w.fieldState = FieldState(sceneManager);
            fieldObject = FieldStateObject(sceneManager, fieldVtable);
            const u32 mgr = Word(sceneManager);
            sceneObject = Word(mgr);
            w.festivalPlaza = CroLoaded(CroModuleOf(Word(sceneObject)), "JoinFesta");
            const u32 gm = Word(GameManagerGlobal);
            const u32 manager = Word(gm + GmEventManager);
            w.eventRunning = manager == 0 || Word(manager) != 0 || Word(manager + 8) != 0;
            if (fieldObject == 0 && w.fieldState == 0)
                w.fieldState = -1;
        }

        const std::string why = PcRefusal(w);
        if (!why.empty())
        {
            s_refused++;
            log.push_back("pc: refused (" + why + ") [" + source + "]");
            return;
        }

        s_source = source;
        s_sceneManager = sceneManager;
        s_sceneObject = sceneObject;
        s_fieldObject = fieldObject;
        s_fieldVtable = fieldVtable;
        s_late = LateNone;
        s_waited = 0;
        s_appEvent = 0;
        s_boxEvent = 0;
        s_seenBoxCalls = s_boxCalls;
        s_openFrames = 0;
        s_sawBox = false;
        __sync_synchronize();
        s_state = StatePending;
    }

    void PcAnywhereUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        const u32 key = GetSettings().pcKey;
        if (key != s_comboKey)
        {
            s_comboKey = key;
            ButtonsSetCombo(key);
            s_comboSeen = ButtonsComboHits();
        }
        const u32 hits = ButtonsComboHits();
        if (hits != s_comboSeen)
        {
            s_comboSeen = hits;
            if (key != 0)
                PcAnywhereRequest(sceneManager, ("key " + KeysToString(key)).c_str(), log);
        }

        switch (s_state)
        {
        case StateRefused:
            s_refused++;
            log.push_back("pc: refused (" + LateReason(s_late) + ") [" + s_source + "]");
            s_state = StateIdle;
            break;
        case StateStarted:
            s_opened++;
            log.push_back(Utils::Format("pc: opened [%s] (EventAppCall %08lX, waited %lu frames)",
                                        s_source.c_str(), s_appEvent, s_waited));
            s_state = StateOpen;
            break;
        case StateOpen:
            if (s_boxCalls != s_seenBoxCalls)
            {
                s_seenBoxCalls = s_boxCalls;
                log.push_back(Utils::Format("pc: box started (BoxEvent %08lX)", s_boxEvent));
                if (s_boxEvent == 0)
                {
                    s_state = StateIdle;
                    break;
                }
            }
            if (scene == "Box")
                s_sawBox = true;
            else if (s_sawBox && scene == "FieldRo")
            {
                log.push_back("pc: back in the field");
                s_state = StateIdle;
                break;
            }
            if (++s_openFrames > OpenTimeout && !s_sawBox)
            {
                log.push_back("pc: the boxes never opened (scene " + (scene.empty() ? std::string("(none)") : scene)
                              + "); giving up watching");
                s_state = StateIdle;
            }
            break;
        default:
            break;
        }
    }

    bool PcAnywhereCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        if (args.empty() || args == "open")
            PcAnywhereRequest(sceneManager, "cmd", log);
        else if (args == "status")
            log.push_back(PcAnywhereStatus());
        else
            return false;
        return true;
    }

    std::string PcAnywhereStatus(void)
    {
        static const char *states[] = { "idle", "pending", "started", "refused", "open" };
        const u32 state = s_state;
        return Utils::Format("pc: %s, state %s, pc_key %s, opened %lu, refused %lu | %s",
                             s_ready ? "ready" : "off", state < 5 ? states[state] : "?",
                             KeysToString(GetSettings().pcKey).c_str(), s_opened, s_refused,
                             s_startReport.c_str());
    }
}
