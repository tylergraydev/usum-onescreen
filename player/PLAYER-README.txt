USUM One Screen
===============

Plays Pokémon Ultra Sun (USA) in the Azahar emulator with only the top screen,
using the USUM One Screen plugin. Windows 10 or 11.

You need
- Azahar, installed the normal way (tested with Azahar 2126.2).
- Your own dump of Pokémon Ultra Sun (USA), title 00040000001B5000:
  a .3ds, .cci, .cxi or .cia file, decrypted.
- Your own dump of its v1.2 update (title 0004000E001B5000, version 0x0820):
  a .cia file, decrypted.
This app doesn't include, download or explain how to get game files or keys.

Play
1. Unzip this folder somewhere you can write to (not Program Files).
2. Run UsumOneScreen.exe.
3. Browse for your ROM and the v1.2 update. Each file is checked as soon as
   you pick it; anything else is refused with the reason.
4. Press Play. The first time, the app copies your Azahar into an "azahar"
   folder next to it, sets it up and installs the update there (this takes a
   minute). After that, Play starts the game straight away.

Your own Azahar and its settings and saves are never changed: the app plays
with its own copy, and keeps its saves in azahar\user\sdmc.

Settings
- Controls: Keyboard (Azahar's default keys), Gamepad (any controller,
  Xbox button layout), or Custom: press "Map buttons in Azahar…", then in
  Azahar open Emulation > Configure > Controls, map your buttons, press OK
  and close Azahar.
- Plugin: the keys for the bottom-screen view (default L+R), touch (ZL) and
  the map (ZR), what moves the touch cursor, the panel's size and corner,
  and the automatic view. They are saved to
  azahar\user\sdmc\usum-onescreen.ini.
- Fullscreen: F11 switches, Esc leaves it.

Known problems
- Soft reset (L+R+Start) closes Azahar. This is a bug in Azahar's plugin
  loader. Save in the game before you try it, or don't use it.
- Ultra Moon isn't supported yet.
- If your v1.2 update reports version 0x0800 (the app tells you when you
  pick it), a brand-new game stops at its first-boot language screen. An
  existing save plays fine. A dump of the update that reports 0x0820 works
  for new games too.

If something goes wrong, the plugin's log is azahar\user\sdmc\usum-onescreen.log.
Don't share game files, saves or keys when you report a problem.
