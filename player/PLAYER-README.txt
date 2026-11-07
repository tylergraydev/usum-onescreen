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
- Widescreen (experimental): Auto, Original (5:3), 16:9 or 21:9.
  Auto matches the primary monitor. This sets both the plugin's aspect in
  azahar\user\sdmc\usum-onescreen.ini and Azahar's screen_top_stretch in
  azahar\user\config\qt-config.ini. Original turns stretch off.
  Command line: UsumOneScreen set aspect=16:9 (also auto, 0, off, 21:9 or
  a positive float). Before Azahar is copied, stretch applies on setup.
- Game options: Battle menu (Classic, Bar, Rail), Text and battle speed
  (Normal, Fast, Instant), Graphics (Keep, Native 1x, High 3x, Ultra 5x).
  21:9 widescreen looks best on High or Ultra. Keep leaves Azahar's scale alone.
- Button icons: Auto detects Xbox, PlayStation or Nintendo controllers;
  you can override it, including 3DS icons. Icon drawing needs a future plugin.
- Backups...: Play backs up your save automatically, keeping the newest ten
  per game in backups\TITLEID. Back up now, restore or open that folder here.
  Restore asks first and backs up the current save. Close Azahar first.
  Backups include save data only, not extdata. No save yet means no backup.
- Cheats...: Back up first. Tick cheats, then Save; changes apply next start.
  Close Azahar before saving cheats. Shiny and its undo cannot both be on.
  Money/items stay changed after disabling cheats. Item cheats need an existing
  slot and clear its NEW badge and Free Space position. Restart with shiny off
  to undo its memory patch. Sun and Moon keep separate selections.
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
