#pragma once

// Action layer: carries out a menu choice by driving the hidden bottom
// screen (taps from Layouts.hpp, key presses, waits for a scene). This header
// is the pure sequencing logic, with no CTRPF dependency, so tests/ can build
// it on the PC; Actions.cpp feeds it the game state and applies its output.

#include <string>
#include <vector>

#ifdef __3DS__
#include <3ds/types.h>
#else
#include <cstdint>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
#endif

#include "Layouts.hpp"

namespace OneScreen
{
    // HID key bits used by the actions (same values as CTRPF's Key::).
    const u32 ActionKeyB = 1u << 1;
    const u32 ActionKeyX = 1u << 10;

    // Scene IDs (SceneModule(), docs/OFFSETS.md).
    extern const char *const SceneField;     // "FieldRo", also with the X menu open
    extern const char *const SceneDex;       // "Zukan"
    extern const char *const SceneParty;     // "main:00647FCC"
    extern const char *const SceneBag;       // "main:006488C8"
    extern const char *const SceneTownMap;   // "TownMap"
    extern const char *const SceneBattle;    // "Battle"
    extern const char *const SceneStatus;    // "Status" (Pokémon summary)

    // What the action layer sees of the game, once per frame.
    const u32 WorldXMenu         = 1u << 0;  // the X menu is on the bottom screen (pixel probe)
    const u32 WorldBattleCommand = 1u << 1;  // battle: Fight / Pokémon / Bag / Run is up
    const u32 WorldXMenuPage2    = 1u << 2;  // with WorldXMenu: its second page is showing
    const u32 WorldPartyList     = 1u << 8;  // Party screen: the plain list (no menu, message or move mode; pixel probe)

    struct ActionWorld
    {
        std::string scene;      // SceneModule(); empty during transitions
        u32 flags = 0;          // World* bits
    };

    // A test on the world: the scene is (or, with sceneNot, is not) `scene`
    // (nullptr = any), every `set` flag is on and every `clear` flag is off.
    struct ActionCond
    {
        const char *scene = nullptr;
        bool sceneNot = false;
        u32 set = 0;
        u32 clear = 0;

        ActionCond With(u32 flag) const    { ActionCond c = *this; c.set |= flag; return c; }
        ActionCond Without(u32 flag) const { ActionCond c = *this; c.clear |= flag; return c; }
        bool Matches(const ActionWorld &world) const;
        std::string Describe(void) const;
    };

    ActionCond AnyScene(void);
    ActionCond InScene(const char *scene);
    ActionCond NotInScene(const char *scene);

    enum class StepKind : u8
    {
        Tap,        // touch `button`'s tap point for `frames`, then lift
        Press,      // hold `keys` for `frames`, then release
        Wait,       // `frames` frames
        WaitFor,    // until `until`; after `frames` retry from `retryFrom`, then fail (or go on if optional)
        SucceedIf,  // finish the action now, successfully, if `until` holds
        BackOut,    // tap the scene's back button until the field shows, X menu closed
    };

    struct ActionStep
    {
        StepKind kind = StepKind::Wait;
        const char *button = nullptr;   // Tap: a LayoutButtons name
        u32 keys = 0;                   // Press
        u32 frames = 0;                 // Tap/Press: held; Wait: length; WaitFor/BackOut: timeout
        ActionCond until;               // WaitFor
        bool hasSkip = false;           // skip the step if `skipIf` holds when it starts
        ActionCond skipIf;
        s8 retryFrom = -1;              // WaitFor timeout: step to go back to (-1 = fail)
        u8 retries = 0;                 // how many times
        bool optional = false;          // WaitFor: a timeout (after the retries) goes on to the next step

        ActionStep SkipIf(const ActionCond &cond) const { ActionStep s = *this; s.hasSkip = true; s.skipIf = cond; return s; }
        ActionStep Retry(s8 from, u8 times) const       { ActionStep s = *this; s.retryFrom = from; s.retries = times; return s; }
        ActionStep Optional(void) const                 { ActionStep s = *this; s.optional = true; return s; }
        std::string Describe(void) const;
    };

    // Frames a Tap or Press is held by default, and the gap after it so the
    // next one is a new touch / press.
    const u32 ActionHoldFrames = 6;
    const u32 ActionGapFrames  = 2;

    ActionStep StepTap(const char *button, u32 frames = ActionHoldFrames);
    ActionStep StepPress(u32 keys, u32 frames = ActionHoldFrames);
    ActionStep StepWait(u32 frames);
    ActionStep StepWaitFor(const ActionCond &until, u32 timeout);
    ActionStep StepSucceedIf(const ActionCond &cond);
    ActionStep StepBackOut(u32 timeout);

    struct ActionDef
    {
        const char *name;
        const char *summary;
        ActionCond needs;               // checked when the action starts
        std::vector<ActionStep> steps;
    };

    // The built-in actions (open-pokedex, open-party, open-bag, battle-run, close).
    const std::vector<ActionDef> &Actions(void);
    const ActionDef *FindAction(const std::string &name);

    // A layout button by "screen.button" name; nullptr if unknown.
    const LayoutButton *FindLayoutButton(const char *name);

    // How BackOut leaves `scene` (with `flags`): a button to tap, or keys to
    // press (the X menu: B, which AutoPanel follows; it doesn't see touches).
    // Neither (nullptr, 0) if it doesn't know a way.
    const char *BackButtonFor(const std::string &scene, u32 flags);
    u32 BackKeysFor(const std::string &scene, u32 flags);

    // Empty if every Tap names a known button verified by a tap, else why not.
    std::string CheckAction(const ActionDef &def);

    // What the runner wants the game to see this frame.
    struct ActionOutput
    {
        u32 keys = 0;
        bool touching = false;
        u16 x = 0, y = 0;
    };

    // Runs one action, one Update per frame. Log lines are appended to `log`.
    class ActionRunner
    {
    public:
        // Starts `def` if nothing is running and `world` meets def.needs.
        bool Start(const ActionDef &def, const ActionWorld &world, std::vector<std::string> &log);
        ActionOutput Update(const ActionWorld &world, std::vector<std::string> &log);
        void Cancel(std::vector<std::string> &log);

        bool Running(void) const { return m_def != nullptr; }
        std::string Status(void) const;

        // Frames a BackOut waits for the screen to settle before it taps, and
        // before it calls the field done (the X menu shows a little after
        // FieldRo returns from Party or the Bag).
        static const u32 SettleFrames = 20;
        static const u32 FieldSettleFrames = 60;
        static const u32 RetapFrames = 120;
        static const u32 BackOutMaxTaps = 8;

    private:
        void Next(void);
        void Finish(bool ok, const std::string &why, const ActionWorld &world, std::vector<std::string> &log);

        const ActionDef *m_def = nullptr;
        u32 m_step = 0;
        u32 m_frame = 0;            // frames into the current step
        u32 m_total = 0;            // frames since Start
        std::vector<u8> m_retries;  // retries used, per step
        // BackOut: taps so far, the tap in progress, and how long the world
        // has looked the same.
        u32 m_taps = 0;
        const LayoutButton *m_tapping = nullptr;
        u32 m_pressing = 0;
        u32 m_tapFrame = 0;
        u32 m_stable = 0;
        ActionWorld m_last;
        ActionWorld m_tapWorld;     // the world when the last back tap started
        bool m_tapChanged = false;  // and whether it has changed since
        u32 m_sinceTap = 0;
        std::string m_lastResult;   // "open-party ok ..." / "... FAILED ...", for Status
    };
}
