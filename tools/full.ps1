Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$s = [System.Windows.Forms.Screen]::PrimaryScreen
Write-Output ("Bounds: " + $s.Bounds.Width + "x" + $s.Bounds.Height)
Write-Output ("Work:   " + $s.WorkingArea.Width + "x" + $s.WorkingArea.Height)
$b = New-Object System.Drawing.Bitmap $s.Bounds.Width, $s.Bounds.Height
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen(0, 0, 0, 0, $b.Size)
$b.Save("C:\Users\m1597\WorkBuddy AI\2026-10-27-21-39-52\LazuliLauncher\build\full.png")
$g.Dispose(); $b.Dispose()
Write-Output "saved"
