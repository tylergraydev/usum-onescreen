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
