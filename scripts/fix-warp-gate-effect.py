"""Make Eucar's teleporter portal effect always on (it was night-only).

The teleporter platform in the Magic City of Eucar (LMT01, 33_33.IFO) is
event object 11 of 3DDATA/SPECIAL/EVENT_OBJECT.ZSC (warp_gate02.zms). Its
one dummy point carries the blue portal, _warp_gate03.eft ->
PARTICLES/_warp_gate_03.ptl, all present in data/. But the point's effect
type was 1, POINT_EFFECT_DAYNNIGHT, so CObjFIXED::Create registered it with
the day/night manager as a night effect and it was hidden all day.

Every reference dump we own (139, 667, Evo, Jrose, QQ-iROSE, Rose Brazil,
RoseZA, ruff, titanRose, tsuki) has type 0 (POINT_EFFECT_NORMAL) on that
point; ours alone had 1, and the file is untouched since the April data drop,
so it came that way with our iROSE base. Object 11 is placed only in LMT01.
Object 1 (_warp_gate01, the Junon gates) was already type 0.

    python scripts/fix-warp-gate-effect.py --dry-run
    python scripts/fix-warp-gate-effect.py
    python scripts/fix-warp-gate-effect.py --verify
    python scripts/fix-warp-gate-effect.py --restore

Two bytes change (the type short). Client-only data: re-bake, no server
restart. Backup in build/warp-gate-effect/ (never a .bak beside the ZSC:
pack.ps1 errors on it). `data/` is gitignored, so this file is the record.
"""

import argparse
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
ZSC = os.path.join(REPO, "data", "3DDATA", "SPECIAL", "EVENT_OBJECT.ZSC")
BACKUP_DIR = os.path.join(REPO, "build", "warp-gate-effect")
BACKUP = os.path.join(BACKUP_DIR, "EVENT_OBJECT.ZSC")

# (object index, dummy point index): (effect file, wanted type)
FIXES = {
    (11, 0): ("_warp_gate03.eft", 0),
}


def effect_points(raw):
    """[(object, point, effect name, type, byte offset of the type short)]."""
    p = [0]

    def take(fmt):
        v = struct.unpack_from(fmt, raw, p[0])
        p[0] += struct.calcsize(fmt)
        return v if len(v) > 1 else v[0]

    def cstr():
        e = raw.index(b"\0", p[0])
        s = raw[p[0]:e].decode("latin-1")
        p[0] = e + 1
        return s

    def skip_tags():
        while True:
            if take("<B") == 0:
                return
            size = take("<B")      # not p[0] += take(): reads p[0] before take advances it
            p[0] += size

    for _ in range(take("<h")):                    # meshes
        cstr()
    for _ in range(take("<h")):                    # materials
        cstr()
        p[0] += 9 * 2 + 4 + 2 + 12
    effects = [cstr() for _ in range(take("<h"))]
    out = []
    for obj in range(take("<h")):
        take("<3i")
        n = take("<h")
        if not n:
            continue
        for _ in range(n):
            take("<2h")
            skip_tags()
        for pt in range(take("<h")):
            idx = take("<h")
            at = p[0]
            typ = take("<h")
            name = effects[idx] if 0 <= idx < len(effects) else ""
            out.append((obj, pt, name, typ, at))
            skip_tags()
        p[0] += 24
    if p[0] != len(raw):
        raise SystemExit("%s: parsed %d of %d bytes" % (ZSC, p[0], len(raw)))
    return out


def plan(raw):
    points = {(o, pt): (name, typ, at) for o, pt, name, typ, at in effect_points(raw)}
    todo, done = [], []
    for key, (want_name, want_type) in FIXES.items():
        if key not in points:
            raise SystemExit("object %d point %d: no such effect point" % key)
        name, typ, at = points[key]
        if os.path.basename(name).lower() != want_name.lower():
            raise SystemExit("object %d point %d is %s, expected %s -- the ZSC changed, review"
                             % (key + (name, want_name)))
        (done if typ == want_type else todo).append((key, name, typ, want_type, at))
    return todo, done


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    if a.restore:
        if not os.path.exists(BACKUP):
            raise SystemExit("no backup at %s" % BACKUP)
        shutil.copyfile(BACKUP, ZSC)
        print("restored %s" % ZSC)
        return

    raw = open(ZSC, "rb").read()
    todo, done = plan(raw)
    for (o, pt), name, _, want, _ in done:
        print("ok    object %d point %d %s type %d" % (o, pt, name, want))
    for (o, pt), name, typ, want, _ in todo:
        print("fix   object %d point %d %s type %d -> %d" % (o, pt, name, typ, want))
    if a.verify:
        sys.exit(1 if todo else 0)
    if a.dry_run or not todo:
        return

    os.makedirs(BACKUP_DIR, exist_ok=True)
    if not os.path.exists(BACKUP):
        shutil.copyfile(ZSC, BACKUP)
    buf = bytearray(raw)
    for _, _, _, want, at in todo:
        struct.pack_into("<h", buf, at, want)
    open(ZSC, "wb").write(bytes(buf))
    todo, _ = plan(open(ZSC, "rb").read())
    if todo:
        raise SystemExit("verify failed after write")
    print("wrote %s (backup %s)" % (ZSC, BACKUP))


if __name__ == "__main__":
    main()
