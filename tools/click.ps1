param(
  [string]$Exe = "build\LazuliLauncher.exe",
  [int]$Page = -1,
  [string]$Out = "build\click.png",
  [int]$X = 0,
  [int]$Y = 0,
  [int]$Wait = 3800,
  [int]$After = 900,
  [switch]$Keep
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class CW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
}
"@
[void][CW]::SetProcessDpiAwarenessContext([IntPtr](-4))

cd "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"

if (-not $Keep) {
  Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 600
  if ($Page -ge 0) { Start-Process -FilePath $Exe -ArgumentList "page=$Page" | Out-Null }
  else { Start-Process -FilePath $Exe | Out-Null }
  Start-Sleep -Milliseconds $Wait
}
$p = Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "NO_PROCESS"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "NO_WINDOW"; exit 1 }

[void][CW]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400

$scale = [CW]::GetDpiForWindow($h) / 96.0
$pt = New-Object CW+POINT
$pt.X = [int]($X * $scale)
$pt.Y = [int]($Y * $scale)
[void][CW]::ClientToScreen($h, [ref]$pt)
[void][CW]::SetCursorPos($pt.X, $pt.Y)
Start-Sleep -Milliseconds 250
[CW]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
Start-Sleep -Milliseconds 90
[CW]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
Start-Sleep -Milliseconds $After

# 把鼠标移开，避免悬停高亮干扰截图
[void][CW]::SetCursorPos(10, 10)
Start-Sleep -Milliseconds 350

$r = New-Object CW+RECT
[void][CW]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][CW]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"clicked ($X,$Y) scale=$scale -> $Out"
