# copia_techo.ps1 - Copia de seguridad del firmware de un LilyGO T-Echo ANTES de grabarlo.
#
# POR QUE EXISTE: cuando se graba nuestro firmware encima del que trae la placa, el
# que habia se pierde. El bootloader del T-Echo (Adafruit) expone, cuando esta en
# modo grabacion, un fichero CURRENT.UF2 que es LA COPIA COMPLETA de lo que lleva
# grabado. Esto lo guarda en data\rescue\ con fecha y hora, y comprueba que la
# copia es identica (SHA256) antes de dar el visto bueno.
#
# USO (con la placa en modo grabacion: doble toque al boton de reset de arriba a la
# izquierda hasta que aparezca una unidad llamada TECHOBOOT):
#
#     powershell -File tools\copia_techo.ps1
#
# El script NO escribe nada en la placa: solo LEE. Es imposible que estropee nada.

$ErrorActionPreference = "Stop"

$raiz = Split-Path -Parent $PSScriptRoot
$destino = Join-Path $raiz "data\rescue"
New-Item -ItemType Directory -Force -Path $destino | Out-Null

Write-Host ""
Write-Host "=== Buscando una placa T-Echo en modo grabacion ===" -ForegroundColor Cyan

# El bootloader de LilyGO monta el volumen con este nombre.
$candidatos = @()
foreach ($letra in (68..90 | ForEach-Object { [char]$_ })) {   # D: .. Z:
  $u = "${letra}:"
  if (Test-Path $u) {
    $vol = Get-Volume -DriveLetter $letra -ErrorAction SilentlyContinue
    if ($vol -and ($vol.FileSystemLabel -match 'TECHOBOOT|T-ECHO|TECHO')) {
      $candidatos += $u
    }
  }
}

if ($candidatos.Count -eq 0) {
  Write-Host "No encuentro ninguna placa en modo grabacion." -ForegroundColor Yellow
  Write-Host "  Como se entra: doble toque al boton de reset (arriba a la izquierda) hasta"
  Write-Host "  que aparezca una unidad llamada TECHOBOOT. Si no aparece, mira que el cable"
  Write-Host "  sea USB-A a USB-C (con USB-C a USB-C algunas placas no se alimentan)."
  Write-Host ""
  Write-Host "Unidades que hay ahora mismo:"
  Get-Volume | Where-Object { $_.DriveLetter } | Select-Object DriveLetter, FileSystemLabel, @{n='MB';e={[math]::Round($_.Size/1MB,1)}} | Format-Table -AutoSize
  exit 1
}

$unidad = $candidatos[0]
Write-Host ("Placa encontrada en {0}" -f $unidad) -ForegroundColor Green

$origen = Join-Path $unidad "CURRENT.UF2"
if (-not (Test-Path $origen)) {
  Write-Host ("En {0} no hay CURRENT.UF2. ¿Seguro que es un T-Echo en modo grabacion?" -f $unidad) -ForegroundColor Red
  Get-ChildItem $unidad -ErrorAction SilentlyContinue | Select-Object Name, Length | Format-Table -AutoSize
  exit 1
}

$sello = Get-Date -Format "yyyyMMdd-HHmm"
$nombre = "techo_original_$sello.uf2"
$copia = Join-Path $destino $nombre

Write-Host ("Copiando ({0:N0} bytes)..." -f (Get-Item $origen).Length)
Copy-Item $origen $copia -Force

$h1 = (Get-FileHash $origen -Algorithm SHA256).Hash
$h2 = (Get-FileHash $copia -Algorithm SHA256).Hash

Write-Host ""
Write-Host "=== COMPROBACION ===" -ForegroundColor Cyan
Write-Host ("  original : {0}" -f $h1)
Write-Host ("  copia    : {0}" -f $h2)
if ($h1 -ne $h2) {
  Write-Host "  LAS DOS NO COINCIDEN: NO des esta copia por buena." -ForegroundColor Red
  exit 1
}
Write-Host "  IDENTICAS: la copia es buena." -ForegroundColor Green
Write-Host ""
Write-Host ("Guardada en: {0}" -f $copia)
Write-Host "Anota de QUE placa es (¿T-Echo normal o Plus?) en el nombre del fichero si no lo dice."
Write-Host ""
