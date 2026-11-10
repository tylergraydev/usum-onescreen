# Custom battle backgrounds

Repaint the scenery behind Pokémon battles with Azahar custom textures. No
plugin code and no game files are involved: you dump the game's own textures,
edit the PNGs, and Azahar swaps them in by hash. Azahar only (not hardware).

Verified 2026-11-09 in Azahar 2126.2 (Vulkan), Ultra Sun USA v1.2, in a wild
battle in the Route 2 tall grass at night (details at the end).

## What a battle background is

The backdrop is not one flat picture. It is a few small textures wrapped on 3D
shapes behind the ground: a hill, a rock wall, a row of bushes, grass tufts.
You repaint those pieces; their shapes stay the same. In the Route 2 grass
battle the visible pieces were:

| Dump file | Size | Format | What it is |
|---|---|---|---|
| `tex1_256x128_03FBEE41A30A0A91_13_mip0.png` | 256x128 | ETC1A4 | green hill |
| `tex1_256x128_6137A1C83B430D3F_13_mip0.png` | 256x128 | ETC1A4 | brown rocks |
| `tex1_256x64_3EAEE692B4BEB7F4_13_mip0.png` | 256x64 | ETC1A4 | bush row |
| `tex1_128x128_BDFA276BE68F028A_12_mip0.png` | 128x128 | ETC1 | grass |
| `tex1_128x128_4C37DD6AAAF64292_12_mip0.png` | 128x128 | ETC1 | grass |
| `tex1_128x128_A96C760E51F26A5F_13_mip0.png` | 128x128 | ETC1A4 | grass tufts (alpha) |

Other places use other textures. A piece shared between places (for example a
rock used by several backgrounds) changes everywhere it is used, because Azahar
matches on the texture's content, not on the place.

## Steps (launcher)

These use the launcher's **Texture packs...** dialog. Close the game before
changing anything there; changes apply on the next start.

1. **Dump.** Tick **Dump textures for pack makers**. Start the game and walk
   to where you want to battle. Note the time (or click **Open dump folder**
   and sort by date), then start a battle and wait about 10 seconds. Close the
   game and untick **Dump textures** (it slows the game).
2. **Find the pieces.** Click **Open dump folder**. The files written during
   the battle are the battle's textures (Pokémon, effects, HUD and the
   backdrop). The backdrop pieces are the scenery ones: hills, rocks, bushes,
   grass, usually `_13` (ETC1A4) or `_12` (ETC1), 128 to 512 pixels wide.
3. **Edit.** Repaint the PNGs in place. Keep the file names. Keep the layout
   and the transparent areas, since the 3D shapes map the image as it is. You
   may scale the image up 2x or 4x for more detail (same aspect ratio).
4. **Trim.** Delete the files you did not edit from the dump folder (or move
   them out), so the pack holds only your changes. Azahar will dump them again
   the next time dumping is on.
5. **Make the pack.** Click **Make pack from dump folder** and give it a name.
   The launcher copies the folder and adds the `pack.json` it needs
   (`"use_new_hash": true`; without it Azahar matches nothing).
6. **Use it.** Select the pack and click **Use selected**. Start the game and
   enter a battle in that place. **Off** turns the pack off again.

## Steps (Azahar by hand)

1. In Azahar's graphics settings enable **Dump Textures**, play a
   battle, quit. Dumps go to `user/dump/textures/00040000001B5000/` (Ultra Moon:
   `00040000001B5100`).
2. Edit the backdrop PNGs as above.
3. Put the edited PNGs in `user/load/textures/00040000001B5000/` together with
   this `pack.json`:

   ```json
   {"author":"you","version":"1.0.0","description":"Battle background",
    "options":{"skip_mipmap":false,"flip_png_files":true,"use_new_hash":true},
    "textures":{}}
   ```

4. Disable **Dump Textures**, enable **Use Custom Textures**, start the game.

If nothing changes, check `user/log/azahar_log.txt`: it should say
`Utility_CustomTextures: true`, and a hash you replaced should not appear in an
"Unable to find replacement for surface with hash ..." line.

## How this was verified

- Dumping on, Route 2 tall grass start point, one wild battle (night): 93 new dump files during the battle; the six scenery pieces above
  were among them. **No 512x256 backdrop texture** appeared in this battle
  (the research expected one per scene); the only new 512x256 file was an
  RGBA8 one, and the backdrop here is made of the smaller pieces.
- Colour-inverted copies of those six PNGs plus the `pack.json` above in the
  load folder, custom textures on, another battle in the same grass: the
  backdrop's mound showed red-magenta and its rocks blue-grey (brown before);
  the Pokémon and HUD were unchanged. None of the six hashes was in the log's
  "Unable to find replacement" lines, while the untouched textures were.
