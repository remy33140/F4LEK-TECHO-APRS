# ejecuta_banco.ps1 — COMPILA Y EJECUTA EL BANCO DE PRUEBAS DEL BOTON (T-Echo Project Butter).
#
# QUE HACE: compila DOS programas con el MISMO banco (banco.cpp) y los ejecuta:
#   1) banco_antes.exe  -> con `button_antes.cpp`, que es el button.cpp que estaba en git
#   2) banco_nuevo.exe  -> con `../../src/button.cpp`, que es el firmware de verdad de hoy
# y saca por pantalla, para ocho situaciones, QUE GESTO SALE Y EN QUE MILISEGUNDO.
#
# OJO: esto NO es el firmware. Es el modulo del boton compilado en el ordenador con un
# Arduino.h de mentira (reloj y pines simulados). Sirve para MEDIR la logica del boton
# (cuando se decide cada gesto y si un toque se pierde), no para saber como se siente.
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
# (Asi se hizo la primera vez. El compilador NO se deja dentro del proyecto a proposito:
#  son 94 MB de binarios que no pintan nada en el arbol del firmware.)
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

# zig quiere escribir su cache en %LOCALAPPDATA%\zig. Si eso no se puede (maquinas con
# la carpeta de usuario restringida), se le dice que la use AQUI AL LADO, que siempre se
# puede. No afecta al resultado: es solo donde deja los ficheros temporales.
if (-not $env:ZIG_GLOBAL_CACHE_DIR) {
  $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot ".zig-cache\global"
}
if (-not $env:ZIG_LOCAL_CACHE_DIR) {
  $env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot ".zig-cache\local"
}
New-Item -ItemType Directory -Force -Path $env:ZIG_GLOBAL_CACHE_DIR | Out-Null
New-Item -ItemType Directory -Force -Path $env:ZIG_LOCAL_CACHE_DIR | Out-Null

# `-w`: se callan los avisos del compilador. No es pereza: el zig que se usa aqui trae
# su propia libc++ y suelta cientos de avisos de SUS cabeceras que no son de este codigo
# y que taparian el resultado de la prueba, que es lo unico que importa.
$comun = @("-std=gnu++17", "-O1", "-w", "-I.", "-I../../src", "-DFAKETEC_BOARD_TECHO")

# Compila y, si falla, ENSENA el error. OJO: el compilador escribe avisos por stderr (la
# primera vez compila SU libc++ y suelta cientos), y con `$ErrorActionPreference = "Stop"`
# PowerShell convertiria esos avisos en un fallo falso. Por eso el stderr va a un fichero y
# lo que manda es el codigo de salida del compilador. (Mismo arreglo que en banco_usb.)
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

Write-Host "`n--- compilando el CODIGO ANTERIOR (button_antes.cpp, de git) ---" -ForegroundColor Yellow
Compila "banco_antes.exe" @("banco.cpp", "button_antes.cpp") @()

Write-Host "--- compilando el CODIGO NUEVO (src/button.cpp) ---" -ForegroundColor Yellow
Compila "banco_nuevo.exe" @("banco.cpp", "../../src/button.cpp") @("-DBANCO_NUEVO")

Write-Host "`n======================================================================" -ForegroundColor Green
& ".\banco_antes.exe"
Write-Host "======================================================================" -ForegroundColor Green
& ".\banco_nuevo.exe"
Write-Host "======================================================================" -ForegroundColor Green
Write-Host "RECORDATORIO: esto mide la LOGICA del boton. Los 0,35-2 s del panel de tinta"
Write-Host "se suman aparte (ver docs/PROYECTO_BUTTER_TECHO.md)."
