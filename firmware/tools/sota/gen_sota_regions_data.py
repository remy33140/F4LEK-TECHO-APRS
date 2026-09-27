#!/usr/bin/env python3
"""
Genere firmware/src/sota_regions_data.h a partir de tools/sota/sota_regions.csv
(association,region,region_name, produit par fetch_sota_regions.py). Ne garde QUE les codes -- les noms ne
servent a rien pour construire le message APRS2SOTA, ca alourdirait la
table sans utilite reelle (~8 Ko avec juste les codes, sur 226
associations x 1516 regions).

Usage (depuis n'importe ou) :
    python3 tools/sota/gen_sota_regions_data.py [sota_regions.csv] [sortie.h]
Par defaut : le CSV a cote du script -> src/sota_regions_data.h du firmware.
Apres regeneration, recompiler le firmware.
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.join(HERE, "sota_regions.csv")
DEFAULT_DST = os.path.join(HERE, "..", "..", "src", "sota_regions_data.h")
from collections import OrderedDict

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(DEFAULT_DST)

    grouped = OrderedDict()
    with open(src, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            grouped.setdefault(row["association"], []).append(row["region"])

    assocs = sorted(grouped.keys())
    for a in assocs:
        grouped[a].sort()

    max_assoc_len = max(len(a) for a in assocs)
    max_region_len = max(len(r) for regions in grouped.values() for r in regions)
    total_regions = sum(len(v) for v in grouped.values())

    lines = []
    lines.append("// Genere automatiquement par tools/sota/gen_sota_regions_data.py --")
    lines.append("// NE PAS EDITER A LA MAIN (regenerer a partir du CSV source).")
    lines.append(f"// {len(assocs)} associations, {total_regions} regions SOTA (donnee du "
                  f"{__import__('datetime').date.today().isoformat()}, sotadata.org.uk).")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"constexpr uint8_t kSotaAssocCodeLen = {max_assoc_len + 1};  // +1 para el '\\0'")
    lines.append(f"constexpr uint8_t kSotaRegionCodeLen = {max_region_len + 1};")
    lines.append("")
    lines.append("struct SotaAssocEntry {")
    lines.append(f"  char code[{max_assoc_len + 1}];")
    lines.append("  uint16_t regionStart;  // indice del primer region en kSotaRegionCodes")
    lines.append("  uint16_t regionCount;")
    lines.append("};")
    lines.append("")

    # flat region-code table
    region_lines = []
    starts = {}
    idx = 0
    for a in assocs:
        starts[a] = idx
        for r in grouped[a]:
            region_lines.append(f'  "{r}",')
            idx += 1

    lines.append(f"// {total_regions} codigos de region, agrupados por asociacion (ver")
    lines.append("// kSotaAssocs[].regionStart/regionCount para el rango de cada una).")
    lines.append(f"static const char kSotaRegionCodes[{total_regions}][{max_region_len + 1}] = {{")
    lines.extend(region_lines)
    lines.append("};")
    lines.append("")

    lines.append(f"// {len(assocs)} asociaciones, orden alfabetico (busqueda binaria posible")
    lines.append("// si hiciera falta -- de momento se recorre entera, son solo 226).")
    lines.append(f"static const SotaAssocEntry kSotaAssocs[{len(assocs)}] = {{")
    for a in assocs:
        lines.append(f'  {{"{a}", {starts[a]}, {len(grouped[a])}}},')
    lines.append("};")
    lines.append("")
    lines.append(f"constexpr int kSotaAssocCount = {len(assocs)};")
    lines.append(f"constexpr int kSotaRegionTotalCount = {total_regions};")
    lines.append("")

    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"{len(assocs)} associations, {total_regions} regions -> {dst}")
    print("taille du fichier genere:", os.path.getsize(dst), "octets")

if __name__ == "__main__":
    main()
