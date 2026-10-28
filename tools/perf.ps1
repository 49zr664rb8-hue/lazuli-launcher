param([string]$Exe = "build\LazuliLauncher_dbg.exe", [string]$Out = "build\perf.txt", [int]$Wait = 35)
cd "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher"
Get-Process LazuliLauncher,LazuliLauncher_dbg -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
Remove-Item "$env:APPDATA\Lazuli\launcher.log" -ErrorAction SilentlyContinue

$env:LZ_NO_TEXT = ""; $env:LZ_FORCE_ANIM = "1"
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds $Wait
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
"=== WITH TEXT ==="
Get-Content "$env:APPDATA\Lazuli\launcher.log" -Encoding UTF8 | Select-String "ms"

Remove-Item "$env:APPDATA\Lazuli\launcher.log" -ErrorAction SilentlyContinue
$env:LZ_NO_TEXT = "1"
$p2 = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds $Wait
Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
"=== NO TEXT ==="
Get-Content "$env:APPDATA\Lazuli\launcher.log" -Encoding UTF8 | Select-String "ms"
$env:LZ_NO_TEXT = ""; $env:LZ_FORCE_ANIM = "1"
