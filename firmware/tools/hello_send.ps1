<#
.SYNOPSIS
  Habla con el nodo por USB esperando a que arranque (la version lenta de send.ps1).

.POR QUE EXISTE
  `send.ps1` abre el puerto con DTR activo, espera 150 ms y escribe. Los nRF52 se
  reinician al abrir el puerto, asi que a los 150 ms el firmware todavia no ha
  arrancado y la linea se pierde: parece que el nodo no contesta.

  Esta version: abre, ESPERA a que el nodo arranque, y entonces escribe. Si el puerto
  se cae al reiniciarse la placa (es normal), lo vuelve a abrir.

.EXAMPLE
  .\hello_send.ps1 -Port COM40 -Line "version"                 # solo leer
  .\hello_send.ps1 -Port COM40 -Line "dfu confirm" -WaitMs 6000
#>
param(
  [string]$Port = "COM40",
  [int]$Baud = 115200,
  [string[]]$Line = @(),
  [int]$BootMs = 3500,     # cuanto se espera, tras abrir, a que el nodo arranque
  [int]$WaitMs = 6000,     # cuanto se lee despues de escribir
  [switch]$OnlyRead
)

function Abrir($p) {
  $sp = New-Object System.IO.Ports.SerialPort($p, $Baud, "None", 8, "One")
  $sp.ReadTimeout = 300
  $sp.NewLine = "`n"
  $sp.DtrEnable = $true
  $sp.RtsEnable = $true
  try { $sp.Open() } catch { return $null }
  return $sp
}

$sp = Abrir $Port
if (-not $sp) { Write-Error ("No puedo abrir " + $Port); exit 1 }

# Se espera a que arranque. Si el puerto se cae (la placa se esta reiniciando), se
# vuelve a abrir.
$fin = (Get-Date).AddMilliseconds($BootMs)
$lineas = New-Object System.Collections.Generic.List[string]
while ((Get-Date) -lt $fin) {
  if (-not $sp.IsOpen) { Start-Sleep -Milliseconds 300; $sp = Abrir $Port; if (-not $sp) { continue } }
  try {
    $r = $sp.ReadLine()
    if ($r) { $lineas.Add($r) }
  } catch {
    if (-not $sp.IsOpen) { $sp = $null; $sp = Abrir $Port }
  }
}
foreach ($l in $lineas) { Write-Output $l }

if (-not $OnlyRead) {
  foreach ($l in $Line) {
    if (-not $sp -or -not $sp.IsOpen) { $sp = Abrir $Port }
    if (-not $sp) { Write-Error "puerto perdido"; exit 1 }
    try { $sp.WriteLine($l) } catch { Write-Output ("[no se pudo escribir: " + $_.Exception.Message + "]") }
    Start-Sleep -Milliseconds 250
  }
  $deadline = (Get-Date).AddMilliseconds($WaitMs)
  while ((Get-Date) -lt $deadline) {
    if ($sp -and $sp.IsOpen) {
      try { $r = $sp.ReadLine(); if ($r) { Write-Output $r } } catch {}
    }
    Start-Sleep -Milliseconds 20
  }
}

if ($sp -and $sp.IsOpen) { try { $sp.Close() } catch {} }
