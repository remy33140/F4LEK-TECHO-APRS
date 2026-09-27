<#
.SYNOPSIS
  Habla con un nodo por USB ESPERANDO a que arranque (herramienta de taller).

.PARAMETER Port
  Puerto serie (por ejemplo COM40).

.PARAMETER EsperaArranqueMs
  Cuanto se espera, vaciando el buffer, antes de mandar nada. Por defecto 20000.
  Con la T-Echo hacen falta: ABRIR EL PUERTO REINICIA LA PLACA y el arranque se
  pasa ~10-15 s pintando. Si se manda antes, la primera linea llega corrompida y
  el nodo contesta "comando desconocido" con basura.

.PARAMETER Linea
  Lineas de comando a mandar, una detras de otra.

.PARAMETER WaitMs
  Cuanto se escucha despues de cada linea. La baliza tarda ~5 s en contestar
  (el paquete ocupa el aire ~4 s a SF12): para `beacon`, 9000 es lo suyo.

.EXAMPLE
  .\charla.ps1 -Port COM40 -Linea @("status","usb") -WaitMs 2500
  .\charla.ps1 -Port COM5 -Linea @("beacon") -WaitMs 9000

.EXAMPLE
  # Vigilar sin mandar nada (para ver los toques en la pantalla):
  .\charla.ps1 -Port COM40 -EsperaArranqueMs 240000 -Linea @() -WaitMs 0

.NOTES
  POR QUE EXISTE (2026-09-16): `tools\send.ps1` abre el puerto y manda en el acto,
  y con la T-Echo eso falla por lo de arriba. Esta version espera, va imprimiendo
  TODO lo que el nodo suelta (con marca de tiempo) y solo entonces habla.
  OJO al mirar la salida: por aqui pasa trafico APRS recibido, con posiciones
  reales de otros nodos. NO se copia a ningun documento ni a ningun repositorio.
#>
param(
  [string]$Port = "COM40",
  [int]$Baud = 115200,
  [int]$EsperaArranqueMs = 20000,
  [string[]]$Linea = @("status"),
  [int]$WaitMs = 2500
)

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 250
$sp.WriteTimeout = 1000

try { $sp.Open() } catch {
  Write-Error ("No puedo abrir " + $Port + ": " + $_.Exception.Message)
  exit 1
}

Write-Host ("[abierto " + $Port + "] esperando " + $EsperaArranqueMs + " ms (vaciando el buffer)...")
$t0 = Get-Date
try {
  $sp.DiscardInBuffer()
  $deadline = $t0.AddMilliseconds($EsperaArranqueMs)
  while ((Get-Date) -lt $deadline) {
    try {
      $r = $sp.ReadLine()
      if ($r) { Write-Output ("[" + ((Get-Date) - $t0).TotalSeconds.ToString("0.0") + "s] " + $r) }
    } catch { }
  }
  foreach ($l in $Linea) {
    Write-Host ("--> " + $l)
    $sp.WriteLine($l)
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline) {
      try {
        $r = $sp.ReadLine()
        if ($r) { Write-Output ("[" + ((Get-Date) - $t0).TotalSeconds.ToString("0.0") + "s] " + $r) }
      } catch { }
    }
  }
} finally {
  try { $sp.Close() } catch { }
}
Write-Host "[cerrado]"
