#!/bin/bash
# Post a left click at client coordinates to the amped3 window: tools/click.sh x y
powershell.exe -NoProfile -Command '
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
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
$lp = [IntPtr]((('"$2"' -shl 16) -bor '"$1"'))
[W]::PostMessage($h, 0x0200, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 120   # WM_MOUSEMOVE
[W]::PostMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null; Start-Sleep -Milliseconds 120   # WM_LBUTTONDOWN
[W]::PostMessage($h, 0x0202, [IntPtr]0, $lp) | Out-Null                                  # WM_LBUTTONUP
'
