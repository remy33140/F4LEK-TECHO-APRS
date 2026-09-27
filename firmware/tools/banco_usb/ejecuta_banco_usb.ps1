# ejecuta_banco_usb.ps1 — COMPILA Y EJECUTA EL BANCO DEL USB DURANTE EL REPINTADO
#                          (T-Echo Project Butter II).
#
# QUE HACE: compila TRES programas con el MISMO banco (banco_usb.cpp) y los ejecuta:
#   1) banco_usb_antes.exe    -> con `lector_antes.cpp` (el feed() que estaba en git)
#   2) banco_usb_nuevo.exe    -> con `../../src/usb_lector.cpp` + `../../src/kiss.cpp`,
#                                que son el firmware DE VERDAD de hoy
#   3) banco_usb_ingenuo.exe  -> el CONTRA-EJEMPLO: el mismo codigo nuevo pero EJECUTANDO
#                                desde el gancho del driver. Tiene que salir que SI hay
#                                reentrada: si no saliera, el banco no valdria para
#                                demostrar que el bueno no la tiene.
#
# OJO: esto NO es el firmware. Son los ficheros del transporte (C++ puro) compilados en el
# ordenador con un puerto USB de mentira (FIFO de 256 B y host a 64 B/ms) y un reloj de
# mentira. Mide CUANDO se atiende un comando, CUANTOS bytes se leen durante el repintado y
# CUANTO espera el host; no dice como se siente.
#
# NECESITA un compilador de C++ del ordenador. Se busca en este orden:
#   - $env:BANCO_CXX si esta puesto
#   - zig (zig.exe) en ..\..\..\_tmp_zig\zigpkg\ziglang\  o en el PATH
#   - g++ / clang++ en el PATH
#
# SI NO HAY NINGUNO, se puede traer zig sin instalar nada en el sistema (94 MB, en una
# carpeta temporal FUERA del repositorio):
#   python -m pip install --target C:\Temp\zig ziglang
#   # y luego:  $env:BANCO_CXX="C:\Temp\zig\ziglang\zig.exe"
#
# License: GPL-3.0

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Busca-Compilador {
  if ($env:BANCO_CXX) { return @{ exe = $env:BANCO_CXX; pre = @() } }
  $zig = Get-Command zig -ErrorAction SilentlyContinue
  if (-not $zig) {
    $cand = Join-Path $PSScriptRoot "..\..\..\_tmp_zig\zigpkg\ziglang\zig.exe"
    if (Test-Path $cand) { $zig = @{ Source = $cand } }
  }
  if ($zig) { return @{ exe = $zig.Source; pre = @("c++") } }
  foreach ($c in @("g++", "clang++")) {
    $g = Get-Command $c -ErrorAction SilentlyContinue
    if ($g) { return @{ exe = $g.Source; pre = @() } }
  }
  throw "No encuentro compilador de C++ (zig, g++ o clang++). Pon BANCO_CXX con la ruta."
}

$cc = Busca-Compilador
Write-Host "Compilador: $($cc.exe) $($cc.pre -join ' ')" -ForegroundColor Cyan

# zig quiere escribir su cache en %LOCALAPPDATA%\zig. Si eso no se puede, se le dice que la
# use AQUI AL LADO. No afecta al resultado: es solo donde deja los temporales.
if (-not $env:ZIG_GLOBAL_CACHE_DIR) {
  $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot ".zig-cache\global"
}
if (-not $env:ZIG_LOCAL_CACHE_DIR) {
  $env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot ".zig-cache\local"
}
New-Item -ItemType Directory -Force -Path $env:ZIG_GLOBAL_CACHE_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $env:ZIG_LOCAL_CACHE_DIR | Out-Null

# `-w`: se callan los avisos de compilador (el zig que se usa trae su propia libc++ y suelta
# cientos de avisos de SUS cabeceras que taparian el resultado, que es lo unico que importa).
$comun = @("-std=gnu++17", "-O1", "-w", "-I.", "-I../../src")

# Compila y, si falla, ENSENA el error. OJO: el compilador escribe avisos por stderr, y con
# `$ErrorActionPreference = "Stop"` PowerShell convertiria esos avisos en un fallo falso: por
# eso aqui el stderr va a un fichero y lo que manda es el codigo de salida del compilador.
function Compila([string]$salida, [string[]]$fuentes, [string[]]$defines) {
  $log = Join-Path $PSScriptRoot "compila_$salida.log"
  $ErrorActionPreference = "Continue"
  & $cc.exe @($cc.pre) @comun @defines @fuentes "-o" $salida 2> $log
  $code = $LASTEXITCODE
  $ErrorActionPreference = "Stop"
  if ($code -ne 0) {
    Write-Host "--- el compilador ha dicho ---" -ForegroundColor Red
    Get-Content $log -ErrorAction SilentlyContinue | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
    throw "fallo al compilar $salida"
  }
  Remove-Item $log -Force -ErrorAction SilentlyContinue
}

Write-Host "`n--- compilando el CODIGO ANTERIOR (lector_antes.cpp, de git) ---" -ForegroundColor Yellow
Compila "banco_usb_antes.exe" @("banco_usb.cpp", "lector_antes.cpp", "../../src/kiss.cpp") @()

Write-Host "--- compilando el CODIGO NUEVO (src/usb_lector.cpp + src/kiss.cpp) ---" -ForegroundColor Yellow
Compila "banco_usb_nuevo.exe" @("banco_usb.cpp", "../../src/usb_lector.cpp", "../../src/kiss.cpp") @("-DBANCO_NUEVO")

Write-Host "--- compilando el CONTRA-EJEMPLO (ejecutar desde el driver) ---" -ForegroundColor Yellow
Compila "banco_usb_ingenuo.exe" @("banco_usb.cpp", "../../src/usb_lector.cpp", "../../src/kiss.cpp") @("-DBANCO_NUEVO", "-DBANCO_INGENUO")

# ---------------------------------------------------------------------------
#  EJECUTAR Y GUARDAR LA SALIDA
#  El fichero `salida_banco_usb.txt` es el REGISTRO de la prueba (es lo que se cita en
#  docs/PROYECTO_BUTTER_USB.md). Se escribe desde aqui, en UTF-8 SIN BOM, a proposito:
#  con `.\ejecuta_banco_usb.ps1 > salida.txt` lo escribiria PowerShell 5.1 en UTF-16 y
#  quedaria como fichero "binario" para git.
# ---------------------------------------------------------------------------
$registro = New-Object System.Text.StringBuilder
[void]$registro.AppendLine("BANCO DEL USB DURANTE EL REPINTADO DE LA PANTALLA (T-Echo Project Butter II)")
[void]$registro.AppendLine("Compilador: $($cc.exe) $($cc.pre -join ' ')")
[void]$registro.AppendLine("")

function CorreYGuarda([string]$exe) {
  Write-Host "`n======================================================================" -ForegroundColor Green
  [void]$registro.AppendLine("======================================================================")
  $txt = (& ".\$exe" 2>&1 | Out-String)
  Write-Host $txt
  [void]$registro.AppendLine($txt.TrimEnd())
  [void]$registro.AppendLine("")
}

CorreYGuarda "banco_usb_antes.exe"
CorreYGuarda "banco_usb_nuevo.exe"
CorreYGuarda "banco_usb_ingenuo.exe"

[void]$registro.AppendLine("======================================================================")
[void]$registro.AppendLine("RECORDATORIO: esto mide la LECTURA del puerto y CUANDO se ejecuta el comando.")
[void]$registro.AppendLine("El rato que el panel tarda en pintar es el mismo antes y despues (tinta electronica).")
$destino = Join-Path $PSScriptRoot "salida_banco_usb.txt"
[System.IO.File]::WriteAllText($destino, $registro.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "======================================================================" -ForegroundColor Green
Write-Host "Salida guardada en $destino" -ForegroundColor Cyan
Write-Host "RECORDATORIO: esto mide la LECTURA del puerto y CUANDO se ejecuta el comando."
Write-Host "El rato que el panel tarda en pintar es el mismo antes y despues (tinta electronica)."
