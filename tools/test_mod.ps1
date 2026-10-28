param([string]$Out1 = "build\m1.png", [string]$Out2 = "build\m2.png")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MW {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
}
"@
[void][MW]::SetProcessDpiAwarenessContext([IntPtr](-4))
$root = "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"
Set-Location $root

function Shot($h, $path) {
  $r = New-Object MW+RECT
  [void][MW]::GetWindowRect($h, [ref]$r)
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  if ($w -le 0) { return "bad rect" }
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][MW]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  return "ok"
}
function ClickAt($h, $x, $y) {
  $scale = [MW]::GetDpiForWindow($h) / 96.0
  $pt = New-Object MW+POINT
  $pt.X = [int]($x * $scale); $pt.Y = [int]($y * $scale)
  [void][MW]::ClientToScreen($h, [ref]$pt)
  [void][MW]::SetCursorPos($pt.X, $pt.Y)
  Start-Sleep -Milliseconds 250
  [MW]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 90
  [MW]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  [void][MW]::SetCursorPos(10, 10)
}

Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
Start-Process -FilePath "build\LazuliLauncher.exe" -ArgumentList "page=2" | Out-Null
Start-Sleep -Seconds 5
$p = Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "NO_PROCESS"; exit 1 }
$h = $p.MainWindowHandle
[void][MW]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400

# 1) 点左侧「模组」
ClickAt $h 300 160
Start-Sleep -Seconds 4
"shot1: " + (Shot $h $Out1)

# 2) 点第一个「详情」
ClickAt $h 1120 208
Start-Sleep -Seconds 4
"shot2: " + (Shot $h $Out2)

Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
"done"
