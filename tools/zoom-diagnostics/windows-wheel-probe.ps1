param(
    [ValidateRange(5, 900)]
    [int]$DurationSeconds = 120,

    [string]$LogPath = "$env:LOCALAPPDATA\MoonlightZoomDebug\windows-wheel.log"
)

$source = @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static class MoonlightWheelProbe
{
    private const int WH_MOUSE_LL = 14;
    private const int WM_MOUSEWHEEL = 0x020A;
    private const int WM_MOUSEHWHEEL = 0x020E;
    private const int WM_QUIT = 0x0012;
    private const uint LLMHF_INJECTED = 0x00000001;
    private const uint LLMHF_LOWER_IL_INJECTED = 0x00000002;

    [StructLayout(LayoutKind.Sequential)]
    private struct Point
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseHookData
    {
        public Point Point;
        public uint MouseData;
        public uint Flags;
        public uint Time;
        public UIntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Message
    {
        public IntPtr HWnd;
        public uint Value;
        public UIntPtr WParam;
        public IntPtr LParam;
        public uint Time;
        public Point Point;
    }

    private delegate IntPtr HookCallback(int code, IntPtr wParam, IntPtr lParam);

    private static HookCallback callback;
    private static StreamWriter writer;
    private static Stopwatch clock;
    private static long sequence;
    private static uint lastEventTime;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int hook, HookCallback callback, IntPtr module, uint threadId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(IntPtr hook);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern sbyte GetMessage(out Message message, IntPtr window, uint min, uint max);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool TranslateMessage(ref Message message);

    [DllImport("user32.dll")]
    private static extern IntPtr DispatchMessage(ref Message message);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostThreadMessage(uint threadId, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string moduleName);

    private static string GetForegroundProcess()
    {
        uint processId;
        GetWindowThreadProcessId(GetForegroundWindow(), out processId);

        try
        {
            return Process.GetProcessById((int) processId).ProcessName;
        }
        catch
        {
            return "unknown";
        }
    }

    private static IntPtr HandleMouse(int code, IntPtr wParam, IntPtr lParam)
    {
        int message = wParam.ToInt32();
        if (code >= 0 && (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL))
        {
            MouseHookData data = Marshal.PtrToStructure<MouseHookData>(lParam);
            short delta = unchecked((short) ((data.MouseData >> 16) & 0xFFFF));
            uint interval = lastEventTime == 0 ? 0 : unchecked(data.Time - lastEventTime);
            lastEventTime = data.Time;
            long currentSequence = Interlocked.Increment(ref sequence);
            bool injected = (data.Flags & LLMHF_INJECTED) != 0;
            bool lowerIntegrityInjected = (data.Flags & LLMHF_LOWER_IL_INJECTED) != 0;

            writer.WriteLine(
                "WheelProbe seq={0} elapsedMs={1} eventIntervalMs={2} axis={3} delta={4} injected={5} lowerIntegrityInjected={6} foreground={7} position=({8},{9})",
                currentSequence,
                clock.ElapsedMilliseconds,
                interval,
                message == WM_MOUSEWHEEL ? "y" : "x",
                delta,
                injected ? 1 : 0,
                lowerIntegrityInjected ? 1 : 0,
                GetForegroundProcess(),
                data.Point.X,
                data.Point.Y);
        }

        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    public static int Run(string logPath, int durationSeconds)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(logPath));
        using (writer = new StreamWriter(logPath, false))
        {
            writer.AutoFlush = true;
            clock = Stopwatch.StartNew();
            callback = HandleMouse;

            IntPtr hook = SetWindowsHookEx(WH_MOUSE_LL, callback, GetModuleHandle(null), 0);
            if (hook == IntPtr.Zero)
            {
                writer.WriteLine("WheelProbe error=set-hook-failed win32={0}", Marshal.GetLastWin32Error());
                return 1;
            }

            uint threadId = GetCurrentThreadId();
            using (Timer timer = new Timer(
                delegate { PostThreadMessage(threadId, WM_QUIT, UIntPtr.Zero, IntPtr.Zero); },
                null,
                durationSeconds * 1000,
                Timeout.Infinite))
            {
                writer.WriteLine("WheelProbe start durationSeconds={0}", durationSeconds);
                Message message;
                while (GetMessage(out message, IntPtr.Zero, 0, 0) > 0)
                {
                    TranslateMessage(ref message);
                    DispatchMessage(ref message);
                }
            }

            UnhookWindowsHookEx(hook);
            writer.WriteLine("WheelProbe stop events={0}", sequence);
        }

        return 0;
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp
exit [MoonlightWheelProbe]::Run($LogPath, $DurationSeconds)
