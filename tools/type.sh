#!/bin/bash
# Post text (WM_CHAR) and optionally Enter to the amped3 window: tools/type.sh "Rider" [enter]
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
foreach ($c in "'"$1"'".ToCharArray()) {
  [W]::PostMessage($h, 0x0102, [IntPtr][int]$c, [IntPtr]1) | Out-Null   # WM_CHAR
  Start-Sleep -Milliseconds 60
}
if ("'"$2"'" -eq "enter") {
  [W]::PostMessage($h, 0x0100, [IntPtr]0x0D, [IntPtr]0x001C0001) | Out-Null  # WM_KEYDOWN VK_RETURN
  Start-Sleep -Milliseconds 80
  [W]::PostMessage($h, 0x0101, [IntPtr]0x0D, [IntPtr]0xC01C0001) | Out-Null  # WM_KEYUP
}
'
