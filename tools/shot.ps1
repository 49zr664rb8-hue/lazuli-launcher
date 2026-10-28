param([string]$Exe = "", [string]$Out = "shot.png", [int]$Wait = 3500, [int]$Page = -1, [int]$W = 0, [int]$H = 0)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WC {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[WC]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

if ($Exe -ne "") {
  Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  if ($Page -ge 0) { Start-Process -FilePath $Exe -ArgumentList "page=$Page" | Out-Null }
  else { Start-Process -FilePath $Exe | Out-Null }
  Start-Sleep -Milliseconds $Wait
}
$p = Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "NO_PROCESS"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 1 }
if ($W -gt 0) { [WC]::SetWindowPos($h, [IntPtr]::Zero, 40, 30, $W, $H, 0x0014) | Out-Null; Start-Sleep -Milliseconds 800 }
Start-Sleep -Milliseconds 500
$r = New-Object WC+RECT
[WC]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [WC]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
if (-not $ok) { Write-Output "PRINTWINDOW_FAILED" }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "OK $w x $ht -> $Out (pw=$ok)"
