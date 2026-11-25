#include "Usum.hpp"
#include "QuickSave.hpp"
#include <CTRPluginFramework.hpp>
#include "Actions.hpp"
#include "Panel.hpp"
#include "Buttons.hpp"
#include "PcAnywhere.hpp"
#include "Scene.hpp"
#include "Settings.hpp"
#include "Screens/Map.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;
    static bool s_running = false;
    static SaveProgress s_progress;
    static int s_reportState = -1, s_loggedState = -1;
    static u32 s_key = 0, s_hits = 0;
    static std::string s_problem;
    static PanelMode s_previousPanel = PanelMode::Off;

    static u32 Word(u32 address)
    {
        u32 value = 0;
        if (address != 0) Process::Read32(address, value);
        return value;
    }
    static int Byte(u32 address)
    {
        u8 value = 0;
        return address != 0 && Process::Read8(address, value) ? value : -1;
    }

    // Read-only observation; no calls into the game and no writes to save data.
    // ReportDraw is an input responder on the scene stack (like RotomDraw).
    // Also accept the owning FieldMenuFrame, whose +0x78 points to it.
    // Signatures are checked before interpreting either object's fields.
    static int Report(u32 sceneManager, int &choice)
    {
        const u32 manager = Word(sceneManager);
        for (u32 i = 0; manager != 0 && i < 8; ++i)
        {
            const u32 frame = Word(manager + i * 4);
            if (frame == 0) break;
            const u32 vt = Word(frame), module = CroModuleOf(vt);
            if (module == 0 || CroModuleName(module) != "FieldMenu" || (vt != module + Usum::FieldMenuSaveFrame && vt != module + Usum::FieldMenuReportDraw))
                continue;
            if (Word(module + Usum::FieldMenuSaveStateRead) != 0xE5D400E9   // ldrb state, [r4,#E9]
                || Word(module + Usum::FieldMenuSaveMainCompare) != 0xE3500001 // SaveMain: 1 still writing
                || Word(module + Usum::FieldMenuSaveResultCompare) != 0xE3550000 // 0 success, otherwise error
                || Word(module + Usum::FieldMenuReportDrawCtor) != 0xE5803000 // ReportDraw constructor's vtable
                || Word(module + Usum::FieldMenuReportDrawVtableEntry) != module + Usum::FieldMenuReportDrawMain)
            {
                s_problem = "save observer signature mismatch";
                return -1;
            }
            const u32 report = vt == module + Usum::FieldMenuReportDraw ? frame : Word(frame + 0x78);
            if (Word(report) != module + Usum::FieldMenuReportDraw) return -1;
            choice = Byte(report + 0xE8);
            return Byte(report + 0xE9);
        }
        return -1;
    }

    bool QuickSaveRunning(void) { return s_running; }

    u32 QuickSaveWorldFlags(u32 sceneManager)
    {
        if (!s_running) return 0;
        PcWorld w = ReadPcWorld(sceneManager);
        w.actionRunning = false; // our action owns capture
        u32 flags = SaveRefusal(w, false).empty() ? WorldSaveIdle : 0;
        if (w.scene != SceneField)
        {
            s_problem = "the scene changed";
            return 0;
        }
        int choice = -1;
        const int state = Report(sceneManager, choice);
        s_reportState = state;
        s_progress.Observe(state, choice);
        if (s_problem.empty() && SaveAsk(state, choice)) flags |= WorldSaveAsk;
        if (SaveError(state)) s_problem = "the game reported a save error";
        if (s_progress.done) flags |= WorldSaveDone;
        return flags;
    }

    static void ReportResult(const std::string &result, std::vector<std::string> &log)
    {
        log.push_back("save: " + result);
        OSD::Notify(result == "saved" ? "Saved" : "Save: " + result);
    }

    void QuickSaveRequest(u32 sceneManager, std::vector<std::string> &log)
    {
        PcWorld world = ReadPcWorld(sceneManager);
        if (MapIsOpen()) world.fieldState = PcFieldMenu;
        const std::string why = SaveRefusal(world, s_running);
        if (!why.empty()) { ReportResult("refused (" + why + ")", log); return; }
        s_progress = SaveProgress();
        s_reportState = s_loggedState = -1;
        s_problem.clear();
        s_running = true; // enables fresh world sampling in ActionStart
        if (!ActionStart(QuickSaveAction(), sceneManager, log))
        {
            s_running = false;
            ReportResult("refused (the field changed before starting)", log);
            return;
        }
        s_previousPanel = GetPanelMode();
        SetPanelMode(PanelMode::Off);
        OSD::Notify("Saving...");
        log.push_back("save: saving");
    }

    void QuickSaveUpdate(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 key = GetSettings().saveKey;
        if (key != s_key)
        {
            s_key = key;
            ButtonsSetSaveCombo(key);
            s_hits = ButtonsSaveComboHits();
        }
        const u32 hits = ButtonsSaveComboHits();
        if (hits != s_hits)
        {
            s_hits = hits;
            if (key != 0) QuickSaveRequest(sceneManager, log);
        }
        if (!s_running) return;
        if (s_reportState != s_loggedState)
        {
            s_loggedState = s_reportState;
            log.push_back("save: report state " + std::to_string(s_reportState));
        }
        if (!s_problem.empty() && ActionRunning())
            ActionCommand("cancel", sceneManager, log);
        if (ActionRunning()) return;
        const bool ok = s_progress.done;
        s_running = false;
        SetPanelMode(!ok && FieldState(sceneManager) == FieldStateMenu ? PanelMode::Panel : s_previousPanel);
        if (ok) ReportResult("saved", log);
        else if (!s_problem.empty()) ReportResult("failed (" + s_problem + ")", log);
        else if (!s_progress.asked && !s_progress.writing && SceneModule(sceneManager) == SceneField
                 && (ActionProbeFlags() & WorldXMenu))
            ReportResult("refused (the game did not offer save confirmation)", log);
        else ReportResult("failed (" + std::string(s_progress.writing ? "save completion was not confirmed; do not retry until the game is idle"
            : s_progress.asked ? "confirmation changed or action was cancelled"
            : "saving is not allowed, the save prompt was not recognized, or the action was cancelled") + ")", log);
    }
}
