using System;
using System.IO;

namespace UsumOneScreen
{
    // First-run setup and play, shared by the window and the command line.
    static class Setup
    {
        public sealed class Options
        {
            public string Rom;
            public string Update;          // may be null when the update is already installed
            public string AzaharSource;    // null: keep the app's copy, or find an install
            public string Plugin;          // null: the one next to the app
            public ControlPreset Controls = ControlPreset.Keyboard;
        }

        // Returns the file Azahar should boot.
        public static string Run(Home home, Options o, Action<string> log)
        {
            if (string.IsNullOrEmpty(o.Rom))
                throw new SetupException("Pick your Pokémon Ultra Sun ROM.");
            var game = Dump.Check(o.Rom, DumpKind.Game);
            if (!game.Ok)
                throw new SetupException(game.Message);
            log(game.Message);

            if (!string.IsNullOrEmpty(o.Update))
            {
                var update = Dump.Check(o.Update, DumpKind.Update);
                if (!update.Ok)
                    throw new SetupException(update.Message);
                log(update.Message);
            }

            if (o.AzaharSource != null || !home.HasAzahar)
            {
                string source = o.AzaharSource;
                if (source == null)
                    foreach (var c in home.AzaharCandidates()) { source = c; break; }
                if (source == null)
                    throw new SetupException("Azahar isn't installed where the app looks. Install Azahar, or pick its folder.");
                home.InstallAzahar(source, log);
            }

            home.WriteConfig(o.Controls);
            log("Azahar set up: plugin loader on, top screen only, custom textures off.");
            home.InstallPlugin(o.Plugin ?? Home.BundledPlugin(), log);

            if (home.InstalledUpdateVersion() != Dump.UpdateVersion)
            {
                if (string.IsNullOrEmpty(o.Update))
                    throw new SetupException("Pick the Ultra Sun v1.2 update (a .cia file); the game needs it.");
                home.InstallCia(o.Update, log);
                int v = home.InstalledUpdateVersion();
                if (v != Dump.UpdateVersion)
                    throw new SetupException(v < 0 ? "Azahar didn't install the update."
                                                   : $"Azahar installed update version {Dump.VersionText(v)}, not {Dump.VersionText(Dump.UpdateVersion)}.");
            }
            else
                log("Update v1.2 is installed.");

            string boot = o.Rom;
            if (game.Info.Format == "CIA")
            {
                if (home.InstalledGame() == null)
                    home.InstallCia(o.Rom, log);
                boot = home.InstalledGame()
                       ?? throw new SetupException("Azahar didn't install the game.");
            }
            home.SetState("rom", Path.GetFullPath(o.Rom));
            home.SetState("game", boot);
            log("Ready to play.");
            return boot;
        }

        // What still stops Play, or null when it can start.
        public static string NotReady(Home home)
        {
            if (!home.HasAzahar)
                return "Not set up yet.";
            string game = home.GetState("game");
            if (game == null)
                return "Not set up yet.";
            if (!File.Exists(game))
                return $"The game file is gone: {game}. Pick it again.";
            if (!File.Exists(home.PluginPath))
                return "The plugin isn't installed. Run setup again.";
            if (home.InstalledUpdateVersion() != Dump.UpdateVersion)
                return "The v1.2 update isn't installed. Run setup again.";
            return null;
        }
    }
}
