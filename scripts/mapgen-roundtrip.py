"""Round-trip every zone file through the mapgen codecs (phase 0 check).

For every .ZON, .IFO, .HIM, .TIL, .MOV and .LIT under the map root, this
parses the file into a model, serialises the model back and compares the
bytes. The writer rebuilds each file from the parsed fields; container
offsets are recomputed, not copied. A file passes only if every byte matches.

It also accounts for every byte the codecs carry WITHOUT interpreting:

* container gaps;
* bytes after a lump's parsed content ("tails");
* lumps no parser accepted ("raw");
* trailing bytes after a grid file.

Those are listed per category. They round-trip, but the generator cannot
produce them from fields.

Read-only: it never writes under the map root.

    python scripts/mapgen-roundtrip.py                    # data/3DDATA/MAPS
    python scripts/mapgen-roundtrip.py --root <dump>\\3DDATA\\MAPS
    python scripts/mapgen-roundtrip.py --json build/mapgen/roundtrip.json
    python scripts/mapgen-roundtrip.py --selftest         # edit-a-field checks

Exit status: 0 when every file is byte-identical (and the selftest passes),
1 otherwise. See docs/mapgen/PLAN.md, phase 0.
"""

import argparse
import json
import os
import sys
from collections import Counter, defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mapgen  # noqa: E402
from mapgen import chunk, ifo, lit, zon  # noqa: E402
from mapgen.binio import FormatError  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ROOT = os.path.join(REPO, "data", "3DDATA", "MAPS")

ZON_NAMES = {0: "INFO", 1: "EVENTS", 2: "TEXTURES", 3: "TILES", 4: "ECONOMY"}


def lump_name(ext, t):
    names = ZON_NAMES if ext == ".zon" else ifo.LUMP_NAMES
    return names.get(t, "type%d" % t)


def first_diff(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return n if len(a) != len(b) else -1


def where_in_container(c, data, offset):
    """Name the lump whose region holds `offset` in the ORIGINAL file."""
    import struct
    count = struct.unpack_from("<i", data, 0)[0]
    if offset < 4 + 8 * count:
        return "lump table"
    table = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(count)]
    best = None
    for t, off in table:
        if off <= offset and (best is None or off > best[1]):
            best = (t, off)
    return "lump %d at %d" % best if best else "pre-gap"


def account(ext, model, opaque, variants):
    """Record opaque bytes (category -> [files, bytes]) and format variants."""
    def add(cat, n):
        if n:
            opaque[cat][0] += 1
            opaque[cat][1] += n

    if ext in (".zon", ".ifo"):
        add("%s pre-gap after lump table" % ext, len(model.pre_gap))
        if model.odd_entries:
            add("%s lump-table entry pointing outside the file" % ext, 8 * len(model.odd_entries))
        for reg in model.regions:
            name = lump_name(ext, reg.lump_type)
            if reg.is_raw:
                add("%s raw lump %s (%s)" % (ext, name, reg.raw_reason), len(reg.body))
            else:
                add("%s tail after lump %s" % (ext, name), len(reg.tail))
        types = [lump_name(ext, r.lump_type) for r in model.regions]
        variants["%s lump set" % ext][",".join(types)] += 1
        if len(model.entries) != len(model.regions):
            variants["%s entries sharing an offset" % ext]["yes"] += 1
    if ext == ".zon":
        info = model.lump(zon.INFO)
        if info is not None:
            variants["zon info cell table"]["present" if info.cells is not None else "absent"] += 1
            variants["zon grid"]["%d x %g" % (info.grid_per_patch, info.grid_size)] += 1
        variants["zon economy lump"]["present" if model.lump(zon.ECONOMY) else "absent"] += 1
    elif ext == ".ifo":
        w = model.lump(ifo.WATER)
        if w is not None:
            used = int(np.count_nonzero(w.cells["use"]))
            variants["ifo lump7 WATER grid"]["%dx%d, %s" % (w.x, w.y, "some use!=0" if used else "all use=0")] += 1
    elif ext == ".him":
        add(".him trailing bytes", len(model.tail))
        if model.bounds is None:
            variants["him bounds"]["absent"] += 1
        else:
            b = model.bounds
            placeholder = bool(np.any(np.abs(b.patches) > 1e30))
            variants["him bounds"]["placeholder" if placeholder else "real"] += 1
            variants["him bounds counts"]["%d patches / %d quads / name %r" % (
                len(b.patches), len(b.quads), b.name)] += 1
        variants["him header"]["%dx%d grid %d patch_size %g" % (
            model.width, model.height, model.grid_per_patch, model.patch_size)] += 1
    elif ext == ".til":
        add(".til trailing bytes", len(model.tail))
        variants["til size"]["%dx%d" % (model.width, model.height)] += 1
    elif ext == ".mov":
        add(".mov trailing bytes", len(model.tail))
        variants["mov size"]["%dx%d" % (model.width, model.height)] += 1
    elif ext == ".lit":
        add(".lit trailing bytes", len(model.tail))
        variants["lit dds catalogue"]["present" if model.dds_list is not None else "absent"] += 1


def run(root, only_ext, verbose):
    results = []
    opaque = defaultdict(lambda: [0, 0])
    variants = defaultdict(Counter)
    for dirpath, _, files in os.walk(root):
        for fn in sorted(files):
            ext = os.path.splitext(fn)[1].lower()
            if ext not in mapgen.CODECS or (only_ext and ext not in only_ext):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root)
            parse, build = mapgen.CODECS[ext]
            with open(path, "rb") as f:
                data = f.read()
            res = {"file": rel, "ext": ext, "size": len(data)}
            try:
                model = parse(data)
            except (FormatError, ValueError) as e:
                res.update(status="PARSE-ERROR", reason=str(e))
                results.append(res)
                continue
            out = build(model)
            if out == data:
                res["status"] = "OK"
            else:
                off = first_diff(data, out)
                res.update(status="DIFF", offset=off, out_size=len(out))
                if ext in (".zon", ".ifo"):
                    res["where"] = where_in_container(model, data, off)
            account(ext, model, opaque, variants)
            results.append(res)
            if verbose and res["status"] != "OK":
                print(res)
    return results, opaque, variants


def selftest(root):
    """Edit one field per format, rebuild, re-parse, and check the edit landed.

    Proves the writers serialise from the model rather than replaying input
    bytes.
    """
    problems = []
    found = {}
    for dirpath, _, files in os.walk(root):
        for fn in files:
            ext = os.path.splitext(fn)[1].lower()
            if ext in mapgen.CODECS and ext not in found:
                found[ext] = os.path.join(dirpath, fn)
        if len(found) == len(mapgen.CODECS):
            break

    def check(ext, mutate, probe, expect):
        if ext not in found:
            problems.append("%s: no sample file found" % ext)
            return
        parse, build = mapgen.CODECS[ext]
        model = parse(open(found[ext], "rb").read())
        if not mutate(model):
            print("  %-5s skipped (sample has nothing to edit): %s" % (ext, found[ext]))
            return
        again = parse(build(model))
        got = probe(again)
        ok = got == expect
        print("  %-5s %s  %s" % (ext, "ok  " if ok else "FAIL", found[ext]))
        if not ok:
            problems.append("%s: expected %r, got %r" % (ext, expect, got))

    def m_him(h):
        h.heights[0, 0] = 1234.5
        return True

    def m_til(t):
        t.tiles["tile_id"][3, 4] = 77
        return True

    def m_mov(m):
        m.cells[5, 6] = 1
        return True

    def m_zon(z):
        evs = z.lump(zon.EVENTS)
        if not evs:
            return False
        evs.append(zon.EventPos(1.0, 2.0, 3.0, b"mapgen-selftest"))
        return True

    def m_ifo(f):
        objs = f.lump(ifo.OBJECT)
        if objs is None:
            return False
        objs.append(ifo.Record(obj_id=42, pos=(100.0, 200.0, 300.0)))
        return True

    def m_lit(l):
        l.objects.append(lit.LitObject(9, [lit.LitPart(b"a.tga", 0, b"b.dds", 0, 64, 4, 3)]))
        return True

    check(".him", m_him, lambda h: float(h.heights[0, 0]), 1234.5)
    check(".til", m_til, lambda t: int(t.tiles["tile_id"][3, 4]), 77)
    check(".mov", m_mov, lambda m: int(m.cells[5, 6]), 1)
    check(".zon", m_zon, lambda z: z.lump(zon.EVENTS)[-1].name, b"mapgen-selftest")
    check(".ifo", m_ifo, lambda f: (f.lump(ifo.OBJECT)[-1].obj_id, f.lump(ifo.OBJECT)[-1].pos),
          (42, (100.0, 200.0, 300.0)))
    check(".lit", m_lit, lambda l: (l.objects[-1].obj_index, l.objects[-1].parts[0].position_in_map),
          (9, 3))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=DEFAULT_ROOT, help="map root to scan (default: data/3DDATA/MAPS)")
    ap.add_argument("--ext", action="append", help="only this extension, e.g. --ext .him (repeatable)")
    ap.add_argument("--json", help="also write the full per-file results here")
    ap.add_argument("--selftest", action="store_true", help="also run the edit-a-field checks")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    only = {e.lower() if e.startswith(".") else "." + e.lower() for e in (args.ext or [])}
    results, opaque, variants = run(args.root, only, args.verbose)

    by_ext = defaultdict(Counter)
    for r in results:
        by_ext[r["ext"]][r["status"]] += 1
    print("Round-trip of %s" % args.root)
    print("%-6s %8s %8s %8s" % ("ext", "files", "OK", "not OK"))
    total_bad = 0
    for ext in sorted(by_ext):
        c = by_ext[ext]
        bad = sum(v for k, v in c.items() if k != "OK")
        total_bad += bad
        print("%-6s %8d %8d %8d" % (ext, sum(c.values()), c["OK"], bad))
    print("%-6s %8d %8d %8d" % ("all", len(results), len(results) - total_bad, total_bad))

    bad = [r for r in results if r["status"] != "OK"]
    if bad:
        print("\nFiles that did not round-trip:")
        for r in bad:
            print("  %s  %s  %s" % (r["status"], r["file"],
                                    r.get("reason") or "first diff at %s (%s), size %d -> %d" % (
                                        r.get("offset"), r.get("where", "-"), r["size"], r.get("out_size", 0))))

    print("\nBytes carried without interpretation (round-trip, but not generated from fields):")
    if not opaque:
        print("  none")
    for cat in sorted(opaque):
        files, nbytes = opaque[cat]
        print("  %-70s %6d files %10d bytes" % (cat, files, nbytes))

    print("\nFormat variants seen:")
    for key in sorted(variants):
        for val, n in variants[key].most_common():
            print("  %-34s %6d  %s" % (key, n, val))

    problems = []
    if args.selftest:
        print("\nSelftest (edit a field, rebuild, re-parse):")
        problems = selftest(args.root)
        for p in problems:
            print("  " + p)

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"root": args.root, "results": results,
                       "opaque": {k: v for k, v in opaque.items()},
                       "variants": {k: dict(v) for k, v in variants.items()}}, f, indent=1)
        print("\nwrote %s" % args.json)

    return 1 if (total_bad or problems) else 0


if __name__ == "__main__":
    sys.exit(main())
