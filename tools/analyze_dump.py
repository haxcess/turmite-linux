#!/usr/bin/env python3
"""Headless inspection of a Turmite Universe debug dump."""
from __future__ import annotations

import argparse
import re
from pathlib import Path
from collections import defaultdict


def read_manifest(path: Path):
    data = {}
    page_rows = []
    in_pages = False
    with path.open() as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line == "PAGE TABLE":
                in_pages = True
                next(f, None)  # column headings
                for row in f:
                    row = row.rstrip("\n")
                    if not row:
                        break
                    fields = row.split("\t")
                    if len(fields) >= 7:
                        page_rows.append({
                            "page": int(fields[0]),
                            "age": float(fields[1]),
                            "hash": fields[2],
                            "changed": int(fields[3]),
                            "collisions": int(fields[4]),
                            "dispatches": int(fields[5]),
                            "instructions": int(fields[6]),
                        })
                in_pages = False
                # Continue parsing the rest of the manifest is not necessary for scalar data.
                continue
            if "=" in line and not in_pages:
                k, v = line.split("=", 1)
                data[k] = v
    return data, page_rows


def longest_equal_run(rows):
    best = cur = 0
    for i, row in enumerate(rows):
        if i and row["hash"] == rows[i - 1]["hash"] and row["changed"] == 0:
            cur += 1
        else:
            cur = 1
        best = max(best, cur)
    return best


def cycles(rows):
    out = []
    hashes = [r["hash"] for r in rows]
    for period in range(1, min(64, len(hashes) // 2) + 1):
        matches = 0
        for i in range(period, len(hashes)):
            if hashes[i] == hashes[i - period]:
                matches += 1
        if matches >= max(4, len(hashes) // 3):
            out.append((period, matches))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", type=Path)
    ap.add_argument("--page", type=int, help="show basic stats for one raw page")
    args = ap.parse_args()

    manifest = args.dump / "manifest.txt"
    if not manifest.exists():
        raise SystemExit(f"not a Turmite dump directory: {args.dump}")

    data, rows = read_manifest(manifest)
    print("TURMITE DEBUG DUMP")
    print(f"seed:       {data.get('seed', '?')}")
    print(f"world:      {data.get('world_width', '?')} x {data.get('world_height', '?')}")
    print(f"workers:    {data.get('workers', '?')}")
    print(f"scheduler:  {data.get('scheduler', '?')}")
    print(f"quantum:    {data.get('quantum', '?')}")
    print(f"interval:   {data.get('capture_interval_seconds', '?')} s")
    print(f"pages:      {len(rows)}")
    if not rows:
        return

    unique = len({r['hash'] for r in rows})
    print(f"unique page hashes: {unique}/{len(rows)}")
    print(f"longest unchanged run: {longest_equal_run(rows)} pages")
    print(f"first age / last age: {rows[0]['age']:.3f} / {rows[-1]['age']:.3f} s")
    print(f"collisions: {rows[-1]['collisions']}")
    print(f"instructions: {rows[-1]['instructions']}")

    found = cycles(rows)
    if found:
        print("possible repeating periods:")
        for period, matches in found[:10]:
            print(f"  period {period}: {matches} matching page pairs")
    else:
        print("possible repeating periods: none detected (<=64 pages)")

    print("recent pages:")
    for r in rows[-min(12, len(rows)):]:
        print(f"  {r['page']:04d} age={r['age']:8.3f} changed={r['changed']:7d} hash={r['hash']} collisions={r['collisions']:8d} instructions={r['instructions']:12d}")

    if args.page is not None:
        if args.page < 0 or args.page >= len(rows):
            raise SystemExit("page out of range")
        width = int(data["world_width"])
        height = int(data["world_height"])
        page_path = args.dump / "pages" / f"page-{args.page:04d}.bin"
        raw = page_path.read_bytes()
        if len(raw) != width * height:
            raise SystemExit(f"page size mismatch: got {len(raw)}, expected {width*height}")
        counts = [raw.count(i) for i in range(6)]
        print(f"page {args.page:04d} color counts: {counts}")


if __name__ == "__main__":
    main()
