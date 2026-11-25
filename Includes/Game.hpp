#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // A function the plugin hooks: its address and its first instruction words,
    // checked at boot so a wrong address is caught before anything is patched.
    struct HookTarget
    {
        u32 address;    // 0 means "not found yet"
        u32 prologue[4];
    };

    // Addresses inside the game's code/data that the plugin hooks or reads.
    // Found by static analysis of the matching code.bin (base game + v1.2
    // update applied); see docs/OFFSETS.md.
    struct Offsets
    {
        // gx library "set framebuffer": (screen 0=top/1=bottom, left addr,
        // right addr, stride, format, select, attribute). Writes the GSP
        // shared-memory framebuffer info for that screen.
        HookTarget gspSetFramebuffer;
        // HID touch sample reader shared by TouchPanelReader::Read and
        // ReadLatest: (touch shm, status* out, count, ...). Each status is
        // 8 bytes: u16 x, u16 y, u8 touching.
        HookTarget hidTouchRead;
        // HID pad sample reader shared by PadReader::Read and ReadLatest:
        // (pad shm, status* out, count, int *count out, ...). Each status is
        // 16 bytes: u32 held, u32 pressed, u32 released, s16 stick x, s16 stick y.
        HookTarget hidPadRead;
        // Global pointer to the scene manager. Manager +0 points to the current
        // scene object; that object's vtable lives in the CRO module of the
        // scene (FieldRo, Zukan, Bag, ...), which names the scene.
        u32 sceneManager;
        // gx library calls that push one command onto the game's GX command
        // queue (lock-free; the push triggers the queue when it was empty).
        // DisplayTransfer: (in, in width, in height, out, out width, out
        // height, flags). TextureCopy: (in, out, size, in width, in gap,
        // out width, out gap, flags), widths and gaps in 16-byte units.
        // Called, not hooked; the panel copies the bottom screen with them.
        HookTarget gxDisplayTransfer;
        HookTarget gxTextureCopy;
        // GX command-list wrapper and command-queue state global.
        HookTarget gxProcessList;
        u32 gxState;
    };

    struct ImageLayout
    {
        u32 rodataStart;
        u32 dataStart;
        u32 imageEnd; // page-rounded end of bss; CRO modules load above it
    };

    // Read global, then dereference each pointer + offset to reach the scene.
    // Any failed read or null pointer means no current scene.
    struct ScenePath
    {
        u32 global;
        u32 count;
        u32 offsets[4];
    };

    enum GameFeature : u32
    {
        // Scene names, polling and automatic panel selection.
        FeatureScenes = 1u << 0,
        // Readers, native screens and patches for USUM engine structures.
        FeatureUsumEngine = 1u << 1,
    };

    struct GameInfo
    {
        const char *name;
        u64         titleId;
        u16         version;    // title version reported by Process::GetVersion()
        bool        confirmed;  // titleId/version checked against a real dump
        Offsets     offsets;
        ImageLayout layout;
        ScenePath   scene;
        u32         features;
        // Address of a u32 nonzero while the field menu is open; 0 = none.
        // Found by global-data diff; see docs/GEN6.md.
        u32         fieldMenuFlag;
    };

    // Returns the entry for the running game, or nullptr if it isn't a supported
    // title and version. The plugin does nothing when this returns nullptr.
    const GameInfo *DetectGame(void);
    // Last DetectGame result; nullptr before detection or for unsupported games.
    const GameInfo *CurrentGame(void);

    // True if every non-zero hook target in the table has the expected prologue
    // in memory. `report` gets one line per target.
    bool VerifyHooks(const GameInfo &game, std::string &report);
}
