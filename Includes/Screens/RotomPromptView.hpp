#pragma once

// The Rotom Dex's field prompts on the top screen (W6-2): what the plugin
// reads of Rotom's applet, how a line of Rotom's message file becomes text,
// what the prompt shows, its keys, layout and drawing. Pure C++ (no CTRPF),
// so tests/ can build it on the PC; Screens/RotomPrompt.cpp runs it in the
// plugin. See docs/ROTOM-FIELD.md.

#include <string>
#include <vector>

#include "PartyData.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // One entry of Rotom's chat table (FieldMenu CRO, 0x9A entries of eight
    // u16): the question line, the two answers and Rotom's reply to each
    // (0xFFFF: none), all lines of message file 89. The last three words are
    // not used.
    struct RotomEntry
    {
        u16 question = 0;
        u16 answer[2] = {};
        u16 reply[2] = {};
    };

    static const u32 RotomTableEntries = 0x9A;
    static const u32 RotomEntryBytes = 16;
    static const u16 RotomNoLine = 0xFFFF;

    // The entry the applet points at (+0xF8), from the table's start and the
    // pointer; -1 if it is 0 or not an entry of the table.
    int RotomEntryIndex(u32 table, u32 pointer);
    // An entry from its eight words; false if the question or an answer is missing.
    bool RotomEntryFromWords(const u16 words[8], RotomEntry &entry);

    // Roto Loto (applet +0x100, docs/ROTOM-FIELD.md): 0 none, 3 "It's Roto
    // Loto time!", 5 the reel spins, 6/7 "That's an amazing power!", 8 fading,
    // 9 the power's description. Every step but 8 waits for a touch.
    enum class RotoLotoStep : u8 { None, Intro, Spinning, Result, Fading, Power };
    RotoLotoStep RotoLotoStepFrom(u32 state);

    // What the plugin reads of Rotom's applet each frame.
    struct RotomReading
    {
        bool applet = false;    // the applet (FieldMenu + 0x18284) is on the scene stack
        int entry = -1;         // chat table entry (+0xF8), -1 none
        u32 answers = 0;        // +0xC8: 2 the answers are on screen, 1 one was picked, 0 none
        bool ready = false;     // Rotom wants to talk: applet animation +0xF0 == 0x5C
        u32 loto = 0;           // +0x100, Roto Loto state
        int lotoPower = -1;     // +0x104: Roto Loto power table entry, -1 none
    };

    // A message line as text: `encrypted` is the line as the message file
    // stores it (docs/RE2-text.md). Tags become what Rotom shows: 0x0100 and
    // 0x0189 (the player's name, and what Rotom calls the player) `name`,
    // 0xBE00 / 0xBE01 (next line / next page) a line break; other tags are
    // dropped. The game font's private glyphs become ' ', '…', '♂', '♀'.
    std::string RotomLineText(const u16 *encrypted, u32 length, u32 line, const std::string &name);

    // Splits text into lines at its breaks and where a line would be wider
    // than `width` (at spaces when it can), as the box draws its text (the
    // game font once it is loaded, else the System font).
    std::vector<std::string> RotomWrap(const std::string &text, int width);

    // What the prompt shows. Plain data (fixed-size text), so the plugin
    // thread can hand a copy to the OSD callback.
    enum class RotomMode : u8
    {
        None,
        Ready,      // a small badge: Rotom wants to talk (talk key)
        Choice,     // Rotom's question and its two answers
        Reply,      // Rotom's reply to the answer, for a few seconds
        Loto,       // Roto Loto: Rotom's line, A goes on
    };

    static const int RotomMaxLines = 4;

    struct RotomView
    {
        RotomMode mode = RotomMode::None;
        int cursor = 0;             // Choice: 0 or 1
        bool busy = false;          // the answer's tap is being made
        int lines = 0;
        char text[RotomMaxLines][96] = {};  // question / reply / Roto Loto line, wrapped
        char answer[2][64] = {};
        char key[16] = {};          // the talk key, for the badge ("Up")
        RotoLotoStep loto = RotoLotoStep::None;
    };

    bool operator==(const RotomView &a, const RotomView &b);
    inline bool operator!=(const RotomView &a, const RotomView &b) { return !(a == b); }

    // Fills view.text from `text`, wrapped to the box (at most RotomMaxLines).
    void RotomSetText(RotomView &view, const std::string &text);

    // What the keys pressed this frame do in `view`.
    enum class RotomKey { None, Up, Down, Answer, Next, Talk };
    RotomKey RotomKeys(const RotomView &view, u32 pressed, u32 talkKey);
    static const u32 RotomKeyA = 1u << 0;
    static const u32 RotomKeyUp = 1u << 6;
    static const u32 RotomKeyDown = 1u << 7;

    // One log line for a view ("choice: \"Heya...\" / \"I'm doing fine!\" | ...").
    std::string DescribeRotomView(const RotomView &view);

    // Top-screen layout: the box sits at the bottom of the top screen, as the
    // game's own text boxes do, and is as tall as its text; it is drawn like
    // Rotom's own box (yellow, orange line), the answers as the game's option
    // pills with its cursor. The badge is a pill in the top-right corner.
    static constexpr UI::Rect RotomBadge = { 196, 8, 196, 26 };
    static constexpr int RotomRadius = 6;
    static constexpr int RotomBoxW = 376;
    static constexpr int RotomBoxMaxH = 196;
    UI::Rect RotomArea(const RotomView &view);
    // Answer `i`'s pill inside RotomArea(view).
    UI::Rect RotomAnswerRect(const RotomView &view, int i);

    void DrawRotomPrompt(UI::Canvas &canvas, const RotomView &view);
}
