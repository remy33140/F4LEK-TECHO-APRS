#!/usr/bin/env python3
"""Build app/src/main/assets/summits.sota from the official SOTA summit list.

Usage: python3 tools/make_summits_asset.py summitslist.csv
(download it first from https://storage.sota.org.uk/summitslist.csv)

Keeps the official CSV layout (title line + header + rows) so the app parses the
bundled copy and a fresh download with the same code; only summits still valid
today are kept, which makes the asset smaller.
"""
import csv
import datetime
import gzip
import io
import sys


def main(src, dst="app/src/main/assets/summits.sota"):
    today = datetime.date.today()
    with open(src, encoding="utf-8", newline="") as f:
        title = f.readline()
        rows = list(csv.reader(f))
    header, body = rows[0], rows[1:]
    out = io.StringIO()
    out.write(title)
    w = csv.writer(out, lineterminator="\n")
    w.writerow(header)
    kept = 0
    for r in body:
        try:
            d, m, y = map(int, r[13].split("/"))
            if datetime.date(y, m, d) < today:
                continue
        except (ValueError, IndexError):
            continue
        w.writerow(r)
        kept += 1
    with gzip.open(dst, "wt", encoding="utf-8", compresslevel=9) as g:
        g.write(out.getvalue())
    print(f"{kept} summits -> {dst}")


if __name__ == "__main__":
    main(*sys.argv[1:])
