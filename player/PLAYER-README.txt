USUM One Screen
===============

Plays Pokémon Ultra Sun or Ultra Moon (USA) in the Azahar emulator with only
the top screen, using the USUM One Screen plugin. Windows 10 or 11.

You need
- Your own decrypted USA ROM (.3ds, .cci, .cxi or .cia):
  Ultra Sun: title 00040000001B5000; Ultra Moon: title 00040000001B5100.
- Your own decrypted v1.2 update (.cia), version 0x0820, matching your ROM:
  Ultra Sun: 0004000E001B5000; Ultra Moon: 0004000E001B5100.
The plugin is built into the release exe. Azahar 2126.2 downloads on first
setup (internet required). The offline Windows zip includes Azahar instead.
Encrypted dumps are refused because Azahar 2126.2 can't install them.
This app doesn't include, download or explain how to get game files or keys.

Play
1. Put UsumOneScreen.exe in a folder you can write to (not Program Files).
   If using the offline zip, unzip the whole folder first.
2. Run UsumOneScreen.exe.
3. Drag your ROM and matching v1.2 update onto the window, or use Browse.
   You can also drop a zip containing either or both, in any subfolder.
   Files are checked automatically; zip contents go into the games folder.
4. Press Play. First setup downloads and verifies Azahar, makes a portable
   copy in the azahar folder and installs your update. Later starts reuse it.
   Keep the extracted game files in games; the app remembers their paths.

If the download fails, install Azahar 2126.2 yourself or use "Use another
Azahar" to pick its folder. A bundled Azahar takes priority over selection.

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
- Ultra Moon has limited testing; a new game stalls in the opening bedroom
  scene in Azahar.
- If your Ultra Sun v1.2 update reports version 0x0800 (the app tells you
  when you pick it), a brand-new game stops at its first-boot language screen. An
  existing save plays fine. A dump of the update that reports 0x0820 works
  for new Ultra Sun games too. Ultra Moon requires 0x0820; 0x0800 is refused.

If something goes wrong, the plugin's log is azahar\user\sdmc\usum-onescreen.log.
Don't share game files, saves or keys when you report a problem.
