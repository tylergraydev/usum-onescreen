#include "QuickSaveRules.hpp"

namespace OneScreen
{
    const ActionDef &QuickSaveAction(void)
    {
        // No retries: in particular never replay a confirmation press.
        static const ActionDef def = { "quick-save", "Save the adventure through the game's own menu",
            InScene(SceneField).With(WorldSaveIdle), {
                StepPress(ActionKeyX).Guard(InScene(SceneField).With(WorldSaveIdle)),
                StepWaitFor(InScene(SceneField).With(WorldXMenu), 180),
                StepWait(20),
                StepPress(1u << 11).Guard(InScene(SceneField).With(WorldXMenu)), // Y
                StepWaitFor(InScene(SceneField).With(WorldSaveAsk), 240),
                StepPress(1u).Guard(InScene(SceneField).With(WorldSaveAsk)), // A: Yes, once
                StepWaitFor(InScene(SceneField).With(WorldSaveDone), 1800),
                // ReportDraw closes itself, normally returning straight to
                // the field. Rotom may chat next; close only a remaining X menu.
                StepWait(90),
                StepPress(ActionKeyB).SkipIf(InScene(SceneField).Without(WorldXMenu))
                    .Guard(InScene(SceneField).With(WorldXMenu)),
            } };
        return def;
    }
}
