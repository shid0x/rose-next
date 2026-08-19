"""Cut "In Need of Water" (Oro 4413 / 4422 / 4423) from three cacti to two.

The quest sends you to one cactus in each of the three Golden Ring zones -- a
long walk between three map loads for one of five proofs -- and the cactus is an
invisible trigger box next to a prop, so a wrong guess gives no feedback. Two
pickups keeps the shape and roughly halves the trek.

What this changes
-----------------
Two numbers, twelve times, in `QP401.QSD`:

  * the nine cactus triggers gate on `Waterskin < 3` (COND_004, op "<"), which
    is what stops you taking a fourth. -> `< 2`
  * the three hand-in triggers gate on `Waterskin >= 3` (op ">="). -> `>= 2`

Both live in `STR_ITEM_DATA.iRequestCnt`, a plain i32 inside the condition
payload, so this is a fixed-size poke: the file length and every offset in it are
unchanged, and nothing structural is touched. The switches are untouched, so a
cactus still cannot be farmed twice; you simply pick whichever two of your three
are nearest.

Why all three quest ids
-----------------------
4413, 4422 and 4423 are not a sequence -- their accept triggers (`4413-01`,
`4413-22`, `4413-23`) are byte-for-byte identical apart from the quest id, they
reset the same nine switches, and each hand-in pays the same single Skully's
Earring. Skully gives out one of the three and you do it once. So the count has
to drop for all three or the change would land on a coin flip.

The `LIST_QUEST_S.STL` descriptions say "fill up 3 Waterskins", so they are
retitled to 2 to match. That writer is exercised by `--selftest` first, which
proves a no-op re-serialise of the untouched file is byte-identical before
anything is written.

`data/` is gitignored, so this script is the committed record of the change.
Idempotent; `--dry-run` previews, `--verify` re-reads and checks, `--restore`
puts the `.bak` files back.
"""
import argparse, os, re, shutil, struct, sys

QSD_REL = os.path.join("data", "3DDATA", "QUESTDATA", "QP401.QSD")
STL_REL = os.path.join("data", "3DDATA", "STB", "LIST_QUEST_S.STL")

WATERSKIN = 13180                  # questitem:180, packed type*1000 + no
OLD_COUNT, NEW_COUNT = 3, 2

PICKUP_TRIGGERS = {"%02d_Cactus%02d" % (z, run)
                   for z in (1, 2, 3) for run in (1, 2, 3)}
HANDIN_TRIGGERS = {"4413-02", "4413-03", "4413-04"}
TARGETS = PICKUP_TRIGGERS | HANDIN_TRIGGERS

QUEST_KEYS = ("LQUE4413", "LQUE4422", "LQUE4423")


def waterskin_counts(blob):
    """-> [(trigger, offset_of_iRequestCnt, count, op)] for every COND_004 that
    checks the Waterskin. Walks the file in order; the format carries no internal
    offsets, so this is the only way to find anything in it."""
    b, o = blob, 0

    def u32():
        nonlocal o
        v, = struct.unpack_from("<I", b, o); o += 4; return v

    def skip_str():
        nonlocal o
        n, = struct.unpack_from("<h", b, o); o += 2 + n

    def read_str():
        nonlocal o
        n, = struct.unpack_from("<h", b, o); o += 2
        v = b[o:o + n]; o += n
        return v.split(b"\0")[0].decode("latin-1")

    out = []
    u32()
    npat = u32()
    skip_str()
    for _ in range(npat):
        ntrig = u32()
        skip_str()
        for _ in range(ntrig):
            o += 1                                   # check_next
            ncond, nrew = u32(), u32()
            name = read_str()
            for idx in range(ncond + nrew):
                ent = o
                esz = u32()
                etype = struct.unpack_from("<i", b, o)[0] & 0xFFFFFF; o += 4
                if idx < ncond and etype == 4:       # COND_004, has-item
                    body = ent + 8
                    n, = struct.unpack_from("<i", b, body)
                    stride = (esz - 12) // n if n > 0 else 0
                    for i in range(max(0, n)):
                        rec = body + 4 + i * stride
                        sn, where, cnt = struct.unpack_from("<iii", b, rec)
                        op = b[rec + 12]
                        if sn == WATERSKIN:
                            out.append((name, rec + 8, cnt, op))
                o = ent + esz
    return out


def patch_qsd(path, dry):
    blob = open(path, "rb").read()
    hits = waterskin_counts(blob)
    todo, already, unexpected = [], [], []
    for name, off, cnt, op in hits:
        if name not in TARGETS:
            unexpected.append((name, cnt, op))
        elif cnt == NEW_COUNT:
            already.append(name)
        elif cnt == OLD_COUNT:
            todo.append((name, off, op))
        else:
            unexpected.append((name, cnt, op))

    print("    Waterskin conditions found: %d" % len(hits))
    if unexpected:
        raise SystemExit("    unexpected Waterskin conditions, refusing: %r" % unexpected)
    if not todo:
        print("    already %d, nothing to do" % NEW_COUNT)
        return False
    for name, off, op in sorted(todo):
        print("        %-14s %s %d -> %s %d"
              % (name, "<" if op == 3 else ">=", OLD_COUNT,
                 "<" if op == 3 else ">=", NEW_COUNT))
    if dry:
        return True

    out = bytearray(blob)
    for _, off, _ in todo:
        struct.pack_into("<i", out, off, NEW_COUNT)
    if len(out) != len(blob):
        raise SystemExit("    length changed, refusing to write")
    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(bytes(out))

    back = open(path, "rb").read()
    left = [(n, c) for n, _, c, _ in waterskin_counts(back) if c != NEW_COUNT]
    if left:
        raise SystemExit("    VERIFY FAILED, still at %r" % left)
    print("    %d conditions rewritten and verified" % len(todo))
    return True


def load_stl(root):
    sys.path.insert(0, os.path.join(root, "scripts"))
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(root, "scripts", "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def patch_stl(root, path, dry, selftest_only=False):
    mod = load_stl(root)
    orig = open(path, "rb").read()
    stl = mod.Stl(path)
    if stl.to_bytes() != orig:
        raise SystemExit("    STL does not round-trip, refusing to touch it")
    print("    STL round-trip selftest OK (%d bytes)" % len(orig))
    if selftest_only:
        return False

    idx = {k.decode("latin-1"): i for i, (k, _) in enumerate(stl.keys)}
    changed = 0
    for key in QUEST_KEYS:
        if key not in idx:
            print("        %s: no such key, skipped" % key)
            continue
        i = idx[key]
        for rows in stl.langs:
            for f in range(len(rows[i])):
                txt = rows[i][f]
                new = re.sub(rb"\b3 Waterskins\b", b"2 Waterskins", txt)
                if new != txt:
                    rows[i][f] = new
                    changed += 1
    if not changed:
        print("    descriptions already say 2 Waterskins")
        return False
    print("    %d description strings retitled" % changed)
    if dry:
        return True
    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(stl.to_bytes())
    back = mod.Stl(path)
    bidx = {k.decode("latin-1"): i for i, (k, _) in enumerate(back.keys)}
    for key in QUEST_KEYS:
        if key in bidx:
            for rows in back.langs:
                for txt in rows[bidx[key]]:
                    if b"3 Waterskins" in txt:
                        raise SystemExit("    VERIFY FAILED: %s still says 3" % key)
    print("    written and verified")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the STL writer is byte-faithful, then exit")
    args = ap.parse_args()

    qsd = os.path.join(args.root, QSD_REL)
    stl = os.path.join(args.root, STL_REL)
    for p in (qsd, stl):
        if not os.path.isfile(p):
            raise SystemExit("not found: %s" % p)

    if args.selftest:
        patch_stl(args.root, stl, True, selftest_only=True)
        return

    if args.restore:
        for p in (qsd, stl):
            bak = p + ".bak"
            if os.path.isfile(bak):
                shutil.copyfile(bak, p)
                os.remove(bak)
                print("restored %s" % os.path.basename(p))
            else:
                print("no backup for %s" % os.path.basename(p))
        return

    if args.verify:
        bad = [(n, c) for n, _, c, _ in waterskin_counts(open(qsd, "rb").read())
               if c != NEW_COUNT]
        if bad:
            print("VERIFY FAILED, these are not %d: %r" % (NEW_COUNT, bad))
            sys.exit(1)
        print("verify OK -- every Waterskin condition in QP401.QSD asks for %d" % NEW_COUNT)
        return

    print("QP401.QSD")
    patch_qsd(qsd, args.dry_run)
    print("LIST_QUEST_S.STL")
    patch_stl(args.root, stl, args.dry_run)
    if args.dry_run:
        print("(dry run, nothing written)")


if __name__ == "__main__":
    main()
