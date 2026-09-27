# ============================================================================
# generar_pdf.ps1 - Convierte los manuales en Markdown a PDF bonito.
#
# QUE HACE: coge un .md de docs\ y saca un PDF con la plantilla del proyecto
# (tools\plantilla_kacho.tex): fondo oscuro, titulos dorados, el logo de la antena
# dibujado en vector en la portada y el indice automatico.
#
# HERRAMIENTAS: Pandoc + MiKTeX (xelatex). Las dos estan instaladas en este PC.
# Si algun dia no estan, el script lo dice claro en vez de fallar a medias.
#
# USO (desde la carpeta del firmware):
#     powershell -File tools\generar_pdf.ps1                      # todos los manuales
#     powershell -File tools\generar_pdf.ps1 -Archivo MANUAL_USUARIO.md
#
# Lecciones aprendidas de los manuales de NavaTastic (proyecto del operador), que
# se aplican aqui:
#   1. NO generar un PDF de cada cosa: solo los manuales que va a leer una persona.
#      Un PDF de notas internas es el "tercer PDF que no sirve".
#   2. Los emojis necesitan su propia fuente o XeLaTeX aborta.
#   3. Con fondo oscuro hay que forzar el color del texto en tablas y cajas, o sale
#      letra oscura sobre fondo oscuro.
#   4. XeLaTeX se pelea con los espacios en las rutas de las imagenes: aqui el logo
#      va DIBUJADO (TikZ), asi que ese problema no existe.
# ============================================================================

param(
    # Lista blanca: SOLO los manuales de usuario. Los informes internos no se
    # convierten (no los lee nadie de fuera y envejecen solos).
    [string[]]$Incluir = @("MANUAL_USUARIO.md"),
    [string]$Archivo = "",
    [string]$Carpeta = "$PSScriptRoot\..\docs",
    [string]$Salida = "$PSScriptRoot\..\docs\pdf",
    [string]$Plantilla = "$PSScriptRoot\plantilla_kacho.tex"
)

$ErrorActionPreference = "Continue"

# ---------------------------------------------------------------- herramientas
$pandoc = (Get-Command pandoc -ErrorAction SilentlyContinue).Source
if (-not $pandoc) {
    $local = "$env:LOCALAPPDATA\Pandoc\pandoc.exe"
    if (Test-Path $local) { $pandoc = $local } else {
        Write-Host "FALLO: no encuentro Pandoc. Instalalo (winget install --id JohnMacFarlane.Pandoc) y vuelve a probar." -ForegroundColor Red
        exit 1
    }
}
$xelatex = (Get-Command xelatex -ErrorAction SilentlyContinue).Source
if (-not $xelatex) {
    $miktex = "C:\Program Files\MiKTeX\miktex\bin\x64\xelatex.exe"
    if (Test-Path $miktex) { $xelatex = $miktex } else {
        Write-Host "FALLO: no encuentro xelatex (MiKTeX). Instalalo y vuelve a probar." -ForegroundColor Red
        exit 1
    }
}
if (-not (Test-Path -LiteralPath $Plantilla)) {
    Write-Host "FALLO: no encuentro la plantilla $Plantilla" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path -LiteralPath $Salida)) {
    New-Item -ItemType Directory -Path $Salida -Force | Out-Null
}

# ------------------------------------------------------------- que se convierte
if ($Archivo) {
    $candidato = Join-Path $Carpeta $Archivo
    if (-not (Test-Path -LiteralPath $candidato)) {
        Write-Host "FALLO: no existe $candidato" -ForegroundColor Red
        exit 1
    }
    $archivos = @($candidato)
} else {
    $archivos = Get-ChildItem -Path $Carpeta -Filter "*.md" |
                Where-Object { $_.Name -in $Incluir } |
                ForEach-Object { $_.FullName }
}

if ($archivos.Count -eq 0) {
    Write-Host "No hay ningun manual que convertir (lista blanca: $($Incluir -join ', '))." -ForegroundColor Yellow
    exit 0
}

Write-Host ""
Write-Host "=== Generador de PDF del proyecto ===" -ForegroundColor Cyan
Write-Host "  Pandoc  : $pandoc"
Write-Host "  XeLaTeX : $xelatex"
Write-Host "  Plantilla: $Plantilla"
Write-Host "  Salida  : $Salida"
Write-Host ""

$ok = 0
$fail = 0
foreach ($md in $archivos) {
    $nombre = [System.IO.Path]::GetFileNameWithoutExtension($md)
    $pdf = Join-Path $Salida ($nombre + ".pdf")
    Write-Host "-> $([System.IO.Path]::GetFileName($md))" -ForegroundColor White
    try {
        # xelatex tiene que estar en el PATH para que pandoc lo encuentre.
        $env:Path = "$([System.IO.Path]::GetDirectoryName($xelatex));$([System.IO.Path]::GetDirectoryName($pandoc));" + $env:Path

        $argumentos = @(
            $md,
            "-o", $pdf,
            "--pdf-engine=xelatex",
            "--template=$Plantilla",
            "-V", "colorlinks=true",
            "-V", "linkcolor=kachoBlue",
            "-V", "urlcolor=kachoGreen",
            "-V", "geometry:margin=2.4cm",
            "-V", "toc=true",
            "--highlight-style=tango"
        )
        & $pandoc @argumentos 2>$null
        if ($LASTEXITCODE -ne 0) { throw "pandoc termino con codigo $LASTEXITCODE" }
        if (-not (Test-Path -LiteralPath $pdf)) { throw "no se ha creado el PDF" }
        $kb = [math]::Round((Get-Item -LiteralPath $pdf).Length / 1KB)
        Write-Host "   OK -> $nombre.pdf ($kb KB)" -ForegroundColor Green
        $ok++
    } catch {
        Write-Host "   ERROR: $($_.Exception.Message)" -ForegroundColor Red
        $fail++
    }
}

Write-Host ""
Write-Host ("Resumen: {0} generados, {1} con errores." -f $ok, $fail) -ForegroundColor Cyan
if ($fail -gt 0) { exit 1 } else { exit 0 }
