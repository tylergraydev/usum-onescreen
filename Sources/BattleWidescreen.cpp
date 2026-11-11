#include "BattleWidescreen.hpp"
#include "Scene.hpp"
#include "Widescreen.hpp"
#include "SidePanels.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using namespace CTRPluginFramework;
    namespace B = BattleWidescreen;
    static Hook s_battleHook, s_unloadHook;
    static Mutex s_lifetimeLock;
    static B::Lifetime s_lifetime;
    static u32 s_callback = 0, s_unloads = 0;
    static u32 s_reportedUnloads = 0;
    static bool s_ready = false;

    static void BattlePerspectiveHook(float *out, u32 pivot, float fovy, float aspect, float near, float far)
    {
        const float squeeze = WidescreenSqueeze();
        if (SidePanelsBattleProjection(squeeze, GetSettings().sidePanels, aspect, pivot))
        {
            // All seven checked Battle callers use pivot 0. This is a row-major
            // 4x4: builder stores cot(fovy/2)/aspect at +0, cot(fovy/2) at +0x14.
            HookContext::GetCurrent().OriginalFunction<void>(out, pivot, fovy, aspect, near, far);
            SidePanelsScaleProjection(out, squeeze);
        }
        else
        {
            // Preserve the existing widescreen filter/counters and hard-float ABI.
            using Callback = void (*)(float *, u32, float, float, float, float);
            reinterpret_cast<Callback>(s_callback)(out, pivot, fovy, aspect, near, far);
        }
    }

    static bool ReadWord(uint32_t address, uint32_t &word)
    {
        u32 value = 0;
        const bool ok = Process::Read32(address, value);
        word = value;
        return ok;
    }

    // Do NOT change the CRO page permissions here. Process::ProtectMemory on
    // Battle.cro's text (perm RX, state AliasCode) made Azahar's ldr:ro reject
    // the next LoadCRO ("CRO original buffer is in invalid state"; Bag.cro at
    // battle start), and the game then spun in its fatal handler (0x3FD518).
    // CTRPF's Enable/Disable write the RX alias page fine as-is (verified in
    // Azahar; unverified on hardware).
    static HookResult ChangeBattleHook(u32 address, bool install)
    {
        if (install) s_battleHook.InitializeForMitm(address, (u32)BattlePerspectiveHook);
        return install ? s_battleHook.Enable() : s_battleHook.Disable();
    }

    static bool RemoveBattleHook()
    {
        return ChangeBattleHook(s_battleHook.GetContext().targetAddress, false) == HookResult::Success;
    }

    // This is the SDK's unload IPC wrapper: preserve all four integer args
    // and the Result. No allocation/logging on the loader's small game stack.
    // r1 is the loaded CRO base (verified at the SDK caller 0x1732C0).
    // Keep the lock until the IPC returns; frame polling uses TryLock.
    static u32 UnloadHook(u32 process, u32 module, u32 zero, u32 size)
    {
        Lock lock(s_lifetimeLock);
        const bool hadHook = s_lifetime.active != 0 && s_lifetime.active == module;
        if (!s_lifetime.BeforeUnload(module, RemoveBattleHook))
            return 0xFFFFFFFF; // Refuse to unmap a module with a live hook.
        if (hadHook) { ++s_unloads; SetBattlePanelsHud(false); }
        return HookContext::GetCurrent().OriginalFunction<u32>(process, module, zero, size);
    }

    std::string StartBattleWidescreen(uint32_t perspectiveCallback)
    {
        // Fail closed if any instruction/literal in the wrapper differs.
        if (!B::UnloadSignature(ReadWord))
            return "widescreen: CRO unload signature MISMATCH; battle hook disabled";
        s_callback = perspectiveCallback;
        s_unloadHook.InitializeForMitm(B::UnloadAddress, (u32)UnloadHook);
        const HookResult result = s_unloadHook.Enable();
        s_ready = result == HookResult::Success;
        return Utils::Format("widescreen: CRO pre-unload hook at %08lX -> %d", B::UnloadAddress, (int)result);
    }

    std::string BattleWidescreenFrame(uint32_t sceneManager)
    {
        if (!s_ready || s_lifetimeLock.TryLock()) return {};
        // Same scene -> vtable -> CroModuleOf/CroModuleName path as FastText.
        // Resolve afresh: cached scene addresses can be reused on the next load.
        u32 manager = 0, scene = 0, vtable = 0, module = 0, magic = 0;
        if (Process::Read32(sceneManager, manager) && manager
            && Process::Read32(manager, scene) && scene && Process::Read32(scene, vtable))
        {
            module = CroModuleOf(vtable);
            if (!module || !Process::Read32(module + 0x80, magic) || magic != 0x44584946
                || CroModuleName(module) != "Battle") module = 0;
        }
        HookResult result = HookResult::Success;
        const B::Event event = s_lifetime.Update(module, SidePanelsBattleHook(WidescreenSqueeze()), ReadWord,
            [&](uint32_t address) {
                result = ChangeBattleHook(address, true);
                return result == HookResult::Success;
            }, RemoveBattleHook);
        SetBattlePanelsHud(module != 0 && s_lifetime.active == module && WidescreenSqueeze() > 1.f
            && SidePanelsInclude(GetSettings().sidePanels, true));
        const u32 unloads = s_unloads;
        s_lifetimeLock.Unlock();

        std::string report;
        if (unloads != s_reportedUnloads)
        {
            s_reportedUnloads = unloads;
            report = Utils::Format("widescreen: battle hook removed before CRO unload (%lu since boot)", unloads);
        }
        if (event != B::Event::None)
        {
            if (!report.empty()) report += '\n';
            switch (event)
            {
            case B::Event::Installed:
                report += Utils::Format("widescreen: battle.cro at %08lX perspective +736D0 -> %d", module, (int)result);
                break;
            case B::Event::Removed: report += "widescreen: battle hook removed (scene/aspect)"; break;
            case B::Event::Mismatch: report += "widescreen: battle perspective signature MISMATCH; no writes"; break;
            case B::Event::InstallFailed:
                report += Utils::Format("widescreen: battle perspective hook failed -> %d", (int)result);
                break;
            case B::Event::RemoveFailed: report += "widescreen: battle hook removal FAILED"; break;
            default: break;
            }
        }
        return report;
    }
}
