using System;
using System.Windows.Forms;

namespace UsumOneScreen
{
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length == 4 && args[0] == "--apply-launcher-update")
                return UpdateCheck.Helper(args);
            UpdateCheck.CleanupHelper();
            if (args.Length > 0)
                return Cli.Run(args);    // --home DIR alone opens the window too
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm(Home.Default()));
            return 0;
        }
    }
}
