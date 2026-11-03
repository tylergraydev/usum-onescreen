#pragma once

#include <string>
#include <vector>

#include <3ds/types.h>

namespace OneScreen
{
    // The Rotom Dex's field prompts on the top screen (W6-2). In the field
    // the bottom screen is Rotom's: when Rotom asks something (a chat, the
    // save reminder, a story chat), the question and its two answers show on
    // the top screen; Up/Down choose and A taps the answer on the hidden
    // bottom screen. Roto Loto's lines show the same way, A going on. When
    // Rotom wants to talk, a badge says so and Up starts the chat (a tap on
    // Rotom's eye). While a box shows, the panel is held off and the native
    // map steps aside. See docs/ROTOM-FIELD.md.

    // Registers the OSD callback. Call after StartMap (it draws over the map)
    // and before StartPanel (screenshots are taken in the panel's callback).
    void StartRotomPrompt(void);

    // Once per frame, from the plugin thread, after MapUpdate. `scene` is the
    // latest SceneModule() result and `fieldState` FieldState().
    void RotomPromptUpdate(u32 sceneManager, const std::string &scene, int fieldState,
                           std::vector<std::string> &log);

    // "rotom on|off|status|read|select N|answer [N]|talk|next" (the part
    // after "rotom "). False if it isn't one of those.
    bool RotomPromptCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log);

    std::string RotomPromptStatus(void);
}
