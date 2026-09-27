<#
.SYNOPSIS
  Descarga el registro de viaje guardado en la flash del nodo.
.EXAMPLE
  .\log_dump.ps1 -Port COM35
  .\log_dump.ps1 -Port COM35 -Out paseo.txt
.NOTES
  Manda "log dump" por USB y guarda las lineas hasta "LOG END".
  Luego: node tools\log2gpx.js paseo.txt   (genera .gpx / .kml / .csv)
#>
param(
  [string]$Port = "COM35",
  [int]$Baud = 115200,
  [string]$Out = "",
  [int]$TimeoutSec = 60
)

if (-not $Out) { $Out = "log_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".txt" }

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 1000

try { $sp.Open() } catch {
  Write-Error ("No puedo abrir " + $Port + ": " + $_.Exception.Message)
  exit 1
}

$lines = New-Object System.Collections.Generic.List[string]
try {
  $sp.DiscardInBuffer()
  Start-Sleep -Milliseconds 200
  $sp.WriteLine("log dump")
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $done = $false
  while (-not $done -and (Get-Date) -lt $deadline) {
    try { $line = $sp.ReadLine() } catch { continue }
    if ($null -eq $line) { continue }
    if ($line -eq "LOG BEGIN") { continue }
    if ($line -eq "LOG END") { $done = $true; break }
    if ($line.StartsWith("LOG ")) { $lines.Add($line.Substring(4)) }
  }
} finally {
  try { $sp.Close() } catch {}
}

$lines | Set-Content -Path $Out -Encoding UTF8
Write-Host ("Lineas guardadas: " + $lines.Count)
Write-Host ("Fichero: " + (Resolve-Path $Out))
if ($lines.Count -gt 0) {
  Write-Host "Siguiente paso: node tools\log2gpx.js $Out"
}
