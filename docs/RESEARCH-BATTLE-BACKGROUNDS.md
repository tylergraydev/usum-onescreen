# Research: custom battle backgrounds (#30)

Backlog #30: players put their own background art into battles; the plugin
ships only a loader, never game assets. This page records what the battle
backgrounds are, the ways to replace them, and which one is cheapest.

Research only: no product code, the emulator was not run. Every claim is
marked **verified** (checked here with a command, a file read or source code)
or **unverified**.

## Verdict

**No loader is needed.** Azahar already has two replacement routes that work
without plugin code:

1. **Azahar custom textures** (`load/textures/<TID>/`, PNG named by hash).
   Players repaint the backdrop textures at any resolution. No ROM files are
   touched or shipped, and no re-encoding is needed. **Cheapest path.**
2. **Azahar LayeredFS** (`load/mods/<TID>/romfs/a/0/8/1`). Replaces the whole
   background GARC. Players can change textures and models, but someone has
   to rebuild the GARC. The same folder layout works on hardware through
   Luma's `luma/titles/<TID>/romfs`.

What we'd ship: a guide (`docs/CUSTOM-BATTLE-BACKGROUNDS.md`), plus later an
optional helper script (`tools/battle_bg.py`) that runs on the player's own
dump. A plugin-side file hook would add risk and do nothing these two routes
don't already do.

## 1. How battle backgrounds are stored

### The GARC: `a/0/8/1` (ARC ID 81)

- **verified**: `a/0/8/1` is the battle background archive: 14,619,516
  bytes, GARC v6, 168 entries. Each entry is LZ11-compressed and unpacks to a
  GF `BG` pack (a 2-character magic, a u16 count, then u32 offsets) of 30
  sub-files. Two entries, 166 and 167, are empty packs.
  (Scratch survey with `tools/romfs_text.py`'s `RomFS`/`Garc` classes plus an
  LZ11 decoder, run on `analysis/private/dec/umoon-base.cia`.)
- **verified**: its ARC ID is 81 (`0x51`). It is entry 81 of `code.bin`'s
  `a/x/y/z` path-pointer table at `0x671C14`, which points at the string
  `"a/0/8/1"` at `0x65170C`. This checks out against the
  `a/0/6/2` = ARC ID 62 entry in `docs/GAME-ASSETS.md`. The table is the
  same in both games' `code.bin`.
- **verified**: `a/0/8/1` is byte-identical in Ultra Sun (the base CCI that
  `tools/instance.py`'s `ROM` points at) and Ultra Moon (SHA-256 starts
  `c41358110691f8d3`). Neither v1.2 update has a copy (both update romfs
  listings checked), so the base file is the one the game uses.
- **Unrelated GARCs** (checked): `a/0/8/6` (1500 `BG` packs of 8, field
  objects such as `land01_plant_shadow`) and `a/2/7/2` (10 `BG` packs that
  hold only `EnvCamera1` motions) also use the `BG` magic but aren't
  backgrounds.

### An entry's layout (verified on entries 0 and 1, names seen in all 166)

| Sub-file | Content |
|---|---|
| 0 | GF model pack (`00 00 01 00` header, then u32 counts 1, 9, 3, 0, 3 in entry 0; the 9 matches the textures found, the other fields' meaning is a guess (models, shaders)). Holds the background's models (`btl_G_kusa`, mesh names such as `pCylinder234`), the `.gffsh` shaders (`BATTLE_bg_default01GRE`, `BATTLE_bg_blend01GRE`) and the GF textures. |
| 1 | GF motion pack (`00 00 06 00`): the background's own animation (`btl_G_kusa_kusa01`). |
| 2-4 | Rain-overlay motions (`btl_G_all_rain`, `btl_G_all_rain0m`). |
| 14-16 | Snow-overlay motions (`btl_G_all_snow0m`). |
| others | empty in the entries checked |

How the models are named: `btl_G_*` is the ground/stage model (383 of the
model-name hits contain `btl_G_all`, the shared weather meshes), `btl_N_*` and
`btl_F_*` are further parts, and `btl_A_sea_*` is animated water. **Unverified**: the meaning of
the G/N/F/A letters (near/far is a guess). The place names are romanized
Japanese, so entry 0 `kusa` reads as grass, 1 `kaza` as volcano, 2 `haka` as
graveyard, 6 `city`, 8 `hama` as beach, 9 `sea`. These are readings of the
names, not checked in game.

### Textures (verified, all 166 non-empty entries)

GF texture header (`13 12 04 15`, then `"texture\0"`, u32 section length,
u32 data length at `+0x18`, the name at `+0x28` (64 bytes, e.g.
`btl_G_kusa_mori01.tga`), u16 width / height / format / mip count at `+0x68`,
data at `+0x80`). Format values are SPICA's `GFTextureFormat` (`0x2A` ETC1,
`0x2B` ETC1A4, ...).

- 1,665 texture instances: ETC1A4 917, ETC1 724, the rest A4/A8/LA8/RGB565/RGBA8.
  Each entry has 9-15 textures.
- The usual sizes are 32x32 (415), 64x64 (344), 128x128 (243) and
  256x256 (111). Each scene's large backdrop is 512x256 ETC1A4 (for example
  `btl_G_kusa_mori01` "forest", `btl_G_city_bld00` "buildings",
  `btl_G_hama_mori01`).
- The backdrop isn't a flat picture: it is a texture wrapped on 3D meshes
  (cylinders and planes) behind the ground. "Your own background art" means
  repainting those textures on the existing geometry. To get a flat
  full-screen picture you would need new geometry (route 2) or a plugin draw
  (not proposed).

### How a battle picks its background (unverified)

Not traced. `Battle.cro` has `btl::BgSystem`, `btl::BtlvLoaderBg` and
`BtlvBg` (RTTI/symbol strings, verified present). The entry index probably
comes from the battle setup parameters, which the field fills in from zone
or encounter data. That is general Gen 7 knowledge, **unverified** here.
Of the 28 `mov rX, #0x51` sites in `Battle.cro`, the decompiled ones (Ghidra,
`tools/ghidra/decompile.sh` with the CRO loaded flat at `0x100000`) were
message or server-command code, not the ARC open. The ID probably reaches
the loader as data, not as an immediate. This doesn't block the texture or
LayeredFS routes: both replace data whatever index the game picks. It only
matters for a "which file is which place" table in the guide. That table is
easy to build empirically: dump textures in a few battles.

## 2. Redirect options

### A. Azahar custom textures (recommended)

From Azahar source (`src/video_core/custom_textures/custom_tex_manager.cpp`,
`src/video_core/rasterizer_cache/rasterizer_cache.h`, `pixel_format.h`;
read 2026-11-09 from `azahar-emu/azahar` master, **verified as source**):

- Load folder `user/load/textures/<program ID>/`, dump folder
  `user/dump/textures/<program ID>/`. The ID is the running process's
  program ID (`00040000001B5000` Sun, `00040000001B5100` Moon).
- `pack.json` `options`: `use_new_hash`, `skip_mipmap`, `flip_png_files`.
  Without a `pack.json` the legacy defaults apply (`use_new_hash` false).
- Hash: CityHash64. With `use_new_hash` it hashes the **raw guest bytes** of
  the texture. The legacy hash covers the decoded pixels.
- File names: `tex1_<W>x<H>_<HASH>_<format>` (dumps add `_mip<n>`), with
  format being `VideoCore::PixelFormat`: ETC1 = 12, ETC1A4 = 13, RGBA8 = 0,
  A4 = 11 and so on. Replacements can be PNG, DDS or KTX at any resolution.
- No pixel-format exclusion: ETC1/ETC1A4 surfaces are dumped and replaced
  like any other. Only non-power-of-two surfaces (framebuffers) are skipped
  from dumping. **Verified indirectly**: the user's own installed USUM HD
  pack (`%APPDATA%\Azahar\load\textures\00040000001B5000\HD Textures`,
  3,539 files, no `pack.json`, so legacy hash) has 480 `_12` (ETC1) and
  130 `_13` (ETC1A4) replacements. Whether it covers battle backgrounds is
  **unverified**.

Player workflow, zero plugin code:

1. Azahar → Graphics → enable **Dump Textures**, start a battle in the place
   you want, leave.
2. Find the backdrop PNG in `dump/textures/<TID>/`. It is 512x256 for most
   scenes. Editing it in place keeps the layout the mesh's UVs expect.
3. Put the edited PNG (it can be 2x or 4x) under `load/textures/<TID>/` with
   the `pack.json` the dump wrote, then enable **Use Custom Textures**.

**Verified 2026-11-09** (Azahar 2126.2, Route 2 grass battle, see
`docs/CUSTOM-BATTLE-BACKGROUNDS.md`): the backdrop pieces show up in a dump and
recoloured copies show in battle with a `use_new_hash: true` pack.json. That
backdrop was 256x128 / 256x64 ETC1A4 pieces (hill, rocks, bushes) plus 128x128
grass, not a 512x256 texture; no 512x256 ETC1/ETC1A4 texture was dumped in
that battle. Still unverified: whether
the new hash equals CityHash64 of the GF texture's data bytes as stored in
`a/0/8/1`. If it does, a script can name the PNGs from the player's dump
with no texture dumping at all. GFL2 uploads the PICA-tiled data as-is, so
it probably does.

Caveats: Azahar only, not hardware. Textures shared between scenes (the
weather overlays, `btl_G_kusa_iwa01` used in entries 0 and 3) change
everywhere they are used, because the replacement keys on content, not on
scene. The instances `tools/instance.py` makes run with
`custom_textures=false` per game, so tests have to turn it on.

### B. LayeredFS romfs replacement

- **Azahar (verified as source, `src/core/file_sys/ncch_container.cpp`,
  `layered_fs.cpp`)**: `user/load/mods/<TID>/romfs/` and `romfs_ext/` are
  layered over the RomFS. Files are matched by full relative path (so
  `romfs/a/0/8/1`), new files can be added, `.ips`/`.bps` in `romfs_ext`
  patch files and `.stub` removes them, with no size limit. Update title
  IDs map to the base ID's mods folder. Azahar reads `luma/titles/<TID>/`
  **only** for `code.ips`, not for romfs, so a romfs mod has to go in
  `load/mods`.
- **Hardware (Luma3DS wiki)**: `/luma/titles/<TID>/romfs/` with "Enable game
  patching" on. The same `a/0/8/1` file works there.
- Cost: someone has to rebuild `a/0/8/1`. That means: unpack the entry →
  replace the GF texture data (ETC1/ETC1A4 encode, same size, or rewrite
  the pack offsets) → re-LZ11 (or store uncompressed: whether the game
  accepts non-LZ entries is **unverified**) → rebuild the GARC tables.
  The 14.6 MB output is a derived game file. Players build it from their own
  dump and must not share it. We'd ship only the script.
- **Unverified**: how Azahar's LayeredFS combines with the separate update
  romfs that USUM mounts. `a/0/8/1` is only in the base romfs, so the base
  layer should be the one read.

### C. Plugin-side file hook (not recommended)

Possible in principle: hook the game's GARC entry read (gfl2 `ArcFile`, ARC
ID 81) or the FS `OpenFile` of `rom:/a/0/8/1` and serve bytes from the SD
card. But the game keeps the GARC open and reads entries by offset, so a
per-entry redirect means finding and hooking the arc-read function (not
found yet, see above), handling async reads, and keeping a heap copy in
the plugin's tight memory (`docs/MEMORY.md`). It covers nothing that B
doesn't on both Azahar and hardware. Hard, as the backlog says, and
redundant.

## 3. Community tools and mods

- **SPICA** (gdkchan, Reisyukaku): reads and writes GFL2 models and
  textures, the formats in `a/0/8/1`. It is the successor to Ohana3DS.
  https://github.com/gdkchan/SPICA,
  https://projectpokemon.org/index/news/release-spica-r69 . A 2018 thread
  reports gaps in its Ultra Moon texture support:
  https://vg-resource.com/printthread.php?tid=32935
- **Ohana3DS** (gdkchan): the older 3DS model and texture viewer and ripper.
- **pk3DS** (kwsch): edits data tables (trainers, encounters, personal data).
  No battle-background editor is known (**unverified**, from memory; not
  checked in its source this session).
- **USUM model and texture guide** (texture replacement workflow, general):
  https://gamebrew.org/wiki/Ultra_Sun_and_Ultra_Moon_Pokemon_Model_Data_and_Tools_3DS
- USUM romhacks (VICIOUS/BRUTAL, Supernova) ship LayeredFS romfs folders. The
  install notes mention `a/0/8/2` (Sun) and `a/0/8/3` (Moon) swaps. Verified
  here: that GARC sits at `a/0/8/2` in Sun and `a/0/8/3` in Moon (483 MB,
  game-specific), while `a/0/8/1` has the same path in both:
  https://www.gamebrew.org/wiki/Pokemon_Ultra_VICIOUS_Sun_and_Ultra_BRUTAL_Moon_3DS
- No USUM battle-background mod or editor was found in a short search.

## 4. Cost, risks, next steps

| Route | Plugin code | Our effort | Player effort | Works on |
|---|---|---|---|---|
| A. Texture pack + guide | none | S (guide + one dump run to confirm names) | dump, edit a PNG, copy | Azahar |
| A + naming script | none | S-M (CityHash64 + GF texture walk over the player's own dump) | run script, edit PNGs | Azahar |
| B. LayeredFS + rebuild script | none | M-L (ETC1 encoder, LZ11 + GARC writer) | run script, copy folder | Azahar and hardware |
| C. Plugin fs hook | yes | L, high risk | as B | as B |

Risks: shared textures change several scenes at once (route A); route B
outputs are derived game files (license and sharing rule: never commit them,
only the script); the background index for each place is still unmapped.

Next steps, in order:

1. ~~One emulator run with Dump Textures on in a grass battle~~ Done
   2026-11-09: dump and replacement work (guide:
   `docs/CUSTOM-BATTLE-BACKGROUNDS.md`). Which GF texture names the Route 2
   pieces are (no 512x256 one was dumped) is still open.
2. Check whether that hash equals CityHash64 of the texture's data in entry 0
   (data length at header `+0x18`, data at `+0x80`). If it does, write
   `tools/battle_bg.py list|names` for the player's own dump.
3. Write the guide. Do route B only if hardware support is asked for.

Open questions: what the battle-setup field and table are that choose the
entry (index ↔ place map); whether Azahar's new hash equals the stored GF
data bytes; whether the user's HD pack already replaces any battle
backgrounds.
