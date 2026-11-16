#pragma once
#include "PcAnywhereRules.hpp"
#include "ActionQueue.hpp"

namespace OneScreen
{
    const u32 WorldSaveAsk = 1u << 19;
    const u32 WorldSaveDone = 1u << 20;  // latched only during our request
    const u32 WorldSaveIdle = 1u << 21;

    inline std::string SaveRefusal(const PcWorld &w, bool pending)
    {
        if (pending) return "already saving";
        if (w.pending) return "the PC is opening";
        const std::string why = PcRefusal(w);
        if (!why.empty()) return why;
        return "";
    }

    // FieldMenu ReportDraw +0xE9, established by its SaveStart/SaveMain
    // branches (docs/OFFSETS.md). Unknown values never authorize input.
    inline bool SaveAsk(int state, int choice) { return state == 3 && choice == 0xFF; }
    inline bool SaveDone(int state) { return state == 6 || state == 7; }
    inline bool SaveError(int state) { return state == 8 || state == 9; }
    struct SaveProgress
    {
        bool asked = false, writing = false, done = false;
        void Observe(int state, int choice)
        {
            if (SaveAsk(state, choice)) asked = true;
            if (asked && state == 5) writing = true;
            if (asked && writing && SaveDone(state)) done = true;
        }
    };
    const ActionDef &QuickSaveAction(void);
}
