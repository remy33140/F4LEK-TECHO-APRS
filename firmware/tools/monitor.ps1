<#
.SYNOPSIS
  Captura el flujo de diagnostico JSON del nodo Faketec por USB.
.EXAMPLE
  .\monitor.ps1 -Port COM35                       # hasta Ctrl+C
  .\monitor.ps1 -Port COM35 -Seconds 600          # 10 minutos
  .\monitor.ps1 -Port COM35 -Nmea -Seconds 120    # incluye NMEA crudo
.NOTES
  Activa el modo diag, guarda la salida JSONL en un fichero y la muestra.
  Al terminar manda "diag off"/"diag nmea off" y cierra el puerto.
#>
param(
  [string]$Port = "COM35",
  [int]$Baud = 115200,
  [int]$Seconds = 0,
  [switch]$Nmea,
  [string]$Out = ""
)

if (-not $Out) { $Out = "diag_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".jsonl" }

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 1000

try { $sp.Open() } catch {
  Write-Error ("No puedo abrir " + $Port + ": " + $_.Exception.Message)
  exit 1
}

$sw = New-Object System.IO.StreamWriter($Out, $false, [System.Text.Encoding]::UTF8)
$sw.AutoFlush = $true

Write-Host ("Capturando en " + $Out + "  (Ctrl+C para parar)")
$sp.DiscardInBuffer()
Start-Sleep -Milliseconds 200
$sp.WriteLine("diag on")
if ($Nmea) { $sp.WriteLine("diag nmea on") }

$start = Get-Date
try {
  while ($true) {
    if ($Seconds -gt 0 -and ((Get-Date) - $start).TotalSeconds -ge $Seconds) { break }
    try { $line = $sp.ReadLine() } catch { continue }
    if ($null -eq $line) { continue }
    $sw.WriteLine($line)
    Write-Host $line
  }
} finally {
  try { $sp.WriteLine("diag off") } catch {}
  if ($Nmea) { try { $sp.WriteLine("diag nmea off") } catch {} }
  Start-Sleep -Milliseconds 250
  $sw.Close()
  $sp.Close()
  Write-Host ("`nGuardado: " + (Resolve-Path $Out))
}
