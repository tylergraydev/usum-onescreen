#pragma once

#include <string>

#include "Settings.hpp"

// Plain C++ only (no libctru / CTRPF), so tests/ can build this on the PC.
// Text-level edits to usum-onescreen.ini; reading and writing the SD card file
// is in main.cpp (SaveSetting).

namespace OneScreen
{
    // Returns `text` with `key = value` set inside `[section]`. Every other
    // line, comment and line ending is kept. Section and key names match the
    // way ParseSettings reads them: case-insensitive, any spacing, a `;` / `#`
    // comment after either is ignored.
    //  - The first `key` line in `[section]` (in any `[section]` block, if the
    //    file has more than one) is rewritten: its indentation, the key's own
    //    spelling and any trailing comment stay, the value is replaced.
    //  - If no block has the key, `key = value` goes after the last
    //    non-comment line (or the header) of the first `[section]` block, so
    //    comments and blank lines before the next header stay with it.
    //  - An empty `section` means the lines before any header.
    //  - If there is no `[section]`, a blank line, `[section]` and the key are
    //    appended at the end of the file.
    // New lines use the file's line ending (CRLF if it has any, else LF). A
    // last line without a line ending gets one if a line is added after it.
    std::string SetIniValue(const std::string &text, const std::string &section,
                            const std::string &key, const std::string &value);

    // [battle] layout values: classic, bar, rail (case-insensitive, '-', '_'
    // and spaces ignored). Invalid input leaves `out` unchanged.
    bool ParseBattleLayout(const std::string &value, BattleLayout &out);

    // "classic", "bar", "rail": the value written to the file.
    const char *BattleLayoutName(BattleLayout layout);

    // "Classic", "Bar", "Rail": for the plugin menu.
    const char *BattleLayoutTitle(BattleLayout layout);

    // Classic -> Bar -> Rail -> Classic.
    BattleLayout NextBattleLayout(BattleLayout layout);
}
