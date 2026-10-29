#!/bin/bash
cd "$(dirname "$0")/.." && . tools/env.sh
# Capture N window screenshots at a fixed interval in one process:
#   tools/burst.sh prefix count interval_ms   -> private_work/prefix_00.png ...
tasklist.exe | grep -qi amped3 || { echo "burst: game not running"; exit 1; }
powershell.exe -NoProfile -Command '
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
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
  // The game window (title "Amped 3 | ..."), not the F10 frame-graph overlay.
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var s = new StringBuilder(256); GetWindowText(h, s, 256);
      if (p == pid && s.ToString().StartsWith("Amped 3")) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@
$h = [GameWindow]::Find((Get-Process amped3 | Select-Object -First 1).Id)
$r = New-Object W+RECT; [W]::GetClientRect($h, [ref]$r) | Out-Null
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt '"$2"'; $i++) {
  while ($sw.ElapsedMilliseconds -lt $i * '"$3"') { Start-Sleep -Milliseconds 1 }
  $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [W]::PrintWindow($h, $dc, 3) | Out-Null
  $g.ReleaseHdc($dc)
  $bmp.Save(("'"$WINROOT"'\private_work\'"$1"'_{0:D2}.png" -f $i)); $g.Dispose(); $bmp.Dispose()
}
"elapsed $($sw.ElapsedMilliseconds) ms"
'
