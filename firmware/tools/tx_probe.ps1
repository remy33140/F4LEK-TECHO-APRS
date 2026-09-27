<#
.SYNOPSIS
  Manda una secuencia de comandos al nodo y muestra las TRAMAS que salen al aire.
.EXAMPLE
  .\tx_probe.ps1 -Port COM36 -Line "beacon","wx" -WaitMs 8000
  .\tx_probe.ps1 -Port COM36 -Line "obj PRUEBA 42.80 -1.62 hola" -WaitMs 9000 -Out tramas.txt
.NOTES
  Activa el modo diag (sin el, el nodo no publica las tramas), manda cada linea,
  captura los eventos {"diag":"tx"} y apaga el diag al terminar.
  OJO: solo un proceso puede tener el puerto abierto; no lo lances a la vez que
  send.ps1 ni que otra captura.
#>
param(
  [string]$Port = "COM36",
  [int]$Baud = 115200,
  [string[]]$Line = @("beacon"),
  [int]$WaitMs = 8000,
  [string]$Out = ""
)

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 300

try { $sp.Open() } catch {
  Write-Error ("No puedo abrir " + $Port + ": " + $_.Exception.Message)
  exit 1
}

$frames = New-Object System.Collections.Generic.List[string]
try {
  $sp.DiscardInBuffer()
  Start-Sleep -Milliseconds 250
  $sp.WriteLine('{"cmd":"diag","diag":{"active":true}}')
  Start-Sleep -Milliseconds 500
  foreach ($l in $Line) {
    $sp.WriteLine($l)
    $deadline = (Get-Date).AddMilliseconds($WaitMs)
    while ((Get-Date) -lt $deadline) {
      try {
        $r = $sp.ReadLine()
        if ($r -and $r.Contains('"diag":"tx"')) {
          $mf = [regex]::Match($r, '"frame":"((?:[^"\\]|\\.)*)"')
          $mc = [regex]::Match($r, '"code":(-?\d+)')
          if ($mf.Success) {
            $f = $mf.Groups[1].Value -replace '\\"', '"' -replace '\\\\', '\'
            $frames.Add($f)
            Write-Output ("TX[" + $mc.Groups[1].Value + "] " + $f)
          }
        }
      } catch {
        # timeout de lectura: seguimos hasta agotar WaitMs
      }
    }
  }
  $sp.WriteLine('{"cmd":"diag","diag":{"active":false}}')
  Start-Sleep -Milliseconds 300
} finally {
  try { $sp.Close() } catch {}
}

if ($Out -and $frames.Count -gt 0) {
  $frames | Set-Content -Path $Out -Encoding UTF8
  Write-Output ("guardadas " + $frames.Count + " tramas en " + $Out)
}
