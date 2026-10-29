#!/bin/bash
cd "$(dirname "$0")/.." && . tools/env.sh
# Bring the game to the foreground and capture its area of the screen, including
# overlay windows such as the F10 frame graph: tools/screen.sh name
tasklist.exe | grep -qi amped3 || { echo "screen: game not running"; exit 1; }
powershell.exe -NoProfile -Command '
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
}
"@
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class GameWindow {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  // The game window (title "ReAmped 3 | ..."), not the F10 frame-graph overlay.
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var s = new StringBuilder(256); GetWindowText(h, s, 256);
      if (p == pid && s.ToString().StartsWith("ReAmped 3")) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@
$h = [GameWindow]::Find((Get-Process amped3 | Select-Object -First 1).Id)
[W]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)  # Alt unlocks SetForegroundWindow
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save("'"$WINROOT"'\private_work\'"$1"'.png")
'
