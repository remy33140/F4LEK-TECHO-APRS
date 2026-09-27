<#
.SYNOPSIS
  Envia una o varias lineas de comando al nodo Faketec por USB y muestra la respuesta.
.EXAMPLE
  .\send.ps1 -Port COM35 -Line "status"
  .\send.ps1 -Port COM35 -Line '{"cmd":"get"}' -WaitMs 2500
  .\send.ps1 -Port COM35 -Line "dfu confirm" -WaitMs 500
.NOTES
  Solo abre el puerto, escribe las lineas y lee la respuesta durante WaitMs.
  No cambia nada por si mismo: lo que haga el nodo depende de las lineas enviadas.
#>
param(
  [string]$Port = "COM35",
  [int]$Baud = 115200,
  [string[]]$Line = @("status"),
  [int]$WaitMs = 2000
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

try {
  $sp.DiscardInBuffer()
  Start-Sleep -Milliseconds 150
  foreach ($l in $Line) {
    $sp.WriteLine($l)
    Start-Sleep -Milliseconds 120
  }
  $deadline = (Get-Date).AddMilliseconds($WaitMs)
  while ((Get-Date) -lt $deadline) {
    try {
      $resp = $sp.ReadLine()
      if ($resp) { Write-Output $resp }
    } catch {
      # timeout de lectura: seguimos hasta agotar WaitMs
    }
  }
} finally {
  try { $sp.Close() } catch {}
}
