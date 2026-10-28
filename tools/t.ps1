param(
  [int]$Page = -1,
  [string]$Out = "build\t.png",
  [string]$Steps = "",
  [string]$Args = ""
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SM {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[void][SM]::SetProcessDpiAwarenessContext([IntPtr](-4))
Set-Location "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"

Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
$argl = $Args
if ($Page -ge 0) { $argl = "page=$Page" }
if ($argl -ne "") { Start-Process -FilePath "build\LazuliLauncher.exe" -ArgumentList $argl | Out-Null }
else { Start-Process -FilePath "build\LazuliLauncher.exe" | Out-Null }
Start-Sleep -Seconds 6

$p = Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "NO_PROCESS"; exit 1 }
$h = $p.MainWindowHandle
$scale = [SM]::GetDpiForWindow($h) / 96.0

function TypeText($h, $text) {
  foreach ($ch in $text.ToCharArray()) {
    [void][SM]::SendMessage($h, 0x0102, [IntPtr][int]$ch, [IntPtr]1)
    Start-Sleep -Milliseconds 70
  }
}

function Click($h, $scale, $x, $y) {
  $px = [int]($x * $scale); $py = [int]($y * $scale)
  $lp = [IntPtr](($py -shl 16) -bor ($px -band 0xFFFF))
  [void][SM]::SendMessage($h, 0x0200, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds 120
  [void][SM]::SendMessage($h, 0x0201, [IntPtr]1, $lp)
  Start-Sleep -Milliseconds 90
  [void][SM]::SendMessage($h, 0x0202, [IntPtr]::Zero, $lp)
  Start-Sleep -Milliseconds 700
}

if ($Steps -ne "") {
  foreach ($s in $Steps.Split(";")) {
    if ($s.StartsWith("c:")) {
      $xy = $s.Substring(2).Split(",")
      Click $h $scale ([int]$xy[0]) ([int]$xy[1])
    } elseif ($s.StartsWith("t:")) {
      TypeText $h $s.Substring(2)
    } elseif ($s.StartsWith("w:")) {
      Start-Sleep -Milliseconds ([int]$s.Substring(2))
    }
  }
}
Start-Sleep -Milliseconds 4500

$r = New-Object SM+RECT
[void][SM]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][SM]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()

Get-Process LazuliLauncher -ErrorAction SilentlyContinue | Stop-Process -Force
"OK -> $Out"
