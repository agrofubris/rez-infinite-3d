// Test helper: runs a program on a separate, never-shown Windows desktop, so
// nothing it opens (Rez's own mirror window, launchers, dialogs) can appear on
// the user's screen. Child processes inherit the hidden desktop. Sets
// REZ3D_HEADLESS=1 so the stereo runtime renders off-screen as well.
//
//   RunHidden.exe <seconds> <exe> [args...]
//
// Waits <seconds>, then kills the process tree it started and exits.
//
// Build: %WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe /out:RunHidden.exe RunHidden.cs

using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

static class RunHidden
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb;
        public string lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION
    {
        public IntPtr hProcess, hThread;
        public int dwProcessId, dwThreadId;
    }

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, uint access, IntPtr sa);

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool CloseDesktop(IntPtr desktop);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcess(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                     IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);

    const uint GENERIC_ALL = 0x10000000;

    static int Main(string[] args)
    {
        // --show-presenter: keep the stereo window (it is created on the hidden desktop too)
        bool showPresenter = args.Length > 0 && args[0] == "--show-presenter";
        if (showPresenter)
        {
            var rest = new string[args.Length - 1];
            Array.Copy(args, 1, rest, 0, rest.Length);
            args = rest;
        }
        if (args.Length < 2)
        {
            Console.Error.WriteLine("usage: RunHidden <seconds> <exe> [args...]");
            return 2;
        }
        int seconds = int.Parse(args[0]);
        string exe = Path.GetFullPath(args[1]);
        var cmd = new StringBuilder("\"" + exe + "\"");
        for (int i = 2; i < args.Length; i++) cmd.Append(' ').Append(args[i]);

        string name = "Rez3DTest";
        IntPtr desk = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, GENERIC_ALL, IntPtr.Zero);
        if (desk == IntPtr.Zero) throw new Win32Exception();

        if (!showPresenter) Environment.SetEnvironmentVariable("REZ3D_HEADLESS", "1");
        Environment.SetEnvironmentVariable("REZ3D_MUTE", "1");  // test runs never make sound
        var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)), lpDesktop = name };
        PROCESS_INFORMATION pi;
        if (!CreateProcess(null, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, Path.GetDirectoryName(exe),
                           ref si, out pi))
            throw new Win32Exception();
        Console.WriteLine("started pid " + pi.dwProcessId + " on hidden desktop " + name);

        System.Threading.Thread.Sleep(seconds * 1000);

        // The launcher may already have exited after spawning the game: kill by tree and by name
        Process.Start(new ProcessStartInfo("taskkill", "/F /T /PID " + pi.dwProcessId)
                      { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true }).WaitForExit();
        foreach (var p in Process.GetProcessesByName("REZ")) { try { p.Kill(); p.WaitForExit(5000); } catch { } }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseDesktop(desk);
        Console.WriteLine("done");
        return 0;
    }
}
