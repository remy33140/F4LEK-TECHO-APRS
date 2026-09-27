# ble_scan.ps1 — NO FUNCIONA. Se deja escrito para no repetir el intento.
#
# ESTADO (2026-09-13): probado y descartado. NO usar.
#
# Por que no funciona, con las pruebas hechas:
#   1) El tipo de delegado de Windows no se puede cargar desde PowerShell:
#      "No se encuentra el tipo ...BluetoothLEAdvertisementReceivedEventHandler".
#   2) Enganchar el evento con Register-ObjectEvent tampoco:
#      "Windows PowerShell no se puede suscribir a eventos de Windows RT".
#   3) No se puede compilar un ayudante en C# porque en este PC no hay
#      ni SDK de .NET (dotnet) ni SDK de Windows (Windows Kits\10\UnionMetadata).
#   4) Node no trae Bluetooth y las librerias que lo anaden se instalan por
#      HTTPS, que en este PC esta roto (schannel sin credenciales).
#
# LO QUE SI SIRVE para comprobar el Bluetooth del nodo:
#   a) El registro del puerto serie del nodo: puede apuntar que anuncia, que
#      alguien intenta emparejarse, que PIN ha ensenado y si el emparejamiento
#      salio bien. Con eso se audita toda la logica sin necesidad de un receptor.
#   b) Una pagina web local (http://localhost es contexto seguro) con Web
#      Bluetooth, abierta en Chrome o Edge: el operador pulsa "conectar" y elige
#      el nodo. La pagina puede devolver el registro al servidor local para que
#      el agente lo lea.
#   c) El movil del operador (nRF Connect o APRSdroid).
#
# Lo de abajo se conserva solo como referencia de lo intentado.

param(
  [int]$Secs = 12,
  [string]$Filter = "",
  [switch]$All
)

$ErrorActionPreference = "Stop"

# --- puente entre las tareas de Windows y PowerShell -------------------------
Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
  $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
  $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

# --- escaner -----------------------------------------------------------------
[Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher, Windows.Devices.Bluetooth.Advertisement, ContentType = WindowsRuntime] | Out-Null
[Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null

$vistos = @{}
$watcher = New-Object Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher
$watcher.ScanningMode = [Windows.Devices.Bluetooth.Advertisement.BluetoothLEScanningMode]::Active

# El tipo de delegado de Windows no se puede usar directamente desde PowerShell
# (probado: "No se encuentra el tipo ...ReceivedEventHandler"), asi que se
# engancha el evento con Register-ObjectEvent, que es la via que si funciona.
$global:bleVistos = @{}
Register-ObjectEvent -InputObject $watcher -EventName Received -SourceIdentifier bleRecv -Action {
  $a = $event.SourceEventArgs
  if ($null -eq $a) { return }
  $clave = "{0:X}" -f $a.BluetoothAddress
  $nombre = $a.Advertisement.LocalName
  if (-not $nombre) { $nombre = "(sin nombre)" }
  $global:bleVistos[$clave] = [pscustomobject]@{
    Dir = $clave; Nombre = $nombre; RSSI = $a.RawSignalStrengthInDBm
  }
} | Out-Null

Write-Host "Escaneando Bluetooth durante $Secs segundos..." -ForegroundColor Cyan
$watcher.Start()
Start-Sleep -Seconds $Secs
$watcher.Stop()
Unregister-Event -SourceIdentifier bleRecv -ErrorAction SilentlyContinue
$vistos = $global:bleVistos

# --- resultados --------------------------------------------------------------
$lista = $vistos.Values | Sort-Object -Property RSSI -Descending
if ($Filter) { $lista = $lista | Where-Object { $_.Nombre -like "*$Filter*" } }
if (-not $All) { $lista = $lista | Where-Object { $_.Nombre -ne "(sin nombre)" } }

Write-Host ""
if (-not $lista -or $lista.Count -eq 0) {
  Write-Host "No se ha visto ningun dispositivo. Comprueba que el Bluetooth del PC esta encendido." -ForegroundColor Yellow
  exit 1
}

Write-Host ("{0,-16} {1,6}  {2}" -f "DIRECCION", "SENAL", "NOMBRE") -ForegroundColor Cyan
foreach ($d in $lista) {
  Write-Host ("{0,-16} {1,4} dBm  {2}" -f $d.Dir, $d.RSSI, $d.Nombre)
}
Write-Host ""
Write-Host "Total: $($lista.Count) dispositivos."
