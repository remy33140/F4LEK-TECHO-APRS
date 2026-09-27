# anonimiza_coords.ps1 - Desplaza las coordenadas reales antes de publicar el repositorio.
#
# POR QUE EXISTE: los ficheros de ejemplo y el historico estan llenos de coordenadas
# REALES del operador (casa y paseos). Son valiosisimos como casos de prueba, pero no
# deben salir a internet.
#
# QUE HACE: suma el MISMO desplazamiento a TODAS las coordenadas y altitudes. Asi los
# datos siguen siendo coherentes entre si (la ruta conserva su forma y sus distancias)
# pero ya no apuntan a ningun sitio real.
#
# AVISO IMPORTANTE (leccion aprendida a golpes el 2026-09-14): la primera version de
# este script desplazo TAMBIEN numeros que no eran coordenadas (los valores de las
# pruebas, como el limite de latitud 85.08 del mapa) y rompio dos comprobadores. Por eso
# ahora:
#   - SOLO se tocan coordenadas dentro de un RANGO geografico plausible (40..46 de
#     latitud, -3..1 de longitud). Cualquier otro numero se deja intacto.
#   - Las ALTITUDES se tocan solo en los formatos donde de verdad van (el registro del
#     nodo, el GPX y el JSON del parser), nunca "cualquier numero seguido de m".
#   - Es a prueba de repetirse: si el fichero ya esta desplazado, se salta.
#
# REVERSIBLE: el desplazamiento queda en DESPLAZAMIENTO.txt para poder deshacerlo.
#
# USO (desde la RAIZ del repositorio que se va a publicar):
#     powershell -File <ruta>\anonimiza_coords.ps1 -Comprobar
#     powershell -File <ruta>\anonimiza_coords.ps1

param(
    [switch]$Comprobar,
    [double]$dLat = 0.03127,
    [double]$dLon = 0.00941,
    [int]$dAlt = 55
)

$ErrorActionPreference = "Stop"
$raiz = (Get-Location).Path
$utf8 = New-Object System.Text.UTF8Encoding($false)

# Solo los ficheros que git versiona: lo que no se publica, no se toca.
$versionados = @(git ls-files 2>$null)
if ($versionados.Count -eq 0) {
    Write-Host "No hay ficheros versionados: ejecuta este script desde la raiz del repositorio." -ForegroundColor Red
    exit 1
}

$latMin = 40.0; $latMax = 46.0      # la comarca del operador
$lonMin = -3.0; $lonMax = 1.0

function NuevaCoord([double]$v, [int]$decimales) {
    $nuevo = $v + $(if ($v -gt 0) { $dLat } else { $dLon })
    if ($decimales -eq 6) { return $nuevo.ToString('0.000000', [System.Globalization.CultureInfo]::InvariantCulture) }
    return $nuevo.ToString('0.00000', [System.Globalization.CultureInfo]::InvariantCulture)
}

Write-Host ""
Write-Host "=== Anonimizado de coordenadas (para publicar) ===" -ForegroundColor Cyan
Write-Host ("  desplazamiento: lat {0:+0.00000;-0.00000}  lon {1:+0.00000;-0.00000}  alt {2:+0;-#} m" -f $dLat, $dLon, $dAlt)
Write-Host ("  rango que se toca: lat {0}..{1}   lon {2}..{3}" -f $latMin, $latMax, $lonMin, $lonMax)
if ($Comprobar) { Write-Host "  MODO COMPROBAR: no se escribe nada" -ForegroundColor Yellow }
Write-Host ""

$totCoord = 0; $totAlt = 0; $tocados = 0; $saltados = 0

foreach ($rel in $versionados) {
    $ruta = Join-Path $raiz ($rel -replace '/', '\')
    if (-not (Test-Path -LiteralPath $ruta)) { continue }
    $ext = [System.IO.Path]::GetExtension($ruta).ToLower()
    if ($ext -in @('.jpg','.jpeg','.png','.gif','.uf2','.pdf','.zip','.webp','.ico','.svg')) { continue }

    try { $texto = [System.IO.File]::ReadAllText($ruta, $utf8) } catch { continue }
    $original = $texto

    # Si ya tiene coordenadas desplazadas y ninguna de las de origen, ya se hizo.
    if (([regex]::IsMatch($texto, '42\.83\d{3,4}')) -and (-not [regex]::IsMatch($texto, '42\.80\d{3,4}'))) {
        $saltados++
        continue
    }

    $nCoord = 0
    # --- Coordenadas de 6 decimales (configuracion, historico): -29.995868 / -140.023517
    $texto = [regex]::Replace($texto, '(-?\d{1,3}\.\d{6})', {
        param($m)
        $v = [double]$m.Groups[1].Value
        if (($v -ge $latMin -and $v -le $latMax) -or ($v -ge $lonMin -and $v -le $lonMax)) {
            $script:nCoord++
            return (NuevaCoord $v 6)
        }
        return $m.Groups[1].Value
    })
    # --- Coordenadas de 5 decimales (registro del nodo, GPX): -29.99587 / -140.02351
    $texto = [regex]::Replace($texto, '(-?\d{1,3}\.\d{5})(?!\d)', {
        param($m)
        $v = [double]$m.Groups[1].Value
        if (($v -ge $latMin -and $v -le $latMax) -or ($v -ge $lonMin -and $v -le $lonMax)) {
            $script:nCoord++
            return (NuevaCoord $v 5)
        }
        return $m.Groups[1].Value
    })

    $nAlt = 0
    # --- Altitudes, SOLO en los formatos reales (nunca "cualquier numero + m"):
    #     registro del nodo:  "... 15.5km/h 31deg 445m 111b R ok"
    $texto = [regex]::Replace($texto, '(deg )(-?\d{3,4})m\b', {
        param($m)
        $script:nAlt++
        return $m.Groups[1].Value + ([int]$m.Groups[2].Value + $dAlt) + 'm'
    })
    #     JSON del parser:    "alt":445
    $texto = [regex]::Replace($texto, '("alt"\s*:\s*)(-?\d{3,4})\b', {
        param($m)
        $script:nAlt++
        return $m.Groups[1].Value + ([int]$m.Groups[2].Value + $dAlt)
    })
    #     GPX:                <ele>445</ele>
    $texto = [regex]::Replace($texto, '(<ele>)(-?\d{3,4})(</ele>)', {
        param($m)
        $script:nAlt++
        return $m.Groups[1].Value + ([int]$m.Groups[2].Value + $dAlt) + $m.Groups[3].Value
    })

    if ($texto -ne $original) {
        $tocados++
        $totCoord += $nCoord; $totAlt += $nAlt
        Write-Host ("  {0,-58} {1,4} coord  {2,3} alt" -f $rel, $nCoord, $nAlt)
        if (-not $Comprobar) {
            $item = Get-Item -LiteralPath $ruta
            $eraRO = ($item.Attributes -band [System.IO.FileAttributes]::ReadOnly) -ne 0
            if ($eraRO) { $item.Attributes = $item.Attributes -bxor [System.IO.FileAttributes]::ReadOnly }
            [System.IO.File]::WriteAllText($ruta, $texto, $utf8)
            if ($eraRO) { (Get-Item -LiteralPath $ruta).Attributes = (Get-Item -LiteralPath $ruta).Attributes -bor [System.IO.FileAttributes]::ReadOnly }
        }
    }
}

Write-Host ""
Write-Host ("Resumen: {0} ficheros tocados ({1} ya estaban), {2} coordenadas y {3} altitudes." -f $tocados, $saltados, $totCoord, $totAlt) -ForegroundColor Cyan
if ($Comprobar) { Write-Host "No se ha escrito nada (modo comprobar)." -ForegroundColor Yellow }
