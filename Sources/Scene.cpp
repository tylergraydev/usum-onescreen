#include "Scene.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // A loaded CRO has "FIXD" at +0x80 (the loader replaces "CRO0") and an
    // absolute pointer to its name at +0x84.
    static const u32 CroMagicFixed = 0x44584946;   // "FIXD"
    static const u32 CroArea       = 0x006D6000;   // modules load above the main image's bss

    u32 CroModuleOf(u32 address)
    {
        if (address < CroArea)
            return 0;
        // Modules are page aligned; walk back to the header of the one that
        // contains the address (no module seen so far is larger than 2 MB).
        for (u32 page = address & ~0xFFFu; page >= CroArea && address - page < 0x200000; page -= 0x1000)
        {
            u32 magic = 0;
            if (Process::Read32(page + 0x80, magic) && magic == CroMagicFixed)
                return page;
        }
        return 0;
    }

    std::string CroModuleName(u32 module)
    {
        u32 namePtr = 0;
        if (module == 0 || !Process::Read32(module + 0x84, namePtr))
            return "";
        std::string name;
        for (u32 i = 0; i < 32; i++)
        {
            u32 word = 0;
            if (!Process::Read32((namePtr + i) & ~3u, word))
                break;
            const char c = (char)(word >> (((namePtr + i) & 3) * 8));
            if (c == '\0')
                break;
            name += c;
        }
        return name;
    }

    std::string SceneModule(u32 sceneManager)
    {
        u32 manager = 0, scene = 0, vtable = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0
            || !Process::Read32(manager, scene) || scene == 0
            || !Process::Read32(scene, vtable))
            return "";

        // Some scenes (Party, Bag) are classes in the main image, not a CRO;
        // name those by their vtable address.
        if (vtable < CroArea)
            return Utils::Format("main:%08lX", vtable);

        return CroModuleName(CroModuleOf(vtable));
    }

    // The FieldRo scene object holds a state object at +0x618 (vtable FieldRo
    // +0xD735C) whose word at +4 is the field state.
    static const u32 FieldStateObject = 0x618;
    static const u32 FieldStateVtable = 0xD735C;

    int FieldState(u32 sceneManager)
    {
        // Finding the module walks pages, so remember it per scene vtable.
        static u32 s_vtable = 0, s_module = 0;

        u32 manager = 0, scene = 0, vtable = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0
            || !Process::Read32(manager, scene) || scene == 0
            || !Process::Read32(scene, vtable) || vtable < CroArea)
            return -1;
        if (vtable != s_vtable)
        {
            const u32 module = CroModuleOf(vtable);
            s_vtable = vtable;
            s_module = CroModuleName(module) == "FieldRo" ? module : 0;
        }

        u32 object = 0, state = 0;
        if (s_module == 0
            || !Process::Read32(scene + FieldStateObject, object) || object != s_module + FieldStateVtable
            || !Process::Read32(scene + FieldStateObject + 4, state))
            return -1;
        return (int)state;
    }
}
