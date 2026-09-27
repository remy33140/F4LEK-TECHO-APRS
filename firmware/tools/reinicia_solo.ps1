# reinicia_solo.ps1 - Reinicia el nodo ABRIENDO EL PUERTO UNA VEZ Y SIN ESCRIBIR NADA.
#
# PARA QUE EXISTE: en los nRF52, abrir el puerto serie con DTR activo reinicia la placa. El
# firmware, al arrancar, repinta la pantalla normal por si solo. Asi que este script sirve
# para dejar la pantalla en el estado "de fabrica" SIN mandar ningun comando: ni status, ni
# epd, ni epdrot, ni nada. Es justo lo que hace falta cuando alguien tiene que mirar la
# pantalla y no queremos que un comando posterior la vuelva a cambiar.
#
# NO ESCRIBE NI UN BYTE. Solo abre, espera, cierra.
param(
  [string]$Port = "COM40",
  [int]$Baud = 115200,
  [int]$EsperaMs = 12000     # tiempo para que arranque y repinte (el refresco son ~2,8 s)
)

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$sp.ReadTimeout = 300
$sp.DtrEnable = $true      # esto es lo que provoca el reinicio
$sp.RtsEnable = $true
try { $sp.Open() } catch {
  Write-Error ("No puedo abrir " + $Port + ": " + $_.Exception.Message)
  exit 1
}
Write-Output ("PUERTO ABIERTO en " + $Port + " (reinicio provocado). No se escribe NADA.")
Write-Output ("Esperando " + $EsperaMs + " ms a que arranque y repinte la pantalla normal...")
Start-Sleep -Milliseconds $EsperaMs
try { $sp.Close() } catch {}
Write-Output "PUERTO CERRADO. No se ha mandado ningun comando."
