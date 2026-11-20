# Shared helpers for Notch tests. Dot-source this file: . "$PSScriptRoot\lib.ps1"
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
$Exe = Join-Path $Root 'build\Perch.exe'
$Shots = Join-Path $PSScriptRoot 'screenshots'
New-Item -ItemType Directory -Force $Shots | Out-Null

Add-Type -AssemblyName System.Drawing, System.Windows.Forms
if (-not ('NotchTest' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class NotchTest {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    public const uint WM_COMMAND = 0x0111, WM_APP = 0x8000;
    public static IntPtr FindByClass(string cls) { return FindWindow(cls, null); }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, System.Text.StringBuilder sb, int max);
    public static string Title(IntPtr h) { var sb = new System.Text.StringBuilder(600); GetWindowText(h, sb, 600); return sb.ToString(); }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern uint GlobalGetAtomName(ushort atom, System.Text.StringBuilder sb, int max);
    // Text of the last toast ("main | sub"), published by the notch for tests.
    public static string Toast(IntPtr h) {
        var atom = (ushort)(long)GetProp(h, "PerchToastAtom");
        if (atom == 0) return "";
        var sb = new System.Text.StringBuilder(256); GlobalGetAtomName(atom, sb, 256); return sb.ToString();
    }
    public static int ToastLevel(IntPtr h) { return (int)SendMessage(h, WM_APP + 7, IntPtr.Zero, IntPtr.Zero); }
    public static int Battery(IntPtr h) { return (int)SendMessage(h, WM_APP + 8, IntPtr.Zero, IntPtr.Zero); }
    public static void SimPower(IntPtr h, int onAC, int pct) { SendMessage(h, WM_APP + 9, (IntPtr)onAC, (IntPtr)pct); }
    public static int Brightness(IntPtr h) { return (int)SendMessage(h, WM_APP + 10, IntPtr.Zero, IntPtr.Zero); }
    public static int State(IntPtr h) { return (int)SendMessage(h, WM_APP + 1, IntPtr.Zero, IntPtr.Zero); }
    public static void Command(IntPtr h, int id) { SendMessage(h, WM_COMMAND, (IntPtr)id, IntPtr.Zero); }

    // Press a chord like Ctrl+Alt+<key>.
    public static void Chord(byte[] mods, byte key) {
        foreach (var m in mods) keybd_event(m, 0, 0, UIntPtr.Zero);
        keybd_event(key, 0, 0, UIntPtr.Zero);
        keybd_event(key, 0, 2, UIntPtr.Zero);
        for (int i = mods.Length - 1; i >= 0; i--) keybd_event(mods[i], 0, 2, UIntPtr.Zero);
    }
}
'@
}
[NotchTest]::SetProcessDPIAware() | Out-Null

# Core Audio access for tests: read/set the output volume and switch the default output device.
if (-not ('NotchAudio' -as [type])) {
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCo {}
[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumeratorT {
    [PreserveSig] int EnumAudioEndpoints(int flow, int stateMask, out IMMDeviceCollectionT devices);
    [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDeviceT device);
}
[ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollectionT {
    [PreserveSig] int GetCount(out uint n);
    [PreserveSig] int Item(uint i, out IMMDeviceT d);
}
[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceT {
    [PreserveSig] int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
    [PreserveSig] int OpenPropertyStore(int access, out IntPtr ps);
    [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
}
[ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolumeT {
    [PreserveSig] int RegisterControlChangeNotify(IntPtr p);
    [PreserveSig] int UnregisterControlChangeNotify(IntPtr p);
    [PreserveSig] int GetChannelCount(out uint c);
    [PreserveSig] int SetMasterVolumeLevel(float db, ref Guid ctx);
    [PreserveSig] int SetMasterVolumeLevelScalar(float level, ref Guid ctx);
    [PreserveSig] int GetMasterVolumeLevel(out float db);
    [PreserveSig] int GetMasterVolumeLevelScalar(out float level);
    [PreserveSig] int SetChannelVolumeLevel(uint ch, float db, ref Guid ctx);
    [PreserveSig] int SetChannelVolumeLevelScalar(uint ch, float level, ref Guid ctx);
    [PreserveSig] int GetChannelVolumeLevel(uint ch, out float db);
    [PreserveSig] int GetChannelVolumeLevelScalar(uint ch, out float level);
    [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}
// Undocumented but long-standing interface Windows' Sound settings use to change the default device.
[ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")] class PolicyConfigClientCo {}
[ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IPolicyConfigT {
    [PreserveSig] int GetMixFormat(IntPtr a, IntPtr b);
    [PreserveSig] int GetDeviceFormat(IntPtr a, int b, IntPtr c);
    [PreserveSig] int ResetDeviceFormat(IntPtr a);
    [PreserveSig] int SetDeviceFormat(IntPtr a, IntPtr b, IntPtr c);
    [PreserveSig] int GetProcessingPeriod(IntPtr a, int b, IntPtr c, IntPtr d);
    [PreserveSig] int SetProcessingPeriod(IntPtr a, IntPtr b);
    [PreserveSig] int GetShareMode(IntPtr a, IntPtr b);
    [PreserveSig] int SetShareMode(IntPtr a, IntPtr b);
    [PreserveSig] int GetPropertyValue(IntPtr a, IntPtr b, IntPtr c);
    [PreserveSig] int SetPropertyValue(IntPtr a, IntPtr b, IntPtr c);
    [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string id, int role);
    [PreserveSig] int SetEndpointVisibility(IntPtr a, int b);
}

public static class NotchAudio {
    static IMMDeviceEnumeratorT Enum() { return (IMMDeviceEnumeratorT)new MMDeviceEnumeratorCo(); }
    static IAudioEndpointVolumeT OutVol() {
        IMMDeviceT d; Marshal.ThrowExceptionForHR(Enum().GetDefaultAudioEndpoint(0, 0, out d));
        Guid iid = typeof(IAudioEndpointVolumeT).GUID; object o;
        Marshal.ThrowExceptionForHR(d.Activate(ref iid, 23, IntPtr.Zero, out o));
        return (IAudioEndpointVolumeT)o;
    }
    public static float GetVolume() { float v; OutVol().GetMasterVolumeLevelScalar(out v); return v; }
    public static void SetVolume(float v) { Guid g = Guid.Empty; Marshal.ThrowExceptionForHR(OutVol().SetMasterVolumeLevelScalar(v, ref g)); }
    public static bool GetMute() { bool m; OutVol().GetMute(out m); return m; }
    public static void SetMute(bool m) { Guid g = Guid.Empty; Marshal.ThrowExceptionForHR(OutVol().SetMute(m, ref g)); }

    public static string[] OutputIds() {
        IMMDeviceCollectionT c; var list = new List<string>();
        if (Enum().EnumAudioEndpoints(0, 1, out c) != 0) return list.ToArray();
        uint n; c.GetCount(out n);
        for (uint i = 0; i < n; i++) { IMMDeviceT d; string id; c.Item(i, out d); d.GetId(out id); list.Add(id); }
        return list.ToArray();
    }
    public static string DefaultOutputId(int role) { IMMDeviceT d; if (Enum().GetDefaultAudioEndpoint(0, role, out d) != 0) return null; string id; d.GetId(out id); return id; }
    public static void SetDefaultOutput(string id, int role) {
        var pc = (IPolicyConfigT)new PolicyConfigClientCo();
        Marshal.ThrowExceptionForHR(pc.SetDefaultEndpoint(id, role));
    }
}
'@
}

function Get-ToastText { [NotchTest]::Toast((Get-NotchWindow)) }

# Real OLE drag and drop for the shelf tests.
# Dragging INTO the notch uses a genuine OLE drag with our own drop source, so no mouse button
# is pressed at all: Windows still delivers the drop to whatever is under the cursor (the
# always-on-top notch). Dragging OUT of the notch needs a real press on a notch tile; the test
# only releases it over its own window after checking that window is on top.
if (-not ('NotchDrag' -as [type])) {
Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

[ComImport, Guid("00000121-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IOleDropSource {
    [PreserveSig] int QueryContinueDrag(int escapePressed, int keyState);
    [PreserveSig] int GiveFeedback(int effect);
}

// Drops when told to (or cancels after a deadline), instead of watching the mouse button.
[ComVisible(true)]
public class TimedDropSource : IOleDropSource {
    public volatile bool DropNow;
    public DateTime Deadline = DateTime.Now.AddSeconds(15);
    public int QueryContinueDrag(int escapePressed, int keyState) {
        if (DropNow) return 0x00040100;                                   // DRAGDROP_S_DROP
        if (escapePressed != 0 || DateTime.Now > Deadline) return 0x00040101; // DRAGDROP_S_CANCEL
        return 0;
    }
    public int GiveFeedback(int effect) { return 0x00040102; }            // DRAGDROP_S_USEDEFAULTCURSORS
}

public static class NotchDrag {
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("ole32.dll")] static extern int DoDragDrop(System.Runtime.InteropServices.ComTypes.IDataObject data, IOleDropSource source, int okEffects, out int effect);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(System.Drawing.Point p);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint flags);
    public const uint LEFTDOWN = 0x2, LEFTUP = 0x4;
    public static string Log = "";

    // Puts one of our windows at the top of the always-on-top band.
    public static void BringToFront(Form f) {
        SetWindowPos(f.Handle, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010 | 0x0040); // TOPMOST, no move/size/activate, show
        Application.DoEvents(); Thread.Sleep(150); Application.DoEvents();
    }
    // True only if our window is what's actually under (x, y). Tests never drop onto anything else.
    public static bool IsTopAt(Form f, int x, int y) {
        return !f.IsDisposed && GetAncestor(WindowFromPoint(new System.Drawing.Point(x, y)), 2) == f.Handle;
    }

    // Ends a stuck drag after `ms` by releasing the button and pressing Esc.
    static Thread Watchdog(int ms) {
        var t = new Thread(() => {
            try { Thread.Sleep(ms); } catch (ThreadInterruptedException) { return; }
            Log += "watchdog fired; ";
            mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
            keybd_event(0x1B, 0, 0, UIntPtr.Zero); keybd_event(0x1B, 0, 2, UIntPtr.Zero);
        });
        t.IsBackground = true; t.Start(); return t;
    }

    // Genuine OLE file drag from (sx,sy) to (tx,ty), dropped there. Returns the drop effect.
    // Like a real drag it begins with the button pressed on one of our own windows (so this
    // thread gets the mouse capture). It refuses to press if that window isn't on top.
    public static string DragFilesTo(string[] files, int sx, int sy, int tx, int ty) {
        var handle = new Form { FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual,
                                Bounds = new System.Drawing.Rectangle(sx - 40, sy - 40, 80, 80), TopMost = true,
                                ShowInTaskbar = false, BackColor = System.Drawing.Color.SteelBlue };
        handle.Show();
        BringToFront(handle);
        if (!IsTopAt(handle, sx, sy)) { handle.Close(); handle.Dispose(); return "NotOnTop"; }
        var dog = Watchdog(12000);
        SetCursorPos(sx, sy);
        Thread.Sleep(100); Application.DoEvents();
        mouse_event(LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(100); Application.DoEvents();
        var mover = new Thread(() => {
            Thread.Sleep(300);
            for (int i = 1; i <= 25; i++) { SetCursorPos(sx + (tx - sx) * i / 25, sy + (ty - sy) * i / 25); Thread.Sleep(25); }
            Thread.Sleep(700);                       // the notch opens into a drop zone
            SetCursorPos(tx, ty + 30); Thread.Sleep(250);
            SetCursorPos(tx + 2, ty + 40); Thread.Sleep(400);
            mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
        });
        mover.Start();
        var data = new DataObject(DataFormats.FileDrop, files);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var effect = handle.DoDragDrop(data, DragDropEffects.Copy | DragDropEffects.Link);
        Log += "DoDragDrop returned " + effect + " after " + sw.ElapsedMilliseconds + " ms; ";
        mover.Join();
        dog.Interrupt();
        handle.Close(); handle.Dispose();
        return effect.ToString();
    }
}
'@
}

# State flags (must match src\notch.cpp StateFlags)
$ST = @{ Expanded = 1; FullscreenHidden = 32; Autostart = 64; Toast = 128; Media = 256; Playing = 512; Art = 1024; Hidden = 4096; Hover = 16384 }
$CMD = @{ Quit = 100; Autostart = 101; PlayPause = 104; Next = 105; Prev = 106; ClearShelf = 107; Settings = 109 }
$VK = @{ Ctrl = [byte]0x11; Alt = [byte]0x12; M = [byte]0x4D; C = [byte]0x43 }

$script:Failures = 0
function Assert($cond, $msg) {
    if ($cond) { Write-Host "  PASS  $msg" -ForegroundColor Green }
    else { Write-Host "  FAIL  $msg" -ForegroundColor Red; $script:Failures++ }
}

function Wait-Until([scriptblock]$cond, [int]$timeoutMs = 4000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        if (& $cond) { return $true }
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 50
    }
    return [bool](& $cond)
}

function Get-NotchWindow { [NotchTest]::FindByClass('PerchWindow') }
function Get-NotchState { [NotchTest]::State((Get-NotchWindow)) }
function Test-Flag([string]$name) { ((Get-NotchState) -band $ST[$name]) -ne 0 }

# The mascot replaces the clock pill after 15 s without real input; only test_mascot wants it.
if (-not $script:KeepMascot) { $env:PERCH_MASCOT_OFF = '1' } else { Remove-Item Env:PERCH_MASCOT_OFF -ErrorAction SilentlyContinue }
# Perch looks for updates on GitHub a minute after it starts; tests point it at a closed local port instead
# (test_update sets its own fake GitHub).
if (-not $env:PERCH_UPDATE_URL) { $env:PERCH_UPDATE_URL = 'http://127.0.0.1:9/releases/latest' }
function Start-Notch {
    Get-Process Perch -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    Start-Process $Exe | Out-Null
    if (-not (Wait-Until { (Get-NotchWindow) -ne [IntPtr]::Zero } 5000)) { throw 'Notch window did not appear' }
    Start-Sleep -Milliseconds 500
}

function Stop-Notch { Get-Process Perch -ErrorAction SilentlyContinue | Stop-Process -Force }

function Get-Screen { [System.Windows.Forms.Screen]::PrimaryScreen.Bounds }

function Save-TopShot([string]$name) {
    $b = Get-Screen; $w = 700; $h = 340
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]($b.Width / 2 - $w / 2), 0, 0, 0, $bmp.Size)
    $path = Join-Path $Shots "$name.png"
    $bmp.Save($path); $g.Dispose(); $bmp.Dispose()
    return $path
}

# Remember/restore the user's mouse position around tests that move it.
function Save-Cursor { $p = New-Object NotchTest+POINT; [NotchTest]::GetCursorPos([ref]$p) | Out-Null; $p }
function Restore-Cursor($p) { [NotchTest]::SetCursorPos($p.X, $p.Y) | Out-Null }

function Hover-Notch {
    $b = Get-Screen
    [NotchTest]::SetCursorPos([int]($b.Width / 2), 8) | Out-Null; Start-Sleep -Milliseconds 80
    [NotchTest]::SetCursorPos([int]($b.Width / 2) + 2, 10) | Out-Null
}

function Finish-Test([string]$name) {
    if ($script:Failures -eq 0) { Write-Host "[$name] OK" -ForegroundColor Green; exit 0 }
    Write-Host "[$name] $($script:Failures) failure(s)" -ForegroundColor Red; exit 1
}
