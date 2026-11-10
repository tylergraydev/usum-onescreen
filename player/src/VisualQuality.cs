using System;
using System.IO;
using System.Linq;
using System.Text;

namespace UsumOneScreen
{
    static class VisualQuality
    {
        public static readonly string[] Values = { "keep", "off", "sharpen", "aa", "sharpen-aa" };
        public static readonly string[] Labels = { "Keep Azahar's setting", "Off", "Sharpen", "Anti-aliasing (FXAA-style)", "Sharpen + AA" };
        static readonly string[] Shaders = { null, "None (builtin)", "usum-sharpen", "usum-aa", "usum-sharpen-aa" };

        static QtIni GameConfig(Home home) => QtIni.Load(Path.Combine(home.User, "config", "custom", Dump.Title(home.CurrentTitle) + ".ini"));

        // Verified: 2126.2 config.cpp ReadGlobalSetting/ReadBasicSetting.
        // An absent/default-true value uses the compiled Windows default.
        static string Effective(Home home, string section, string key, string fallback)
        {
            var game = GameConfig(home);
            var ini = game.Get(section, key + "\\use_global") == "false" ? game : QtIni.Load(home.ConfigPath);
            return ini.Get(section, key + "\\default") == "true" ? fallback : ini.Get(section, key) ?? fallback;
        }

        public static string UnavailableReason(Home home)
        {
            // Verified in 2126.2 renderer_opengl.cpp / renderer_vulkan.cpp:
            // only OpenGL loads pp_shader_name; Vulkan compiles fixed shaders.
            if (Effective(home, "Renderer", "graphics_api", "1") != "1")
                return "Unavailable: requires OpenGL. Change Graphics API in Azahar, then reopen this window.";
            if (Effective(home, "Layout", "render_3d", "0") != "0")
                return "Unavailable: turn off stereoscopic 3D in Azahar, then reopen this window.";
            return null;
        }

        public static string Current(Home home)
        {
            if (home.GetState("visual_quality", "keep") == "keep") return "keep";
            int index = Array.IndexOf(Shaders, Effective(home, "Layout", "pp_shader_name", "None (builtin)"));
            return index > 0 ? Values[index] : "keep";
        }

        public static void Validate(Home home, string value)
        {
            if (!Values.Contains(value)) throw new SetupException("bad visual_quality (use keep, off, sharpen, aa or sharpen-aa)");
            if (value != "keep" && value != "off" && UnavailableReason(home) is string reason)
                throw new SetupException(reason);
        }

        public static void Set(Home home, string value)
        {
            value = value.ToLowerInvariant();
            Validate(home, value);
            home.SetState("visual_quality", value);
            if (home.HasAzahar) Sync(home);
        }

        public static void Sync(Home home)
        {
            string value = home.GetState("visual_quality", "keep");
            int index = Array.IndexOf(Values, value);
            if (index <= 0) return;
            // Retain the saved choice when the player changes API externally;
            // never switch their API or prevent Play on Vulkan.
            if (value != "off" && UnavailableReason(home) != null) return;
            if (value != "off") Install(home, value, Shaders[index]);
            var ini = QtIni.Load(home.ConfigPath);
            ini.Set("Layout", "pp_shader_name", Shaders[index]);
            ini.Save(home.ConfigPath);
            string custom = Path.Combine(home.User, "config", "custom");
            if (Directory.Exists(custom))
                foreach (var file in Directory.GetFiles(custom, "*.ini"))
                {
                    var game = QtIni.Load(file);
                    game.Remove("Layout", k => k == "pp_shader_name" || k == "pp_shader_name\\use_global");
                    game.Save(file);
                }
        }

        static void Install(Home home, string value, string name)
        {
            // Verified on upstream master post_processing_opengl.cpp: bare stem,
            // user/shaders/<stem>.glsl, Dolphin-style helper header supplied by Azahar.
            // Verified 2026-11-09 on Azahar 2126.2 (OpenGL): our shader compiled from
            // user/shaders/<stem>.glsl; a broken file there logged compile errors.
            // No #version: Azahar supplies the GLSL version and interface.
            using (var resource = typeof(VisualQuality).Assembly.GetManifestResourceStream("UsumOneScreen.PostProcess"))
            using (var reader = new StreamReader(resource ?? throw new SetupException("Missing post-processing shader resource.")))
            {
                string source = "#define USUM_AA " + (value == "sharpen" ? "0" : "1") + "\n" +
                                "#define USUM_SHARPEN " + (value == "aa" ? "0" : "1") + "\n" + reader.ReadToEnd();
                string dir = Path.Combine(home.User, "shaders");
                Directory.CreateDirectory(dir);
                string path = Path.Combine(dir, name + ".glsl");
                if (!File.Exists(path) || File.ReadAllText(path) != source)
                    File.WriteAllText(path, source, new UTF8Encoding(false));
            }
        }
    }
}
