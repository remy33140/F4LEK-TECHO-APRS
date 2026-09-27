# sirve_web.ps1 - Sirve el configurador en local (http://localhost) para que las
# fotos del mapa se puedan cargar.
#
# POR QUE HACE FALTA: OpenStreetMap exige que las peticiones lleven cabecera
# "referer" y lo dice en su politica de uso:
#   - "From web pages, ensure a valid HTTP Referer header is sent"
#   - "Do not set a restrictive Referrer-Policy that prevents the Referer header"
#   - "Offline use is not permitted on tile.openstreetmap.org"
# Al abrir web\index.html con doble clic la direccion es file:///... y el
# navegador NO manda referer: OSM bloquea las teselas y el mapa queda en blanco.
# Sirviendolo por http://localhost SI se manda referer, y ademas el navegador
# guarda las teselas en su cache (otro requisito de su politica).
#
# Uso:  powershell -File tools\sirve_web.ps1            (dice la direccion a abrir)
#       powershell -File tools\sirve_web.ps1 -Puerto 8080
# Para pararlo: Ctrl+C.
#
# NOTA TECNICA: se usa un servidor de sockets propio y NO System.Net.HttpListener,
# porque en este PC HttpListener no arranca ("Operacion no permitida en esta
# plataforma": falta el servicio HTTP.SYS o esta capado). Con TcpListener va bien.

param(
  [int]$Puerto = 8000
)

$ErrorActionPreference = "Stop"
$raiz = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $raiz "web\index.html"))) {
  Write-Error "No encuentro web\index.html en $raiz"
}

$tipos = @{
  ".html" = "text/html; charset=utf-8"
  ".js"   = "application/javascript; charset=utf-8"
  ".css"  = "text/css; charset=utf-8"
  ".json" = "application/json; charset=utf-8"
  ".svg"  = "image/svg+xml"
  ".png"  = "image/png"
  ".ico"  = "image/x-icon"
  ".txt"  = "text/plain; charset=utf-8"
  ".md"   = "text/plain; charset=utf-8"
}

try {
  $escucha = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Puerto)
  $escucha.Start()
} catch {
  Write-Host "No he podido abrir el puerto $Puerto : $($_.Exception.Message)" -ForegroundColor Red
  Write-Host "Prueba con otro:  powershell -File tools\sirve_web.ps1 -Puerto 8080"
  exit 1
}

$url = "http://localhost:$Puerto/web/"
Write-Host ""
Write-Host "  Configurador servido en local" -ForegroundColor Green
Write-Host "  ------------------------------------------------"
Write-Host "  Abre esta direccion en Chrome o Edge:" -ForegroundColor White
Write-Host "     $url" -ForegroundColor Yellow
Write-Host ""
Write-Host "  Para pararlo: Ctrl+C en esta ventana."
Write-Host ""

function Enviar-Respuesta {
  param($flujo, [int]$codigo, [string]$tipo, [byte[]]$cuerpo)
  $cabecera = "HTTP/1.1 $codigo " + $(if ($codigo -eq 200) { "OK" } elseif ($codigo -eq 404) { "Not Found" } else { "Error" }) + "`r`n" +
              "Content-Type: $tipo`r`n" +
              "Content-Length: $($cuerpo.Length)`r`n" +
              "Cache-Control: no-store`r`n" +
              "Connection: close`r`n`r`n"
  $hb = [System.Text.Encoding]::ASCII.GetBytes($cabecera)
  $flujo.Write($hb, 0, $hb.Length)
  if ($cuerpo.Length -gt 0) { $flujo.Write($cuerpo, 0, $cuerpo.Length) }
  $flujo.Flush()
}

try {
  while ($true) {
    $cliente = $escucha.AcceptTcpClient()
    try {
      $flujo = $cliente.GetStream()
      $flujo.ReadTimeout = 5000

      # --- lee la peticion (solo interesa la primera linea) ---
      $buf = New-Object byte[] 4096
      $leidos = 0
      try { $leidos = $flujo.Read($buf, 0, $buf.Length) } catch { $leidos = 0 }
      if ($leidos -le 0) { $cliente.Close(); continue }
      $texto = [System.Text.Encoding]::ASCII.GetString($buf, 0, $leidos)
      $primera = ($texto -split "`r`n")[0]
      $partes = $primera -split " "
      $metodo = $partes[0]
      $ruta = if ($partes.Count -gt 1) { $partes[1] } else { "/" }

      # --- resuelve el fichero ---
      if ($ruta -eq "/" -or $ruta -eq "/web" -or $ruta -eq "/web/") { $ruta = "/web/index.html" }
      $rel = [System.Uri]::UnescapeDataString($ruta).TrimStart("/")
      if ($rel -match "\.\.") {
        Enviar-Respuesta $flujo 400 "text/plain" ([System.Text.Encoding]::UTF8.GetBytes("Ruta no valida"))
        $cliente.Close(); continue
      }
      $fichero = Join-Path $raiz ($rel -replace "/", "\")

      if ($metodo -ne "GET" -and $metodo -ne "HEAD") {
        Enviar-Respuesta $flujo 405 "text/plain" ([System.Text.Encoding]::UTF8.GetBytes("Solo GET"))
      } elseif (-not (Test-Path $fichero -PathType Leaf)) {
        Enviar-Respuesta $flujo 404 "text/plain; charset=utf-8" ([System.Text.Encoding]::UTF8.GetBytes("No encontrado: $rel"))
      } else {
        $ext = [System.IO.Path]::GetExtension($fichero).ToLower()
        $tipo = if ($tipos.ContainsKey($ext)) { $tipos[$ext] } else { "application/octet-stream" }
        $datos = [System.IO.File]::ReadAllBytes($fichero)
        if ($metodo -eq "HEAD") { $datos = New-Object byte[] 0 }
        Enviar-Respuesta $flujo 200 $tipo $datos
      }
    } catch {
      # Un cliente que se va a mitad no debe tumbar el servidor.
    } finally {
      try { $cliente.Close() } catch { }
    }
  }
} finally {
  $escucha.Stop()
  Write-Host ""
  Write-Host "  Servidor parado." -ForegroundColor Yellow
}
