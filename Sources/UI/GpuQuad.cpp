// The command list walker runs on every game list (~70k words a frame): -O2.
#pragma GCC optimize("O2")

#include "UI/GpuQuad.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstring>
#include <atomic>
#include <algorithm>
#include "UI/Layer.hpp"
#include "UI/QuadLayout.hpp"
#include "UI/QuadAllocator.hpp"
#include "UI/ColumnMapping.hpp"

#include "UI/Raster.hpp"
#include "Widescreen.hpp"

#include "UI/GpuQuadShader.inc"

namespace OneScreen
{
namespace UI
{
    using namespace CTRPluginFramework;

    using DisplayTransferFn = void (*)(u32 in, u32 inWidth, u32 inHeight, u32 out, u32 outWidth,
                                       u32 outHeight, u32 flags);
    // The game's gx ProcessCommandList (GX command 1): (list, size in bytes,
    // push a FlushCacheRegions first, flags). Only caller: nngx's request
    // runner at 0x5614F4. Same address and prologue on Ultra Moon.
    using ProcessListFn = void (*)(u32 list, u32 size, u32 flush, u32 flags);
    static constexpr u32 ProcessListAddress = 0x279D5C;
    static constexpr u32 ProcessListPrologue[3] = { 0xE92D43F0, 0xE1A05000, 0xE3A00000 };

    static DisplayTransferFn s_displayTransfer = nullptr;
    static ProcessListFn s_processList = (ProcessListFn)ProcessListAddress;
    static Hook s_hook;
    static bool s_hooked = false;

    // ---------------------------------------------------------------- shadow

    static constexpr u32 RegCount = 0x300;
    static u32 s_reg[RegCount];
    static u8  s_seen[RegCount];        // 4-bit byte mask of bytes ever written
    static u8 *const s_seenShadow = s_seen;
    static u32 s_code[512];
    static bool s_codeSeen[512];
    static u32 s_opdesc[128];
    static bool s_opdescSeen[128];
    static u32 s_codeOffset = 0, s_opdescOffset = 0;

    static RecursiveLock s_lock;
    static volatile bool s_ownPush = false;
    // Debug switches: `gpuquad restore off` skips the restore (to see whether
    // the game needs it); `gpuquad shadow off` stops parsing the game's lists.
    static std::atomic<bool> s_restore{true}, s_shadow{true};
    // `gpuquad mode fb|target` (see GpuQuadOnScreen / DisplayTransferHook).
    static std::atomic<bool> s_inTarget{true};
    static Hook s_dtHook;
    static bool s_dtHooked = false;

    // Stats.
    static u32 s_lists = 0, s_jumps = 0, s_badJumps = 0;
    static u64 s_words = 0;
    static u64 s_parseTicks = 0;
    static u32 s_minSize = 0xFFFFFFFF, s_maxSize = 0;

    static u32 ByteMask(u32 mask)
    {
        return ((mask & 1) ? 0x000000FFu : 0) | ((mask & 2) ? 0x0000FF00u : 0)
             | ((mask & 4) ? 0x00FF0000u : 0) | ((mask & 8) ? 0xFF000000u : 0);
    }

    // Physical address of game memory -> its virtual address (FCRAM linear
    // heap at 0x30000000, VRAM at 0x1F000000). 0 if neither.
    static u32 PhysToVirt(u32 pa)
    {
        if (pa >= 0x20000000 && pa < 0x28000000)
            return pa - 0x20000000 + 0x30000000;
        if (pa >= 0x18000000 && pa < 0x18600000)
            return pa - 0x18000000 + 0x1F000000;
        return 0;
    }

    // Applies one register write to a register file + shader memories.
    struct State
    {
        u32 *reg;
        u8  *seen;
        u32 *code;
        bool *codeSeen;
        u32 *opdesc;
        bool *opdescSeen;
        u32 *codeOffset;
        u32 *opdescOffset;
    };

    // Diagnostic: does the game set again everything our draw changed before
    // its next draw? After each of our draws, every register (and vertex
    // shader code / operand word) we changed is "dirty"; the game's writes
    // clean it. At the game's first draw after ours, whatever is still dirty
    // is state the game relied on keeping (counted and listed in status).
    static bool s_dirty[0x300];
    static bool s_dirtyCode[512], s_dirtyOpdesc[128];
    static volatile bool s_watching = false;
    static u32 s_checks = 0, s_staleChecks = 0, s_staleCode = 0;
    static u8 s_stale[0x300];

    static void Apply(const State &s, u32 id, u32 value, u32 mask)
    {
        if (id >= RegCount)
            return;
        if (s_watching && s.seen == s_seenShadow)
        {
            if (id == GPUREG_DRAWARRAYS || id == GPUREG_DRAWELEMENTS)
            {
                bool stale = false;
                for (u32 r = 0; r < RegCount; r++)
                    if (s_dirty[r])
                    {
                        s_stale[r] = 1;
                        stale = true;
                    }
                for (u32 i = 0; i < 512; i++)
                    if (s_dirtyCode[i])
                    {
                        s_staleCode++;
                        stale = true;
                        break;
                    }
                for (u32 i = 0; i < 128; i++)
                    if (s_dirtyOpdesc[i])
                    {
                        s_staleCode++;
                        stale = true;
                        break;
                    }
                s_checks++;
                s_staleChecks += stale;
                s_watching = false;
            }
            else if ((mask & (s_seen[id] | mask)) == (s_seen[id] | mask))
                s_dirty[id] = false;
            if (id >= GPUREG_VSH_CODETRANSFER_DATA && id < GPUREG_VSH_CODETRANSFER_DATA + 8)
                s_dirtyCode[*s.codeOffset & 511] = false;
            else if (id >= GPUREG_VSH_OPDESCS_DATA && id < GPUREG_VSH_OPDESCS_DATA + 8)
                s_dirtyOpdesc[*s.opdescOffset & 127] = false;
        }
        const u32 m = ByteMask(mask);
        s.reg[id] = (s.reg[id] & ~m) | (value & m);
        s.seen[id] |= (u8)mask;
        if (id == GPUREG_VSH_CODETRANSFER_CONFIG)
            *s.codeOffset = value & 0xFFF;
        else if (id >= GPUREG_VSH_CODETRANSFER_DATA && id < GPUREG_VSH_CODETRANSFER_DATA + 8)
        {
            const u32 at = (*s.codeOffset)++ & 511;
            s.code[at] = value;
            s.codeSeen[at] = true;
        }
        else if (id == GPUREG_VSH_OPDESCS_CONFIG)
            *s.opdescOffset = value & 0x7F;
        else if (id >= GPUREG_VSH_OPDESCS_DATA && id < GPUREG_VSH_OPDESCS_DATA + 8)
        {
            const u32 at = (*s.opdescOffset)++ & 127;
            s.opdesc[at] = value;
            s.opdescSeen[at] = true;
        }
    }

    // Walks a PICA command list like the GPU does: each command is a first
    // parameter, a header (register, byte mask, extra parameter count,
    // consecutive flag), the extra parameters, padded to 8 bytes. A write to
    // CMDBUF_JUMP0/1 continues at CMDBUF_ADDR0/1 (the rest of the current
    // buffer is dropped). Returns the number of words walked.
    // Which registers the shadow follows (null: all). 0 = ignored, 1 = only
    // its final value matters, 2 = every write matters (data ports, jumps,
    // draws). Filled once at start from a dry run of our own draw, so a
    // command writing an ignored register (most of the game's words: float
    // uniforms, LUTs, vertex data) is skipped by its header alone.
    static u8 s_track[0x400];

    static u32 Walk(const State &s, const u32 *p, u32 words, bool followJumps, const u8 *track = nullptr)
    {
        u32 walked = 0, hops = 0;
        u32 i = 0;
        while (i + 1 < words)
        {
            const u32 header = p[i + 1];
            const u32 id = header & 0x3FF;
            const u32 mask = (header >> 16) & 0xF;
            const u32 extra = (header >> 20) & 0x7FF;
            const bool consecutive = (header >> 31) != 0;
            if (i + 2 + extra > words)
                break;
            s32 jump = -1;
            if (track != nullptr && !consecutive)
            {
                const u8 kind = track[id];
                if (kind == 1)
                    Apply(s, id, extra == 0 ? p[i] : p[i + 1 + extra], mask);
                else if (kind == 2)
                    for (u32 k = 0; k <= extra; k++)
                        Apply(s, id, k == 0 ? p[i] : p[i + 1 + k], mask);
                if (id == GPUREG_CMDBUF_JUMP0 || id == GPUREG_CMDBUF_JUMP1)
                    jump = (s32)(id - GPUREG_CMDBUF_JUMP0);
            }
            else
            for (u32 k = 0; k <= extra; k++)
            {
                const u32 r = consecutive ? id + k : id;
                if (track == nullptr || (r < 0x400 && track[r] != 0))
                    Apply(s, r, k == 0 ? p[i] : p[i + 1 + k], mask);
                if (r == GPUREG_CMDBUF_JUMP0 || r == GPUREG_CMDBUF_JUMP1)
                    jump = (s32)(r - GPUREG_CMDBUF_JUMP0);
            }
            i += 2 + extra;
            i += i & 1;
            if (jump >= 0 && followJumps)
            {
                const u32 pa = s.reg[GPUREG_CMDBUF_ADDR0 + jump] << 3;
                const u32 size = (s.reg[GPUREG_CMDBUF_SIZE0 + jump] & 0xFFFFF) << 3;
                const u32 va = PhysToVirt(pa);
                walked += i;
                if (va == 0 || size == 0 || ++hops > 4096)
                {
                    s_badJumps++;
                    return walked;
                }
                s_jumps++;
                p = (const u32 *)va;
                words = size / 4;
                i = 0;
            }
        }
        return walked + i;
    }

    static State Shadow(void)
    {
        return State{ s_reg, s_seen, s_code, s_codeSeen, s_opdesc, s_opdescSeen, &s_codeOffset, &s_opdescOffset };
    }

    static void ProcessListHook(u32 list, u32 size, u32 flush, u32 flags)
    {
        RecursiveLock_Lock(&s_lock);
        if (s_shadow && !s_ownPush && list != 0 && size != 0)
        {
            const u64 t0 = svcGetSystemTick();
            s_lists++;
            if (size < s_minSize) s_minSize = size;
            if (size > s_maxSize) s_maxSize = size;
            s_words += Walk(Shadow(), (const u32 *)list, size / 4, true, s_track);
            s_parseTicks += svcGetSystemTick() - t0;
        }
        HookContext::GetCurrent().OriginalFunction<void>(list, size, flush, flags);
        RecursiveLock_Unlock(&s_lock);
    }

    // ------------------------------------------------------------ resources

    static constexpr u32 TexW = 1024, TexH = 1024;
    static constexpr u32 TargetW = 240, TargetH = 400;     // a top framebuffer, rotated
    static constexpr u32 PageAlign(u32 size) { return (size + 0xFFF) & ~0xFFFu; }
    static constexpr u32 OffTex    = 0;
    static constexpr u32 AtlasBytes = TexW * TexH * 4;
    static constexpr u32 OffBack = OffTex + AtlasBytes;
    static constexpr u32 OffStage = OffBack + AtlasBytes;
    static constexpr u32 OffTarget = OffStage + AtlasBytes;
    static constexpr u32 OffList   = OffTarget + PageAlign(TargetW * TargetH * 4);
    static constexpr u32 ListWords = 0x1000;
    static constexpr u32 OffVbo    = OffList + PageAlign(ListWords * 4);
    static constexpr u32 ScratchSize = OffVbo + 0x2000;

    static u32 s_scratch = 0;
    static std::atomic<bool> s_on{false};
    static std::atomic<bool> s_hiResEnabled{true}, s_test{false};
    static Mutex s_contentLock; // CTRPF TryLock returns zero on success.
    struct Quad {
        Rect region = {}; Rect destination = {}; u32 serial = 0; bool visible = false;
        Pillarbox bands = Pillarbox::Off;
    };
    static Quad s_pending[QuadAllocator::Slots], s_active[QuadAllocator::Slots];
    static QuadAllocator s_regions;
    static Pillarbox s_bandModes[QuadAllocator::Slots] = {
        Pillarbox::Off, Pillarbox::Off, Pillarbox::Off, Pillarbox::Off };
    static u32 s_front = OffTex, s_back = OffBack;
    static bool s_pendingDirty = false, s_backSynced = true, s_activeTest = false;
    static std::atomic<u32> s_topSerial{0}, s_generation{0};
    static u32 Generation(void) { return s_generation.load(); }
    static u32 s_uploads = 0, s_contentBusy = 0;
    static u32 s_vertexQuads = 1;
    static u32 s_bandQuads = 0;
    static std::string s_why = "off";
    static u32 s_frames = 0, s_drawn = 0, s_skipped = 0, s_maxGx = 0, s_listBytes = 0, s_restored = 0;
    static u32 s_unseenRegs = 0, s_unseenCode = 0;
    static u64 s_drawTicks = 0;
    static s32 s_stepY = 0;
    static float s_squeeze = 1.0f;

    static shaderProgram_s s_program;
    static DVLB_s *s_dvlb = nullptr;
    static u32 s_shbin[(sizeof(GpuQuadShbin) + 3) / 4];

    static bool Alloc(void)
    {
        if (s_scratch != 0)
            return true;
        u32 addr = 0;
        const Result res = svcControlMemory(&addr, 0, 0, ScratchSize, MEMOP_ALLOC_LINEAR,
                                            (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
        if (R_FAILED(res) || addr == 0)
        {
            s_why = Utils::Format("no linear memory (0x%08lX)", (u32)res);
            return false;
        }
        std::memset((void *)addr, 0, ScratchSize);
        s_scratch = addr;
        return true;
    }

    // Test picture: detail that 1x can't show. Drawn row-major RGBA8 (bytes
    // A, B, G, R) into `stage`, then tiled into the texture.
    static void MakePicture(u8 *stage)
    {
        const u32 stride = TexW * 4;
        for (u32 y = 0; y < 512; y++)
            for (u32 x = 0; x < TexW; x++)
            {
                u8 *p = stage + y * stride + x * 4;
                p[0] = 0xD0; p[1] = 0x50; p[2] = 0x28; p[3] = 0x18;     // dark blue, alpha 0xD0
            }

        Surface surface;
        surface.origin = stage;
        surface.stepX = 4;
        surface.stepY = (s32)stride;
        surface.format = PixelFormat::RGBA8;
        surface.width = TexW;
        surface.height = 512;
        Canvas canvas(surface, 0, 0);

        // Frame and orientation marks.
        canvas.StrokeRect(Rect{ 0, 0, (s32)TexW, (s32)512 }, Rgb(255, 255, 255), 4);
        canvas.FillRect(Rect{ 8, 8, 64, 64 }, Rgb(230, 40, 40));          // red: top left
        canvas.FillRect(Rect{ (s32)TexW - 72, (s32)512 - 72, 64, 64 }, Rgb(40, 200, 60)); // green: bottom right

        // 1-texel checkerboard, then 1/2/3/4-texel line pairs.
        for (s32 y = 90; y < 250; y++)
            for (s32 x = 16; x < 336; x++)
                if (((x ^ y) & 1) == 0)
                    canvas.FillRect(Rect{ x, y, 1, 1 }, Rgb(255, 255, 255));
                else
                    canvas.FillRect(Rect{ x, y, 1, 1 }, Rgb(0, 0, 0));
        s32 x = 352;
        for (s32 w = 1; w <= 4; w++)
        {
            for (s32 k = 0; k < 10; k++, x += 2 * w)
                canvas.FillRect(Rect{ x, 90, w, 160 }, Rgb(255, 255, 255));
            x += 12;
        }
        // Diagonal hairlines (aliasing shows at 1x).
        for (s32 k = 0; k < 8; k++)
            canvas.Line(16.0f + 40 * k, 500.0f, 136.0f + 40 * k, 270.0f, 1.0f, Rgb(255, 220, 80));

        // Alpha ramp: left transparent, right opaque.
        for (u32 yy = 420; yy < 470; yy++)
            for (u32 xx = 360; xx < 1000; xx++)
            {
                u8 *p = stage + yy * stride + xx * 4;
                p[0] = (u8)((xx - 360) * 255 / 639); p[1] = 0xFF; p[2] = 0xFF; p[3] = 0xFF;
            }
    }

    // Row-major picture (row 0 = top) -> PICA tiled texture. The first row
    // in texture memory is the top (t = 1).
    static void Tile(const u8 *stage, u8 *tex)
    {
        const Surface pixels{ const_cast<u8 *>(stage), 4, (s32)TexW * 4, PixelFormat::RGBA8, (s32)TexW, (s32)TexH };
        TileQuadRegion(pixels, Rect{ 0, 0, (s32)TexW, (s32)TexH }, tex, TexW, TexH);
    }

    static bool Prepare(void)
    {
        return s_dvlb != nullptr && Alloc();
    }

    bool HiResReady(void)
    {
        return s_on.load() && s_hiResEnabled.load() && !s_test.load() && s_hooked && s_dtHooked
            && s_inTarget && s_shadow && s_restore && s_scratch != 0;
    }

    bool UpdateGpuQuad(u32 slot, const Surface &source, const Rect &region, const Rect &destination, bool dirty)
    {
        if (!HiResReady() || source.origin == nullptr || slot >= QuadAllocator::Slots || region.Empty() || destination.Empty()
            || region.x < 0 || region.y < 0 || region.Right() > QuadAllocator::Width || region.Bottom() > (s32)TexH
            || source.width != region.w || source.height != region.h || source.format != PixelFormat::RGBA8)
            return false;
        if (s_contentLock.TryLock()) return false;
        // Generic publishers must obey the same disjoint-region contract.
        const Rect padded{ region.x - 1, region.y - 1, region.w + 2, region.h + 2 };
        for (u32 i = 0; i < QuadAllocator::Slots; i++)
            if (i != slot && s_pending[i].visible && s_topSerial.load() - s_pending[i].serial <= 2
                && !Intersect(padded, s_pending[i].region).Empty())
            { s_contentLock.Unlock(); return false; }
        if (!dirty && (!s_pending[slot].visible
            || region.x != s_pending[slot].region.x || region.y != s_pending[slot].region.y
            || region.w != s_pending[slot].region.w || region.h != s_pending[slot].region.h))
        { s_contentLock.Unlock(); return false; }
        if (dirty)
        {
            // Never modify the atlas being sampled by the hook. After a swap,
            // seed the back atlas once so unchanged slots survive the update.
            if (!s_backSynced)
            {
                std::memcpy((void *)(s_scratch + s_back), (const void *)(s_scratch + s_front), AtlasBytes);
                s_backSynced = true;
            }
            TileQuadRegion(source, region, (u8 *)(s_scratch + s_back), TexW, TexH);
            ClearQuadBorder(region, (u8 *)(s_scratch + s_back), TexW, TexH);
            // The twelve legacy column phases, reserved outside all layer
            // allocations. Re-seed on dirty publication, including after a test.
            static u8 bandPixels[12 * ScreenH * 4];
            u8 column[ScreenH * 4];
            for (int phase = 0; phase < 12; phase++)
            {
                WriteBandColumn(column, phase, PixelFormat::RGBA8, false);
                for (int y = 0; y < ScreenH; y++)
                    std::memcpy(bandPixels + (y * 12 + phase) * 4, column + y * 4, 4);
            }
            const Surface bands{ bandPixels, 4, 12 * 4, PixelFormat::RGBA8, 12, ScreenH };
            TileQuadRegion(bands, Rect{ 1012, 0, 12, ScreenH }, (u8 *)(s_scratch + s_back), TexW, TexH);
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_scratch + s_back, AtlasBytes);
            s_pendingDirty = true;
            s_uploads++;
        }
        s_pending[slot] = Quad{ region, destination, s_topSerial.load(), true, s_bandModes[slot] };
        s_contentLock.Unlock();
        return true;
    }

    void HideGpuQuad(u32 slot)
    {
        if (slot >= QuadAllocator::Slots || s_contentLock.TryLock()) return;
        s_pending[slot].visible = false;
        s_regions.Release(slot);
        s_contentLock.Unlock();
    }

    static bool SetupLayer(u32 slot, s32 width, s32 height, Surface &pixels)
    {
        if (!HiResReady() || slot >= QuadAllocator::Slots || s_contentLock.TryLock()) return false;
        for (u32 i = 0; i < QuadAllocator::Slots; i++)
            if (s_topSerial.load() - s_pending[i].serial > 2) s_regions.Release(i);
        s_pending[slot].visible = false; // no old UVs may reference the replacement
        const bool fits = s_regions.Allocate(slot, width, height);
        const Rect region = s_regions.regions[slot];
        s_contentLock.Unlock();
        if (!fits) return false;
        pixels = Surface{ (u8 *)(s_scratch + OffStage + (region.y * TexW + region.x) * 4), 4, (s32)TexW * 4,
                          PixelFormat::RGBA8, width, height };
        pixels.transparent = true;
        return true;
    }

    static bool PresentLayer(u32 slot, const Surface &pixels, const Rect &area, bool dirty)
    {
        if (slot >= QuadAllocator::Slots) return false;
        return UpdateGpuQuad(slot, pixels, s_regions.regions[slot], area, dirty);
    }

    static void LayerBands(u32 slot, const Surface &screen, const Rect &area)
    {
        if (slot < QuadAllocator::Slots)
            s_bandModes[slot] = PillarboxColumns(screen, area).active ? screen.pillarbox : Pillarbox::Off;
    }

    static bool LayerValid(u32 slot, const Surface &pixels)
    {
        if (slot >= QuadAllocator::Slots) return false;
        const Rect r = s_regions.regions[slot];
        return !r.Empty() && r.w == pixels.width && r.h == pixels.height
            && pixels.origin == (u8 *)(s_scratch + OffStage + (r.y * TexW + r.x) * 4);
    }

    // Hook-side snapshot: never wait for an uploader. Both texture address and
    // all quad metadata change together; contention keeps the complete old frame.
    static void Snapshot(void)
    {
        if (!s_contentLock.TryLock())
        {
            if (s_pendingDirty)
            {
                std::swap(s_front, s_back);
                s_pendingDirty = false;
                s_backSynced = false;
            }
            s_activeTest = s_test.load();
            std::memcpy(s_active, s_pending, sizeof(s_active));
            for (Quad &q : s_active)
                if (s_topSerial.load() - q.serial > 2) q.visible = false;
            s_contentLock.Unlock();
        }
        else s_contentBusy++;
    }

    // --------------------------------------------------------------- drawing

    // Registers never restored: triggers and data ports (writing them again
    // would redo an action, not set a value).
    static bool NoRestore(u32 id)
    {
        switch (id)
        {
        case GPUREG_FINALIZE:
        case GPUREG_EARLYDEPTH_CLEAR:
        case GPUREG_FRAMEBUFFER_INVALIDATE:
        case GPUREG_FRAMEBUFFER_FLUSH:
        case GPUREG_DRAWARRAYS:
        case GPUREG_DRAWELEMENTS:
        case GPUREG_VTX_FUNC:
        case GPUREG_RESTART_PRIMITIVE:
        case GPUREG_VSH_CODETRANSFER_END:
        case GPUREG_GSH_CODETRANSFER_END:
        case GPUREG_VSH_CODETRANSFER_CONFIG:
        case GPUREG_VSH_OPDESCS_CONFIG:
        case GPUREG_FOG_LUT_INDEX:
        case GPUREG_LIGHTING_LUT_INDEX:
        case GPUREG_PROCTEX_LUT:
        case GPUREG_GAS_LUT_INDEX:
        case GPUREG_GAS_LUT_DATA:
        case GPUREG_VSH_FLOATUNIFORM_CONFIG:
        case GPUREG_GSH_FLOATUNIFORM_CONFIG:
        case GPUREG_FIXEDATTRIB_INDEX:
            return true;
        default:
            break;
        }
        return (id >= GPUREG_FIXEDATTRIB_DATA0 && id <= GPUREG_FIXEDATTRIB_DATA2)
            || (id >= GPUREG_CMDBUF_SIZE0 && id <= GPUREG_CMDBUF_JUMP1)
            || (id >= GPUREG_PROCTEX_LUT_DATA0 && id <= GPUREG_PROCTEX_LUT_DATA7)
            || (id >= GPUREG_FOG_LUT_DATA0 && id <= GPUREG_FOG_LUT_DATA7)
            || (id >= GPUREG_LIGHTING_LUT_DATA0 && id <= GPUREG_LIGHTING_LUT_DATA7)
            || (id >= GPUREG_VSH_FLOATUNIFORM_DATA && id < GPUREG_VSH_FLOATUNIFORM_DATA + 8)
            || (id >= GPUREG_GSH_FLOATUNIFORM_DATA && id < GPUREG_GSH_FLOATUNIFORM_DATA + 8)
            || (id >= GPUREG_VSH_CODETRANSFER_DATA && id < GPUREG_VSH_CODETRANSFER_DATA + 8)
            || (id >= GPUREG_VSH_OPDESCS_DATA && id < GPUREG_VSH_OPDESCS_DATA + 8)
            || (id >= GPUREG_GSH_CODETRANSFER_CONFIG && id <= GPUREG_GSH_OPDESCS_DATA + 7);
    }

    static void WriteVertices(void)
    {
        float *v = (float *)(s_scratch + OffVbo);
        s_vertexQuads = 0;
        bool overflow = false;
        const auto write = [&](const Rect &r, const Rect &tex, float squeeze) {
            // Also guard arbitrary public publishers, whose destinations may
            // request several overlapping full-screen sets of bands.
            if (s_vertexQuads >= 0x2000 / (24 * sizeof(float)))
            { overflow = true; return; }
            QuadVertices(v, r, tex, TexW, TexH, squeeze, s_stepY < 0);
            v += 24;
            s_vertexQuads++;
        };
        s_bandQuads = 0;
        if (s_activeTest) write(Rect{ 0, 20, 400, 200 }, Rect{ 0, 0, (s32)TexW, 512 }, s_squeeze);
        else if (HiResReady())
        {
            // Destination-coordinate bands are deliberately not squeezed again.
            // Pattern runs use all eight phases then repeat; tint stretches one
            // phase. The three edge columns retain phases 9,10,11 exactly.
            for (const Quad &q : s_active)
            {
                if (!q.visible || q.bands == Pillarbox::Off) continue;
                Surface screen{};
                screen.width = ScreenW; screen.height = ScreenH;
                screen.squeeze = s_squeeze; screen.pillarbox = q.bands;
                const ColumnRange range = PillarboxColumns(screen, q.destination);
                if (!range.active) continue;
                BandRuns(range, q.bands, [&](int x, int width, int phase) {
                    write(Rect{ x, 0, width, ScreenH },
                          Rect{ 1012 + phase, 0, phase < 8 ? width : 1, ScreenH }, 1.f);
                });
            }
            s_bandQuads = s_vertexQuads;
            for (const Quad &q : s_active)
                if (q.visible) write(q.destination, q.region, s_squeeze);
        }
        if (overflow) s_vertexQuads = 0; // never submit a partial/overrun list
    }

    static u32 Pa(u32 va)
    {
        return osConvertVirtToPhys((const void *)va);
    }

    // Our state and the draw. Everything it writes is restored afterwards.
    // The viewport covers the visible 240x400 of the target; a bigger target
    // (the game's own, 256x512, cropped by its DisplayTransfer) keeps the
    // visible rows at the start of memory, which is window y = targetH - 400
    // (window y runs bottom to top, memory rows top to bottom).
    static void EmitDraw(u32 target = 0, u32 targetW = TargetW, u32 targetH = TargetH, bool rgb8 = false)
    {
        const u32 viewY = targetH - TargetH;
        const u32 tex = Pa(s_scratch + s_front), vbo = Pa(s_scratch + OffVbo);

        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);

        // Render target: tiled RGBA8 (or RGB8) 240x400, no depth buffer.
        const u32 dim = targetW | ((targetH - 1) << 12) | (1 << 24);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_FORMAT, rgb8 ? (1 | (GPU_RB_RGB8 << 16)) : (2 | (GPU_RB_RGBA8 << 16)));
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_LOC, 0);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_LOC, target >> 3);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_DIM, dim);
        GPUCMD_AddWrite(GPUREG_RENDERBUF_DIM, dim);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_BLOCK32, 0);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_READ, 0xF);
        GPUCMD_AddWrite(GPUREG_COLORBUFFER_WRITE, 0xF);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_READ, 0);
        GPUCMD_AddWrite(GPUREG_DEPTHBUFFER_WRITE, 0);

        // Viewport, scissor, culling, clipping, early depth.
        GPUCMD_AddWrite(GPUREG_VIEWPORT_WIDTH, f32tof24(TargetW / 2.0f));
        GPUCMD_AddWrite(GPUREG_VIEWPORT_INVW, f32tof31(2.0f / TargetW) << 1);
        GPUCMD_AddWrite(GPUREG_VIEWPORT_HEIGHT, f32tof24(TargetH / 2.0f));
        GPUCMD_AddWrite(GPUREG_VIEWPORT_INVH, f32tof31(2.0f / TargetH) << 1);
        GPUCMD_AddWrite(GPUREG_VIEWPORT_XY, viewY << 16);
        GPUCMD_AddWrite(GPUREG_SCISSORTEST_MODE, 0);
        GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, 0);
        GPUCMD_AddWrite(GPUREG_FRAGOP_CLIP, 0);
        GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST1, 0);
        GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 0);

        // Fragment operations: alpha blend, no tests.
        GPUCMD_AddWrite(GPUREG_COLOR_OPERATION, 0x00E40100);
        GPUCMD_AddWrite(GPUREG_BLEND_FUNC, GPU_BLEND_ADD | (GPU_BLEND_ADD << 8) | (GPU_SRC_ALPHA << 16)
                                           | (GPU_ONE_MINUS_SRC_ALPHA << 20) | (GPU_ONE << 24)
                                           | (GPU_ONE_MINUS_SRC_ALPHA << 28));
        GPUCMD_AddWrite(GPUREG_FRAGOP_ALPHA_TEST, 0);
        GPUCMD_AddWrite(GPUREG_STENCIL_TEST, 0);
        GPUCMD_AddWrite(GPUREG_STENCIL_OP, 0);
        GPUCMD_AddWrite(GPUREG_DEPTH_COLOR_MASK, (GPU_ALWAYS << 4) | (0xF << 8));

        // Texture unit 0: shared RGBA8 1024x1024 atlas, bilinear, clamped.
        GPUCMD_AddWrite(GPUREG_TEXUNIT_CONFIG, 0x00011001);
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_BORDER_COLOR, 0);
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_DIM, TexH | (TexW << 16));
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM, GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
                                               | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE));
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_LOD, 0);
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_ADDR1, tex >> 3);
        GPUCMD_AddWrite(GPUREG_TEXUNIT0_TYPE, GPU_RGBA8);

        // Texture combiners: stage 0 = texture 0, stages 1-5 pass through.
        static const u16 stages[6] = { GPUREG_TEXENV0_SOURCE, GPUREG_TEXENV1_SOURCE, GPUREG_TEXENV2_SOURCE,
                                       GPUREG_TEXENV3_SOURCE, GPUREG_TEXENV4_SOURCE, GPUREG_TEXENV5_SOURCE };
        for (int i = 0; i < 6; i++)
        {
            const u32 src = i == 0 ? GPU_TEXTURE0 : GPU_PREVIOUS;
            const u32 env[5] = { src | (src << 16), 0, GPU_REPLACE | (GPU_REPLACE << 16), 0, 0 };
            GPUCMD_AddIncrementalWrites(stages[i], env, 5);
        }
        GPUCMD_AddWrite(GPUREG_TEXENV_UPDATE_BUFFER, 0);
        GPUCMD_AddWrite(GPUREG_LIGHTING_ENABLE0, 0);

        // Shader (code, operand descriptors, entry point, output map).
        shaderProgramUse(&s_program);

        // Vertex input: one buffer, attribute 0 = 4 floats, 1 = 2 floats.
        u32 attrib[3 + 12 * 3] = {};
        attrib[0] = vbo >> 3;
        attrib[1] = GPU_ATTRIBFMT(0, 4, GPU_FLOAT) | GPU_ATTRIBFMT(1, 2, GPU_FLOAT);
        attrib[2] = (2 - 1) << 28;
        attrib[3] = 0;                          // buffer 0 offset
        attrib[4] = 0x10;                       // attributes 0, 1
        attrib[5] = (24 << 16) | (2 << 28);     // stride 24, 2 attributes
        GPUCMD_AddIncrementalWrites(GPUREG_ATTRIBBUFFERS_LOC, attrib, 3 + 12 * 3);
        GPUCMD_AddWrite(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xA0000000 | (2 - 1));
        GPUCMD_AddWrite(GPUREG_VSH_NUM_ATTR, 2 - 1);
        const u32 permutation[2] = { 0x10, 0 };
        GPUCMD_AddIncrementalWrites(GPUREG_VSH_ATTRIBUTES_PERMUTATION_LOW, permutation, 2);

        // Draw 4 vertices as a triangle strip (citro3d's C3D_DrawArrays).
        for (u32 quad = 0; quad < s_vertexQuads; quad++)
        {
        if (quad == 0 || quad == s_bandQuads)
            GPUCMD_AddWrite(GPUREG_TEXUNIT0_PARAM,
                GPU_TEXTURE_MAG_FILTER(quad < s_bandQuads ? GPU_NEAREST : GPU_LINEAR)
                | GPU_TEXTURE_MIN_FILTER(quad < s_bandQuads ? GPU_NEAREST : GPU_LINEAR)
                | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE));
        GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 2, GPU_TRIANGLE_STRIP);
        GPUCMD_AddWrite(GPUREG_RESTART_PRIMITIVE, 1);
        GPUCMD_AddWrite(GPUREG_INDEXBUFFER_CONFIG, 0x80000000);
        GPUCMD_AddWrite(GPUREG_NUMVERTICES, 4);
        GPUCMD_AddWrite(GPUREG_VERTEX_OFFSET, quad * 4);
        GPUCMD_AddMaskedWrite(GPUREG_GEOSTAGE_CONFIG2, 1, 0);
        GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 0);
        GPUCMD_AddWrite(GPUREG_DRAWARRAYS, 1);
        GPUCMD_AddMaskedWrite(GPUREG_START_DRAW_FUNC0, 1, 1);
        GPUCMD_AddWrite(GPUREG_VTX_FUNC, 1);
        }

        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 1);
        GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 1);
    }

    static std::string s_unseenList;

    // What our draw touched (filled by walking our own list).
    static u32 s_tReg[RegCount];
    static u8  s_tSeen[RegCount];
    static u32 s_tCode[512];
    static bool s_tCodeSeen[512];
    static u32 s_tOpdesc[128];
    static bool s_tOpdescSeen[128];
    static u32 s_tCodeOffset, s_tOpdescOffset;

    // Writes the game's values back for everything the draw touched.
    // Returns false if a touched value was never seen in the game's lists.
    static bool EmitRestore(u32 &count)
    {
        bool complete = true;
        count = 0;
        for (u32 id = 0; id < RegCount; id++)
        {
            if (s_tSeen[id] == 0 || NoRestore(id))
                continue;
            if (s_seen[id] != 0xF)
            {
                if (s_unseenRegs < 64 || s_unseenList.size() < 200)
                {
                    const std::string item = Utils::Format(" %03lX/%X", id, s_seen[id]);
                    if (s_unseenList.find(item) == std::string::npos)
                        s_unseenList += item;
                }
                // Bytes the game never wrote: it doesn't rely on them, so
                // they go back as 0 (the shadow's start value). Seen in the
                // field: masked writes (viewport floats, flags) and the
                // integer uniforms I1-I3 / GSH_MISC1, which only our
                // shaderProgramUse writes.
                s_unseenRegs++;
            }
            GPUCMD_AddWrite(id, s_reg[id]);
            count++;
        }
        for (u32 i = 0; i < 512;)
        {
            if (!s_tCodeSeen[i])
            {
                i++;
                continue;
            }
            u32 n = 0;
            while (i + n < 512 && s_tCodeSeen[i + n])
                n++;
            for (u32 k = 0; k < n; k++)
                if (!s_codeSeen[i + k])
                {
                    s_unseenCode++;
                    complete = false;
                }
            GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_CONFIG, i);
            GPUCMD_Add(GPUCMD_HEADER(0, 0xF, GPUREG_VSH_CODETRANSFER_DATA), s_code + i, n);
            GPUCMD_AddWrite(GPUREG_VSH_CODETRANSFER_END, 1);
            count += n;
            i += n;
        }
        for (u32 i = 0; i < 128;)
        {
            if (!s_tOpdescSeen[i])
            {
                i++;
                continue;
            }
            u32 n = 0;
            while (i + n < 128 && s_tOpdescSeen[i + n])
                n++;
            for (u32 k = 0; k < n; k++)
                if (!s_opdescSeen[i + k])
                {
                    s_unseenCode++;
                    complete = false;
                }
            GPUCMD_AddWrite(GPUREG_VSH_OPDESCS_CONFIG, i);
            GPUCMD_Add(GPUCMD_HEADER(0, 0xF, GPUREG_VSH_OPDESCS_DATA), s_opdesc + i, n);
            count += n;
            i += n;
        }
        return complete;
    }

    static bool QueueRoom(void)
    {
        const u32 gx = *(volatile u32 *)(0x69EA68 + 0x1C);
        if (gx == 0)
            return false;
        const u32 queue = *(volatile u32 *)(gx + 0x58);
        if (queue == 0)
            return false;
        return *(volatile u8 *)(queue + 1) < 10;
    }

    // Builds our list (state, draw, restore) at s_scratch + OffList for a
    // tiled target. Call with s_lock held. Returns its size in words, 0 if
    // the game's state isn't known well enough to restore.
    static u32 BuildList(u32 targetPa, u32 targetW, u32 targetH, bool rgb8, u32 &restored)
    {
        const u32 list = s_scratch + OffList;
        WriteVertices();
        if (s_vertexQuads == 0) return 0;
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_scratch + OffVbo, s_vertexQuads * 24 * sizeof(float));

        // Our draw, walked into a blank register file to learn what it touches.
        GPUCMD_SetBuffer((u32 *)list, ListWords, 0);
        EmitDraw(targetPa, targetW, targetH, rgb8);
        u32 *buf;
        u32 size, drawWords;
        GPUCMD_GetBuffer(&buf, &size, &drawWords);
        std::memset(s_tSeen, 0, sizeof(s_tSeen));
        std::memset(s_tCodeSeen, 0, sizeof(s_tCodeSeen));
        std::memset(s_tOpdescSeen, 0, sizeof(s_tOpdescSeen));
        s_tCodeOffset = s_tOpdescOffset = 0;
        const State touched{ s_tReg, s_tSeen, s_tCode, s_tCodeSeen, s_tOpdesc, s_tOpdescSeen, &s_tCodeOffset, &s_tOpdescOffset };
        Walk(touched, (const u32 *)list, drawWords, false);

        restored = 0;
        const bool complete = !s_restore || EmitRestore(restored);
        // Pad to 16 bytes with a harmless repeat of a restored value.
        u32 words;
        GPUCMD_GetBuffer(&buf, &size, &words);
        if (words & 3)
            GPUCMD_AddWrite(GPUREG_TEXENV0_COLOR, s_reg[GPUREG_TEXENV0_COLOR]);
        GPUCMD_GetBuffer(&buf, &size, &words);
        if (!complete)
        {
            s_skipped++;
            s_why = "game state not fully seen yet (unseen registers or shader words)";
            return 0;
        }
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, list, words * 4);
        return words;
    }

    // Pushes the list built by BuildList. Call with s_lock held.
    static void PushList(u32 words)
    {
        s_ownPush = true;
        s_processList(s_scratch + OffList, words * 4, 0, 0);
        s_ownPush = false;
        // Watch what the game sets before its next draw (diagnostic).
        for (u32 r = 0; r < RegCount; r++)
            s_dirty[r] = s_tSeen[r] != 0 && !NoRestore(r);
        std::memcpy(s_dirtyCode, s_tCodeSeen, sizeof(s_dirtyCode));
        std::memcpy(s_dirtyOpdesc, s_tOpdescSeen, sizeof(s_dirtyOpdesc));
        s_watching = true;
    }

    static void Done(u64 t0, u32 words, u32 restored, u32 gx)
    {
        s_drawn++;
        s_listBytes = words * 4;
        s_restored = restored;
        if (gx > s_maxGx)
            s_maxGx = gx;
        s_drawTicks += svcGetSystemTick() - t0;
        s_why = "drawing";
    }

    static void UpdateSqueeze(void)
    {
        s_squeeze = WidescreenSqueeze();
        if (!(s_squeeze >= 1.0f))
            s_squeeze = 1.0f;
    }

    // Mode "fb" (from the OSD callback): framebuffer -> our tiled target ->
    // draw -> framebuffer. Three GX commands.
    bool GpuQuadOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
            return false;
        s_frames++;
        if (!s_on.load() || !s_test.load() || s_inTarget || s_scratch == 0 || s_displayTransfer == nullptr)
            return false;
        u8 *const origin = screen.GetFramebuffer(0, 0);
        const s32 stepX = (s32)(screen.GetFramebuffer(1, 0) - origin);
        const s32 stepY = (s32)(screen.GetFramebuffer(0, 1) - origin);
        if ((screen.Format & 7) != GSP_BGR8_OES || stepX != (s32)TargetW * 3 || (stepY != 3 && stepY != -3))
        {
            s_skipped++;
            s_why = Utils::Format("framebuffer not BGR8 columns (format %lu, steps %ld %ld)", (u32)screen.Format, stepX, stepY);
            return false;
        }
        if (!QueueRoom())
        {
            s_skipped++;
            s_why = "GX queue busy";
            return false;
        }
        const u64 t0 = svcGetSystemTick();
        s_stepY = stepY;
        Snapshot();
        UpdateSqueeze();
        const u32 fb = (u32)(origin + (stepY < 0 ? ((s32)TargetW - 1) * stepY : 0));
        const u32 target = s_scratch + OffTarget;

        RecursiveLock_Lock(&s_lock);
        u32 restored;
        const u32 words = BuildList(Pa(target), TargetW, TargetH, false, restored);
        if (words == 0)
        {
            RecursiveLock_Unlock(&s_lock);
            return false;
        }
        // 1. Game picture -> tiled target (GPU, keeps the internal scale).
        s_ownPush = true;
        s_displayTransfer(fb, TargetW, TargetH, target, TargetW, TargetH,
                          (1 << 1) | (GSP_BGR8_OES << 8) | (GSP_RGBA8_OES << 12));
        s_ownPush = false;
        // 2. The quad, then the game's state back.
        PushList(words);
        // 3. Target -> framebuffer.
        s_ownPush = true;
        s_displayTransfer(target, TargetW, TargetH, fb, TargetW, TargetH,
                          (GSP_RGBA8_OES << 8) | (GSP_BGR8_OES << 12));
        s_ownPush = false;
        RecursiveLock_Unlock(&s_lock);
        Done(t0, words, restored, 3);
        return false;
    }

    // Mode "target": hooks the game's own DisplayTransfer (nngx request type
    // 3, called from 0x54A644) and, when it copies the game's tiled
    // top-screen render target to the top framebuffer, draws into that
    // render target first. One GX command (the list), no extra transfers.
    struct DtSeen
    {
        u32 inW, inH, outW, outH, flags, count;
    };
    static DtSeen s_dtSeen[8];
    static u32 s_dtSeenCount = 0;

    static void DisplayTransferHook(u32 in, u32 inW, u32 inH, u32 out, u32 outW, u32 outH, u32 flags)
    {
        if (!s_ownPush)
        {
            u32 k = 0;
            while (k < s_dtSeenCount && !(s_dtSeen[k].inW == inW && s_dtSeen[k].inH == inH && s_dtSeen[k].outW == outW
                                          && s_dtSeen[k].outH == outH && s_dtSeen[k].flags == flags))
                k++;
            if (k == s_dtSeenCount && k < 8)
            {
                s_dtSeen[k] = DtSeen{ inW, inH, outW, outH, flags, 0 };
                s_dtSeenCount++;
            }
            if (k < 8)
                s_dtSeen[k].count++;
        }
        const u32 inFormat = (flags >> 8) & 7;
        if ((s_test.load() || HiResReady()) && s_on.load() && s_inTarget && !s_ownPush && s_scratch != 0 && (flags & ((1 << 1) | (1 << 3) | (1 << 5))) == 0
            && outW == TargetW && outH == TargetH && inW >= outW && inH >= outH
            && ((inW == outW && inH == outH) || (flags & (1 << 2)) != 0)
            && (inFormat == GSP_RGBA8_OES || inFormat == GSP_BGR8_OES) && ((flags >> 24) & 3) == 0)
        {
            const u64 t0 = svcGetSystemTick();
            s_topSerial.fetch_add(1);
            Snapshot();
            UpdateSqueeze();
            s_stepY = -3;
            RecursiveLock_Lock(&s_lock);
            u32 restored;
            const u32 words = BuildList(Pa(in), inW, inH, inFormat == GSP_BGR8_OES, restored);
            if (words != 0)
            {
                PushList(words);
                Done(t0, words, restored, 1);
            }
            RecursiveLock_Unlock(&s_lock);
        }
        HookContext::GetCurrent().OriginalFunction<void>(in, inW, inH, out, outW, outH, flags);
    }

    std::string StartGpuQuad(u32 displayTransfer)
    {
        s_displayTransfer = (DisplayTransferFn)displayTransfer;
        RecursiveLock_Init(&s_lock);
        std::memcpy(s_shbin, GpuQuadShbin, sizeof(GpuQuadShbin));
        s_dvlb = DVLB_ParseFile(s_shbin, sizeof(GpuQuadShbin));
        if (s_dvlb == nullptr)
            return "gpuquad: DVLB_ParseFile failed, not hooked";
        shaderProgramInit(&s_program);
        shaderProgramSetVsh(&s_program, &s_dvlb->DVLE[0]);

        // Dry run of our draw (addresses don't matter) to learn which
        // registers the shadow must follow.
        static u32 dry[ListWords];
        GPUCMD_SetBuffer(dry, ListWords, 0);
        EmitDraw();
        u32 *buf, size, words;
        GPUCMD_GetBuffer(&buf, &size, &words);
        const State touched{ s_tReg, s_tSeen, s_tCode, s_tCodeSeen, s_tOpdesc, s_tOpdescSeen, &s_tCodeOffset, &s_tOpdescOffset };
        Walk(touched, dry, words, false);
        u32 tracked = 0;
        for (u32 r = 0; r < RegCount; r++)
            if (s_tSeen[r] != 0 && !NoRestore(r))
            {
                s_track[r] = 1;
                tracked++;
            }
        s_track[GPUREG_CMDBUF_SIZE0] = s_track[GPUREG_CMDBUF_SIZE1] = 1;
        s_track[GPUREG_CMDBUF_ADDR0] = s_track[GPUREG_CMDBUF_ADDR1] = 1;
        s_track[GPUREG_CMDBUF_JUMP0] = s_track[GPUREG_CMDBUF_JUMP1] = 2;
        s_track[GPUREG_DRAWARRAYS] = s_track[GPUREG_DRAWELEMENTS] = 2;
        s_track[GPUREG_VSH_CODETRANSFER_CONFIG] = s_track[GPUREG_VSH_OPDESCS_CONFIG] = 1;
        for (u32 k = 0; k < 8; k++)
            s_track[GPUREG_VSH_CODETRANSFER_DATA + k] = s_track[GPUREG_VSH_OPDESCS_DATA + k] = 2;
        const u32 *code = (const u32 *)ProcessListAddress;
        for (int i = 0; i < 3; i++)
            if (code[i] != ProcessListPrologue[i])
                return Utils::Format("gpuquad: ProcessCommandList prologue mismatch at %08lX (%08lX), not hooked",
                                     ProcessListAddress + i * 4, code[i]);
        s_hook.InitializeForMitm(ProcessListAddress, (u32)ProcessListHook);
        const HookResult result = s_hook.Enable();
        s_hooked = result == HookResult::Success;
        s_dtHook.InitializeForMitm(displayTransfer, (u32)DisplayTransferHook);
        const HookResult dtResult = s_dtHook.Enable();
        s_dtHooked = dtResult == HookResult::Success;
        s_inTarget = s_dtHooked;
        s_on = s_hooked && s_dtHooked && Prepare();
        if (s_on.load()) s_why = "backend ready; awaiting published quads";
        Layer::s_hiRes = HiResBackend{ HiResReady, SetupLayer, PresentLayer, HideGpuQuad, Generation, LayerBands, LayerValid };
        return Utils::Format("gpuquad: ProcessCommandList hook at %08lX -> %d, following %lu registers; DisplayTransfer hook at %08lX -> %d",
                             ProcessListAddress, (int)result, tracked, displayTransfer, (int)dtResult);
    }

    static std::string Status(void)
    {
        u32 regsSeen = 0, codeSeen = 0;
        for (u32 i = 0; i < RegCount; i++)
            regsSeen += s_seen[i] != 0;
        for (u32 i = 0; i < 512; i++)
            codeSeen += s_codeSeen[i];
        const auto us = [](u64 ticks) { return (u32)(ticks * 1000000 / SYSCLOCK_ARM11); };
        std::string line = Utils::Format("gpuquad: %s, mode %s, restore %s, shadow %s, hook %s, lists %lu (size %lu-%lu B), words %lu, jumps %lu (bad %lu), parse %lu us total",
                                         s_on.load() ? "on" : "off", s_inTarget ? "target" : "fb", s_restore ? "on" : "off", s_shadow ? "on" : "off", s_hooked ? "ok" : "MISSING", s_lists,
                                         s_minSize == 0xFFFFFFFF ? 0 : s_minSize, s_maxSize, (u32)s_words, s_jumps,
                                         s_badJumps, us(s_parseTicks));
        line += Utils::Format("\ngpuquad: regs seen %lu, vs code words seen %lu/512, drawn %lu, skipped %lu, GX per frame %lu, list %lu B, restored %lu",
                              regsSeen, codeSeen, s_drawn, s_skipped, s_maxGx, s_listBytes, s_restored);
        line += Utils::Format("\ngpuquad: partly-seen regs %lu, unseen code %lu, draw %lu us total, stepY %ld, squeeze %.3f, scratch %08lX",
                              s_unseenRegs, s_unseenCode, us(s_drawTicks), s_stepY, (double)s_squeeze, s_scratch);
        for (u32 k = 0; k < s_dtSeenCount; k++)
            line += Utils::Format("\ngpuquad: game DisplayTransfer %lux%lu -> %lux%lu flags %08lX x%lu",
                                  s_dtSeen[k].inW, s_dtSeen[k].inH, s_dtSeen[k].outW, s_dtSeen[k].outH,
                                  s_dtSeen[k].flags, s_dtSeen[k].count);
        line += Utils::Format("\ngpuquad: game state now: colorbuffer %08lX format %08lX dim %08lX, viewport w %06lX h %06lX xy %08lX, scissor %lX",
                              s_reg[GPUREG_COLORBUFFER_LOC] << 3, s_reg[GPUREG_COLORBUFFER_FORMAT], s_reg[GPUREG_FRAMEBUFFER_DIM],
                              s_reg[GPUREG_VIEWPORT_WIDTH], s_reg[GPUREG_VIEWPORT_HEIGHT], s_reg[GPUREG_VIEWPORT_XY],
                              s_reg[GPUREG_SCISSORTEST_MODE]);
        if (s_frames != 0)
            line += Utils::Format("\ngpuquad: per top frame (%lu frames): %lu lists, %lu words, %lu jumps, parse %lu us; draw %lu us",
                                  s_frames, s_lists / s_frames, (u32)(s_words / s_frames), s_jumps / s_frames,
                                  us(s_parseTicks) / s_frames, s_drawn ? us(s_drawTicks) / s_drawn : 0);
        std::string stale;
        for (u32 r = 0; r < RegCount; r++)
            if (s_stale[r])
                stale += Utils::Format(" %03lX", r);
        line += Utils::Format("\ngpuquad: next-draw checks %lu, with stale state %lu, stale shader memory %lu, stale regs:",
                              s_checks, s_staleChecks, s_staleCode) + (stale.empty() ? std::string(" none") : stale);
        line += Utils::Format("\ngpuquad: hires %s, test %s, uploads %lu, snapshot busy %lu, quads %lu, linear %lu bytes",
            HiResReady() ? "ready" : "off/unavailable", s_test.load() ? "on" : "off", s_uploads,
            s_contentBusy, s_vertexQuads, ScratchSize);
        line += "\ngpuquad: " + s_why;
        if (!s_unseenList.empty())
            line += "\ngpuquad: touched but not fully seen:" + s_unseenList;
        return line;
    }

    std::string HiResCommand(const std::string &args)
    {
        if (args == "on") { s_hiResEnabled = true; s_on = s_hooked && s_dtHooked && s_scratch != 0; }
        else if (args == "off") s_hiResEnabled = false;
        else if (args != "status") return "hires: use on|off|status";
        return Status();
    }

    std::string GpuQuadCommand(const std::string &args)
    {
        if (args == "test on" || args == "test off")
        {
            if (args == "test on" && (!s_hooked || !Prepare())) return "gpuquad: backend unavailable";
            // Plugin thread may wait for a short snapshot, never vice versa.
            Lock lock(s_contentLock);
            s_generation.fetch_add(1);
            s_test = false;
            if (args == "test on")
            {
                u32 stage = 0;
                const Result result = svcControlMemory(&stage, 0, 0, AtlasBytes, MEMOP_ALLOC_LINEAR,
                                                       (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
                if (R_FAILED(result) || stage == 0) return "gpuquad: no linear memory for test picture";
                std::memset((void *)stage, 0, AtlasBytes);
                MakePicture((u8 *)stage);
                Tile((const u8 *)stage, (u8 *)(s_scratch + s_back));
                u32 unused;
                svcControlMemory(&unused, stage, 0, AtlasBytes, MEMOP_FREE, (MemPerm)0);
                svcFlushProcessDataCache(CUR_PROCESS_HANDLE, s_scratch + s_back, AtlasBytes);
                s_pendingDirty = true;
                s_backSynced = true;
                s_test = true;
                s_on = true;
            }
            for (Quad &q : s_pending) q.visible = false;
        }
        else if (args == "on")
        {
            if (!s_hooked)
                return "gpuquad: no hook, can't draw";
            if (!Prepare())
                return "gpuquad: " + s_why;
            s_hiResEnabled = true;
            s_on = true;
        }
        else if (args == "off")
        {
            s_on = false;
            s_hiResEnabled = false;
            s_why = "off";
        }
        else if (args == "mode fb" || args == "mode target")
        {
            if (args == "mode target" && !s_dtHooked)
                return "gpuquad: no DisplayTransfer hook";
            s_inTarget = args == "mode target";
        }
        else if (args == "restore on" || args == "restore off")
            s_restore = args == "restore on";
        else if (args == "shadow on" || args == "shadow off")
            s_shadow = args == "shadow on";
        else if (args == "reset")
        {
            std::memset(s_stale, 0, sizeof(s_stale));
            s_checks = s_staleChecks = s_staleCode = 0;
            s_lists = s_jumps = s_badJumps = s_frames = s_drawn = s_skipped = s_unseenRegs = s_unseenCode = 0;
            s_words = s_parseTicks = s_drawTicks = 0;
            s_minSize = 0xFFFFFFFF;
            s_maxSize = 0;
        }
        else if (args != "status")
            return "gpuquad: use on, off, status, reset, test on|off, mode fb|target, restore on|off or shadow on|off";
        return Status();
    }
}
}
