using System;
using System.IO;
using System.Linq;

namespace UsumOneScreen
{
    static class GameOptions
    {
        public static readonly string[] Graphics = { "keep", "native", "high", "ultra" };
        public static readonly string[] Glyphs = { "auto", "xbox", "playstation", "nintendo", "3ds" };
        public static string CurrentGraphics(Home home)
        {
            if (home.GetState("graphics") == "keep") return "keep";
            var ini = QtIni.Load(home.ConfigPath);
            if (ini.Get("Renderer", "resolution_factor\\default") == "true") return "keep";
            string factor = ini.Get("Renderer", "resolution_factor");
            if (factor == null) return home.GetState("graphics", "keep");
            return factor == "1" ? "native" : factor == "3" ? "high" : factor == "5" ? "ultra" : "keep";
        }
        public static string CurrentGlyphs(Home home)
        {
            var ini = PluginIni.Load(home.PluginIniPath);
            return home.GetState("glyphs") == "auto" ? "auto" : !ini.Has("glyphs") ? home.GetState("glyphs", "auto") : ini.Get("glyphs").ToLowerInvariant();
        }
        public static void Sync(Home home)
        {
            VisualQuality.Sync(home);
            string graphics = home.GetState("graphics", "keep");
            if (graphics != "keep")
            {
                var ini = QtIni.Load(home.ConfigPath);
                ini.Set("Renderer", "resolution_factor", graphics == "native" ? "1" : graphics == "high" ? "3" : "5");
                ini.Save(home.ConfigPath);
                string custom = Path.Combine(home.User, "config", "custom");
                if (Directory.Exists(custom))
                    foreach (var file in Directory.GetFiles(custom, "*.ini"))
                    {
                        var game = QtIni.Load(file);
                        game.Remove("Renderer", k => k == "resolution_factor");
                        game.Save(file);
                    }
            }
            string glyphChoice = CurrentGlyphs(home);
            if (glyphChoice == "auto") home.SetState("glyphs", "auto");
            string glyph = glyphChoice == "auto" ? Win32.DetectGlyphs(home.CurrentControls()) : glyphChoice;
            var plugin = PluginIni.Load(home.PluginIniPath);
            if (!plugin.Has("glyphs") || plugin.Get("glyphs") != glyph)
            {
                plugin.Set("glyphs", glyph);
                plugin.Save(home.PluginIniPath);
            }
        }

        public static void SetGraphics(Home home, string value)
        {
            value = value.ToLowerInvariant();
            if (!Graphics.Contains(value)) throw new SetupException("bad graphics (use keep, native, high or ultra)");
            home.SetState("graphics", value);
            if (home.HasAzahar) Sync(home);
        }
    }
}
