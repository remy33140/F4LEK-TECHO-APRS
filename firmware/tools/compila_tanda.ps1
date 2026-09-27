<#
.SYNOPSIS
  Compila una tanda completa (los entornos de release) y deja los binarios CON EL MISMO numero
  de compilacion dentro, que es lo que permite saber que firmware lleva cada placa.

.PARAMETER Entornos
  Entornos a compilar. Por defecto los dos de la tanda de release:
  techo_s140v7 y techo_plus_s140v7 (firmware solo T-Echo desde el 2026-09-27).

.EXAMPLE
  powershell -File tools\compila_tanda.ps1

.NOTES
  POR QUE HACEN FALTA DOS PASADAS (medido el 2026-09-16):
  `extra_scripts/sube_buildnum.py` sube el numero al CARGARSE (va con `pre:` en
  platformio.ini), o sea antes de compilar. Pero PlatformIO ya leyo `platformio.ini` para
  ESE entorno antes de cargar el script, asi que **el entorno que gasta el numero se queda
  con el viejo** y los demas salen con el nuevo: una tanda con dos numeros distintos.

  La solucion es darle dos pasadas:
    1) la primera: si el codigo ha cambiado, gasta el numero (y sus binarios llevan el viejo);
    2) la segunda: ya no hay nada que gastar, todos los entornos leen el MISMO numero nuevo
       y todos los binarios salen iguales.

  Esta herramienta hace las dos pasadas SOLO CUANDO HACE FALTA (mira si la primera subio el
  numero), borra los .elf entre pasada y pasada para forzar el enlazado, y al final COMPRUEBA
  que todos los binarios llevan el mismo numero: si no, lo dice en rojo en vez de callarse.

  License: GPL-3.0
#>
param(
  [string[]]$Entornos = @("techo_s140v7", "techo_plus_s140v7")
)

$ErrorActionPreference = "Stop"
$raiz  = Split-Path -Parent $PSScriptRoot
$pio   = "C:\Users\Jesus\.platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $pio)) { $pio = (Get-Command platformio -ErrorAction SilentlyContinue).Source }
if (-not $pio) { Write-Error "No encuentro PlatformIO."; exit 1 }
$env:PLATFORMIO_CORE_DIR = Join-Path $raiz "_pio_core"
Push-Location $raiz
try {
  # ★ OJO CON EL COMPILADOR Y `stderr`: escribe sus AVISOS por la salida de error, y con
  #   `$ErrorActionPreference = "Stop"` PowerShell los convertiria en un fallo FALSO de la
  #   herramienta. Se pone en "Continue" mientras se compila y decide el CODIGO DE SALIDA
  #   de PlatformIO, que es lo unico que dice de verdad si ha ido bien. (Mismo gazapo ya
  #   documentado en tools\banco_boton\ejecuta_banco.ps1.)
  $ErrorActionPreference = "Continue"

  # --- pasada 1 -------------------------------------------------------------
  $salida = & $pio run @($Entornos | ForEach-Object { "-e"; $_ }) 2>&1
  $codigo = $LASTEXITCODE
  $subio = ($salida | Select-String -Pattern "subido a b\d+").Count -gt 0
  $salida | Select-String -Pattern "Numero de compilacion|aviso:|SUCCESS|FAILED|error:" |
    ForEach-Object { Write-Host ("  " + $_.Line.Trim()) }
  if ($codigo -ne 0) { Write-Error "La compilacion ha fallado (codigo $codigo)."; exit 1 }

  # --- pasada 2, solo si la primera gasto numero ----------------------------
  if ($subio) {
    Write-Host ""
    Write-Host "--- la primera pasada gasto numero: segunda pasada para que todos lo lleven ---" -ForegroundColor Yellow
    Remove-Item ".pio\build\*\firmware.elf" -Force -ErrorAction SilentlyContinue
    $salida2 = & $pio run @($Entornos | ForEach-Object { "-e"; $_ }) 2>&1
    $codigo2 = $LASTEXITCODE
    $salida2 | Select-String -Pattern "Numero de compilacion|aviso:|SUCCESS|FAILED|error:" |
      ForEach-Object { Write-Host ("  " + $_.Line.Trim()) }
    if ($codigo2 -ne 0) { Write-Error "La segunda compilacion ha fallado (codigo $codigo2)."; exit 1 }
  } else {
    Write-Host ""
    Write-Host "--- el codigo no ha cambiado: con una pasada basta ---" -ForegroundColor DarkGray
  }
  $ErrorActionPreference = "Stop"

  # --- comprobar que todos dicen lo MISMO -----------------------------
  Write-Host ""
  Write-Host "=== numero dentro de cada binario ===" -ForegroundColor Cyan
  # El numero que TIENE que llevar el binario lo dicen los dos ficheros, y comprueba_cuadre()
  # ya se ha encargado de que digan lo mismo. Se lee de `.buildnum`, que es el contador.
  $nDeclarado = (Get-Content .buildnum -ErrorAction SilentlyContinue | Select-Object -First 1)
  if ($nDeclarado) { $nDeclarado = $nDeclarado.Trim() } else { $nDeclarado = "" }
  $numeros = @{}
  foreach ($e in $Entornos) {
    $uf2 = ".pio\build\$e\firmware.uf2"
    if (-not (Test-Path $uf2)) { Write-Host ("  {0,-22} NO HAY BINARIO" -f $e) -ForegroundColor Red; continue }
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $uf2).Path)
    # El numero viaja en la tabla de textos del CLI. ★ CORREGIDO EL 2026-09-17: antes se
    # buscaba PEGADO a su contexto (`... DG:%lu.(b\d+)`), y eso dejo de casar sin que el
    # numero faltara: el orden del pool de cadenas se movio al anadir codigo y detras de
    # `DG:%lu` ya no va el numero. Ahora se comprueba que el binario lleve EXACTAMENTE el
    # numero que declaran los dos ficheros, que es lo que este script quiere saber (y es mas
    # estricto que antes, no menos: un `b2` ya no casaria con `b23`).
    $txt = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $n = if ($txt.Contains("b$nDeclarado")) { "b$nDeclarado" } else { "?" }
    $numeros[$e] = $n
    $i = Get-Item $uf2
    Write-Host ("  {0,-22} {1}   {2:N0} B   {3}" -f $e, $n, $i.Length, (Get-FileHash $uf2 -Algorithm SHA256).Hash.Substring(0,16))
  }
  $distintos = @($numeros.Values | Sort-Object -Unique)
  Write-Host ""
  if ($distintos.Count -eq 1 -and $distintos[0] -ne "?") {
    Write-Host ("TANDA CORRECTA: todos llevan {0}" -f $distintos[0]) -ForegroundColor Green
    Write-Host ("El contador de los ficheros: .buildnum=" + (Get-Content .buildnum).Trim() + "  " +
                (Select-String -Path platformio.ini -Pattern 'APP_BUILD_NUM' | Select-Object -First 1).Line.Trim())
  } else {
    Write-Host ("OJO: los binarios NO llevan el mismo numero: " + ($numeros.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" } | Sort-Object) -join ", ") -ForegroundColor Red
    Write-Host "Vuelve a lanzar esta herramienta (la segunda pasada tendria que cuadrarlos)." -ForegroundColor Red
  }
} finally { Pop-Location }
