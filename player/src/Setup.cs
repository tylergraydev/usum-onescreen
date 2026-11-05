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
            public string AzaharSource;    // null: keep the portable copy, or download/discover
            public string Plugin;          // null: adjacent file, then embedded resource
            public ControlPreset Controls = ControlPreset.Keyboard;
        }

        // Returns the file Azahar should boot.
        public static string Run(Home home, Options o, Action<string> log)
        {
            if (string.IsNullOrEmpty(o.Rom))
                throw new SetupException("Pick your Pokémon Ultra Sun or Ultra Moon (USA) ROM.");
            if (GameFiles.IsZip(o.Rom))
            {
                var files = GameFiles.Expand(home, o.Rom, log);
                o.Rom = GameFiles.Pick(files, DumpKind.Game);
                if (string.IsNullOrEmpty(o.Update)) o.Update = GameFiles.Pick(files, DumpKind.Update, false);
            }
            if (GameFiles.IsZip(o.Update))
                o.Update = GameFiles.Pick(GameFiles.Expand(home, o.Update, log), DumpKind.Update);
            var game = Dump.Check(o.Rom, DumpKind.Game);
            if (!game.Ok)
                throw new SetupException(game.Message);
            log(game.Message);
            ulong title = game.Info.TitleId;

            int wanted = -1;    // the version of the update the player picked, if any
            if (!string.IsNullOrEmpty(o.Update))
            {
                var update = Dump.Check(o.Update, DumpKind.Update, title);
                if (!update.Ok)
                    throw new SetupException(update.Message);
                log(update.Message);
                wanted = update.Info.Version;
            }

            if (o.AzaharSource != null || !home.HasAzahar)
            {
                string source = Home.BundledAzahar() ?? o.AzaharSource;
                if (source == null)
                {
                    try { AzaharDownload.Install(home, log); }
                    catch (SetupException e)
                    {
                        log(e.Message);
                        foreach (var c in home.AzaharCandidates()) { source = c; break; }
                        if (source == null) throw;
                    }
                }
                if (source != null) home.InstallAzahar(source, log);
            }

            home.WriteConfig(o.Controls);
            log("Azahar set up: plugin loader on, top screen only, custom textures off.");
            home.InstallSelectedPlugin(o.Plugin, title, log);

            // A picked update replaces a different installed one (Azahar overwrites it).
            int installed = home.InstalledUpdateVersion(title);
            if (wanted >= 0 ? installed != wanted : !Dump.IsSupportedUpdate(installed, title))
            {
                if (wanted < 0)
                    throw new SetupException($"Pick the {Dump.GameName(title)} v1.2 update (a .cia file); the game needs it.");
                home.InstallCia(o.Update, log);
                installed = home.InstalledUpdateVersion(title);
                if (installed != wanted)
                    throw new SetupException(installed < 0 ? "Azahar didn't install the update."
                                                           : $"Azahar installed update version {Dump.VersionText(installed)}, not {Dump.VersionText(wanted)}.");
            }
            else
                log($"{Dump.GameName(title)} update v1.2 is installed (version {Dump.VersionText(installed)}).");
            if (installed == Dump.PatchedUpdateVersion)
                log("Note: " + Dump.PatchedWarning);

            string boot = o.Rom;
            if (game.Info.Format == "CIA")
            {
                if (home.InstalledGame(title) == null)
                    home.InstallCia(o.Rom, log);
                boot = home.InstalledGame(title)
                       ?? throw new SetupException("Azahar didn't install the game.");
            }
            home.SetState("rom", Path.GetFullPath(o.Rom));
            home.SetState("game", boot);
            home.SetState("title_id", Dump.Title(title));
            log($"Ready to play {Dump.GameName(title)}.");
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
            if (!Dump.IsSupportedUpdate(home.InstalledUpdateVersion(), home.CurrentTitle))
                return $"The {home.GameName} v1.2 update isn't installed. Run setup again.";
            return null;
        }
    }
}
