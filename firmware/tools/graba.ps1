# graba.ps1 - Graba un .uf2 en el T-Echo con red de seguridad.
#
# POR QUE EXISTE: grabar a ciegas es como se deja un nodo mudo. Esto hace el ciclo
# completo y COMPRUEBA cada paso:
#   1) pide "dfu confirm" por USB (o, con -Reset, espera a que alguien pulse RESET)
#   2) espera a que aparezca la unidad TECHOBOOT
#   3) copia el .uf2
#   4) espera a que vuelva el puerto serie y pregunta "version" + "status"
#   5) si el nodo NO contesta, lo dice a gritos (y no sigue)
#
# USO:
#   .\graba.ps1 -Uf2 .pio\build\techo_plus_s140v7\firmware.uf2
#   .\graba.ps1 -Uf2 ..\_trabajo_ea2oy\RESCATE_hello.uf2 -NoDfu   # tras doble RESET
param(
  [Parameter(Mandatory=$true)][string]$Uf2,
  [string]$Port = "COM40",
  [string]$BootLabel = "TECHOBOOT",
  [switch]$NoDfu,          # no pide dfu confirm: espera a que la unidad ya este (doble RESET)
  [int]$DriveWaitS = 60,
  [int]$BootWaitS = 25
)

$ErrorActionPreference = "Stop"
$tools = "C:\Users\Jesus\Desktop\LoRa_APRS_iGate-main\Faketec_APRS_Igate_EA2OY\tools"

if (-not (Test-Path $Uf2)) { Write-Error "no existe el .uf2: $Uf2"; exit 1 }
$Uf2 = (Resolve-Path $Uf2).Path
Write-Output ("GRABA: " + $Uf2 + "  (" + (Get-Item $Uf2).Length + " bytes)")

function UnidadBootloader {
  $d = Get-CimInstance Win32_LogicalDisk | Where-Object { $_.VolumeName -eq $BootLabel }
  if ($d) { return ($d.DeviceID + "\") }
  return $null
}

function BuscaPuerto {
  for ($i = 0; $i -lt 30; $i++) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) { return $true }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

# --- 1) al cargador
if (-not $NoDfu) {
  Write-Output "PASO 1: pidiendo 'dfu confirm' por $Port ..."
  try { & "$tools\hello_send.ps1" -Port $Port -Line "dfu confirm" -BootMs 3500 -WaitMs 1500 | Out-Null }
  catch { Write-Output ("  (no se pudo hablar por serie: " + $_.Exception.Message + ")") }
} else {
  Write-Output "PASO 1: -NoDfu -> se espera a la unidad $BootLabel (doble RESET) ..."
}

# --- 2) la unidad
$dest = UnidadBootloader
$fin = (Get-Date).AddSeconds($DriveWaitS)
while (-not $dest -and (Get-Date) -lt $fin) {
  Start-Sleep -Milliseconds 500
  $dest = UnidadBootloader
}
if (-not $dest) {
  Write-Error "NO ha aparecido la unidad $BootLabel. El nodo no ha entrado en el cargador."
  Write-Error "SOLUCION: DOS toques seguidos al boton de RESET y repetir con -NoDfu."
  exit 2
}
Write-Output ("PASO 2: unidad del cargador = " + $dest)
Write-Output ("  INFO_UF2.TXT: " + ((Get-Content ($dest + "INFO_UF2.TXT") -ErrorAction SilentlyContinue) -join " | "))

# --- 3) copiar
Write-Output "PASO 3: copiando firmware.uf2 ..."
Copy-Item $Uf2 ($dest + "firmware.uf2") -Force
$fin = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $fin -and (UnidadBootloader)) { Start-Sleep -Milliseconds 500 }
Write-Output "PASO 3: copiado y unidad desmontada (la placa se reinicia)"

# --- 4) volver a hablar
Write-Output "PASO 4: esperando a que el nodo arranque y conteste ..."
if (-not (BuscaPuerto)) {
  Write-Error "NO ha vuelto el puerto $Port. EL NODO NO HA ARRANCADO."
  Write-Error "SOLUCION: DOS toques al RESET y grabar RESCATE_hello.uf2 con -NoDfu."
  exit 3
}
Start-Sleep -Seconds 1
$out = & "$tools\hello_send.ps1" -Port $Port -Line "version","status","epd" -BootMs ([int]($BootWaitS * 1000 / 4)) -WaitMs 4000 2>&1
Write-Output "----- respuesta del nodo -----"
$out | ForEach-Object { Write-Output $_ }
Write-Output "------------------------------"

$txt = ($out | Out-String)
if ($txt -match "N0CALL-3") {
  Write-Output "OK: el nodo ARRANCA y RESPONDE."
  exit 0
} else {
  Write-Error "EL NODO NO CONTESTA con N0CALL-3. NO seguir grabando: pedir ayuda."
  Write-Error "SOLUCION: DOS toques al RESET y grabar RESCATE_hello.uf2 con -NoDfu."
  exit 4
}
