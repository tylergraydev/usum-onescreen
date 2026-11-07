using System;
using System.Globalization;
using System.Linq;

namespace UsumOneScreen
{
    // The values usum-onescreen.ini accepts, as Sources/Settings.cpp parses them.
    static class PluginSettings
    {
        public static readonly string[] KeyNames =
            { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Start", "Select", "Up", "Down", "Left", "Right" };

        // Offered in the window; the file takes any combo of KeyNames.
        public static readonly string[] KeyChoices =
            { "L+R", "ZL", "ZR", "Select", "L+R+Select", "ZL+ZR", "L", "R", "none" };

        public static readonly string[] CursorMoves = { "cstick", "dpad", "circlepad" };
        public static readonly string[] Sizes = { "1/2", "2/3" };
        public static readonly string[] Corners = { "bottom-right", "bottom-left", "top-right", "top-left" };
        public static readonly string[] OnOff = { "off", "on" };

        public static readonly string[] AspectChoices = { "Auto", "Original (5:3)", "16:9", "21:9" };
        public static readonly string[] AspectValues = { "auto", "0", "16:9", "21:9" };

        public static string NormalizeAspect(string value) =>
            string.Equals(value, "off", StringComparison.OrdinalIgnoreCase) ? "0" : value.Trim().ToLowerInvariant();

        public static bool IsWidescreen(string value)
        {
            string v = value?.Trim();
            return v == "16:9" || v == "21:9" ||
                   (float.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out float ratio) &&
                    ratio > 0 && !float.IsInfinity(ratio) && !float.IsNaN(ratio));
        }

        public static string ResolveAspect(string choice, int width, int height)
        {
            if (!string.Equals(choice, "auto", StringComparison.OrdinalIgnoreCase))
                return NormalizeAspect(choice);
            double ratio = height > 0 ? (double)width / height : 0;
            return ratio >= 2.2 ? "21:9" : ratio >= 1.7 ? "16:9" : "0";
        }

        static string Squash(string s) => new string(s.ToLowerInvariant().Where(c => c != '-' && c != '_' && c != ' ').ToArray());

        // An error message, or null if the plugin will accept the value.
        public static string Validate(string key, string value)
        {
            string v = Squash(value);
            switch (key)
            {
                case "aspect":
                    string aspect = value.Trim().ToLowerInvariant();
                    return new[] { "auto", "0", "off", "16:9", "21:9" }.Contains(aspect) ||
                           (float.TryParse(aspect, NumberStyles.Float, CultureInfo.InvariantCulture, out float ratio) &&
                            ratio > 0 && !float.IsInfinity(ratio) && !float.IsNaN(ratio)) ? null
                         : $"bad aspect '{value}' (use auto, 0, off, 16:9, 21:9 or a positive finite float)";
                case "panel_key":
                case "touch_key":
                case "map_key":
                    if (v == "none" || v.Split('+').All(part => KeyNames.Any(k => k.ToLowerInvariant() == part)))
                        return null;
                    return $"bad {key} '{value}' (use {string.Join(" ", KeyNames)}, joined with +, or none)";
                case "cursor_move":
                    return v == "cstick" || v == "dpad" || v == "circlepad" || v == "cpad" ? null
                         : $"bad cursor_move '{value}' (use cstick, dpad or circlepad)";
                case "cursor_speed":
                    return int.TryParse(v, out int n) && n >= 1 && n <= 20 ? null
                         : $"bad cursor_speed '{value}' (use 1 to 20)";
                case "size":
                    return v == "1/2" || v == "half" || v == "2/3" ? null : $"bad size '{value}' (use 1/2 or 2/3)";
                case "corner":
                    return Corners.Any(c => Squash(c) == v) ? null
                         : $"bad corner '{value}' (use bottom-right, bottom-left, top-right or top-left)";
                case "auto":
                    return new[] { "on", "off", "true", "false", "yes", "no", "1", "0" }.Contains(v) ? null
                         : $"bad auto '{value}' (use on or off)";
            }
            return $"unknown setting '{key}'";
        }
    }
}
