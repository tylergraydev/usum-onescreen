# Third-party code and credits

| Component | How it's used | Licence |
|---|---|---|
| [Gen6CTRPFrameworkOverhauled](https://github.com/samaBR85/Gen6CTRPFrameworkOverhauled) (samaBR85), a fork of [Gen6CTRPluginFramework](https://github.com/biometrix76/Gen6CTRPluginFramework) (biometrix76) | `3gx.ld` copied; `Makefile` and `usum-onescreen.plgInfo` adapted | GPL-3.0 |
| [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework) (The Pixellizer Group) | Statically linked into the `.3gx` (`libctrpf`) | [CTRPluginFramework licence](https://gitlab.com/thepixellizeross/ctrpluginframework/-/blob/develop/LICENSE.txt): binaries must be free of charge or come with their source |
| [libctru](https://github.com/devkitPro/libctru) (devkitPro) | Statically linked into the `.3gx` | zlib |
| [Azahar](https://github.com/azahar-emu/azahar) 2126.2 | Not part of this repository. The launcher downloads the official Windows build on first run (SHA-256 checked); the offline release zip includes the unmodified build with its licence and a pointer to the matching source release | GPL (see `LICENSE-azahar.txt` in the zip) |
| Microsoft.NETFramework.ReferenceAssemblies | Build-time only for the launcher; not shipped | MIT |

The analysis behind this list is in [docs/LICENSING.md](docs/LICENSING.md).

Pokémon Ultra Sun and Ultra Moon are © Nintendo, Creatures Inc. and GAME FREAK inc. No game code, data or assets are included in this repository or its releases; the plugin reads them from the player's own copy at runtime.
