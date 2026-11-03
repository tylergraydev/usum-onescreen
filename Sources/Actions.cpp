#include "Actions.hpp"

#include <CTRPluginFramework.hpp>

#include "ActionQueue.hpp"
#include "Battle.hpp"
#include "Box.hpp"
#include "Buttons.hpp"
#include "Cursor.hpp"
#include "Framebuffer.hpp"
#include "InputCapture.hpp"
#include "Scene.hpp"
#include "Screens/MoveLearn.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert(ActionKeyB == (u32)Key::B, "action key B");
    static_assert(ActionKeyX == (u32)Key::X, "action key X");

    // The X menu is an overlay inside FieldRo, so the scene doesn't show it;
    // these bottom-screen pixels do (yellow background, the Save bar and its
    // blue Y corner). In the field they are Rotom's red frame. Colours from
    // screenshots of the X menu and the field (analysis/private/shots/ui3-*).
    struct PixelProbe
    {
        u16 x, y;
        u8 r, g, b;
    };

    static const PixelProbe XMenuProbe[] =
    {
        { 10, 120, 249, 233, 134 },
        { 300, 225, 229, 255, 246 },
        { 260, 230, 2, 183, 255 },
    };
    static const s32 ProbeTolerance = 40;

    // The page arrow: on page 1 it points right at the top right, on page 2
    // left at the top left (same blue; the other corner is the yellow
    // background). Colours from screenshots of both pages (p2-2 x1, x2).
    static const PixelProbe XMenuPage2Probe = { 16, 12, 119, 187, 255 };

    // The Party screen's plain list: the bottom bar's Y and X buttons (blue,
    // the white edge between them). A slot menu dims them, move mode and
    // messages cover them with a text box. Colours from screenshots of the
    // Party screen in each state (p2-3, docs/PARTY-UI.md).
    static const PixelProbe PartyListProbe[] =
    {
        { 120, 226, 90, 182, 255 },
        { 205, 226, 255, 255, 255 },
        { 310, 226, 90, 182, 255 },
    };

    static volatile bool s_xmenuSeen = false;   // written by the OSD callback
    static volatile bool s_partyListSeen = false;
    static volatile bool s_page2Seen = false;

    static ActionRunner s_runner;
    static ActionWorld s_world;
    static u32 s_frames = 0;
    static u32 s_startFrame = 0;
    static bool s_capturing = false;
    static u32 s_savedCapture = 0;              // capture request before the action

    static bool Near(u8 a, u8 b)
    {
        const s32 d = (s32)a - (s32)b;
        return d <= ProbeTolerance && d >= -ProbeTolerance;
    }

    static bool Matches(const FbLayout &fb, const PixelProbe &p)
    {
        u8 r, g, b;
        DecodePixel(fb.origin + (s32)p.x * fb.stepX + (s32)p.y * fb.stepY, fb.format, r, g, b);
        return Near(r, p.r) && Near(g, p.g) && Near(b, p.b);
    }

    static bool ActionsOnScreen(const Screen &screen)
    {
        if (screen.IsTop)
            return false;
        const FbLayout fb = GetLayout(screen);
        bool all = true;
        for (const PixelProbe &p : XMenuProbe)
            all = all && Matches(fb, p);
        s_xmenuSeen = all;
        s_page2Seen = all && Matches(fb, XMenuPage2Probe);
        bool party = true;
        for (const PixelProbe &p : PartyListProbe)
            party = party && Matches(fb, p);
        s_partyListSeen = party;
        return false;
    }

    void StartActions(void)
    {
        OSD::Run(ActionsOnScreen);
    }

    // Scene names cost a walk over the CRO headers, so read them every few
    // frames (the probe is per frame).
    static void ReadWorld(u32 sceneManager, bool now)
    {
        if (now || s_frames % 5 == 0)
        {
            s_world.scene = SceneModule(sceneManager);
            s_world.flags &= ~(WorldBattleCommand | WorldBattleMoves | WorldLearnAsk | WorldLearnList | WorldLearnConfirm);
            const BattleMenu menu = s_world.scene == SceneBattle ? ReadBattle(sceneManager).menu : BattleMenu::None;
            if (menu == BattleMenu::Command)
                s_world.flags |= WorldBattleCommand;
            if (menu == BattleMenu::Moves)
                s_world.flags |= WorldBattleMoves;
            if (s_world.scene == SceneBattle)
                s_world.flags |= MoveLearnWorldFlags(sceneManager);
        }
        s_world.flags = (s_world.flags & ~(WorldXMenu | WorldXMenuPage2 | WorldPartyList)) | ActionProbeFlags();
        const u32 box = WorldBoxIdle | WorldBoxModal | WorldBoxCarrying;
        s_world.flags = (s_world.flags & ~box) | (s_world.scene == SceneBox ? BoxWorldFlags(sceneManager) : 0);
    }

    u32 ActionProbeFlags(void)
    {
        return (s_xmenuSeen ? WorldXMenu : 0) | (s_page2Seen ? WorldXMenuPage2 : 0)
               | (s_partyListSeen ? WorldPartyList : 0);
    }

    static std::string DescribeWorld(const ActionWorld &world)
    {
        return Utils::Format("scene %s%s%s%s%s%s", world.scene.empty() ? "(none)" : world.scene.c_str(),
                             (world.flags & WorldXMenu) ? ", x menu" : "",
                             (world.flags & WorldXMenuPage2) ? " page 2" : "",
                             (world.flags & WorldBattleCommand) ? ", battle command" : "",
                             (world.flags & WorldBattleMoves) ? ", battle moves" : "",
                             (world.flags & WorldBoxIdle) ? ", box idle"
                             : (world.flags & WorldBoxModal) ? ", box modal"
                             : (world.flags & WorldBoxCarrying) ? ", box carrying" : "")
               + ((world.flags & WorldPartyList) ? ", party list" : "")
               + ((world.flags & WorldLearnAsk) ? ", learn ask" : (world.flags & WorldLearnList) ? ", learn list"
                  : (world.flags & WorldLearnConfirm) ? ", learn confirm" : "");
    }

    static void BeginCapture(void)
    {
        s_savedCapture = InputCaptureRequest();
        InputCaptureBegin(~0u, true, true);
        s_capturing = true;
    }

    static void EndCapture(void)
    {
        ButtonsActionSet(0);
        CursorActionTouch(false, 0, 0);
        const u32 saved = s_savedCapture;
        if (saved == 0)
            InputCaptureEnd();
        else
            InputCaptureBegin(saved, (saved & CaptureStickBits) != 0, (saved & CaptureTouchBit) != 0);
        s_capturing = false;
    }

    void ActionsUpdate(u32 sceneManager, std::vector<std::string> &log)
    {
        s_frames++;
        if (!s_runner.Running())
        {
            if (s_capturing)
                EndCapture();
            return;
        }

        const ActionWorld before = s_world;
        ReadWorld(sceneManager, false);
        if (s_world.scene != before.scene || s_world.flags != before.flags)
            log.push_back("action: sees " + DescribeWorld(s_world)
                          + Utils::Format(" (frame %lu)", s_frames - s_startFrame));
        const ActionOutput out = s_runner.Update(s_world, log);
        if (!s_runner.Running())
        {
            EndCapture();
            return;
        }
        ButtonsActionSet(out.keys);
        CursorActionTouch(out.touching, out.x, out.y);
    }

    bool ActionCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        if (args == "status")
        {
            ReadWorld(sceneManager, true);
            log.push_back(s_runner.Status() + "; sees " + DescribeWorld(s_world));
        }
        else if (args == "cancel")
        {
            s_runner.Cancel(log);
            if (s_capturing)
                EndCapture();
        }
        else if (args == "list")
        {
            std::string line = "action: list";
            for (const ActionDef &def : Actions())
                line += std::string(" | ") + def.name + ": " + def.summary;
            log.push_back(line);
        }
        else if (const ActionDef *def = FindAction(args))
            ActionStart(*def, sceneManager, log);
        else
            return false;
        return true;
    }

    bool ActionStart(const ActionDef &def, u32 sceneManager, std::vector<std::string> &log)
    {
        ReadWorld(sceneManager, true);
        if (!s_runner.Start(def, s_world, log))
            return false;
        s_startFrame = s_frames;
        if (!s_capturing)
            BeginCapture();
        return true;
    }

    bool ActionRunning(void)
    {
        return s_runner.Running();
    }
}
