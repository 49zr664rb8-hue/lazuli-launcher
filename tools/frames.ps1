Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[void][PW]::SetProcessDpiAwarenessContext([IntPtr](-4))
Set-Location "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"
Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
Start-Process -FilePath "build\LazuliLauncher.exe" -ArgumentList "mdtest=1" | Out-Null

function Shot($h, $path) {
  $r = New-Object PW+RECT
  [void][PW]::GetWindowRect($h, [ref]$r)
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  if ($w -le 0) { return "bad" }
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][PW]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  return "ok"
}

for ($i = 1; $i -le 6; $i++) {
  Start-Sleep -Milliseconds 1600
  $p = Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { "gone at $i"; break }
  $h = $p.MainWindowHandle
  if ($h -ne [IntPtr]::Zero) { "frame $i : " + (Shot $h ("build\f" + $i + ".png")) }
}
Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
"done"
