#!/usr/bin/env python3
"""
Recupere la liste des regions de CHAQUE association SOTA (227 appels, un par
association -- l'endpoint /api/associations tout court ne donne que des
compteurs, pas les codes de region) et assemble un CSV plat :

    association,region

A lancer depuis une machine normale (pas depuis le bac a sable de Claude) :
api2.sota.org.uk bloque les robots mais pas curl/urllib -- ce script fait de
simples requetes HTTP, une pause de 0.3s entre chaque pour rester poli avec
le serveur.

Usage (depuis n'importe ou, les fichiers sont lus/ecrits a cote du script) :
    python3 tools/sota/fetch_sota_regions.py
    -> ecrit tools/sota/sota_regions.csv (~227 associations, quelques milliers de lignes)
    Si tools/sota/sota_associations.json n'existe pas, il est telecharge d'abord
    (https://api2.sota.org.uk/api/associations). --refresh-associations le re-telecharge.

Options utiles :
    python3 fetch_sota_regions.py --only F,EA1,EA2,EA3,EA4,EA5,EA6,EA7,EA8
    -> ne recupere que les associations listees (rapide, pour tester)
"""
import argparse
import csv
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ASSOC_LIST_FILE = os.path.join(HERE, "sota_associations.json")
OUT_FILE = os.path.join(HERE, "sota_regions.csv")
BASE_URL = "https://api2.sota.org.uk/api/associations/"
PAUSE_SECONDS = 0.3
USER_AGENT = "Mozilla/5.0 (compatible; sota-region-fetch/1.0; personal use)"


def download_association_list(path):
    req = urllib.request.Request(BASE_URL.rstrip("/"), headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read()
    json.loads(data.decode("utf-8"))  # verifie que c'est bien du JSON avant d'ecrire
    with open(path, "wb") as f:
        f.write(data)
    print(f"Liste des associations telechargee -> {path}")


def load_association_codes(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return [a["associationCode"] for a in data]


def fetch_regions(code):
    url = BASE_URL + code
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        payload = json.loads(resp.read().decode("utf-8"))
    # La forme exacte varie selon les associations ; on prend ce qui existe.
    regions = payload.get("regions", [])
    out = []
    for r in regions:
        region_code = r.get("regionCode") or r.get("code") or ""
        region_name = r.get("regionName") or r.get("name") or ""
        if region_code:
            out.append((code, region_code, region_name))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--only",
        help="Liste d'associations a recuperer, separees par des virgules "
        "(ex: F,EA1,EA2). Par defaut : les 227 de sota_associations.json.",
    )
    ap.add_argument("--assoc-file", default=ASSOC_LIST_FILE)
    ap.add_argument("--out", default=OUT_FILE)
    ap.add_argument("--refresh-associations", action="store_true",
                    help="Re-telecharge sota_associations.json avant de commencer.")
    args = ap.parse_args()

    if not args.only and (args.refresh_associations or not os.path.exists(args.assoc_file)):
        download_association_list(args.assoc_file)

    if args.only:
        codes = [c.strip() for c in args.only.split(",") if c.strip()]
    else:
        codes = load_association_codes(args.assoc_file)

    print(f"{len(codes)} association(s) a recuperer...")
    rows = []
    failed = []
    for i, code in enumerate(codes, 1):
        try:
            regions = fetch_regions(code)
            rows.extend(regions)
            print(f"[{i}/{len(codes)}] {code}: {len(regions)} region(s)")
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
            print(f"[{i}/{len(codes)}] {code}: ECHEC ({e})", file=sys.stderr)
            failed.append(code)
        time.sleep(PAUSE_SECONDS)

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["association", "region", "region_name"])
        w.writerows(rows)

    print(f"\n{len(rows)} ligne(s) ecrites dans {args.out}")
    if failed:
        print(f"{len(failed)} association(s) en echec (a reessayer) : {failed}")


if __name__ == "__main__":
    main()
