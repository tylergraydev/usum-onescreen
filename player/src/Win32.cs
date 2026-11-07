using System;
using System.Runtime.InteropServices;
using System.Text;

namespace UsumOneScreen
{
    static class Win32
    {
        delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

        [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
        [DllImport("user32.dll")] static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
        [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder name, ref int size);
        [DllImport("kernel32.dll")] static extern bool AttachConsole(int pid);
        [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int which);

        [StructLayout(LayoutKind.Sequential)]
        struct RawDevice { public IntPtr Handle; public uint Type; }
        [StructLayout(LayoutKind.Explicit, Size = 32)]
        struct DeviceInfo
        {
            [FieldOffset(0)] public uint Size;
            [FieldOffset(4)] public uint Type;
            [FieldOffset(8)] public uint Vendor;
            [FieldOffset(20)] public ushort UsagePage;
            [FieldOffset(22)] public ushort Usage;
        }
        [DllImport("user32.dll")] static extern uint GetRawInputDeviceList([Out] RawDevice[] list, ref uint count, uint size);
        [DllImport("user32.dll", EntryPoint = "GetRawInputDeviceInfoW")]
        static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, ref DeviceInfo info, ref uint size);

        // Only Generic Desktop joystick/gamepad/multi-axis HID devices count;
        // a Microsoft keyboard or Sony audio device must not select the icons.
        public static string GlyphsForVendors(uint[] vendors, ControlPreset preset)
        {
            foreach (uint vendor in vendors)
            {
                if (vendor == 0x045E) return "xbox";
                if (vendor == 0x054C) return "playstation";
                if (vendor == 0x057E) return "nintendo";
            }
            return preset == ControlPreset.Keyboard ? "3ds" : "xbox";
        }
        public static string DetectGlyphs(ControlPreset preset)
        {
            var vendors = new System.Collections.Generic.List<uint>();
            uint count = 0, stride = (uint)Marshal.SizeOf(typeof(RawDevice));
            if (GetRawInputDeviceList(null, ref count, stride) != uint.MaxValue && count > 0)
            {
                var devices = new RawDevice[count];
                uint received = GetRawInputDeviceList(devices, ref count, stride);
                if (received != uint.MaxValue)
                    for (int i = 0; i < received; i++)
                    {
                        if (devices[i].Type != 2) continue;
                        uint size = 32;
                        var info = new DeviceInfo { Size = size };
                        if (GetRawInputDeviceInfo(devices[i].Handle, 0x2000000b, ref info, ref size) != uint.MaxValue &&
                            info.Type == 2 && info.UsagePage == 1 && (info.Usage == 4 || info.Usage == 5 || info.Usage == 8))
                            vendors.Add(info.Vendor);
                    }
            }
            return GlyphsForVendors(vendors.ToArray(), preset);
        }

        const uint WM_CLOSE = 0x0010;
        const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

        // Closes the first visible top-level window of `pid` whose title is one
        // of `titles`, and returns that title (null if there is none yet).
        public static string CloseWindowTitled(int pid, params string[] titles)
        {
            string found = null;
            EnumWindows((hwnd, _) =>
            {
                if (GetWindowThreadProcessId(hwnd, out uint owner) == 0 || owner != pid || !IsWindowVisible(hwnd))
                    return true;
                var text = new StringBuilder(256);
                GetWindowText(hwnd, text, text.Capacity);
                if (Array.IndexOf(titles, text.ToString()) < 0)
                    return true;
                found = text.ToString();
                PostMessage(hwnd, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
                return false;
            }, IntPtr.Zero);
            return found;
        }

        public static string ProcessPath(int pid)
        {
            IntPtr h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
            if (h == IntPtr.Zero)
                return null;
            try
            {
                var name = new StringBuilder(32768);
                int size = name.Capacity;
                return QueryFullProcessImageName(h, 0, name, ref size) ? name.ToString() : null;
            }
            finally { CloseHandle(h); }
        }

        // A GUI-subsystem exe has no console; when run from one with output not
        // redirected, attach to it so command-line output shows up.
        public static void UseParentConsole()
        {
            const int STD_OUTPUT_HANDLE = -11;
            IntPtr h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h == IntPtr.Zero || h == new IntPtr(-1))
                AttachConsole(-1);
        }
    }
}
