param([string]$Exe = "build\LazuliLauncher.exe", [string]$Prefix = "build\p")
$root = "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"
Set-Location $root
for ($i = 0; $i -lt 7; $i++) {
  powershell -ExecutionPolicy Bypass -File tools\shot.ps1 -Exe $Exe -Out "$Prefix$i.png" -Page $i -Wait 4200
}
Write-Output "all done"
