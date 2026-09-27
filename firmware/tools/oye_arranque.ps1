# oye_arranque.ps1 - Escucha el puerto serie del nodo DURANTE su arranque.
#
# PARA QUE: los mensajes de diagnostico salen al arrancar, y si abres el puerto despues
# ya han pasado. Este script abre el puerto, MANDA UN REINICIO y se queda escuchando todo
# lo que el nodo diga desde el primer instante.
#
# USO:
#     powershell -File tools\oye_arranque.ps1 -Port COM40
#     powershell -File tools\oye_arranque.ps1 -Port COM40 -Segundos 20

param(
    [string]$Port = "COM40",
    [int]$Segundos = 15
)

$ErrorActionPreference = "Continue"

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, "None", 8, "One")
$sp.ReadTimeout = 300
$sp.WriteTimeout = 2000

try {
    $sp.Open()
    Write-Host "Puerto $Port abierto. Mandando reinicio y escuchando $Segundos s..." -ForegroundColor Cyan
    Write-Host ""
    # Nuestro firmware reinicia con esta orden (alias de reboot).
    $sp.Write("reboot confirm`n")
    Start-Sleep -Milliseconds 300

    $fin = (Get-Date).AddSeconds($Segundos)
    $linea = ""
    while ((Get-Date) -lt $fin) {
        try {
            if ($sp.BytesToRead -gt 0) {
                $c = $sp.ReadExisting()
                $linea += $c
                while ($linea.Contains("`n")) {
                    $i = $linea.IndexOf("`n")
                    $l = $linea.Substring(0, $i).TrimEnd("`r")
                    $linea = $linea.Substring($i + 1)
                    if ($l.Trim() -ne "") { Write-Host ("  " + $l) }
                }
            }
        } catch { }
        Start-Sleep -Milliseconds 40
    }
    if ($linea.Trim() -ne "") { Write-Host ("  " + $linea.Trim()) }
} catch {
    Write-Host ("No he podido abrir el puerto: " + $_.Exception.Message) -ForegroundColor Red
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}
Write-Host ""
Write-Host "Fin de la escucha." -ForegroundColor Cyan
