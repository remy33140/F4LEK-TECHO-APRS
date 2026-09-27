# vigila_placa.ps1 - Vigila si aparece una placa conectada por USB, en el momento.
#
# PARA QUE: cuando una placa "no aparece", lo util es ver SI aparece mientras se prueba
# (cambiar el cable, el puerto, dar al reset). Este script mira cada segundo, durante el
# tiempo que se le diga, y avisa en cuanto ve algo: un puerto serie nuevo, o una unidad
# de memoria de bootloader (con su INFO_UF2.TXT, que dice que placa es).
#
# USO:
#     powershell -File tools\vigila_placa.ps1              # 60 segundos
#     powershell -File tools\vigila_placa.ps1 -Segundos 120
#
# No toca nada: solo mira.

param([int]$Segundos = 60)

$ErrorActionPreference = "Continue"

function Puertos { return @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object) }

function UnidadBootloader {
  $res = @()
  foreach ($l in (68..90 | ForEach-Object { [char]$_ })) {
    $u = "${l}:\"
    if (Test-Path $u) {
      $info = Join-Path $u "INFO_UF2.TXT"
      if (Test-Path $info) { $res += $u }
    }
  }
  return $res
}

Write-Host ""
Write-Host "=== Vigilando la placa durante $Segundos segundos ===" -ForegroundColor Cyan
Write-Host "    (mira cada segundo: si aparece un puerto serie o una unidad de bootloader)"
Write-Host ""

$antes = Puertos
$uniAntes = UnidadBootloader
if ($antes.Count) { Write-Host ("  puertos que ya habia: " + ($antes -join ', ')) } else { Write-Host "  no habia ningun puerto serie" }
if ($uniAntes.Count) { Write-Host ("  unidades de bootloader que ya habia: " + ($uniAntes -join ', ')) } else { Write-Host "  no habia ninguna unidad de bootloader" }
Write-Host ""
Write-Host "  Prueba ahora: cambia el cable, cambia de puerto USB, doble toque al reset..." -ForegroundColor Yellow
Write-Host ""

$visto = $false
for ($i = 1; $i -le $Segundos; $i++) {
  Start-Sleep -Seconds 1

  $ahora = Puertos
  $nuevos = $ahora | Where-Object { $antes -notcontains $_ }
  if ($nuevos) {
    foreach ($p in $nuevos) {
      Write-Host ("  [{0,3}s] APARECE EL PUERTO: {1}" -f $i, $p) -ForegroundColor Green
      $visto = $true
    }
    $antes = $ahora
  }

  $uni = UnidadBootloader
  $uniNuevas = $uni | Where-Object { $uniAntes -notcontains $_ }
  if ($uniNuevas) {
    foreach ($u in $uniNuevas) {
      Write-Host ("  [{0,3}s] APARECE UNA UNIDAD DE BOOTLOADER: {1}" -f $i, $u) -ForegroundColor Green
      $info = Join-Path $u "INFO_UF2.TXT"
      if (Test-Path $info) {
        Write-Host "        Que placa es (INFO_UF2.TXT):" -ForegroundColor Green
        Get-Content $info | ForEach-Object { Write-Host ("          " + $_) -ForegroundColor Green }
      }
      $visto = $true
    }
    $uniAntes = $uni
  }

  if (($i % 10) -eq 0) { Write-Host ("  [{0,3}s] sigo mirando..." -f $i) -ForegroundColor DarkGray }
}

Write-Host ""
if ($visto) {
  Write-Host "RESULTADO: la placa HA aparecido (mira arriba que era y cuando)." -ForegroundColor Green
  exit 0
} else {
  Write-Host "RESULTADO: no ha aparecido nada en $Segundos segundos." -ForegroundColor Red
  Write-Host "  Lo mas probable, por orden:"
  Write-Host "   1. El cable es de SOLO CARGA (muy comun). Prueba otro cable, a ser posible"
  Write-Host "      USB-A a USB-C: los de USB-C a USB-C a veces no alimentan estas placas."
  Write-Host "   2. El puerto USB del PC. Prueba otro (mejor uno trasero, directo a la placa)."
  Write-Host "   3. La placa no arranca: mira si se enciende alguna luz o algo en la pantalla."
  Write-Host "   4. El conector USB de la placa (mira que entre a fondo y no baile)."
  exit 1
}
