# Licensing findings

Checked 2026-11-02 for the P1-7 release work. This is a summary of the licence texts, not legal advice. The repo is licensed GPL-3.0-only (`LICENSE`, chosen by Tyler 2026-11-07).

## What this repo uses

| Component | How it's used | Licence | Source checked |
|---|---|---|---|
| Gen6CTRPFrameworkOverhauled (samaBR85) | `3gx.ld` copied, `Makefile` and `usum-onescreen.plgInfo` adapted | **GPL-3.0** | GitHub API reports `gpl-3.0`; `LICENSE` in the repo is the GPL v3 text. It is a fork of biometrix76/Gen6CTRPluginFramework, also GPL-3.0. |
| CTRPluginFramework / `libctrpf` (The Pixellizer Group, ThePixellizerOSS) | Statically linked into the `.3gx` (`-lctrpf`) | **Custom permissive licence with a source-or-free condition** | `LICENSE.txt` on the `develop` branch of gitlab.com/thepixellizeross/ctrpluginframework (last licence change `6c2bfeea`, 2023-01-23) |
| libctru (devkitPro) | Statically linked (`-lctru`) | **zlib** | "License" section of the libctru `README.md` on GitHub (the repo has no separate licence file) |

Provenance of the build files (diffed 2026-11-02):

- Our `3gx.ld` is identical to Gen6CTRPFrameworkOverhauled's (one leading blank line differs). That file is also byte-identical to the parent biometrix76/Gen6CTRPluginFramework (same git blob `97811222`). It is almost the same as `TestPlugin/3gx.ld` in CTRPluginFramework itself: the CTRPF version adds a `__text_end__` symbol and writes `.tdata : ALIGN(4)` instead of `.tdata ALIGN(4) :`. So the linker script most likely originates from CTRPF's sample plugin and reached us through the GPL repo.
- Our `Makefile` differs from Gen6's in 43 diff lines and from CTRPF's `TestPlugin/Makefile` in 166, so it is closest to Gen6's. Its lower half is the standard devkitPro makefile pattern ("No real need to edit anything past this point"); the origin of that pattern was not traced further.

## What each licence requires

### GPL-3.0 (Gen6CTRPFrameworkOverhauled)

- Copying or adapting its files makes those parts of this repo a derivative work. If you distribute the result (source or the `.3gx`), the work as a whole must be released under GPL-3.0. Its README says "Licensed under GNU GPL-3.0, inherited from upstream"; whether it allows "or later" was not checked file by file.
- Distributing the `.3gx` requires offering the corresponding source under GPL-3.0 (a public repo at the tagged commit satisfies this).
- Keep the copyright and licence notices, and mark that files were modified.
- How much of a linker script and makefile is copyrightable is debatable, but the cautious reading is that the GPL applies to them.

### CTRPluginFramework licence (The Pixellizer Group)

Full text: gitlab.com/thepixellizeross/ctrpluginframework/-/blob/develop/LICENSE.txt. In short:

1. Any binary that uses CTRPF must not be restricted to people who paid a fee or other compensation…
2. …unless the source code of that work is provided with the binary.
3. Copies or modifications of **CTRPF's own source** must keep the same licence text.

Plus a standard "as is" warranty disclaimer. For this project: a free `.3gx` download meets condition 1, and publishing the source meets condition 2 as well. We don't redistribute CTRPF source, so condition 3 only matters if CTRPF files are ever vendored into the repo. This licence is not an OSI licence, and its compatibility with the GPL has not been settled anywhere we found; Gen6CTRPluginFramework is itself GPL-3.0 and links CTRPF the same way, so the combination is common in this scene.

### zlib (libctru)

- Don't misrepresent the origin; acknowledgement in documentation is appreciated but **not required** for binaries.
- Altered source must be marked; the notice must stay in source distributions.
- We don't modify or redistribute libctru source, so nothing is strictly required. A credit line is good practice.

## Recommendation

**Licence this repo under GPL-3.0** before going public.

- It is the only option that is clearly compatible with the files already taken from Gen6CTRPFrameworkOverhauled, without rewriting them.
- It also keeps us inside the CTRPF licence, because GPL-3.0 forces the source to be available with every binary.
- It matches what the Gen6 plugins already do.

The alternative, if you want a permissive licence (MIT, BSD) for your own code, is to drop the Gen6-derived files first: replace `3gx.ld` and the `Makefile` with CTRPF's `TestPlugin` versions (Pixellizer licence) or write them fresh, and re-check the diff. Even then the shipped `.3gx` links CTRPF, so its source-or-free condition still applies to releases.

### Before making the repo public

1. Add `LICENSE` (GPL-3.0 text) and a short licence line in `README.md`.
2. Add a `THIRD-PARTY.md` (or a section in `README.md`) crediting: Gen6CTRPFrameworkOverhauled / Gen6CTRPluginFramework (GPL-3.0) for `3gx.ld` and the `Makefile`; CTRPluginFramework (Pixellizer licence, with a link to its text); libctru (zlib).
3. Add a "modified from …" comment at the top of `3gx.ld` and `Makefile` (GPL-3.0 section 5a asks modified files to carry a notice saying so).
4. Check the repo and its history for game data, keys or dumps: `.gitignore` blocks common types, but `analysis/` outputs and screenshots of game content should stay out (`analysis/private/` is ignored).
5. Keep releases free, with the source at the tagged commit public (satisfies both GPL-3.0 and CTRPF's conditions).

## Sources

- https://github.com/samaBR85/Gen6CTRPFrameworkOverhauled (`LICENSE`, `3gx.ld`, `Makefile`; latest commit checked `16fc26e`)
- https://github.com/biometrix76/Gen6CTRPluginFramework (GPL-3.0 per GitHub API)
- https://gitlab.com/thepixellizeross/ctrpluginframework/-/blob/develop/LICENSE.txt (latest repo commit checked `96fd3d94`, 2026-10-26)
- https://gitlab.com/thepixellizeross/ctrpluginframework/-/tree/develop/TestPlugin (`3gx.ld`, `Makefile`)
- https://github.com/devkitPro/libctru/blob/master/README.md ("License" section)
