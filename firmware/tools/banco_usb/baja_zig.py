"""baja_zig.py — baja el compilador zig (wheel de PyPI) SIN pip.

POR QUE EXISTE: en esta maquina `pip install` falla al escribir su fichero temporal
(`[Errno 13] Permission denied: ...\\pip-unpack-...\\*.whl.metadata`), pero Python si
puede escribir ficheros normales. Asi que aqui se baja el wheel a mano y se descomprime
(el .whl es un ZIP). NO forma parte del firmware: es una herramienta del banco de pruebas.

USO:  python baja_zig.py <carpeta_destino>
"""
import json
import os
import sys
import urllib.request
import zipfile

dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "pkg")
os.makedirs(dest, exist_ok=True)

print("consultando PyPI...")
with urllib.request.urlopen("https://pypi.org/pypi/ziglang/json", timeout=60) as r:
    meta = json.load(r)
cand = [u for u in meta["urls"] if u["filename"].endswith("win_amd64.whl")]
if not cand:
    print("FALLO: no hay wheel para win_amd64")
    sys.exit(1)
url = cand[0]["url"]
print("bajando", cand[0]["filename"], round(cand[0]["size"] / 1048576, 1), "MB")

whl = os.path.join(dest, cand[0]["filename"])
with urllib.request.urlopen(url, timeout=600) as r, open(whl, "wb") as f:
    total = 0
    while True:
        trozo = r.read(1 << 20)
        if not trozo:
            break
        f.write(trozo)
        total += len(trozo)
        print("\r  %d MB" % (total // 1048576), end="", flush=True)
print("\ndescomprimiendo...")
with zipfile.ZipFile(whl) as z:
    z.extractall(dest)

zig = os.path.join(dest, "ziglang", "zig.exe")
print("zig.exe:", zig, "existe" if os.path.exists(zig) else "NO EXISTE")
