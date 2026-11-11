#include "Widescreen.hpp"
#include "BattleWidescreen.hpp"
#include "SidePanels.hpp"
#include "Game.hpp"
#include "PcAnywhere.hpp"
#include "Settings.hpp"
#include "Screens/Map.hpp"
#include <atomic>

#include <CTRPluginFramework.hpp>
#include <cmath>
#include <cstring>

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static constexpr u32 PerspectiveAddress = 0x3631CC;
    static constexpr u32 CalculateMtxAddress = 0x2A22EC;
    static Hook s_perspectiveHook;
    static Hook s_calculateMtxHook;
    static volatile bool s_battlePanelsHud = false;
    void SetBattlePanelsHud(bool enabled) { s_battlePanelsHud = enabled; }
    bool BattlePanelsHudActive(void) { return s_battlePanelsHud; }

    static_assert(std::atomic<float>::is_always_lock_free, "FOV publication must be lock-free");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "FOV counter must be lock-free");
    static std::atomic<float> s_fieldScale{1.0f};
    static std::atomic<uint32_t> s_fovRewritten{0};
    static u32 s_sceneManager = 0;
    static volatile float s_aspect = 0.0f;
    static volatile u32 s_rewritten = 0;
    static volatile u32 s_culled = 0;
    static volatile u32 s_hudRoots = 0, s_hudFits = 0;
    static volatile float s_lastHudFit = 1.f;
    static Mutex s_scaleLock;
    static volatile u32 s_scalePane = 0;
    static volatile u32 s_scaleVtable = 0;
    static volatile u32 s_scaleNameWord = 0;
    static volatile float s_scaleX = 1.0f;
    static float s_savedScaleX = 1.0f;
    static volatile bool s_restoreScaleX = false;
    // nw::lyt::Pane fields, USUM v1.2 (both games).
    static constexpr u32 PaneScaleX = 0x34, PaneFlags = 0x44, PaneName = 0x80;
    static constexpr u8 PaneMatrixDirty = 0x10;

    // Debug capture only: never allocate or log from the game hook.
    struct CulledPane
    {
        const void *pane;
        char name[24];
        float gx;
    };
    static CulledPane s_cullNames[32];
    static volatile bool s_captureCullNames = false;
    static unsigned s_cullNameCount = 0;

    static u32 FloatBits(float value)
    {
        u32 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    // Preflight BOTH templates before writing either. Only the horizontal
    // bounds may differ from stock, and only by an exact pair we last wrote.
    static std::string PatchLayoutTemplates(float halfWidth)
    {
        static const u32 addresses[] = { 0x5C3180, 0x5E8468 };
        static const u32 stock[][7] = {
            { 2, 0xC3480000, 0x43480000, 0xC2F00000, 0x42F00000, 0x3F800000, 0x447A0000 },
            { 2, 0xC3480000, 0x43480000, 0xC2F00000, 0x42F00000, 0x3DCCCCCD, 0x44480000 }
        };
        static u32 last[2][2] = {
            { 0xC3480000, 0x43480000 }, { 0xC3480000, 0x43480000 }
        };
        for (unsigned i = 0; i < 2; i++)
        {
            u32 current[7];
            for (unsigned j = 0; j < 7; j++)
                if (!Process::Read32(addresses[i] - 4 + j * 4, current[j]))
                    return "widescreen: 2d templates MISMATCH (unreadable); no writes";
            const bool stockPair = current[1] == stock[i][1] && current[2] == stock[i][2];
            const bool lastPair = current[1] == last[i][0] && current[2] == last[i][1];
            if ((!stockPair && !lastPair) || current[0] != stock[i][0]
                || std::memcmp(current + 3, stock[i] + 3, 4 * sizeof(u32)) != 0)
                return Utils::Format("widescreen: 2d templates MISMATCH at 0x%lX; no writes", addresses[i]);
        }
        u32 bounds[] = { FloatBits(-halfWidth), FloatBits(halfWidth) };
        for (unsigned i = 0; i < 2; i++)
        {
            if (!Process::Patch(addresses[i], bounds, sizeof(bounds)))
                return Utils::Format("widescreen: 2d templates PATCH FAILED at 0x%lX (may be partial)", addresses[i]);
            std::memcpy(last[i], bounds, sizeof(bounds));
        }
        return Utils::Format("widescreen: 2d templates 0x5C3180 0x5E8468 left/right ±%.7g", (double)halfWidth);
    }

    float WidescreenSqueeze(void)
    {
        const float aspect = s_aspect;
        return aspect == 0.0f ? 1.0f : aspect / (400.0f / 240.0f);
    }

    void SetGamePaneScaleX(u32 pane, u32 vtable, u32 nameWord, float scaleX)
    {
        Lock lock(s_scaleLock);
        if (pane != s_scalePane || vtable != s_scaleVtable || nameWord != s_scaleNameWord)
        {
            // Drop the old restore on replacement; the game rewrites its scale.
            s_savedScaleX = 1.0f;
        }
        s_scalePane = 0;
        s_scaleX = scaleX;
        s_scaleVtable = vtable;
        s_scaleNameWord = nameWord;
        s_restoreScaleX = false;
        // Publish only after all fields are ready; the lock protects both threads.
        s_scalePane = pane;
    }

    void ClearGamePaneScaleX(void)
    {
        Lock lock(s_scaleLock);
        if (s_scalePane != 0)
            s_restoreScaleX = true;
    }

    std::string DescribeGamePaneScaleX(void)
    {
        Lock lock(s_scaleLock);
        const u32 pane = s_scalePane;
        if (pane == 0)
            return "bottom squeeze off";
        if (s_restoreScaleX)
            return Utils::Format("bottom squeeze restoring %.4f (All %08lX)", (double)s_savedScaleX, pane);
        return Utils::Format("bottom squeeze %.4f (All %08lX)", (double)s_scaleX, pane);
    }

    // Opaque output matrix: the hook only forwards the game's pointer.
    struct Matrix34or44;

    // Plain C++ preserves the game's hard-float ABI: out/pivot in r0/r1,
    // fovy/aspect/near/far in s0-s3 (including the typed original call).
    static void PerspectiveHook(Matrix34or44 *out, u32 pivot, float fovy,
                                float aspect, float near, float far)
    {
        const float active = s_aspect;
        if (active != 0.0f && TopPerspective(aspect))
        {
            aspect = PerspectiveAspect(aspect, active);
            s_rewritten++;
        }
        HookContext::GetCurrent().OriginalFunction<void>(out, pivot, fovy, aspect, near, far);
    }

    // CalculateMtx recursively visits children before returning. Fit only at
    // the outer top root, AFTER that traversal; force dirty on the next call
    // so adjusted global matrices are never multiplied into the next frame.
    // Intrusive child node = pane+4; parent=+0xC; child sentinel=+0x14.
    template<class Visit>
    static bool VisitBattlePanes(u8 *root, Visit visit)
    {
        return VisitSidePanelPanes((uintptr_t)root,
            [](uintptr_t address) { return (uintptr_t)*reinterpret_cast<const u32 *>(address); },
            [&](uintptr_t pane) { visit(reinterpret_cast<u8 *>(pane)); });
    }

    static void FitBattleTopLayout(u8 *root)
    {
        float left = -200.f, right = 200.f;
        if (!VisitBattlePanes(root, [&](u8 *pane) {
            if (!pane[0x46]) return;
            const float *matrix = reinterpret_cast<const float *>(pane+0x48);
            const float width = *reinterpret_cast<const float *>(pane+0x3C) * std::fabs(matrix[0]);
            if (!std::isfinite(width) || width < 1.f || width > 400.f
                || std::fabs(matrix[1]) > .01f * std::fabs(matrix[0])) return;
            const float x = matrix[3];
            const unsigned anchor = pane[0x47] & 3;
            const float l = x - (anchor == 1 ? 0.f : anchor == 2 ? width : width*.5f);
            const float r = l + width;
            // Ignore parked/animating panes. This bounded fit is for the HUD's
            // small decorative overflow, not arbitrary cut-in/fade geometry.
            if (!std::isfinite(l) || l < -256.f || r > 256.f) return;
            const float inside = std::min(r,200.f) - std::max(l,-200.f);
            if (inside < .3f*width) return;
            left = std::min(left,l); right = std::max(right,r);
        })) return;
        const float scale = SidePanelsHudFit(left,right);
        s_lastHudFit = scale;
        if (scale == 1.f) return;
        ++s_hudFits;
        VisitBattlePanes(root, [&](u8 *pane) {
            SidePanelsScaleHud(reinterpret_cast<float *>(pane+0x48),scale);
        });
    }

    // Only code.bin uses this wrapper. Battle keeps the aspect-only callback,
    // even if a scene transition occurs between plugin frame publications.
    static void FieldPerspectiveHook(Matrix34or44 *out, u32 pivot, float fovy,
                                     float aspect, float near, float far)
    {
        const float scaled = FieldFovY(fovy, s_fieldScale.load(std::memory_order_relaxed), aspect);
        if (scaled != fovy && std::isfinite(fovy))
            s_fovRewritten.fetch_add(1, std::memory_order_relaxed);
        PerspectiveHook(out, pivot, scaled, aspect, near, far);
    }

    struct DrawInfo;

    static void CalculateMtxHook(u8 *pane, const DrawInfo *info, u32 dirty)
    {
        // Never wait on the plugin thread from the game's matrix calculation.
        if (!s_scaleLock.TryLock())
        {
            const u32 target = s_scalePane;
            if (target != 0 && (u32)pane == target
                && *reinterpret_cast<const u32 *>(pane) == s_scaleVtable
                && *reinterpret_cast<const u32 *>(pane + PaneName) == s_scaleNameWord)
            {
                float &scale = *reinterpret_cast<float *>(pane + PaneScaleX);
                if (s_restoreScaleX)
                {
                    scale = s_savedScaleX;
                    s_scalePane = 0;
                    s_restoreScaleX = false;
                }
                else
                {
                    const float overrideScale = s_scaleX;
                    // Keep the game's last value if our previous write is still there.
                    if (std::fabs(scale - overrideScale) > 0.00001f)
                        s_savedScaleX = scale;
                    scale = overrideScale;
                }
                pane[PaneFlags] |= PaneMatrixDirty;
            }
            s_scaleLock.Unlock();
        }
        const bool fitBattle = s_battlePanelsHud && WidescreenSqueeze() > 1.f
            && *reinterpret_cast<const u32 *>(pane) == 0x646B28
            && SidePanelsTopRoot(reinterpret_cast<const char *>(pane+0x80),
                *reinterpret_cast<const u32 *>(pane+0xC),
                *reinterpret_cast<const float *>(pane+0x3C),
                *reinterpret_cast<const float *>(pane+0x40));
        if (fitBattle) { pane[PaneFlags] |= PaneMatrixDirty; dirty = 1; }
        HookContext::GetCurrent().OriginalFunction<void>(pane, info, dirty);
        if (fitBattle)
        {
            ++s_hudRoots;
            FitBattleTopLayout(pane);
            pane[PaneFlags] |= PaneMatrixDirty; // also rebuild once after panels turn off
        }
        if (WidescreenSqueeze() <= 1.0f)
            return;

        const float gx = *reinterpret_cast<const float *>(pane + 0x54);
        const float sx = std::fabs(*reinterpret_cast<const float *>(pane + 0x48));
        const float sy = std::fabs(*reinterpret_cast<const float *>(pane + 0x4C));
        const float width = *reinterpret_cast<const float *>(pane + 0x3C) * sx;
        if (sy > 0.01f * sx || width < 1.0f)
            return;

        const u8 anchor = pane[0x47] & 3;
        const float left = gx - (anchor == 1 ? 0.0f : anchor == 2 ? width : width * 0.5f);
        const float right = left + width;
        const float clippedLeft = left > -200.0f ? left : -200.0f;
        const float clippedRight = right < 200.0f ? right : 200.0f;
        const float inside = clippedRight > clippedLeft ? clippedRight - clippedLeft : 0.0f;
        // Hand check (sx=1, sy=0): inside/W for Totaltime=43/188,
        // Battle_BaseTimer_02=0/76, TexTime_00=3/80, Battle_BaseTimer_01=0/64:
        // all < 30%, so cull (anchor 9 & 3 = left).
        // Battle_Hpgauge_01=86/86, Battle_Hpgauge_02=86/86, TextBox_00=198/370,
        // Gauge_Exp=38/38, IconBall_00=10/10: all >= 30%, so keep.
        if (!(inside < 0.3f * width))
            return;
        pane[0x46] = 0;
        s_culled++;

        if (!s_captureCullNames || s_cullNameCount == 32)
            return;
        for (unsigned i = 0; i < s_cullNameCount; i++)
            if (s_cullNames[i].pane == pane)
                return;
        CulledPane &entry = s_cullNames[s_cullNameCount++];
        entry.pane = pane;
        std::memcpy(entry.name, pane + 0x80, sizeof(entry.name));
        entry.gx = gx;
    }

    void RequestWidescreenCullNames(void)
    {
        s_captureCullNames = false;
        s_cullNameCount = 0;
        s_captureCullNames = true;
    }

    std::string WidescreenFrame(void)
    {
        // Fresh scene/event reads on the plugin thread, never the 15-frame
        // logging cache. Publish only one scalar for the game thread.
        const float requested = GetSettings().fieldFov;
        const bool allowed = s_sceneManager != 0 && requested != 1.0f
            && FieldFovAllowed(ReadPcWorld(s_sceneManager), MapIsOpen());
        s_fieldScale.store(allowed ? requested : 1.0f, std::memory_order_relaxed);
        if (!s_captureCullNames)
            return {};
        s_captureCullNames = false;
        std::string report;
        for (unsigned i = 0; i < s_cullNameCount; i++)
        {
            const CulledPane &entry = s_cullNames[i];
            if (!report.empty())
                report += '\n';
            report += Utils::Format("widescreen: culled %.24s gx=%.7g", entry.name, (double)entry.gx);
        }
        return report;
    }

    bool StartWidescreen(std::string &report)
    {
        const GameInfo *game = DetectGame();
        s_sceneManager = game ? game->offsets.sceneManager : 0;
        s_perspectiveHook.InitializeForMitm(PerspectiveAddress, (u32)FieldPerspectiveHook);
        const HookResult result = s_perspectiveHook.Enable();
        report = Utils::Format("widescreen: perspective hook at %08lX -> %d",
                               PerspectiveAddress, (int)result);
        s_calculateMtxHook.InitializeForMitm(CalculateMtxAddress, (u32)CalculateMtxHook);
        const HookResult cullResult = s_calculateMtxHook.Enable();
        report += Utils::Format("\nwidescreen: pane CalculateMtx hook at %08lX -> %d",
                                CalculateMtxAddress, (int)cullResult);
        report += "\n" + StartBattleWidescreen((u32)PerspectiveHook);
        return result == HookResult::Success && cullResult == HookResult::Success;
    }

    std::string DescribeFieldFov(void)
    {
        return Utils::Format("fov: scale %.7g; published %.7g; rewritten %lu since boot",
            (double)GetSettings().fieldFov, (double)s_fieldScale.load(std::memory_order_relaxed),
            (unsigned long)s_fovRewritten.load(std::memory_order_relaxed));
    }

    std::string SetFieldFov(float scale)
    {
        if (!std::isfinite(scale) || scale < 0.8f || scale > 1.5f)
            return "fov: invalid scale (use 0.8..1.5; 1 = stock)";
        Settings settings = GetSettings();
        settings.fieldFov = scale;
        SetSettings(settings);
        return DescribeFieldFov();
    }

    std::string DescribeWidescreenAspect(void)
    {
        return Utils::Format("widescreen: aspect %.7g; rewritten %lu since boot; culled %lu panes since boot; battle HUD roots %lu fits %lu last %.4f",
                             (double)s_aspect, s_rewritten, s_culled, s_hudRoots, s_hudFits, (double)s_lastHudFit);
    }

    std::string SetWidescreenAspect(float aspect)
    {
        const float halfWidth = aspect == 0.0f ? 200.0f : 200.0f * (aspect / (400.0f / 240.0f));
        if (!std::isfinite(aspect) || aspect < 0.0f || !std::isfinite(halfWidth) || halfWidth <= 0.0f)
            return "widescreen: invalid aspect";
        if (aspect <= 400.0f / 240.0f)
            ClearGamePaneScaleX();
        s_aspect = aspect;
        const std::string status = aspect == 0.0f ? "widescreen: off"
            : Utils::Format("widescreen: aspect %.7g", (double)aspect);
        return status + "\n" + PatchLayoutTemplates(halfWidth);
    }
}
