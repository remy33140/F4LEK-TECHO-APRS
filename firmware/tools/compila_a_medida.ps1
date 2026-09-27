<#
.SYNOPSIS
  Compila un firmware con los valores de fabrica de OTRA estacion (indicativo, mensaje de
  dormido, modo y perfil de movimiento) sin tocar el arbol oficial.

.PARAMETER Indicativo
  Indicativo que queda grabado como valor de fabrica (por ejemplo EA2BIZ). Se guarda tal cual.

.PARAMETER MensajeDormido
  Texto que sale en la pantalla de dormido Y en el splash de arranque (el mismo `sleepMsg`).
  Maximo 63 caracteres. Si no se pone, se deja vacio.

.PARAMETER Modo
  0 = repetidor, 1 = rastreador, 2 = digi+tracker. Por defecto 2 (el de fabrica).

.PARAMETER Perfil
  0 = sin perfil, 1 = a pie, 2 = bici, 3 = coche. Por defecto 0 (el de fabrica).
  Con un perfil puesto, la baliza se firma con el SSID de ESE perfil y lleva su icono.

.PARAMETER Entorno
  Entorno de PlatformIO. Por defecto `techo_s140v7` (T-Echo normal con cargador S140 v7).
  Otros utiles: `techo_plus_s140v7` (T-Echo Plus), `techo` / `techo_plus` (T-Echo con
  cargador v6).

.PARAMETER Salida
  Ruta del .uf2 que se deja al final. Por defecto, un fichero en el Escritorio con el
  indicativo en el nombre.

.PARAMETER CarpetaTrabajo
  Carpeta temporal donde se copia el arbol y se compila. Se CONSERVA a proposito: si hay que
  cambiar algo (el SSID, el modo, el texto), se recompila en un minuto. Por defecto
  `%USERPROFILE%\Desktop\_build_<indicativo en minusculas>`.

.EXAMPLE
  powershell -File tools\compila_a_medida.ps1 -Indicativo EA2BIZ -MensajeDormido "645 722 625" -Perfil 3

.NOTES
  POR QUE ASI (2026-09-16): la receta de los binarios a medida anteriores (EA2IP, EB2FIZ) se
  perdio y hubo que reinventarla. Las reglas que sigue, y que NO hay que saltarse:
    1) El arbol oficial `_trabajo_ea2oy\` NO se toca: se copia a una carpeta aparte y se
       compila alli. Asi el repositorio nunca se queda con el indicativo de un tercero.
    2) El indicativo y el mensaje son DATOS DE OTRA PERSONA: no van a ningun fichero
       versionado ni a ningun sitio publico. Solo dentro del binario.
    3) Se comprueba que las dos cadenas estan DENTRO del .uf2 antes de darlo por bueno.
    4) El binario lleva el numero de compilacion del arbol en el momento de copiarlo, y la
       copia sube su propio contador: el arbol oficial no gasta numeros por esto.

  License: GPL-3.0
#>
param(
  [Parameter(Mandatory=$true)][string]$Indicativo,
  [string]$MensajeDormido = "",
  [int]$Modo = 2,
  [int]$Perfil = 0,
  [string]$Entorno = "techo_s140v7",
  [string]$Salida = "",
  [string]$CarpetaTrabajo = ""
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------- rutas
$raizOficial = Split-Path -Parent $PSScriptRoot          # _trabajo_ea2oy
$repo        = Split-Path -Parent $raizOficial           # raiz del repositorio
$pio         = "C:\Users\Jesus\.platformio\penv\Scripts\platformio.exe"
if (-not (Test-Path $pio)) {
  $pio = (Get-Command platformio -ErrorAction SilentlyContinue).Source
  if (-not $pio) { Write-Error "No encuentro PlatformIO."; exit 1 }
}
$coreDir = Join-Path $raizOficial "_pio_core"
if (-not (Test-Path $coreDir)) { $coreDir = Join-Path $repo "_pio_core" }

if (-not $CarpetaTrabajo) {
  $CarpetaTrabajo = Join-Path ([Environment]::GetFolderPath("Desktop")) ("_build_" + $Indicativo.ToLower())
}
if (-not $Salida) {
  $Salida = Join-Path ([Environment]::GetFolderPath("Desktop")) ($Indicativo.ToLower() + ".uf2")
}

# El nombre del fichero de salida NO se publica: es para entregar a mano.
if ($Indicativo -notmatch '^[A-Z0-9]{3,6}(-[0-9]{1,2})?$') {
  Write-Error "El indicativo '$Indicativo' no tiene pinta de indicativo (mayusculas y numeros, con SSID opcional)."
  exit 1
}
if ($MensajeDormido.Length -gt 63) { Write-Error "El mensaje de dormido no cabe (maximo 63 caracteres)."; exit 1 }
if ($Modo -lt 0 -or $Modo -gt 2)   { Write-Error "Modo tiene que ser 0, 1 o 2."; exit 1 }
if ($Perfil -lt 0 -or $Perfil -gt 3) { Write-Error "Perfil tiene que ser 0, 1, 2 o 3."; exit 1 }

$perfilNombre = @("sin perfil", "a pie", "bici", "coche")[$Perfil]
Write-Host ""
Write-Host "=== Compilacion a medida ===" -ForegroundColor Cyan
Write-Host "  Indicativo      : $Indicativo"
Write-Host "  Mensaje dormido : '$MensajeDormido'"
Write-Host "  Modo            : $Modo ($(@('repetidor','rastreador','digi+tracker')[$Modo]))"
Write-Host "  Perfil          : $Perfil ($perfilNombre)"
Write-Host "  Entorno         : $Entorno"
Write-Host "  Copia de trabajo: $CarpetaTrabajo"
Write-Host "  Salida          : $Salida"
Write-Host ""

# ---------------------------------------------------------------- copia del arbol
# Se copia TODO menos lo que no hace falta compilar y lo que trae datos reales.
$excluir = @(".pio", "_pio_core", "logs", "data", "docs", "web", "tools", "_archivo",
             "_resp_ea2oy", "_revision_pantalla_ea2oy", "_tmp_aviso_movil_ea2oy",
             "_tmp_iconos_ea2oy", ".tmp_zig")
$primeraVez = -not (Test-Path (Join-Path $CarpetaTrabajo "platformio.ini"))
if ($primeraVez) {
  Write-Host "--- copiando el arbol oficial (sin .pio, logs, docs ni datos) ---"
  New-Item -ItemType Directory -Force -Path $CarpetaTrabajo | Out-Null
  $argsRobo = @($raizOficial, $CarpetaTrabajo, "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/R:1", "/W:1")
  $argsRobo += "/XD"
  foreach ($d in $excluir) { $argsRobo += (Join-Path $raizOficial $d) }
  $argsRobo += "/XF"; $argsRobo += "*.log"; $argsRobo += "*.jsonl"; $argsRobo += "*.txt"; $argsRobo += "*.uf2"
  & robocopy @argsRobo | Out-Null   # robocopy devuelve 1 cuando copia: no es un error
  if (-not (Test-Path (Join-Path $CarpetaTrabajo "platformio.ini"))) {
    Write-Error "La copia no ha salido bien: no hay platformio.ini en $CarpetaTrabajo"
    exit 1
  }
} else {
  Write-Host "--- reutilizo la copia que ya existe (recompilacion rapida) ---"
}

# ---------------------------------------------------------------- los valores de fabrica
$cfg = Join-Path $CarpetaTrabajo "src\config.h"
if (-not (Test-Path $cfg)) { Write-Error "No encuentro $cfg"; exit 1 }
$texto = Get-Content -LiteralPath $cfg -Raw

# OJO: estas son las lineas EXACTAS de config.h. Si algun dia cambian de forma, el script
# para en vez de cambiar lo que no toca (una sustitucion a ciegas aqui es un disgusto).
$cambios = @(
  @{ que = 'callsign';        pat = 'char callsign\[16\]\s*=\s*"[^"]*";[^\r\n]*';      val = 'char callsign[16] = "' + $Indicativo + '";   // uppercase' },
  @{ que = 'sleepMsg';        pat = 'char sleepMsg\[64\]\s*=\s*"[^"]*";[^\r\n]*';      val = 'char sleepMsg[64] = "' + $MensajeDormido + '";' },
  @{ que = 'mode';            pat = 'uint8_t mode\s*=\s*\d+;[^\r\n]*';                 val = 'uint8_t mode = ' + $Modo + ';                // 0=digipeater, 1=tracker, 2=both' },
  @{ que = 'smartBeaconPreset'; pat = 'uint8_t smartBeaconPreset\s*=\s*\d+;[^\r\n]*';  val = 'uint8_t smartBeaconPreset = ' + $Perfil + ';   // 0=off(fixed/digi) 1=human 2=bike 3=car' }
)
foreach ($c in $cambios) {
  if ($texto -notmatch $c.pat) { Write-Error "En config.h no encuentro la linea de '$($c.que)' con la forma esperada. No cambio nada."; exit 1 }
  $texto = [regex]::Replace($texto, $c.pat, $c.val, 1)
}
# ★ SIN BOM, como todo el proyecto: `Set-Content -Encoding UTF8` en PowerShell 5.1 METE BOM, y
#   el config.h de la copia quedaba empezando por EF BB BF (el oficial empieza por 2F 2F). No
#   rompia la compilacion, pero era una diferencia real y el proyecto ya se ha llevado sustos con
#   los BOM. Se escribe con .NET, que no lo pone si se le dice que no.
[System.IO.File]::WriteAllText($cfg, $texto, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "--- config.h de la copia: indicativo, mensaje, modo y perfil puestos ---"
Select-String -Path $cfg -Pattern 'char callsign\[16\]|char sleepMsg\[64\]|uint8_t mode\s*=|uint8_t smartBeaconPreset' |
  ForEach-Object { "    " + $_.Line.Trim() }
$bom = ([System.IO.File]::ReadAllBytes($cfg)[0..2] | ForEach-Object { $_.ToString("X2") }) -join " "
if ($bom -eq "EF BB BF") { Write-Warning "El config.h de la copia ha salido CON BOM: revisa el metodo de escritura." }

# ---------------------------------------------------------------- el numero que llevara
# ★★ EL BINARIO A MEDIDA LLEVA EL MISMO NUMERO QUE EL FIRMWARE PUBLICADO (2026-09-16) ★★
#   Una compilacion a medida es EL MISMO firmware con otros valores de fabrica (el indicativo y
#   el mensaje del dueño), asi que tiene que contestar el mismo numero por USB que el que se
#   publica: si no, dos nodos con el mismo firmware dicen cosas distintas.
#   Como la copia arranca con el contador que tenia el arbol cuando se copio, y ademas lo sube
#   sola al detectar el cambio de `config.h`, aqui se le PONE el numero del arbol oficial justo
#   antes de compilar. Y se borra la marca de la huella del codigo para que no reste nada.
#   (Se comprobo que hacia falta: reciclando una copia, el binario salia con el numero que la
#   copia habia gastado en la vuelta anterior, distinto del publicado.)
$nOficial = (Get-Content -LiteralPath (Join-Path $raizOficial ".buildnum") -Raw).Trim()
if ($nOficial -notmatch '^\d+$') { Write-Error "El .buildnum del arbol oficial no es un numero ('$nOficial')."; exit 1 }
[System.IO.File]::WriteAllText((Join-Path $CarpetaTrabajo ".buildnum"), "$nOficial`n", (New-Object System.Text.UTF8Encoding($false)))
$iniCop = Join-Path $CarpetaTrabajo "platformio.ini"
$tIni = [System.IO.File]::ReadAllText($iniCop, [System.Text.Encoding]::UTF8)
$tIni = [regex]::Replace($tIni, '(-DAPP_BUILD_NUM=\\?"b)\d+(\\?")', '${1}' + $nOficial + '${2}', 1)
[System.IO.File]::WriteAllText($iniCop, $tIni, (New-Object System.Text.UTF8Encoding($false)))
Remove-Item (Join-Path $CarpetaTrabajo ".pio\.buildnum_fuentes") -Force -ErrorAction SilentlyContinue
Write-Host ("--- numero de compilacion de la copia: b{0} (el del arbol oficial) ---" -f $nOficial)

# ---------------------------------------------------------------- compilar
# ★★ COMO SE GARANTIZA QUE EL .uf2 ES DE ESTA COMPILACION (2026-09-16, medido) ★★
#   NO vale borrar el `.elf` ni el `.uf2` antes de compilar, y se probo:
#     - SCons decide POR CONTENIDO, asi que borrar y reenlazar con el mismo codigo da un `.elf`
#       identico -> el `.hex` no se rehace -> y si ademas se habia borrado el `.uf2`, se queda
#       SIN `.uf2` y la herramienta se queda mirando el aire.
#   Lo que SI vale, y es mas honesto: compilar, y despues COMPROBAR EN EL BINARIO que lleva el
#   numero que le toca (el del arbol oficial). Si no lo lleva, es que el `.uf2` era de una
#   compilacion anterior con otra configuracion, y entonces se borra la carpeta de compilacion
#   entera y se rehace de cero (mas lento, pero correcto).
function Compila([string]$entorno) {
  $env:PLATFORMIO_CORE_DIR = $coreDir
  Push-Location $CarpetaTrabajo
  $ErrorActionPreference = "Continue"   # el compilador escribe AVISOS por stderr: no son fallos
  try { $sal = & $pio run -e $entorno 2>&1; $cod = $LASTEXITCODE } finally { Pop-Location }
  $ErrorActionPreference = "Stop"
  if ($cod -ne 0) {
    $sal | Select-Object -Last 25 | ForEach-Object { Write-Host ("  " + $_) }
    Write-Error "La compilacion ha fallado (codigo $cod)."
    exit 1
  }
}

function NumeroDentro([string]$rutaUf2) {
  if (-not (Test-Path $rutaUf2)) { return $null }
  $txt = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($rutaUf2))
  # El numero es un LITERAL de C: en el binario va rodeado de terminadores (byte 0).
  # ★ DOS TRAMPAS, LAS DOS MEDIDAS:
  #   (1) hay que comparar trozos EXACTOS y sensibles a mayusculas: el operador -match de
  #       PowerShell no distingue mayusculas y colaba `B0`, `B8`... de las tablas de datos;
  #   (2) los delimitadores NO se pueden CONSUMIR en la coincidencia: si el patron se lleva el
  #       byte 0 que va DELANTE del numero, la coincidencia anterior se lo come y el numero no
  #       se ve nunca (pasaba: devolvia `b0, b1` en un binario que SI llevaba `b13`). Con
  #       lookarounds los delimitadores son de anchura cero y se encuentran TODOS los trozos.
  $encontrados = [regex]::Matches($txt, '(?<=[^\x20-\x7E])([ -~]{1,6})(?=[^\x20-\x7E])') |
                 ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -cmatch '^b\d+$' } | Sort-Object -Unique
  return $encontrados
}

$dirBuild = Join-Path $CarpetaTrabajo ".pio\build\$Entorno"
$uf2 = Join-Path $dirBuild "firmware.uf2"
Write-Host ""
Write-Host "--- compilando $Entorno ---"
Compila $Entorno

# ¿Lleva el numero que le toca?
$tiene = NumeroDentro $uf2
$esperado = "b$nOficial"
if (-not ($tiene -contains $esperado)) {
  Write-Host ("  el .uf2 no lleva $esperado dentro (lleva: " + $(if ($tiene) { $tiene -join ", " } else { "nada" }) + ")") -ForegroundColor Yellow
  Write-Host "  -> era de una compilacion anterior: se rehace de cero (tarda unos minutos)" -ForegroundColor Yellow
  Remove-Item $dirBuild -Recurse -Force -ErrorAction SilentlyContinue
  Compila $Entorno
  $tiene = NumeroDentro $uf2
  if (-not ($tiene -contains $esperado)) {
    Write-Error ("Despues de rehacerlo de cero, el .uf2 sigue sin llevar $esperado (lleva: " + $(if ($tiene) { $tiene -join ", " } else { "nada" }) + ").")
    exit 1
  }
}
if (-not (Test-Path $uf2)) { Write-Error "PlatformIO dijo OK pero no hay $uf2"; exit 1 }
Copy-Item -LiteralPath $uf2 -Destination $Salida -Force

# ---------------------------------------------------------------- comprobar
$info = Get-Item -LiteralPath $Salida
$sha  = (Get-FileHash -LiteralPath $Salida -Algorithm SHA256).Hash
$hex  = [System.IO.File]::ReadAllBytes($Salida)
$txt  = -join ($hex | ForEach-Object { if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { "`0" } })

$okCall = $txt.Contains($Indicativo)
$okMsg  = ($MensajeDormido -eq "") -or $txt.Contains($MensajeDormido)

# ★ EL NUMERO, BUSCADO COMO LITERAL (2026-09-16). Dos intentos fallidos antes de este, los dos
#   medidos: (1) buscar la PRIMERA coincidencia de `b`+digitos del fichero daba `b8`, un trozo de
#   una tabla de direcciones; (2) buscarlo por el contexto de la cadena de `status` del CLI
#   funcionaba en unos binarios y en otros no, porque la tabla de textos se ordena distinta en
#   cada compilacion. Lo que SI vale: el numero es un LITERAL de C, o sea que va rodeado de
#   terminadores; y como en los datos tambien caen trozos que parecen un numero (`b0`, `b8`), se
#   comprueba contra el que TIENE que ser, no contra el primero que aparezca.
$num = NumeroDentro $Salida
$okNum = ($num -contains $esperado)

Write-Host ""
Write-Host "COMPILADO" -ForegroundColor Green
Write-Host ("  Fichero : " + $info.FullName)
Write-Host ("  Tamano  : {0:N0} bytes" -f $info.Length)
Write-Host ("  SHA-256 : " + $sha)
Write-Host ("  Numero  : " + $(if ($okNum) { $esperado + "  (el mismo que el firmware publicado)" } else { "NO es " + $esperado + " -> " + $(if ($num) { $num -join ", " } else { "ninguno" }) })) -ForegroundColor $(if ($okNum) { "Green" } else { "Red" })
Write-Host ("  Indicativo dentro del binario : " + $(if ($okCall) { "SI" } else { "NO (revisar)" }))
Write-Host ("  Mensaje dentro del binario    : " + $(if ($okMsg) { "SI" } else { "NO (revisar)" }))
Write-Host ""
Write-Host "Para grabarlo: doble toque al reset de la placa y copia el .uf2 a la unidad que aparece." -ForegroundColor Yellow
Write-Host "RECUERDA: esto lleva datos de otra persona. No lo subas a ningun repositorio ni lo dejes en una carpeta compartida." -ForegroundColor Yellow
