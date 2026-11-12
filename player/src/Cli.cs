using System;
using System.Collections.Generic;
using System.Linq;

namespace UsumOneScreen
{
    // Command-line mode, for scripted setup and headless tests. With no
    // arguments the app opens its window instead.
    static class Cli
    {
        const string Usage =
@"UsumOneScreen [COMMAND] [--home DIR]

  (no command)           open the app's window
  check FILE...          say what each ROM or update file is and whether it fits
  setup --rom FILE [--update CIA] [--azahar DIR] [--plugin 3GX]
        [--controls keyboard|gamepad|custom]
                         set everything up (as the window's Set up button)
  play [--fullscreen] [--wait SECONDS]
                         start the game; --wait prints the plugin's boot log
  set KEY=VALUE...       plugin settings (panel_key touch_key map_key cursor_move
                         cursor_speed size corner auto aspect) and controls=PRESET
  backup / backups (or list)  create / list save backups
  restore NAME           restore a backup (backs up the current save first)
  slot save NAME / load NAME / list / rename OLD NEW / delete NAME
                         named saves per game; load backs up current save first
  export PATH [--slot NAME | --backup NAME]
                         export save ZIP to a new file (default: current save)
  import PATH [--name NAME]
                         import matching save ZIP into a new slot
  cheats [on NAME|off NAME]  list / change cheats
  set graphics=keep|native|high|ultra  set rendering resolution
  set visual_quality=keep|off|sharpen|aa|sharpen-aa  OpenGL post-processing
  texture-pack list|install ZIP [--game TITLEID]|use NAME|off|remove NAME
  texture-pack from-dump NAME  make a library pack from this game's dumped images
                         packs for selected game (close Azahar first)
  set preload_textures=on|off async_custom_loading=on|off dump_textures=on|off
                         preload uses RAM; dumping slows the game
  status                 show the setup
  check-update           check for a newer stable launcher (does not install)
  snapshot PNG           draw the window into PNG and exit (for tests)

--home DIR uses DIR instead of the app's folder for Azahar and the settings.";

        public static int Run(string[] args)
        {
            Win32.UseParentConsole();
            // UTF-8 when piped; a console keeps its own code page.
            var utf8 = new System.Text.UTF8Encoding(false);
            if (Console.IsOutputRedirected)
                Console.SetOut(new System.IO.StreamWriter(Console.OpenStandardOutput(), utf8) { AutoFlush = true });
            if (Console.IsErrorRedirected)
                Console.SetError(new System.IO.StreamWriter(Console.OpenStandardError(), utf8) { AutoFlush = true });
            try
            {
                var list = args.ToList();
                string homeDir = Take(list, "--home");
                var home = homeDir != null ? new Home(homeDir) : Home.Default();
                if (list.Count == 0)
                    return Window(home);
                string command = list[0];
                list.RemoveAt(0);
                switch (command)
                {
                    case "check-update":
                        Leftover(list);
                        Console.WriteLine(UpdateCheck.Check(home, true, DateTime.UtcNow).GetAwaiter().GetResult().Message);
                        return 0;
                    case "check": return Check(home, list);
                    case "setup": return SetupCommand(home, list);
                    case "play": return Play(home, list);
                    case "set": return Set(home, list);
                    case "backup":
                        Leftover(list);
                        Console.WriteLine(Backups.Create(home) ?? "No save yet; skipped."); return 0;
                    case "backups":
                    case "list":
                        Leftover(list);
                        foreach (var path in Backups.List(home)) Console.WriteLine(System.IO.Path.GetFileName(path) + "  " + Backups.Size(home, path) + " bytes");
                        return 0;
                    case "restore":
                        if (list.Count != 1) throw new SetupException("restore needs one backup NAME");
                        Backups.Restore(home, list[0]); Console.WriteLine("Save restored."); return 0;
                    case "slot": return SlotCommand(home, list);
                    case "export":
                        string slot = Take(list, "--slot"), backup = Take(list, "--backup");
                        if (list.Count != 1) throw new SetupException("export needs PATH [--slot NAME | --backup NAME]");
                        SaveArchive.Export(home, list[0], slot, backup); Console.WriteLine("Save exported."); return 0;
                    case "import":
                        string name = Take(list, "--name");
                        if (list.Count != 1) throw new SetupException("import needs PATH [--name NAME]");
                        Console.WriteLine("Slot imported: " + SaveArchive.Import(home, list[0], name)); return 0;
                    case "cheats": return CheatCommand(home, list);
                    case "texture-pack": return TextureCommand(home, list);
                    case "status": return Status(home);
                    case "snapshot": return Snapshot(home, list);
                    default:
                        Console.WriteLine(Usage);
                        return command == "help" || command == "--help" || command == "-h" ? 0 : 2;
                }
            }
            catch (Exception e) when (e is SetupException || e is System.IO.IOException || e is UnauthorizedAccessException)
            {
                Console.Error.WriteLine("error: " + e.Message);
                return 1;
            }
        }

        static int TextureCommand(Home home, List<string> args)
        {
            void Warn(string name)
            {
                string warning = TexturePacks.Warning(home, name);
                if (warning != null) Console.WriteLine("Warning: " + warning);
            }
            string game = Take(args, "--game");
            if (args.Count == 1 && args[0] == "list")
            {
                Console.WriteLine(home.GameName + " (" + Dump.Title(home.CurrentTitle) + ") " + (TexturePacks.Get(home, "custom_textures") ? "on" : "off"));
                foreach (string pack in TexturePacks.List(home))
                {
                    Console.WriteLine((System.IO.Path.GetFileName(pack) == TexturePacks.Active(home) ? "* " : "  ") + TexturePacks.Name(pack));
                    Warn(TexturePacks.Name(pack));
                }
                return 0;
            }
            if (args.Count == 2 && args[0] == "install")
            {
                string installed = TexturePacks.Install(home, args[1], game);
                Console.WriteLine("Installed: " + installed); Warn(installed);
            }
            else if (game != null) throw new SetupException("--game is only for texture-pack install.");
            else if (args.Count == 2 && args[0] == "from-dump") Console.WriteLine("Created pack: " + TexturePacks.FromDump(home, args[1]));
            else if (args.Count == 2 && args[0] == "use") { TexturePacks.Use(home, args[1]); Warn(args[1]); }
            else if (args.Count == 2 && args[0] == "remove") TexturePacks.Remove(home, args[1]);
            else if (args.Count == 1 && args[0] == "off") TexturePacks.Off(home);
            else throw new SetupException("texture-pack needs list, install ZIP [--game TITLEID], from-dump NAME, use NAME, off or remove NAME");
            return 0;
        }

        static int SlotCommand(Home home, List<string> args)
        {
            if (args.Count == 1 && args[0] == "list")
            {
                foreach (string path in SaveSlots.List(home)) Console.WriteLine(SaveSlots.Name(path) + "  " + Backups.Size(home, System.IO.Path.Combine(path, "data")) + " bytes");
                return 0;
            }
            if (args.Count == 3 && args[0] == "rename") SaveSlots.Rename(home, args[1], args[2]);
            else if (args.Count == 2 && args[0] == "save") SaveSlots.Save(home, args[1]);
            else if (args.Count == 2 && args[0] == "load") SaveSlots.Load(home, args[1]);
            else if (args.Count == 2 && args[0] == "delete") SaveSlots.Delete(home, args[1]);
            else throw new SetupException("slot needs save NAME, load NAME, list, rename OLD NEW or delete NAME");
            Console.WriteLine("Slot " + args[0] + " complete."); return 0;
        }

        static int Window(Home home)
        {
            System.Windows.Forms.Application.EnableVisualStyles();
            System.Windows.Forms.Application.Run(new MainForm(home));
            return 0;
        }

        static int Snapshot(Home home, List<string> args)
        {
            if (args.Count != 1)
                throw new SetupException("snapshot needs one PNG path");
            System.Windows.Forms.Application.EnableVisualStyles();
            using (var form = new MainForm(home) { StartPosition = System.Windows.Forms.FormStartPosition.Manual, Location = new System.Drawing.Point(-4000, -4000), ShowInTaskbar = false })
            {
                form.Show();
                System.Windows.Forms.Application.DoEvents();
                using (var bmp = new System.Drawing.Bitmap(form.Width, form.Height))
                {
                    form.DrawToBitmap(bmp, new System.Drawing.Rectangle(0, 0, form.Width, form.Height));
                    bmp.Save(args[0], System.Drawing.Imaging.ImageFormat.Png);
                }
            }
            Console.WriteLine(args[0]);
            return 0;
        }

        static string Take(List<string> args, string flag)
        {
            int i = args.IndexOf(flag);
            if (i < 0)
                return null;
            if (i + 1 >= args.Count)
                throw new SetupException($"{flag} needs a value");
            string value = args[i + 1];
            args.RemoveRange(i, 2);
            return value;
        }

        static bool Flag(List<string> args, string flag) => args.Remove(flag);

        static void Leftover(List<string> args)
        {
            if (args.Count > 0)
                throw new SetupException("unexpected: " + string.Join(" ", args));
        }

        static int Check(Home home, List<string> files)
        {
            int bad = 0;
            foreach (var f in files.SelectMany(f => GameFiles.Expand(home, f, Console.WriteLine)))
            {
                var info = Dump.Read(f);
                var kind = Dump.IsUpdate(info.TitleId) ? DumpKind.Update : DumpKind.Game;
                var c = Dump.Check(f, kind);
                Console.WriteLine($"{(c.Ok ? "ok" : "refused")}: {c.Message}");
                if (c.Warning != null)
                    Console.WriteLine("  warning: " + c.Warning);
                if (info.Error == null)
                    Console.WriteLine($"  format {info.Format}, title {Dump.Title(info.TitleId)}, " +
                                      $"version {(info.Version < 0 ? "-" : Dump.VersionText(info.Version))}, " +
                                      $"{(info.Encrypted ? "encrypted" : "decrypted")}");
                if (!c.Ok) bad++;
            }
            return bad == 0 ? 0 : 1;
        }

        static ControlPreset ParsePreset(string s)
        {
            if (Enum.TryParse(s, true, out ControlPreset p))
                return p;
            throw new SetupException($"bad controls '{s}' (use keyboard, gamepad or custom)");
        }

        static int SetupCommand(Home home, List<string> args)
        {
            var o = new Setup.Options
            {
                Rom = Take(args, "--rom"),
                Update = Take(args, "--update"),
                AzaharSource = Take(args, "--azahar"),
                Plugin = Take(args, "--plugin"),
            };
            string controls = Take(args, "--controls");
            o.Controls = controls != null ? ParsePreset(controls) : home.CurrentControls();
            Leftover(args);
            string boot = Setup.Run(home, o, Console.WriteLine);
            Console.WriteLine("boot: " + boot);
            return 0;
        }

        static int Play(Home home, List<string> args)
        {
            bool fullscreen = Flag(args, "--fullscreen");
            string wait = Take(args, "--wait");
            Leftover(args);
            string notReady = Setup.NotReady(home);
            if (notReady != null)
                throw new SetupException(notReady);
            using (var p = home.Launch(home.GetState("game"), fullscreen))
            {
                Console.WriteLine($"started {home.GameName}, pid {p.Id}");
                if (wait == null)
                    return 0;
                string log = home.PluginBootLine((int)(double.Parse(wait) * 1000), () => !p.HasExited);
                if (log == null)
                {
                    Console.Error.WriteLine(p.HasExited ? $"Azahar exited (code {p.ExitCode}) before the plugin wrote its log"
                                                        : $"no plugin boot log within {wait} s");
                    return 1;
                }
                Console.Write(log);
                return log.Contains("hooks: all ok") ? 0 : 1;
            }
        }

        static int CheatCommand(Home home, List<string> args)
        {
            var entries = Cheats.Load(home);
            if (args.Count > 0)
            {
                if (args.Count < 2 || (args[0] != "on" && args[0] != "off")) throw new SetupException("cheats on NAME|off NAME");
                string name = string.Join(" ", args.Skip(1));
                // The exact name, else the one cheat whose name starts with it ("max money").
                var entry = entries.FirstOrDefault(e => e.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
                if (entry == null)
                {
                    var matches = entries.Where(e => e.Name.StartsWith(name, StringComparison.OrdinalIgnoreCase)).ToList();
                    if (matches.Count > 1)
                        throw new SetupException("More than one cheat starts with that; use more of the name: "
                                                 + string.Join(", ", matches.Select(e => e.Name)));
                    entry = matches.SingleOrDefault();
                }
                if (entry == null) throw new SetupException("Unknown cheat name; use cheats to list names.");
                Cheats.Enable(entries, entry, args[0] == "on");
                Cheats.Save(home, entries);
            }
            foreach (var entry in entries) Console.WriteLine((entry.Enabled ? "on  " : "off ") + entry.Name);
            return 0;
        }

        static int Set(Home home, List<string> args)
        {
            var ini = PluginIni.Load(home.PluginIniPath);
            bool pluginChanged = false;
            string aspectChoice = null, graphicsChoice = null, glyphChoice = null, visualChoice = null;
            var textureOptions = new Dictionary<string, bool>();
            ControlPreset? controls = null;
            foreach (var a in args)
            {
                int eq = a.IndexOf('=');
                if (eq <= 0)
                    throw new SetupException($"expected KEY=VALUE, got '{a}'");
                string key = a.Substring(0, eq).Trim().ToLowerInvariant(), value = a.Substring(eq + 1).Trim();
                if (key == "controls")
                {
                    if (!home.HasAzahar)
                        throw new SetupException("run setup first");
                    controls = ParsePreset(value);
                }
                else if (key == "dump_textures" || key == "preload_textures" || key == "async_custom_loading")
                    textureOptions[key] = TexturePacks.ParseBool(value);
                else if (key == "graphics")
                {
                    if (!GameOptions.Graphics.Contains(value.ToLowerInvariant())) throw new SetupException("bad graphics");
                    graphicsChoice = value.ToLowerInvariant();
                }
                else if (key == "visual_quality")
                {
                    visualChoice = value.ToLowerInvariant();
                    VisualQuality.Validate(home, visualChoice);
                }
                else if (key == "glyphs" && value.Equals("auto", StringComparison.OrdinalIgnoreCase)) glyphChoice = "auto";
                else if (PluginIni.Keys.Contains(key))
                {
                    string error = PluginSettings.Validate(key, value);
                    if (error != null)
                        throw new SetupException(error);
                    if (key == "aspect")
                    {
                        aspectChoice = value.ToLowerInvariant();
                        value = Home.ResolveAspect(aspectChoice);
                    }
                    if (key == "glyphs") glyphChoice = value.ToLowerInvariant();
                    ini.Set(key, value);
                    pluginChanged = true;
                }
                else
                    throw new SetupException($"unknown setting '{key}'");
            }
            foreach (var option in textureOptions)
            {
                TexturePacks.Set(home, option.Key, option.Value);
                if (option.Value && option.Key == "preload_textures") Console.WriteLine("Preloading textures uses additional RAM.");
                if (option.Value && option.Key == "dump_textures") Console.WriteLine("Dumping textures slows the game. Folder: " + TexturePacks.DumpFolder(home));
            }
            if (pluginChanged)
                ini.Save(home.PluginIniPath);
            if (glyphChoice != null) home.SetState("glyphs", glyphChoice);
            if (visualChoice != null) VisualQuality.Set(home, visualChoice);
            if (graphicsChoice != null) GameOptions.SetGraphics(home, graphicsChoice);
            if (aspectChoice != null)
                home.SetState("aspect", aspectChoice);
            if (controls.HasValue)
                home.WriteConfig(controls.Value);
            else if (aspectChoice != null && home.HasAzahar)
                home.SyncWidescreen();
            if (glyphChoice != null && home.HasAzahar) GameOptions.Sync(home);
            return 0;
        }

        static int Status(Home home)
        {
            Console.WriteLine($"home: {home.Root}");
            Console.WriteLine($"azahar: {(home.HasAzahar ? home.AzaharDir + " (" + (Home.AzaharVersion(home.AzaharDir) ?? "version unknown") + ")" : "not copied yet")}");
            Console.WriteLine($"game: {home.GameName} (USA), title {Dump.Title(home.CurrentTitle)}");
            Console.WriteLine($"rom: {home.GetState("rom", "-")}");
            Console.WriteLine($"boot: {home.GetState("game", "-")}");
            int v = home.InstalledUpdateVersion();
            Console.WriteLine($"update: {(v < 0 ? "not installed" : Dump.VersionText(v))}");
            Console.WriteLine($"controls: {home.CurrentControls()}");
            var ini = PluginIni.Load(home.PluginIniPath);
            Console.WriteLine("plugin: " + string.Join(", ", PluginIni.Keys.Select(k => $"{k}={ini.Get(k)}" + (k == "aspect" && home.GetState("aspect") == "auto" ? " (auto)" : ""))));
            Console.WriteLine($"running: {(home.IsRunning() ? "yes, pid " + home.RunningPid() : "no")}");
            string notReady = Setup.NotReady(home);
            Console.WriteLine(notReady == null ? "ready to play" : "not ready: " + notReady);
            return 0;
        }
    }
}
