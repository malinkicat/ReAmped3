#!/bin/bash
# Post key presses to the amped3 window without taking focus: tools/key.sh Return,Space [hold_ms]
powershell.exe -NoProfile -Command '
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
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
$h = [GameWindow]::Find((Get-Process amped3_launcher | Select-Object -First 1).Id)
# The MnK driver ignores keys unless it believes the window is focused.
[W]::PostMessage($h, 0x0007, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # WM_SETFOCUS
Start-Sleep -Milliseconds 100
foreach ($k in "'"$1"'".Split(",")) {
  $vk = [int]([System.Windows.Forms.Keys]::$k); $sc = [int][W]::MapVirtualKey($vk, 0)
  [W]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16))) | Out-Null   # WM_KEYDOWN
  Start-Sleep -Milliseconds '"${2:-120}"'
  [W]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr](1 -bor ($sc -shl 16) -bor (3 -shl 30))) | Out-Null  # WM_KEYUP
  Start-Sleep -Milliseconds 400
}
"sent '"$1"'"
'
