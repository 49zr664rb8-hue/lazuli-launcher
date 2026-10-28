$ErrorActionPreference = "SilentlyContinue"
Get-Process LazuliLauncher,LazuliLauncher_dbg | Stop-Process -Force
Start-Sleep -Milliseconds 800
"stopped"
