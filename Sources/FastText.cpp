#include "FastText.hpp"
#include "FastTextPatches.hpp"
#include "Scene.hpp"
#include "Game.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using namespace CTRPluginFramework;
    namespace P = FastTextPatches;

    static auto s_textExpected = P::TextStock;
    static bool s_textApplied = false, s_textKnown = true;
    static u32 s_scene = 0, s_vtable = 0, s_module = 0;
    static bool s_battleAttempted = false, s_isBattle = false, s_moduleNamed = false;
    static auto s_battleExpected = P::BattleStock;
    static BattleSpeed s_messages = BattleSpeed::Normal, s_bars = BattleSpeed::Normal;

    static std::string PatchText(bool instant)
    {
        std::array<uint32_t, 24> offsets{};
        for (unsigned i = 0; i < offsets.size(); ++i)
            offsets[i] = i * 4;
        uint32_t bad = 0;
        if (!P::Preflight(offsets, s_textExpected, [](uint32_t offset, uint32_t &word) {
                u32 value = 0;
                const bool ok = Process::Read32(P::TextAddress + offset, value);
                word = value;
                return ok;
            }, bad))
        {
            s_textKnown = false;
            return Utils::Format("fast-text: text tables MISMATCH at %08lX; no writes", P::TextAddress + bad);
        }
        auto desired = instant ? P::TextInstant : P::TextStock;
        if (desired != s_textExpected)
        {
            if (!Process::Patch(P::TextAddress, desired.data(), sizeof(desired)))
            {
                s_textKnown = false;
                return "fast-text: text tables Process::Patch failed (may be partial)";
            }
            s_textExpected = desired;
        }
        s_textApplied = instant;
        s_textKnown = true;
        return std::string("fast-text: instant text ") + (instant ? "on" : "off");
    }

    // Process::Patch is documented in CTRPF Process.hpp to invalidate cache.
    // Keep using it after protection changes, rather than a raw code store.
    static bool PatchCode(u32 address, u32 word, bool &fallback, bool &written, std::string &failure)
    {
        written = Process::Patch(address, word);
        if (written)
            return true;
        fallback = true;
        MemInfo info{};
        PageInfo page{};
        const u32 start = address & ~0xFFFu;
        if (R_FAILED(svcQueryMemory(&info, &page, address))
            || !Process::ProtectMemory(start, 0x1000, MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE))
        {
            failure = "Process::Patch failed; ProtectMemory fallback failed";
            return false;
        }
        written = Process::Patch(address, word);
        const bool restored = Process::ProtectMemory(start, 0x1000, info.perm);
        if (!written || !restored)
        {
            failure = !written ? (restored ? "Process::Patch failed after ProtectMemory"
                                         : "Process::Patch failed; ProtectMemory restore failed")
                               : "Process::Patch succeeded; ProtectMemory restore failed";
            return false;
        }
        return true;
    }

    std::string FastTextFrame(uint32_t sceneManager)
    {
        u32 manager = 0, scene = 0, vtable = 0;
        Process::Read32(sceneManager, manager);
        if (manager) Process::Read32(manager, scene);
        if (scene) Process::Read32(scene, vtable);
        if (scene != s_scene || vtable != s_vtable || (vtable >= CurrentGame()->layout.imageEnd && (!s_module || !s_moduleNamed)))
        {
            const u32 module = vtable ? CroModuleOf(vtable) : 0;
            // Remember non-Battle modules too, so discovery isn't repeated each frame.
            s_module = module;
            const std::string name = CroModuleName(module);
            s_moduleNamed = !name.empty();
            s_isBattle = name == "Battle";
            s_scene = scene;
            s_vtable = vtable;
            s_battleAttempted = false;
            s_battleExpected = P::BattleStock;
        }
        if (!s_isBattle)
            return {};
        const Settings &settings = GetSettings();
        if (s_battleAttempted && settings.battleMessages == s_messages && settings.battleBars == s_bars)
            return {};
        s_battleAttempted = true; // Log once per load / explicit settings change, even on failure.
        s_messages = settings.battleMessages;
        s_bars = settings.battleBars;
        uint32_t bad = 0;
        if (!P::Preflight(P::BattleOffsets, s_battleExpected, [](uint32_t offset, uint32_t &word) {
                u32 value = 0;
                const bool ok = Process::Read32(s_module + offset, value);
                word = value;
                return ok;
            }, bad))
            return Utils::Format("fast-text: battle.cro at %08lX MISMATCH at +%lX; no writes", s_module, bad);
        const auto desired = P::BattleWords(s_messages, s_bars);
        bool fallback = false;
        for (unsigned i = 0; i < desired.size(); ++i)
        {
            if (desired[i] == s_battleExpected[i]) continue;
            std::string failure;
            bool written = false;
            const bool ok = PatchCode(s_module + P::BattleOffsets[i], desired[i], fallback, written, failure);
            if (written) s_battleExpected[i] = desired[i];
            if (!ok)
                return Utils::Format("fast-text: battle.cro at %08lX %s at +%lX (may be partial)",
                    s_module, failure.c_str(), P::BattleOffsets[i]);
        }
        return Utils::Format("fast-text: battle.cro at %08lX patched messages %uf, bars %s%s",
            s_module, P::MessageFrames(s_messages), BattleSpeedName(s_bars),
            fallback ? " (ProtectMemory fallback; permissions restored)" : "");
    }

    std::string ApplyFastTextSettings(uint32_t sceneManager)
    {
        std::string report = PatchText(GetSettings().instantText);
        const std::string battle = FastTextFrame(sceneManager);
        if (!battle.empty()) report += "\n" + battle;
        return report;
    }

    std::string FastTextStatus()
    {
        const Settings &s = GetSettings();
        return std::string("fast-text: instant text requested ") + (s.instantText ? "on" : "off")
            + ", applied " + (!s_textKnown ? "unknown" : s_textApplied ? "on" : "off")
            + "; battle messages " + BattleSpeedName(s.battleMessages)
            + ", bars " + BattleSpeedName(s.battleBars) + "; see patch log for failures";
    }
}
