#pragma once

// The game's font, icons and data tables (UI/GameAssets.hpp), loaded once
// and shared by every native screen (W7-0). docs/GAME-ASSETS.md.
//
// The load starts by itself the first time the field (FieldRo) is reached,
// after the game's own boot loading, or earlier when a screen calls
// RequestAssets(). It runs in a CTRPF Task (its own thread), so neither the
// game nor the plugin thread waits for it; screens draw with the fallback
// look (system font, drawn shapes) until it is Ready. Once published the set
// never changes or moves, so any thread (the OSD callbacks too) may keep
// the pointers it gets.
//
// Usage in a screen:
//
//     const UI::GameAssets *a = UI::Assets();      // nullptr until ready
//     if (a != nullptr) name = a->MoveName(move);
//     UI::Theme::DrawText(...);                    // picks the game font itself
//
// or the helpers below, which fall back for you.

#include <string>
#include <vector>

#include "UI/GameAssets.hpp"

namespace OneScreen
{
namespace UI
{
    enum class AssetsState : u8
    {
        NotStarted, // waiting for the field (or a RequestAssets call)
        Loading,    // the Task is reading the RomFS
        Ready,      // Assets() is set; single parts may still be missing (see ->status)
        Failed,     // no RomFS or no memory: fallback look for the plugin's life
    };

    // --- Any thread ---

    AssetsState AssetsStatus(void);
    const char *AssetsStateName(AssetsState state);
    // The loaded set, or nullptr while not Ready (draw the fallback look).
    const GameAssets *Assets(void);
    bool AssetsReady(void);

    // The game font if it loaded, else nullptr: draw with the current
    // (system) font. Theme::DrawText already does this choice.
    const GameFont *GameFontOrNull(void);
    bool HaveGameFont(void);
    // An icon / ball icon, or nullptr (draw your own shape).
    const Image *AssetIcon(GameIcon icon);
    const Image *AssetBall(u16 ballItem);
    // Move data and names from the game's tables; `fallback` (or an invalid
    // MoveInfo) while they aren't loaded or the ID is out of range.
    MoveInfo AssetMove(u16 move);
    std::string AssetMoveName(u16 move, const std::string &fallback = "");
    std::string AssetTypeName(u8 type, const std::string &fallback = "");

    // --- Plugin thread ---

    // Starts the load now if it hasn't started. Cheap; call it when a
    // screen opens.
    void RequestAssets(void);
    // Once per frame (main.cpp): starts the load when `scene` is first
    // FieldRo, and writes the load's log lines (assets: ...) when it ends.
    void AssetsUpdate(const std::string &scene, std::vector<std::string> &log);
    // One line for status commands: state, time, memory, game font.
    std::string AssetsSummary(void);
}
}
