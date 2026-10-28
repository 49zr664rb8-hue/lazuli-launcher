$ErrorActionPreference = "Stop"
$relay = "C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher\build\relay.exe"
Get-Process relay -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
$p = Start-Process -FilePath $relay -ArgumentList "8899" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 800

function New-Conn {
  $c = New-Object System.Net.Sockets.TcpClient
  $c.Connect("127.0.0.1", 8899)
  return $c
}
function Send-Line($c, $s) {
  $b = [System.Text.Encoding]::ASCII.GetBytes($s + "`n")
  $c.GetStream().Write($b, 0, $b.Length)
  $c.GetStream().Flush()
}
function Read-Line($c) {
  $buf = New-Object byte[] 1
  $sb = ""
  while ($true) {
    $n = $c.GetStream().Read($buf, 0, 1)
    if ($n -le 0) { break }
    if ($buf[0] -eq 10) { break }
    $sb += [char]$buf[0]
  }
  return $sb
}

$h = New-Conn
Send-Line $h "HOST ABC123"
$r1 = Read-Line $h
Write-Output "host reply: $r1"

$g = New-Conn
Send-Line $g "JOIN ABC123"
$r2 = Read-Line $g
Write-Output "guest reply: $r2"

Start-Sleep -Milliseconds 300
Send-Line $g "hello-from-guest"
Start-Sleep -Milliseconds 300
$buf = New-Object byte[] 64
$h.GetStream().ReadTimeout = 2000
$n = $h.GetStream().Read($buf, 0, $buf.Length)
$recv = [System.Text.Encoding]::ASCII.GetString($buf, 0, $n)
Write-Output "host received: $recv"

$b2 = [System.Text.Encoding]::ASCII.GetBytes("hello-from-host")
$h.GetStream().Write($b2, 0, $b2.Length)
Start-Sleep -Milliseconds 300
$g.GetStream().ReadTimeout = 2000
$m = $g.GetStream().Read($buf, 0, $buf.Length)
$recv2 = [System.Text.Encoding]::ASCII.GetString($buf, 0, $m)
Write-Output "guest received: $recv2"

# 测试无效房间
$bad = New-Conn
Send-Line $bad "JOIN NOPE00"
Write-Output "bad room reply: $(Read-Line $bad)"

$h.Close(); $g.Close(); $bad.Close()
Stop-Process -Id $p.Id -Force
Write-Output "DONE"
