"""Step 1 of the Eldeon rebuild: replace our Eldeon art + object tables with a
reference client's.

Our Eldeon map .IFOs were authored against the full retail object tables, but the
copy of `3DDATA/ELDEON/` in our data is both truncated and partly gutted, so a
lot of the world simply does not render. Measured against ruff (and QQ-iROSE,
which ships byte-identical Eldeon art -- all 798 shared files hash the same):

  * 213 of our 798 Eldeon art files differ from both references; only 585 match.
  * `village/day01.zms` is missing from ours entirely.
  * `LIST_DECO_EJ.ZSC` has 146 objects vs 167, `LIST_CNST_EJ.ZSC` 14 vs 20.

Cross-referencing every deco/cnst object id our maps actually place against both
tables gives, across all five Eldeon zones:

     22  object ids where OUR entry is empty (no parts)   -> reference has meshes
     12  object ids past the end of OUR table (>=146)     -> reference has meshes
      0  ids where both have parts but they differ
      0  ids where we have meshes and the reference does not

So the swap is purely additive: nothing our maps currently draw changes, and 34
object ids per zone-set start rendering that previously drew nothing. Typical
recoveries are swamp props, stone faces, bone piles, statues, fences, the Eldeon
towers and the village temples.

The object tables (`LIST_*.ZSC`) live inside `3DDATA/ELDEON/`, so they move with
the art automatically -- which is required, since map .IFO -> ZSC references are
by object index and the two must stay in step.

This does NOT touch spawns, warps, quests, NPC/zone/item tables or any map file.
Those are later steps and each carries real ID coupling; this one carries none.

Safety:
  * Pre-flight refuses to write if any real conflict or regression appears.
  * Backup of the whole existing tree goes to `build/eldeon-art-backup-<stamp>/`
    -- deliberately outside `data/`, because `src/pipeline/src/pack.rs` walks the
    data tree with no extension filter and would otherwise bake the backup into
    the .vfs.
  * `thumbs.db` is never copied.
  * Post-flight re-checks every object id our maps use and reports how many now
    resolve to real meshes.

Idempotent: re-running copies nothing once the trees agree. Use --dry-run first.
After running: rebake/deploy the client VFS. No server restart (client art only).
"""
import argparse, filecmp, glob, hashlib, os, shutil, struct, sys, time

# Our tree keeps game data under data/; a reference client's dump does not.
OUR_DATA = "data"
ELDEON_REL = os.path.join("3DDATA", "ELDEON")
MAPS_REL = os.path.join("3DDATA", "Maps", "ELDEON")
ZONE_STB_REL = os.path.join("3DDATA", "STB", "LIST_ZONE.STB")
DEFAULT_SOURCE = r"C:\Users\Thomas\Desktop\Testclients\ruff\extracted data"
SKIP_NAMES = {"thumbs.db"}

ZONES = {61: "EJT01", 62: "EJ01", 63: "EJ02", 64: "EJ03", 65: "EZ01"}
LUMP_OBJECT, LUMP_CNST = 1, 3
BS = chr(92)


# ------------------------------------------------------------------ readers
class R:
    def __init__(self, d):
        self.d, self.o = d, 0

    def i16(self):
        v, = struct.unpack_from("<h", self.d, self.o); self.o += 2; return v

    def i32(self):
        v, = struct.unpack_from("<i", self.d, self.o); self.o += 4; return v

    def u8(self):
        v = self.d[self.o]; self.o += 1; return v

    def cstr(self):
        e = self.d.index(b"\0", self.o)
        s = self.d[self.o:e].decode("latin-1"); self.o = e + 1; return s

    def bstr(self):
        n = self.u8(); s = self.d[self.o:self.o + n]; self.o += n; return s

    def proplist(self):
        t = self.u8()
        while t:
            n = self.u8(); self.o += n; t = self.u8()


def read_stb(path):
    d = open(path, "rb").read()
    f = R(d); f.o = 4
    off, rows, cols = struct.unpack_from("<III", d, f.o)
    f.o = off
    def ps():
        n, = struct.unpack_from("<H", d, f.o); f.o += 2
        s = d[f.o:f.o + n].decode("latin-1"); f.o += n; return s
    return [[ps() for _ in range(cols - 1)] for _ in range(rows - 1)]


def read_zsc(path):
    r = R(open(path, "rb").read())
    meshes = [r.cstr() for _ in range(r.i16())]
    for _ in range(r.i16()):
        r.cstr(); r.o += 9 * 2 + 4 + 2 + 12
    n_eft = r.i16()
    for _ in range(n_eft if n_eft > 0 else 0):
        r.cstr()
    models = []
    for _ in range(r.i16()):
        r.i32(); r.i32(); r.i32()
        n_part = r.i16()
        if n_part == 0:
            models.append([]); continue      # CMODEL::Load returns early
        parts = []
        for _ in range(n_part):
            m = r.i16(); r.i16(); r.proplist(); parts.append(m)
        for _ in range(r.i16()):
            r.i16(); r.i16(); r.proplist()
        for _ in range(6):
            r.o += 4
        models.append(parts)
    return meshes, models


def ifo_object_ids(path, lump):
    r = R(open(path, "rb").read())
    n = r.i32()
    tab = [(r.i32(), r.i32()) for _ in range(n)]
    out = []
    for t, off in tab:
        if t != lump:
            continue
        r.o = off
        for _ in range(r.i32()):
            r.bstr(); r.i16(); r.i16()
            r.i32(); oid = r.i32(); r.i32(); r.i32(); r.o += 40
            out.append(oid)
    return out


# ------------------------------------------------------------------ analysis
def object_signature(meshes, models, i):
    if i >= len(models):
        return None
    return [meshes[m] if m < len(meshes) else None for m in models[i]]


def analyse(root, source):
    """classify every deco/cnst object id our maps place"""
    zone = read_stb(os.path.join(root, OUR_DATA, ZONE_STB_REL))
    cache = {}

    def zsc(base, rel):
        key = (base, rel.lower())
        if key not in cache:
            cache[key] = read_zsc(os.path.join(base, rel))
        return cache[key]

    empty, missing, conflict, regress, ok = 0, 0, [], [], 0
    for zn, folder in ZONES.items():
        d = os.path.join(root, OUR_DATA, MAPS_REL, folder)
        if not os.path.isdir(d):
            continue
        ifos = {p.lower(): p for p in glob.glob(d + "/*.IFO") + glob.glob(d + "/*.ifo")}
        for lump, col in ((LUMP_OBJECT, 11), (LUMP_CNST, 12)):
            rel = zone[zn][col].replace(BS + BS, os.sep).replace(BS, os.sep)
            try:
                om, omod = zsc(os.path.join(root, OUR_DATA), rel)
                sm, smod = zsc(source, rel)
            except Exception:
                continue
            ids = set()
            for p in ifos.values():
                ids.update(ifo_object_ids(p, lump))
            for i in sorted(ids):
                a = object_signature(om, omod, i)
                b = object_signature(sm, smod, i)
                if a == b:
                    ok += 1
                elif not b:
                    regress.append((zn, rel, i))
                elif a is None:
                    missing += 1
                elif a == []:
                    empty += 1
                else:
                    conflict.append((zn, rel, i))
    return dict(empty=empty, missing=missing, conflict=conflict, regress=regress, ok=ok)


def file_map(base):
    out = {}
    root = os.path.join(base, ELDEON_REL)
    for r, _, files in os.walk(root):
        for f in files:
            if f.lower() in SKIP_NAMES:
                continue
            p = os.path.join(r, f)
            out[os.path.relpath(p, root).replace(os.sep, "/").lower()] = p
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--source", default=DEFAULT_SOURCE,
                    help="reference client data dir (default: ruff)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="copy even if pre-flight finds conflicts (not advised)")
    args = ap.parse_args()

    root, source = args.root, args.source
    for p, what in ((os.path.join(root, OUR_DATA, ELDEON_REL), "our Eldeon art"),
                    (os.path.join(root, OUR_DATA, MAPS_REL), "our Eldeon maps"),
                    (os.path.join(source, ELDEON_REL), "source Eldeon art")):
        if not os.path.isdir(p):
            raise SystemExit(f"not found: {p} ({what})")

    print("pre-flight: classifying every object id our maps place")
    pre = analyse(root, source)
    print(f"   unchanged            : {pre['ok']}")
    print(f"   ours empty  -> source: {pre['empty']}")
    print(f"   ours absent -> source: {pre['missing']}")
    print(f"   real conflicts       : {len(pre['conflict'])}")
    print(f"   regressions          : {len(pre['regress'])}")
    for zn, rel, i in (pre["conflict"] + pre["regress"])[:10]:
        print(f"      !! zone {zn} {os.path.basename(rel)} object {i}")
    if (pre["conflict"] or pre["regress"]) and not args.force:
        raise SystemExit("refusing to write: the source would change or remove models "
                         "our maps already draw (use --force to override)")

    ours, theirs = file_map(os.path.join(root, OUR_DATA)), file_map(source)
    add = sorted(set(theirs) - set(ours))
    upd = sorted(k for k in set(ours) & set(theirs) if not filecmp.cmp(ours[k], theirs[k], shallow=False))
    extra = sorted(set(ours) - set(theirs))
    print(f"\nart: ours={len(ours)} source={len(theirs)}  to add={len(add)}  to update={len(upd)}"
          f"  ours-only (left alone)={len(extra)}")
    for k in add[:8]:
        print(f"   + {k}")
    for k in upd[:8]:
        print(f"   ~ {k}")
    if len(upd) > 8:
        print(f"   ~ ... and {len(upd) - 8} more")

    if not add and not upd:
        print("\nnothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(root, "build", f"eldeon-art-backup-{stamp}")
    src_root = os.path.join(root, OUR_DATA, ELDEON_REL)
    print(f"\nbacking up {len(ours)} files -> {backup}")
    for rel, p in ours.items():
        dst = os.path.join(backup, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(p, dst)

    copied = 0
    for rel in add + upd:
        dst = os.path.join(src_root, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(theirs[rel], dst)
        copied += 1
    print(f"copied {copied} file(s)")

    bad = [rel for rel in add + upd
           if not filecmp.cmp(os.path.join(src_root, rel.replace("/", os.sep)),
                              theirs[rel], shallow=False)]
    if bad:
        raise SystemExit(f"VERIFY FAILED: {len(bad)} file(s) differ after copy, e.g. {bad[:3]}")

    print("\npost-flight: re-classifying")
    post = analyse(root, source)
    print(f"   unchanged            : {post['ok']}")
    print(f"   ours empty  -> source: {post['empty']}")
    print(f"   ours absent -> source: {post['missing']}")
    if post["empty"] or post["missing"] or post["conflict"] or post["regress"]:
        raise SystemExit("VERIFY FAILED: object tables still disagree after copy")
    recovered = pre["empty"] + pre["missing"]
    print(f"verified: every object id our maps place now resolves in our own tables "
          f"({recovered} of them newly render)")
    print(f"backup: {backup}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
