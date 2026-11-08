#pragma once

// PC anywhere (docs/PC-ANYWHERE.md): the pure part, no CTRPF, so tests/ can
// build it on the PC. When the plugin may open the PC boxes, and the key combo
// edge the pad hook uses.

#include <cstdint>
#include <string>

namespace OneScreen
{
    // What the plugin thread sees when a PC request comes in.
    struct PcWorld
    {
        std::string scene;              // SceneModule(): "FieldRo" in the overworld
        int         fieldState = -1;    // FieldState(): 0 walking / standing, -1 unknown
        bool        eventRunning = false;   // the game's event manager has an event (script, cut-scene, menu)
        bool        festivalPlaza = false;  // the JoinFesta CRO is loaded (Festival Plaza's field)
        bool        actionRunning = false;  // a plugin action is driving the bottom screen
        bool        pending = false;        // an earlier request hasn't been carried out yet
    };

    // Field states with a name (Scene.hpp, docs/OFFSETS.md).
    const int PcFieldMenu = 4, PcFieldTalk = 0x11, PcFieldEncounter = 0x1E;

    // "" when the PC may open now, else why not (for "pc: refused (...)").
    inline std::string PcRefusal(const PcWorld &w)
    {
        if (w.pending)
            return "already opening";
        if (w.scene == "Battle")
            return "in battle";
        if (w.scene == "Box")
            return "the boxes are already open";
        if (w.scene.empty())
            return "loading";
        if (w.scene != "FieldRo")
            return "not in the field (scene " + w.scene + ")";
        if (w.actionRunning)
            return "a plugin action is running";
        if (w.festivalPlaza)
            return "in Festival Plaza";
        if (w.fieldState == PcFieldMenu)
            return "a menu is open";
        if (w.fieldState == PcFieldTalk)
            return "talking";
        if (w.fieldState == PcFieldEncounter)
            return "encounter starting";
        if (w.fieldState < 0)
            return "field state unknown";
        if (w.fieldState != 0)
            return "field busy (state " + std::to_string(w.fieldState) + ")";
        if (w.eventRunning)
            return "a script or event is running";
        return "";
    }

    // The pc_key combo as the pad hook sees it, one read at a time. The
    // combo fires on the read where all of its keys are first held together
    // (several reads a frame see the same keys, so only the first fires). Its
    // keys are then hidden from the game until each is released, so the key
    // that completed the combo (X in L+X) never reaches the game as a press.
    struct PcComboState
    {
        bool     down = false;          // the whole combo was held on the last read
        uint32_t suppressed = 0;        // keys hidden from the game until released
    };

    // Returns true when the combo fires. `held` is every key held in this read.
    inline bool PcComboRead(PcComboState &state, uint32_t combo, uint32_t held)
    {
        bool fired = false;
        const bool all = combo != 0 && (held & combo) == combo;
        if (all && !state.down)
        {
            fired = true;
            state.suppressed |= combo;
        }
        state.down = all;
        state.suppressed &= held;
        return fired;
    }
}
