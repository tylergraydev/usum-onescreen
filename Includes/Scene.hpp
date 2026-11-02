#pragma once

#include <string>

#include <3ds/types.h>

namespace OneScreen
{
    // Name of the CRO module that owns the current scene object, e.g.
    // "FieldRo" (overworld), "Zukan" (Pokédex), "TitleMenu". Scenes whose class
    // is in the main image come back as "main:<vtable>". Empty if there is no
    // current scene object (e.g. during a transition).
    std::string SceneModule(u32 sceneManager);

    // Header address of the loaded CRO module containing `address` (0 if
    // none), and a module's name from its header.
    u32 CroModuleOf(u32 address);
    std::string CroModuleName(u32 module);
}
