<#
Input + capture helpers for driving dethrace from a script.

Why not WScript.Shell SendKeys: SDL reads keyboard state from raw scancodes, and
SendKeys only reaches a window that is genuinely foreground. Windows blocks
SetForegroundWindow from a background process, so presses silently land in the
calling terminal instead of the game. This uses SendInput with KEYEVENTF_SCANCODE
and forces the foreground handoff with AttachThreadInput.

Dot-source this file, then use Focus-Game / Send-Scan / Save-Shot.
#>

Add-Type @'
using System;
using System.Runtime.InteropServices;

public class GI {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct PT { public int X, Y; }

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }

    [StructLayout(LayoutKind.Explicit, Size = 40)]
    public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }

    public const uint INPUT_KEYBOARD   = 1;
    public const uint KEYEVENTF_EXTENDED = 0x0001;
    public const uint KEYEVENTF_KEYUP    = 0x0002;
    public const uint KEYEVENTF_SCANCODE = 0x0008;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();

    // Force a real foreground handoff: borrow the target's input queue first.
    public static void ForceForeground(IntPtr h) {
        uint cur = GetCurrentThreadId();
        uint tgt = GetWindowThreadProcessId(h, IntPtr.Zero);
        AttachThreadInput(cur, tgt, true);
        ShowWindow(h, 5 /*SW_SHOW*/);
        BringWindowToTop(h);
        SetForegroundWindow(h);
        SetActiveWindow(h);
        SetFocus(h);
        AttachThreadInput(cur, tgt, false);
    }

    public static void Key(ushort scan, bool ext, bool up) {
        INPUT[] i = new INPUT[1];
        i[0].type = INPUT_KEYBOARD;
        i[0].ki.wVk = 0;
        i[0].ki.wScan = scan;
        i[0].ki.dwFlags = KEYEVENTF_SCANCODE | (ext ? KEYEVENTF_EXTENDED : 0) | (up ? KEYEVENTF_KEYUP : 0);
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@
[void][GI]::SetProcessDPIAware()
Add-Type -AssemblyName System.Drawing

# Set-1 scancodes. SDL maps these straight through, unlike virtual-key codes.
#
# Driving is on the NUMERIC KEYPAD, not the arrow keys -- verified by watching the
# speedo: numpad 8 gets to gear 3 / 71 mph, arrow Up leaves the car in neutral at
# 000. The two share scancode 0x48 and differ only by the extended-key flag, so
# this is easy to get wrong.
$Global:SCAN = @{
    esc = 0x01; enter = 0x1C; tab = 0x0F; space = 0x39; backspace = 0x0E
    up = 0x48; down = 0x50; left = 0x4B; right = 0x4D          # extended (menus)
    kp8 = 0x48; kp2 = 0x50; kp4 = 0x4B; kp6 = 0x4D; kp5 = 0x4C # keypad (driving)
    kp0 = 0x52; kpenter = 0x1C; kpminus = 0x4A; kpplus = 0x4E
    c = 0x2E; m = 0x32; p = 0x19; s = 0x1F; q = 0x10; w = 0x11; e = 0x12
    f1 = 0x3B; f2 = 0x3C; f3 = 0x3D
}
$Global:EXTENDED = @('up', 'down', 'left', 'right')

function Focus-Game([IntPtr]$hwnd) { [GI]::ForceForeground($hwnd); Start-Sleep -Milliseconds 350 }

# Vulkan validation can run at about 15 FPS while pipelines and images settle.
# Keep a toggle down long enough for at least one event-poll pass.
function Send-Scan([IntPtr]$hwnd, [string]$name, [int]$holdMs = 250, [int]$repeat = 1) {
    if (-not $SCAN.ContainsKey($name)) { throw "unknown key '$name'" }
    $scan = [uint16]$SCAN[$name]
    $ext = $EXTENDED -contains $name
    Focus-Game $hwnd
    for ($i = 0; $i -lt $repeat; $i++) {
        [GI]::Key($scan, $ext, $false)
        Start-Sleep -Milliseconds $holdMs
        [GI]::Key($scan, $ext, $true)
        Start-Sleep -Milliseconds 90
    }
}

# Hold a key down for a while (driving), releasing at the end.
function Hold-Scan([IntPtr]$hwnd, [string]$name, [int]$ms) {
    $scan = [uint16]$SCAN[$name]; $ext = $EXTENDED -contains $name
    Focus-Game $hwnd
    [GI]::Key($scan, $ext, $false)
    Start-Sleep -Milliseconds $ms
    [GI]::Key($scan, $ext, $true)
}

function Save-Shot([IntPtr]$hwnd, [string]$dir, [string]$name) {
    Focus-Game $hwnd
    $r = New-Object 'GI+RECT'; [void][GI]::GetClientRect($hwnd, [ref]$r)
    $o = New-Object 'GI+PT';   [void][GI]::ClientToScreen($hwnd, [ref]$o)
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($w -le 0 -or $h -le 0) { Write-Host "  ! $name bad rect"; return $null }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen((New-Object System.Drawing.Point $o.X, $o.Y), [System.Drawing.Point]::Empty,
                      (New-Object System.Drawing.Size $w, $h))
    $bmp.Save((Join-Path $dir "$name.png"))
    # cheap fingerprint so identical consecutive frames (= input not landing) are obvious
    $sum = 0; $nz = 0
    for ($y = 0; $y -lt $h; $y += 11) { for ($x = 0; $x -lt $w; $x += 11) {
        $c = $bmp.GetPixel($x, $y); $v = $c.R + $c.G + $c.B
        if ($v -gt 24) { $nz++ }; $sum = ($sum + $v * ($x + 7) * ($y + 13)) % 2147483647 } }
    $g.Dispose(); $bmp.Dispose()
    Write-Host ("  {0,-16} {1}x{2} nz={3} fp={4}" -f $name, $w, $h, $nz, $sum)
    return $sum
}
