#include "Usum.hpp"
#include "Scene.hpp"
#include "Game.hpp"

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // A loaded CRO has "FIXD" at +0x80 (the loader replaces "CRO0") and an
    // absolute pointer to its name at +0x84.
    static const u32 CroMagicFixed = 0x44584946;   // "FIXD"

    u32 CroModuleOf(u32 address)
    {
        if (address < CurrentGame()->layout.imageEnd)
            return 0;
        // Modules are page aligned; walk back to the header of the one that
        // contains the address (no module seen so far is larger than 2 MB).
        for (u32 page = address & ~0xFFFu; page >= CurrentGame()->layout.imageEnd && address - page < 0x200000; page -= 0x1000)
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

    u32 CurrentSceneObject(void)
    {
        const GameInfo *game = CurrentGame();
        if (!game || game->scene.global == 0 || game->scene.count > 4)
            return 0;
        const ScenePath &path = game->scene;
        u32 scene = 0;
        if (!Process::Read32(path.global, scene) || scene == 0)
            return 0;
        for (u32 i = 0; i < path.count; ++i)
            if (!Process::Read32(scene + path.offsets[i], scene) || scene == 0)
                return 0;
        return scene;
    }

    static std::string SceneObjectName(u32 scene)
    {
        u32 vtable = 0;
        if (scene == 0 || !Process::Read32(scene, vtable))
            return "";

        // Some scenes (Party, Bag) are classes in the main image, not a CRO;
        // name those by their vtable address.
        if (vtable < CurrentGame()->layout.imageEnd)
            return Utils::Format("main:%08lX", vtable);

        return CroModuleName(CroModuleOf(vtable));
    }

    std::string CurrentScene(void)
    {
        return SceneObjectName(CurrentSceneObject());
    }

    std::string SceneModule(u32 sceneManager)
    {
        u32 manager = 0, scene = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0
            || !Process::Read32(manager, scene))
            return "";
        return SceneObjectName(scene);
    }

    // The FieldRo scene object holds a state object (vtable FieldRo +0xD735C)
    // whose word at +4 is the field state. Where it sits depends on the map
    // (+0x618 in the Route 2 Pokemon Center, +0x3DC on Route 2), so it is
    // found by its vtable in the scene object's first FieldStateSearch bytes.
    static const u32 FieldStateVtable = Usum::FieldStateVtable;
    static const u32 FieldStateSearch = 0x800;
    static const u32 FieldStateRetry = 15;

    // Finding the module walks pages and finding the object scans the
    // scene, so FieldState remembers both per scene vtable / scene object,
    // and retries a failed scan only every FieldStateRetry calls.
    static u32 s_vtable = 0, s_module = 0;
    static u32 s_scene = 0, s_offset = 0, s_retry = 0;

    int FieldState(u32 sceneManager)
    {

        u32 manager = 0, scene = 0, vtable = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0
            || !Process::Read32(manager, scene) || scene == 0
            || !Process::Read32(scene, vtable) || vtable < CurrentGame()->layout.imageEnd)
            return -1;
        if (vtable != s_vtable)
        {
            const u32 module = CroModuleOf(vtable);
            s_vtable = vtable;
            s_module = CroModuleName(module) == "FieldRo" ? module : 0;
            s_scene = 0;
        }
        if (s_module == 0)
            return -1;

        u32 object = 0, state = 0;
        if (scene == s_scene && s_offset == 0 && s_retry > 0)
        {
            s_retry--;
            return -1;
        }
        if (scene != s_scene || !Process::Read32(scene + s_offset, object) || object != s_module + FieldStateVtable)
        {
            s_scene = scene;
            s_offset = 0;
            for (u32 offset = 4; offset < FieldStateSearch; offset += 4)
                if (Process::Read32(scene + offset, object) && object == s_module + FieldStateVtable)
                {
                    s_offset = offset;
                    break;
                }
            if (s_offset == 0)
            {
                s_retry = FieldStateRetry;
                return -1;
            }
        }
        if (!Process::Read32(scene + s_offset + 4, state))
            return -1;
        return (int)state;
    }

    u32 FieldStateObject(u32 sceneManager, u32 &vtable)
    {
        if (FieldState(sceneManager) < 0 || s_scene == 0 || s_offset == 0)
            return 0;
        vtable = s_module + FieldStateVtable;
        return s_scene + s_offset;
    }
}
