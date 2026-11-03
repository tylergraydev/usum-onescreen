using System.Collections.Generic;

namespace UsumOneScreen
{
    enum ControlPreset { Keyboard, Gamepad, Custom }

    // Controller presets written into Azahar's [Controls]. "Custom" leaves
    // whatever the player mapped in Azahar's own Controls window alone.
    static class ControlPresets
    {
        public static string Name(ControlPreset p) =>
            p == ControlPreset.Keyboard ? "Keyboard (Azahar's default keys)"
            : p == ControlPreset.Gamepad ? "Gamepad (any controller, Xbox layout)"
            : "Custom (map buttons in Azahar)";

        // SDL game controller buttons and axes (SDL_GameControllerButton/Axis).
        const int A = 0, B = 1, X = 2, Y = 3, Back = 4, Guide = 5, Start = 6,
                  LB = 9, RB = 10, Up = 11, Down = 12, Left = 13, Right = 14;
        const int LeftX = 0, LeftY = 1, RightX = 2, RightY = 3, TriggerL = 4, TriggerR = 5;

        // "all" binds every connected controller, so no GUID is needed
        // (src/input_common/sdl/sdl_impl.cpp, maptype). The layout follows
        // Azahar's own xinput auto-map: the 3DS A button is the controller's
        // right face button.
        static string Button(int b) => $"\"api:controller,button:{b},engine:sdl,maptype:all\"";
        static string Trigger(int axis) => $"\"api:controller,axis:{axis},direction:+,engine:sdl,maptype:all,threshold:0.500000\"";
        static string Stick(int x, int y) => $"\"api:controller,axis_x:{x},axis_y:{y},deadzone:0.100000,engine:sdl,maptype:all\"";

        static readonly Dictionary<string, string> Gamepad = new Dictionary<string, string>
        {
            ["button_a"] = Button(B), ["button_b"] = Button(A), ["button_x"] = Button(Y), ["button_y"] = Button(X),
            ["button_up"] = Button(Up), ["button_down"] = Button(Down), ["button_left"] = Button(Left), ["button_right"] = Button(Right),
            ["button_l"] = Button(LB), ["button_r"] = Button(RB), ["button_start"] = Button(Start), ["button_select"] = Button(Back),
            ["button_zl"] = Trigger(TriggerL), ["button_zr"] = Trigger(TriggerR), ["button_home"] = Button(Guide),
            ["circle_pad"] = Stick(LeftX, LeftY), ["c_stick"] = Stick(RightX, RightY),
        };

        public static void Apply(QtIni ini, ControlPreset preset)
        {
            if (preset == ControlPreset.Custom)
                return;
            // One profile, ours; Azahar fills in every binding we don't write
            // with its keyboard default.
            ini.Remove("Controls", k => k == "profile" || k.StartsWith("profiles\\"));
            ini.Set("Controls", "profile", "0");
            ini.Set("Controls", "profiles\\1\\name", preset == ControlPreset.Gamepad ? "USUM gamepad" : "USUM keyboard");
            if (preset == ControlPreset.Gamepad)
                foreach (var kv in Gamepad)
                    ini.Set("Controls", "profiles\\1\\" + kv.Key, kv.Value);
            ini.SetPlain("Controls", "profiles\\size", "1");
        }
    }
}
