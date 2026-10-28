Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Cap {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[void][Cap]::SetProcessDpiAwarenessContext([IntPtr](-4))
Set-Location "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"
$out = "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher\build\shots"
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Shot($h, $path) {
  $r = New-Object Cap+RECT
  [void][Cap]::GetWindowRect($h, [ref]$r)
  $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap $w, $ht
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Cap]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}

function Click($h, $x, $y) {
  $s = [Cap]::GetDpiForWindow($h) / 96.0
  $px = [int]($x * $s); $py = [int]($y * $s)
  $lp = [IntPtr](($py -shl 16) -bor ($px -band 0xFFFF))
  [void][Cap]::SendMessage($h, 0x0200, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds 120
  [void][Cap]::SendMessage($h, 0x0201, [IntPtr]1, $lp)
  Start-Sleep -Milliseconds 80
  [void][Cap]::SendMessage($h, 0x0202, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds 400
}

function Boot($arg) {
  Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 800
  Start-Process -FilePath "build\LazuliLauncher.exe" -ArgumentList $arg | Out-Null
  Start-Sleep -Seconds 6
  return (Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1).MainWindowHandle
}

# 1) 普通页面
$h = Boot "page=0"
Shot $h "$out\p_home.png"
$ys = @{ 1 = 163; 2 = 209; 3 = 255; 4 = 301; 5 = 347; 6 = 393 }
$names = @{ 1 = "p_ver"; 2 = "p_dl"; 3 = "p_mp"; 4 = "p_skin"; 5 = "p_box"; 6 = "p_set" }
foreach ($k in 1, 2, 3, 4, 5, 6) {
  Click $h 120 $ys[$k]
  if ($k -eq 2) { Start-Sleep -Seconds 4 }
  Shot $h "$out\$($names[$k]).png"
  "shot $($names[$k])"
}
# 下载页展开 Forge
Click $h 120 209
Start-Sleep -Seconds 1
Click $h 700 232
Start-Sleep -Seconds 3
Shot $h "$out\p_forge.png"
"shot p_forge"
Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force

# 2) 模组详情
$h = Boot "mdtest=1"
Start-Sleep -Seconds 4
Shot $h "$out\p_md.png"
"shot p_md"
Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
"done"
