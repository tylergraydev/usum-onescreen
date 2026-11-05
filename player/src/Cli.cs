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
                         cursor_speed size corner auto) and controls=PRESET
  status                 show the setup
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
                    case "check": return Check(home, list);
                    case "setup": return SetupCommand(home, list);
                    case "play": return Play(home, list);
                    case "set": return Set(home, list);
                    case "status": return Status(home);
                    case "snapshot": return Snapshot(home, list);
                    default:
                        Console.WriteLine(Usage);
                        return command == "help" || command == "--help" || command == "-h" ? 0 : 2;
                }
            }
            catch (SetupException e)
            {
                Console.Error.WriteLine("error: " + e.Message);
                return 1;
            }
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

        static int Set(Home home, List<string> args)
        {
            var ini = PluginIni.Load(home.PluginIniPath);
            bool pluginChanged = false;
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
                    home.WriteConfig(ParsePreset(value));
                }
                else if (PluginIni.Keys.Contains(key))
                {
                    string error = PluginSettings.Validate(key, value);
                    if (error != null)
                        throw new SetupException(error);
                    ini.Set(key, value);
                    pluginChanged = true;
                }
                else
                    throw new SetupException($"unknown setting '{key}'");
            }
            if (pluginChanged)
                ini.Save(home.PluginIniPath);
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
            Console.WriteLine("plugin: " + string.Join(", ", PluginIni.Keys.Select(k => $"{k}={ini.Get(k)}")));
            Console.WriteLine($"running: {(home.IsRunning() ? "yes, pid " + home.RunningPid() : "no")}");
            string notReady = Setup.NotReady(home);
            Console.WriteLine(notReady == null ? "ready to play" : "not ready: " + notReady);
            return 0;
        }
    }
}
