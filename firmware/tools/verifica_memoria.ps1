# verifica_memoria.ps1 - Comprobador mecanico de la memoria del proyecto.
#
# QUE HACE: revisa que las Capas 1 y 2 (las de lectura obligatoria) esten donde
# deben, que no contengan datos sabidamente obsoletos ni referencias por numero de
# linea, que la codificacion este sana, que las fichas de la Capa 3 lleven su aviso
# de historico, y que el numero de compilacion cuadre entre el contador, el
# platformio.ini y el binario compilado.
#
# ★ DÓNDE SE EJECUTA IMPORTA (2026-09-15). En el repositorio hay DOS copias del
# proyecto y el script deduce cual esta mirando por su propia carpeta ($PSScriptRoot).
# Ejecutado desde la copia ANTIGUA (Faketec_APRS_Igate_EA2OY) da "0 fallos" y ENGAÑA:
# alli el platformio.ini viejo cuadra con el .buildnum viejo y no hay ni el binario
# ni las fuentes de hoy. Por eso abajo se DETECTA el arbol y se avisa en grande.
#
# COMO SE USA (desde el arbol VIVO, _trabajo_ea2oy):
#     powershell -File tools\verifica_memoria.ps1
#
# SALIDA: 0 = todo bien (puede haber AVISOS)  |  1 = hay FALLOS.
# Regla de diseno: lo que es informativo AVISA; solo falla lo que esta mal de verdad.
# Un verificador que da 40 fallos inutiles se ignora, y uno que se ignora es peor
# que no tenerlo.

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$raizProyecto = Split-Path -Parent $PSScriptRoot
$memoria      = Join-Path (Split-Path -Parent $raizProyecto) "Cerebro_Faketec_APRS_Igate_EA2OY"

$fallos = 0
$avisos = 0

function Fallo($msg) { Write-Host ("FALLO  " + $msg) -ForegroundColor Red;    $script:fallos++ }
function Aviso($msg) { Write-Host ("aviso  " + $msg) -ForegroundColor Yellow; $script:avisos++ }
function Bien ($msg) { Write-Host ("OK     " + $msg) -ForegroundColor Green }

# Lee un fichero como UTF-8 de verdad (PowerShell 5.1 con Get-Content -Raw lo
# interpreta como ANSI y se inventa mojibake: es la trampa que documenta el propio
# prompt de reorganizacion, asi que aqui no se usa).
function LeerUtf8($ruta) {
  return [System.IO.File]::ReadAllText($ruta, (New-Object System.Text.UTF8Encoding($false)))
}

# Quita los acentos para poder comparar texto sin depender de ellos. Hace falta
# porque los patrones de este script se escriben sin acentos (el fichero es ASCII)
# y en PowerShell -match NO ignora las tildes: "codigo" no casa con "codigo" con
# tilde. Normaliza a FormD y elimina las marcas diacriticas.
function SinAcentos($texto) {
  $formD = $texto.Normalize([System.Text.NormalizationForm]::FormD)
  $sb = New-Object System.Text.StringBuilder
  foreach ($ch in $formD.ToCharArray()) {
    if ([System.Globalization.CharUnicodeInfo]::GetUnicodeCategory($ch) -ne
        [System.Globalization.UnicodeCategory]::NonSpacingMark) {
      [void]$sb.Append($ch)
    }
  }
  return $sb.ToString().Normalize([System.Text.NormalizationForm]::FormC)
}

# ---------------------------------------------------------------- 0. ubicacion
Write-Host ""
Write-Host "=== 0. Ubicacion ==="
if (-not (Test-Path (Join-Path $raizProyecto "platformio.ini"))) {
  Fallo "no encuentro platformio.ini: ejecuta el script desde Faketec_APRS_Igate_EA2OY\tools\"
  exit 1
}
if (-not (Test-Path $memoria)) {
  Fallo "no encuentro la carpeta de memoria: $memoria"
  exit 1
}
Bien "proyecto: $raizProyecto"
Bien "memoria : $memoria"

# ------------------------------- 0-bis. ¿EN QUE ARBOL ESTAMOS? (2026-09-15)
# En el repositorio hay DOS copias del proyecto. El arbol VIVO es _trabajo_ea2oy
# (tiene src\haptic.cpp, el driver del motor del T-Echo Plus, y demas trabajo de las
# ultimas sesiones). La copia ANTIGUA es Faketec_APRS_Igate_EA2OY (NO tiene haptic.cpp
# y SI tiene src\hello_techo.cpp, el banco de pruebas de la pantalla). Esos dos
# ficheros son las huellas que los distinguen, y son de fiar porque no dependen de
# ningun numero que alguien pueda olvidarse de subir.
Write-Host ""
Write-Host "=== 0-bis. En que arbol se esta ejecutando ==="

function ArbolPareceVivo($raiz) {
  return (Test-Path (Join-Path $raiz "src\haptic.cpp"))
}
function ArbolPareceAntiguo($raiz) {
  $h = Join-Path $raiz "src\haptic.cpp"
  $c = Join-Path $raiz "src\hello_techo.cpp"
  return ((-not (Test-Path $h)) -and (Test-Path $c))
}

# ¿Existe el otro arbol? Se busca a un lado y a otro de la raiz del repositorio.
$raizRepo = Split-Path -Parent $raizProyecto
$arbolVivo = $null
foreach ($cand in @((Join-Path $raizRepo "_trabajo_ea2oy"), (Join-Path $raizRepo "Faketec_APRS_Igate_EA2OY"))) {
  if ((Test-Path $cand) -and (ArbolPareceVivo $cand)) { $arbolVivo = $cand }
}

$estoyEnVivo    = ArbolPareceVivo    $raizProyecto
$estoyEnAntiguo = ArbolPareceAntiguo $raizProyecto

if ($estoyEnVivo) {
  Bien "estas en el arbol VIVO: $raizProyecto"
} elseif ($estoyEnAntiguo) {
  Fallo "ESTAS EN LA COPIA ANTIGUA DEL PROYECTO: $raizProyecto"
  Write-Host "       Es la carpeta Faketec_APRS_Igate_EA2OY, que NO tiene el codigo de hoy" -ForegroundColor Red
  Write-Host "       (le faltan src\haptic.cpp y los ultimos arreglos, y su platformio.ini es mas viejo)." -ForegroundColor Red
  Write-Host "       AQUI EL VERIFICADOR DA UN RESULTADO QUE ENGAÑA: el platformio.ini viejo cuadra" -ForegroundColor Red
  Write-Host "       con el .buildnum viejo, asi que sale '0 fallos' sin haber mirado el codigo real." -ForegroundColor Red
  Write-Host "       EL ARBOL VIVO ES: _trabajo_ea2oy   ->  ejecuta el script desde ahi." -ForegroundColor Red
} else {
  Aviso "no reconozco este arbol (no tiene src\haptic.cpp ni src\hello_techo.cpp): puede ser una copia nueva"
}

# El numero de compilacion se comprueba SIEMPRE contra el arbol vivo, no contra la
# carpeta desde la que se lance. Si no se encuentra el arbol vivo, se cae a la carpeta
# actual (para que el script siga siendo util en un clon suelto).
$raizBuild = $raizProyecto
if ($arbolVivo) {
  $raizBuild = $arbolVivo
  if ($arbolVivo -ne $raizProyecto) {
    Aviso "el numero de compilacion se comprueba contra el arbol VIVO: $arbolVivo"
  }
} else {
  Aviso "no encuentro el arbol vivo al lado de esta carpeta: compruebo el numero de compilacion aqui"
}

# ------------------------------------------------------- 1. capas obligatorias
Write-Host ""
Write-Host "=== 1. Las capas obligatorias existen ==="
$capa1 = Join-Path $memoria "REGLAS.md"
$capa2 = Join-Path $memoria "ESTADO.md"

# MEMORIA VIGENTE = los documentos que hoy se leen como fuente. Las reglas duras
# (numeros de linea, datos obsoletos) se aplican SOLO a estos: lo que ya es
# historico se AVISA, no se falla, porque su trabajo es contar lo que paso.
# Esta lista crece a medida que avanza la reorganizacion.
$vigentes = @($capa1, $capa2)
foreach ($c in @(@($capa1, "Capa 1 (REGLAS.md)"), @($capa2, "Capa 2 (ESTADO.md)"))) {
  if (Test-Path $c[0]) { Bien "$($c[1]) existe ($((Get-Item $c[0]).Length) B)" }
  else { Fallo "$($c[1]) NO existe" }
}

# ------------------------------------------------------- 2. un solo estado
Write-Host ""
Write-Host "=== 2. Un unico documento de estado ==="
$estados = @()
Get-ChildItem $memoria -Recurse -File -Filter *.md -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -notmatch '\\_archivo\\' -and $_.FullName -notmatch '\\_backups\\' -and $_.Name -ne 'historia.md' -and $_.FullName -notmatch '\\_memoria\\' } |
  ForEach-Object {
    $t = LeerUtf8 $_.FullName
    if ($t -match '(?m)^#\s*ESTADO ACTUAL') { $estados += $_.FullName }
  }
if ($estados.Count -eq 1) { Bien "un solo documento de estado: $(Split-Path $estados[0] -Leaf)" }
elseif ($estados.Count -eq 0) { Fallo "no encuentro ningun documento de estado (falta ESTADO.md)" }
else { Fallo ("hay $($estados.Count) documentos de estado: " + (($estados | ForEach-Object { Split-Path $_ -Leaf }) -join ', ')) }

# ----------------------------------- 3. el historico esta marcado como historico
Write-Host ""
Write-Host "=== 3. El historico se anuncia como historico ==="
$historia = Join-Path $memoria "_memoria\historia.md"
if (Test-Path $historia) {
  $t = LeerUtf8 $historia
  $cab = ($t -split "`n" | Select-Object -First 12) -join "`n"
  if ($cab -match 'HIST[O\u00d3]RICO') { Bien "historia.md lleva el aviso de historico en la cabecera" }
  else { Fallo "historia.md NO lleva el aviso de HISTORICO en las primeras lineas" }
} else {
  Aviso "todavia no existe _memoria\historia.md (el reparto del cerebro esta pendiente)"
}
# ★ LAS FICHAS DE LA CAPA 3 TAMBIEN SON HISTORICO (2026-09-15).
# La regla 4.10 dice que un documento historico lo avisa EN SU PRIMERA LINEA. Las
# subnotas y las neuronas se escribieron en la etapa inicial (septiembre de 2026) y
# afirman cosas como "no code yet" o "we must choose platform", que dejaron de ser
# ciertas hace semanas: sin el aviso, un agente nuevo las lee como estado de hoy.
# Se AVISA (no se falla) porque su contenido no tiene por que ser falso: lo que
# falta es la etiqueta.
$fichasCapa3 = @()
foreach ($sub in @("_memoria\01_subnotas", "_memoria\02_neuronas")) {
  $d = Join-Path $memoria $sub
  if (Test-Path $d) {
    $fichasCapa3 += (Get-ChildItem $d -File -Filter *.md -ErrorAction SilentlyContinue)
  }
}
$fichasSinMarca = @()
foreach ($fi in $fichasCapa3) {
  $t = LeerUtf8 $fi.FullName
  # Primera linea que no este vacia, sin el '#' del titulo, sin acentos y en mayusculas:
  # asi "histórico" y "HISTORICO" cuentan igual.
  $primera = ""
  foreach ($l in ($t -split "`n")) {
    if ($l.Trim().Length -gt 0) { $primera = $l; break }
  }
  $plano = (SinAcentos $primera).ToUpper() -replace '^[#>\s]+', ''
  if ($plano -notmatch 'HIST[O]RICO') { $fichasSinMarca += $fi.Name }
}
if ($fichasCapa3.Count -eq 0) {
  Aviso "no encuentro fichas en _memoria\01_subnotas\ ni _memoria\02_neuronas\"
} elseif ($fichasSinMarca.Count -eq 0) {
  Bien "las $($fichasCapa3.Count) fichas de la Capa 3 llevan su aviso de historico en la primera linea"
} else {
  Aviso "sin aviso de HISTORICO en la primera linea: $($fichasSinMarca.Count) de $($fichasCapa3.Count) fichas de la Capa 3 -> $(($fichasSinMarca | Select-Object -First 6) -join ', ')"
}
# Los documentos del proyecto que son vision/analisis, no estado
foreach ($doc in @("docs\APP_PROPIA_Y_COMPATIBILIDAD.md")) {
  $p = Join-Path $raizProyecto $doc
  if (Test-Path $p) {
    $t = LeerUtf8 $p
    if ($t -match 'NO ES NORMATIVO|no es normativo') { Bien "$doc se declara no normativo" }
    else { Aviso "$doc no declara ser material de consulta (no normativo)" }
  }
}

# ------------------------------------------- 4. codificacion (las dos capas)
Write-Host ""
Write-Host "=== 4. Codificacion de las Capas 1 y 2 ==="
foreach ($f in @($capa1, $capa2)) {
  if (-not (Test-Path $f)) { continue }
  $nombre = Split-Path $f -Leaf
  $bytes  = [System.IO.File]::ReadAllBytes($f)
  $bom    = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
  if ($bom) { Aviso "$nombre tiene BOM (mejor sin BOM)" } else { Bien "$nombre sin BOM" }
  $ctrl = 0
  for ($i = 0; $i -lt $bytes.Length; $i++) {
    $b = $bytes[$i]
    if ($b -lt 32 -and $b -ne 9 -and $b -ne 10 -and $b -ne 13) { $ctrl++ }
  }
  if ($ctrl -gt 0) { Fallo "$nombre tiene $ctrl caracteres de control invisibles" }
  else { Bien "$nombre sin caracteres de control invisibles" }
  $t = LeerUtf8 $f
  $moji = ([regex]::Matches($t, '\u00C3[\u0080-\u00BF]')).Count + ([regex]::Matches($t, '\u00E2\u20AC')).Count
  if ($moji -gt 0) { Fallo "$nombre tiene $moji secuencias de mojibake" }
  else { Bien "$nombre sin mojibake" }
}

# ------------------------------- 5. referencias por numero de linea (prohibidas)
Write-Host ""
Write-Host "=== 5. Referencias por numero de linea (prohibidas fuera del historico) ==="
$permitidos = @("historia.md")
$patronRef = '[A-Za-z0-9_\-/]+\.(cpp|h|ini|js|ps1|py|json|html)\s*:\s*\d+'
$refsTotal = 0
$refsHistoricas = 0

# 5a. MEMORIA VIGENTE: cualquier referencia por numero de linea es un FALLO
foreach ($f in $vigentes) {
  if (-not (Test-Path $f)) { continue }
  $t = LeerUtf8 $f
  $m = [regex]::Matches($t, $patronRef)
  if ($m.Count -gt 0) {
    $refsTotal += $m.Count
    Fallo ("$([System.IO.Path]::GetFileName($f)): $($m.Count) referencias por numero de linea -> " + (($m | Select-Object -First 3 | ForEach-Object { $_.Value }) -join ', '))
  }
}
if ($refsTotal -eq 0) { Bien "ninguna referencia por numero de linea en la memoria vigente" }

# 5b. LO QUE TODAVIA NO SE HA MOVIDO: se cuenta y se avisa (no es un fallo:
#     son los documentos que el reparto tiene que dejar en Capa 3)
Get-ChildItem $memoria -Recurse -File -Filter *.md -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -notmatch '\\_archivo\\' -and $_.FullName -notmatch '\\_backups\\' -and $permitidos -notcontains $_.Name } |
  ForEach-Object {
    if ($vigentes -contains $_.FullName) { return }
    $t = LeerUtf8 $_.FullName
    $c = [regex]::Matches($t, $patronRef).Count
    if ($c -gt 0) { $refsHistoricas += $c }
  }
if ($refsHistoricas -gt 0) {
  Aviso "$refsHistoricas referencias por numero de linea en documentos que aun no son Capa 3 (desaparecen al completar el reparto)"
}

# ----------------------------------- 6. datos sabidamente obsoletos (R7/valores)
Write-Host ""
Write-Host "=== 6. Datos obsoletos conocidos, en la memoria vigente ==="
# Cada entrada: etiqueta, patron, y donde NO debe aparecer.
$obsoletos = @(
  # El patron busca el dato viejo AFIRMADO. Si la linea esta desmintiendo el dato
  # (porque dice cual es el bueno), no cuenta: ver la lista de marcadores de
  # exencion unas lineas mas abajo. Citar un dato viejo para corregirlo es justo
  # lo que queremos que se pueda hacer.
  @("toque largo 800 ms (el real es 600 ms)", 'largo[^\n]{0,40}800\s*ms'),
  @("doble toque 400 ms (el real es 800 ms)", 'doble[^\n]{0,40}400\s*ms'),
  @("clave trackerMinSpacing (la real es trackerMinSpacingSecs)", 'trackerMinSpacing(?!Secs)'),
  @("clave chipTempOffset (la real es chipTempOffsetC)", 'chipTempOffset(?!C)'),
  # ★ CORREGIDO EL 2026-09-17: esta entrada buscaba `ble_kiss.cpp`/`ble_kiss.h` "sin .off".
  #   El operador ordeno reactivar el Bluetooth ese dia, asi que los ficheros volvieron a
  #   llamarse `ble_kiss.cpp` y `ble_kiss.h` A PROPOSITO y el verificador fallaba por hacer
  #   justo lo que se le pide. Lo que sigue siendo un dato viejo es creerse que el Bluetooth
  #   esta APAGADO: eso es lo que se busca ahora, y la misma linea vale para el resto de la
  #   familia (los `.off` ya no existen; si alguien los vuelve a crear, la huella esta en el
  #   arbol con `git log`).
  @("dice que el Bluetooth sigue escrito y apagado",
    'ble_kiss\.(cpp|h)\.off|Bluetooth[^\n]{0,40}(escrito y apagado|fuera de la compilaci)'),
  @("'Pre-alpha' / 'no hay codigo' (el firmware esta hecho)", 'Pre-alpha|no code yet'),
  @("marca de trabajo en curso dejada en un documento", 'POR REVISAR|POR DECIDIR|TODO:')
)
# Marcadores que indican que la linea esta CORRIGIENDO el dato, no afirmandolo.
# Se comparan sobre el texto SIN acentos (ver SinAcentos).
$marcadoresCorreccion = 'el real|real es|hoy es|antes decia|antes dec|obsolet|NO usar|corregid|apagad|ya no existe|ya no es|el manual|estuvo diciendo|cuando el cod'
$ficherosVigentes = $vigentes
foreach ($o in $obsoletos) {
  $donde = @()
  foreach ($f in $ficherosVigentes) {
    if (-not (Test-Path $f)) { continue }
    $t = LeerUtf8 $f
    foreach ($linea in ($t -split "`n")) {
      $plano = SinAcentos $linea
      if ($plano -match $o[1]) {
        if ($plano -notmatch $marcadoresCorreccion) {
          $donde += ([System.IO.Path]::GetFileName($f) + " -> " + $linea.Trim())
        }
      }
    }
  }
  if ($donde.Count -gt 0) { Fallo ("$($o[0]) aparece en la memoria vigente: " + ($donde -join ' | ')) }
}
Bien "revision de datos obsoletos terminada"

# ------------------------- 6-bis. esos mismos datos, en las fichas de la Capa 3
# Las fichas ya estan marcadas como historicas, asi que aqui NO se falla: se avisa,
# para que quien las lea sepa que ese tramo concreto ya no es cierto. El caso tipico
# es la subnota 01_proyecto.md diciendo "Pre-alpha (no code yet)".
Write-Host ""
Write-Host "=== 6-bis. Datos obsoletos dentro de las fichas historicas (aviso) ==="
$obsoletosFichas = @(
  @("dice que no hay codigo / que falta elegir plataforma", 'Pre-alpha|no code yet|we must choose platform'),
  @("dice que el Bluetooth viene encendido de fabrica", 'bleEnabled[^\n]{0,30}(true|encendid)')
)
$avisosFichas = 0
foreach ($o in $obsoletosFichas) {
  foreach ($fi in $fichasCapa3) {
    $t = LeerUtf8 $fi.FullName
    foreach ($linea in ($t -split "`n")) {
      $plano = SinAcentos $linea
      if ($plano -match $o[1]) {
        if ($plano -notmatch $marcadoresCorreccion) {
          Aviso "$($fi.Name) (historica): $($o[0]) -> $($linea.Trim())"
          $avisosFichas++
        }
      }
    }
  }
}
if ($avisosFichas -eq 0) { Bien "ninguna ficha historica afirma datos ya desmentidos de los buscados" }

# --------------------------------------------- 7. enlaces relativos entre docs
Write-Host ""
Write-Host "=== 7. Enlaces relativos rotos ==="
$rotos = 0
Get-ChildItem $memoria -Recurse -File -Filter *.md -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -notmatch '\\_archivo\\' -and $_.FullName -notmatch '\\_backups\\' } |
  ForEach-Object {
    $t = LeerUtf8 $_.FullName
    foreach ($m in [regex]::Matches($t, '\]\(([^)#:]+\.md)\)')) {
      $destino = Join-Path $_.DirectoryName $m.Groups[1].Value
      if (-not (Test-Path $destino)) { Fallo "enlace roto en $($_.Name): $($m.Groups[1].Value)"; $rotos++ }
    }
  }
if ($rotos -eq 0) { Bien "ningun enlace relativo roto" }

# --------------------------------------------- 8. numero de compilacion cuadra
Write-Host ""
Write-Host "=== 8. Numero de compilacion: contador, platformio.ini y binario ==="
# ★ Se miran en el ARBOL VIVO ($raizBuild), no en la carpeta desde la que se lance el
# script: es lo unico que tiene sentido, porque el binario y las fuentes de hoy estan
# alli (ver 0-bis).
$fBuildnum = Join-Path $raizBuild ".buildnum"
$fIni      = Join-Path $raizBuild "platformio.ini"
$fUf2      = Join-Path $raizBuild ".pio\build\techo_plus_s140v7\firmware.uf2"
$nContador = $null; $nIni = $null
if (Test-Path $fBuildnum) {
  $nContador = (LeerUtf8 $fBuildnum).Trim()
  Bien ".buildnum = $nContador"
} else { Fallo "no existe .buildnum" }
if (Test-Path $fIni) {
  $t = LeerUtf8 $fIni
  $m = [regex]::Match($t, 'APP_BUILD_NUM=\\?"(b\d+)\\?"')
  if ($m.Success) { $nIni = $m.Groups[1].Value; Bien "platformio.ini declara $nIni" }
  else { Fallo "platformio.ini no declara APP_BUILD_NUM" }
  if ($t -match 'APP_BUILD_NUM=b0') { Aviso "el valor de socorro b0 no debe quedar en la compilacion" }
}
if ($nContador -and $nIni -and ("b" + $nContador) -ne $nIni) {
  Fallo "descuadre: .buildnum dice $nContador y platformio.ini dice $nIni (deberian coincidir)"
}
# ★ EL AUTOMATISMO DEL NUMERO DE COMPILACION TIENE QUE SEGUIR PUESTO (2026-09-15).
# Desde esa fecha el numero no se sube a mano: lo sube extra_scripts\sube_buildnum.py,
# que ademas PARA la compilacion si los dos numeros no cuadran. Si alguien quita el
# script (o la linea que lo engancha en platformio.ini), el contador se queda quieto y
# volvemos al fallo de antes (el contador en 3 con el ini en b4) sin que nadie se entere
# hasta que el binario ya esta grabado. Por eso se comprueba que siga ahi.
$fScriptBN = Join-Path $raizBuild "extra_scripts\sube_buildnum.py"
if (-not (Test-Path $fScriptBN)) {
  Fallo "falta extra_scripts\sube_buildnum.py: el numero de compilacion volveria a subirse a mano"
} else {
  $iniTxt = LeerUtf8 $fIni
  if ($iniTxt -notmatch 'sube_buildnum\.py') {
    Fallo "platformio.ini ya no llama a extra_scripts\sube_buildnum.py: el numero no se subiria solo"
  } else {
    Bien "el numero de compilacion lo sube solo sube_buildnum.py (enganchado en platformio.ini)"
  }
}
if (Test-Path $fUf2) {
  $bytes = [System.IO.File]::ReadAllBytes($fUf2)
  $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
  if ($nIni -and $ascii.Contains($nIni)) { Bien "el binario compilado contiene $nIni" }
  else { Fallo "el binario compilado NO contiene $nIni (¿se grabo o se copio otro firmware?)" }
  # ¿es mas viejo que los fuentes?
  $fUf2Fecha = (Get-Item $fUf2).LastWriteTime
  $masNuevo = Get-ChildItem (Join-Path $raizProyecto "src") -Recurse -File -Include *.cpp,*.h |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($masNuevo -and $masNuevo.LastWriteTime -gt $fUf2Fecha) {
    Aviso "hay fuentes mas nuevos que el binario ($($masNuevo.Name), $($masNuevo.LastWriteTime)): el .uf2 esta caducado"
  } else { Bien "el binario es mas nuevo que los fuentes" }
} else { Aviso "no hay firmware.uf2 compilado en .pio\build\techo_plus_s140v7" }

# --------------------- 8-bis. el PDF del manual de usuario no se queda atras
Write-Host ""
Write-Host "=== 8-bis. Manual de usuario: fuente PDF y artefacto PDF ==="
# El PDF es un ARTEFACTO DERIVADO: se genera a mano con tools\generar_pdf.ps1 (Pandoc
# + XeLaTeX) y no se entera de que el .md ha cambiado. El 2026-09-15 se corrigio el
# manual a las 21:53 y el PDF siguio siendo el de las 20:51: durante una hora el PDF
# afirmaba que una funcion no servia en modo repetidor si no se activaba antes un
# ajuste a mano, cuando eso ya estaba arreglado. Nadie lo noto hasta mirar las fechas.
# Esto no rompe nada (no hay que recompilar ni regrabar el T-Echo), pero publica un
# manual que miente, asi que se AVISA en vez de fallar, igual que "el .uf2 esta
# caducado" de arriba: lo que se arregla lanzando un generador no debe bloquear el
# trabajo, pero tiene que salir en el resumen y decir QUE HACER.
# Se comprueba SOLO la pareja del manual de usuario, que es la unica que se convierte
# a PDF. MANUAL_DE_USO.md es la version antigua: que no tenga PDF no es un fallo.
# La pareja se mira en la carpeta DESDE LA QUE SE LANZA ($raizProyecto), no en el
# arbol vivo: docs\ viaja con cada copia y la de la copia antigua tiene su propio
# manual, asi que no se puede mezclar el .md de una con el PDF de la otra.
# OJO con el NOMBRE del PDF publicado: el repositorio publico tiene UN SOLO PDF y se
# llama assets\Manual_Kacho_System.pdf (es el que enlaza el README en cuatro sitios).
# Antes habia una segunda copia en docs\pdf\MANUAL_USUARIO.pdf, identica byte a byte:
# se quito a proposito para que no puedan divergir. El PDF SIGUE generandose en
# docs\pdf\ y desde ahi se copia al repositorio publicado: por eso aqui se comprueban
# LOS DOS artefactos si existen, que el que se publica es el que miente si se queda viejo.
# El repositorio vive en la RAIZ (LoRa_APRS_iGate-main), que es la carpeta que
# contiene a las dos: al proyecto y a la memoria. Se resuelve AQUI, antes de mirar el
# manual, porque el PDF publicado (assets\) cuelga de la raiz del repositorio, no de
# la carpeta desde la que se lanza.
$raizRepo = Split-Path -Parent $raizProyecto
$manualFuente = Join-Path $raizProyecto "docs\MANUAL_USUARIO.md"
$manualPdf    = ""
if (Test-Path $manualFuente) {
  $pdfArtefacto = Join-Path $raizProyecto "docs\pdf\MANUAL_USUARIO.pdf"
  $pdfPublicado = Join-Path $raizRepo "assets\Manual_Kacho_System.pdf"
  if (Test-Path $pdfArtefacto) { $manualPdf = $pdfArtefacto }
  elseif (Test-Path $pdfPublicado) { $manualPdf = $pdfPublicado }
}
if (-not (Test-Path $manualFuente)) {
  Aviso "no encuentro docs\MANUAL_USUARIO.md: no puedo comparar el manual con su PDF"
} elseif (-not $manualPdf) {
  Aviso "no encuentro ningun PDF del manual (ni docs\pdf\MANUAL_USUARIO.pdf ni assets\Manual_Kacho_System.pdf): generelo con tools\generar_pdf.ps1"
} else {
  $fechaFuente = (Get-Item $manualFuente).LastWriteTime
  $fechaPdf    = (Get-Item $manualPdf).LastWriteTime
  if ($fechaFuente -gt $fechaPdf) {
    Aviso "el PDF del manual esta DESACTUALIZADO: MANUAL_USUARIO.md es de $fechaFuente y $([System.IO.Path]::GetFileName($manualPdf)) de $fechaPdf"
    Write-Host "       El PDF no se actualiza solo: lo genera tools\generar_pdf.ps1 a mano." -ForegroundColor Yellow
    Write-Host "       Vuelve a lanzarlo para que el PDF publicado no siga diciendo cosas ya corregidas:" -ForegroundColor Yellow
    Write-Host "           pwsh -File tools\generar_pdf.ps1   (o powershell -File tools\generar_pdf.ps1)" -ForegroundColor Yellow
    Write-Host "       Y si el que esta viejo es assets\Manual_Kacho_System.pdf, copia el PDF nuevo" -ForegroundColor Yellow
    Write-Host "       desde docs\pdf\ al repositorio publicado: es el unico PDF que ve el usuario." -ForegroundColor Yellow
  } else {
    Bien "el PDF del manual esta al dia (fuente $fechaFuente, PDF $fechaPdf)"
  }
}

if (Test-Path (Join-Path $raizRepo ".git")) {
  Bien "hay repositorio git en $raizRepo"
} elseif (Test-Path (Join-Path $raizProyecto ".git")) {
  Bien "hay repositorio git en el proyecto"
} else {
  Aviso "no hay repositorio git: no se puede demostrar con git diff que al partir documentos no se pierde nada"
}

# --------------------------------------------- 9. la lista de pendientes existe
Write-Host ""
Write-Host "=== 9. Lista de tareas pendientes del operador ==="
$pend = Join-Path $memoria "_memoria\PENDIENTE.md"
if (Test-Path $pend) { Bien "_memoria\PENDIENTE.md existe" } else { Aviso "no existe _memoria\PENDIENTE.md" }

# ------------------------------------------------------------------- resumen
Write-Host ""
Write-Host "=================== RESUMEN ==================="
Write-Host ("FALLOS: {0}   AVISOS: {1}" -f $fallos, $avisos)
if ($fallos -eq 0) { Write-Host "TODO CORRECTO" -ForegroundColor Green; exit 0 }
else { Write-Host "HAY FALLOS QUE CORREGIR" -ForegroundColor Red; exit 1 }
