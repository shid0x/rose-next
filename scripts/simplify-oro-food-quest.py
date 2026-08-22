"""Trim "In Need of Food" (Oro 4414) and give it a reward that exists.

The quest wants 12 proofs from each of the three Asper families plus 5 Asper
Meat and 5 Asper Skin. All five items exist and drop, but the meat and skin are
far more expensive than they look, because of how the drop chain is ordered on
Desert Asper (2198) and Hooded Asper (2201):

    TRIGGER 4414-32   (check_next)   proofs < 12         -> give proof
    TRIGGER 4414-32-1 (check_next)   15% roll, meat < 5  -> give meat

`CQuestDATA::CheckQUEST` is first-match-wins -- a trigger that succeeds returns
immediately and the chain only advances when one *fails*. So while you hold
fewer than 12 proofs the proof trigger always wins and the meat trigger is never
evaluated at all; meat only begins dropping once the proof cap is reached, and
then at 15%. (That ordering is deliberate -- the quest text says "*After that*,
collect 5 Asper Meats" -- it is the cost that is wrong.)

    12 + 5/0.15 = ~45 Desert Asper, ~45 Hooded Asper, 12 Crowned  =  ~102 kills

Two thirds of that is 15% farming on two specific mob types, and it reads as "the
item does not exist" because nothing drops for the first twelve kills.

What this changes
-----------------
  QP401.QSD   4414-02      Asper Meat `>= 5` -> `>= 2`, Asper Skin `>= 5` -> `>= 2`
  QN-2198.QSD 4414-32-1    Asper Meat `< 5`  -> `< 2`,  roll 0..15% -> 0..40%
  QN-2201.QSD 4414-41-1    Asper Skin `< 5`  -> `< 2`,  roll 0..15% -> 0..40%

which is 12 + 2/0.4 = ~17 kills per family, ~46 all told. The proof counts are
untouched: the kill half of this quest already worked.

The reward
----------
`4414-02` pays `REWD_005` CALC ITEM against `useitem:195`, which is a blank row
here -- RoseZA's "Cactus Root", one of the six reward items the import missed --
so the quest handed over ten nameless items. Repointed to `gem:367 Diamond [7]`,
our highest-grade gem, and the base value dropped from 10 to 1.

That value matters because gems stack (`IsEnableDupCNT` is true for types 10..13),
so `Reward_CalITEM` takes the quantity from `CCal::Get_RewardVALUE` equation 5:

    ((value + 20) * (CHARM + 10) * 100 * (FAME + 20) / (LEVEL + 70) / 30000) + value

At value 1 that is a single gem for an ordinary character, rising to about eleven
for one with maxed charm and fame (fame is a `char`, so it cannot run away).
At the original value of 10 even a plain character would have received a dozen.

All seven edits are fixed-size pokes -- i32 counts, an i32 item id, and a single
percentile byte -- so every file keeps its byte length and no offset moves.

The dialog gates on these same numbers in its own compiled Lua; see
`scripts/patch-jack-food-dialog.py`, which must be applied with this one or the
hand-in stays hidden.

`data/` is gitignored, so this script is the committed record of the change.
Idempotent; `--dry-run` previews, `--verify` re-reads and checks, `--restore`
puts the `.bak` files back.
"""
import argparse, importlib.util, os, re, shutil, struct, sys

QDIR = os.path.join("data", "3DDATA", "QUESTDATA")
STL_REL = os.path.join("data", "3DDATA", "STB", "LIST_QUEST_S.STL")

# The journal entry spells the counts out ("collect 5 Asper Meats from Desert
# Aspers and 5 Asper Skins from Hooded Aspers"), so it has to move with them.
# 4415's text names no salvage count and its "capture 3 Venomous Hooded Aspers"
# is still true, so nothing there needs touching.
QUEST_KEY = "LQUE4414"
TEXT_SUBS = ((rb"\b5 Asper Meats\b", b"2 Asper Meats"),
             (rb"\b5 Asper Skins\b", b"2 Asper Skins"))

MEAT, SKIN = 13175, 13176          # questitem:175 / :176
OLD_MAT, NEW_MAT = 5, 2
OLD_PCT, NEW_PCT = 15, 40
OLD_REWARD, NEW_REWARD = 10195, 11367     # useitem:195 (blank) -> gem:367 Diamond [7]
OLD_VALUE, NEW_VALUE = 10, 1

# file -> {trigger -> what to touch}
PLAN = {
    "QP401.QSD":   {"4414-02":   ("counts", (MEAT, SKIN))},
    "QN-2198.QSD": {"4414-32-1": ("count+roll", (MEAT,))},
    "QN-2201.QSD": {"4414-41-1": ("count+roll", (SKIN,))},
}


def walk(b):
    """Yield (trigger, idx, is_cond, etype, ent_off, esz). No internal offsets in
    the format, so everything is found by walking it in order."""
    o = 0

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

    u32()
    npat = u32()
    skip_str()
    for _ in range(npat):
        ntrig = u32()
        skip_str()
        for _ in range(ntrig):
            o += 1
            ncond, nrew = u32(), u32()
            name = read_str()
            for idx in range(ncond + nrew):
                ent = o
                esz, = struct.unpack_from("<I", b, o)
                etype = struct.unpack_from("<i", b, o + 4)[0] & 0xFFFFFF
                yield name, idx, idx < ncond, etype, ent, esz
                o = ent + esz


def plan_edits(blob, rules):
    """-> ([(label, offset, size, old, new)], [problems])"""
    edits, bad = [], []
    for name, idx, is_cond, etype, ent, esz in walk(blob):
        rule = rules.get(name)
        if not rule:
            continue
        kind, items = rule
        body = ent + 8

        if is_cond and etype == 4:                       # has-item
            n, = struct.unpack_from("<i", blob, body)
            stride = (esz - 12) // n if n > 0 else 0
            for i in range(max(0, n)):
                rec = body + 4 + i * stride
                sn, where, cnt = struct.unpack_from("<iii", blob, rec)
                if sn not in items:
                    continue
                if cnt == NEW_MAT:
                    continue
                if cnt != OLD_MAT:
                    bad.append((name, "item %d count %d" % (sn, cnt)))
                    continue
                edits.append(("%s item %d count" % (name, sn), rec + 8, 4, cnt, NEW_MAT))

        elif is_cond and etype == 10 and kind == "count+roll":   # chance
            low, high = blob[body], blob[body + 1]
            if high == NEW_PCT:
                continue
            if low != 0 or high != OLD_PCT:
                bad.append((name, "roll %d..%d" % (low, high)))
                continue
            edits.append(("%s roll" % name, body + 1, 1, high, NEW_PCT))

        elif (not is_cond) and etype == 5:               # CALC reward
            target = blob[body]
            value, item = struct.unpack_from("<ii", blob, body + 4)
            if target != 2:                              # 2 = ITEM
                continue
            if item == NEW_REWARD and value == NEW_VALUE:
                continue
            if item != OLD_REWARD or value != OLD_VALUE:
                bad.append((name, "reward item %d value %d" % (item, value)))
                continue
            edits.append(("%s reward item" % name, body + 8, 4, item, NEW_REWARD))
            edits.append(("%s reward value" % name, body + 4, 4, value, NEW_VALUE))
    return edits, bad


def apply(path, rules, dry):
    blob = open(path, "rb").read()
    edits, bad = plan_edits(blob, rules)
    if bad:
        raise SystemExit("    unexpected content in %s, refusing: %r"
                         % (os.path.basename(path), bad))
    if not edits:
        print("    %-13s already done" % os.path.basename(path))
        return False
    for label, off, size, old, new in edits:
        print("        %-28s %d -> %d" % (label, old, new))
    if dry:
        return True

    out = bytearray(blob)
    for label, off, size, old, new in edits:
        if size == 4:
            struct.pack_into("<i", out, off, new)
        else:
            out[off] = new
    if len(out) != len(blob):
        raise SystemExit("    length changed, refusing to write")
    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(bytes(out))

    left, _ = plan_edits(open(path, "rb").read(), rules)
    if left:
        raise SystemExit("    VERIFY FAILED, %d edits did not stick" % len(left))
    print("    %-13s %d edits written and verified" % (os.path.basename(path), len(edits)))
    return True


def load_import_oro(root):
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(root, "scripts", "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def patch_stl(root, path, dry, selftest_only=False):
    """Retitle the journal entry. The writer is proven byte-faithful on the
    untouched file first, so a bad round-trip cannot reach disk."""
    mod = load_import_oro(root)
    orig = open(path, "rb").read()
    stl = mod.Stl(path)
    if stl.to_bytes() != orig:
        raise SystemExit("    STL does not round-trip, refusing to touch it")
    print("    round-trip selftest OK (%d bytes)" % len(orig))
    if selftest_only:
        return False

    idx = {k.decode("latin-1"): i for i, (k, _) in enumerate(stl.keys)}
    if QUEST_KEY not in idx:
        print("    %s: no such key, skipped" % QUEST_KEY)
        return False
    i = idx[QUEST_KEY]
    changed = 0
    for rows in stl.langs:
        for f in range(len(rows[i])):
            txt = rows[i][f]
            new = txt
            for pat, rep in TEXT_SUBS:
                new = re.sub(pat, rep, new)
            if new != txt:
                rows[i][f] = new
                changed += 1
    if not changed:
        print("    description already says 2")
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
    for txt in back.langs[0][bidx[QUEST_KEY]]:
        if b"5 Asper" in txt:
            raise SystemExit("    VERIFY FAILED: description still says 5")
    print("    written and verified")
    return True


def stl_is_done(path):
    mod_txt = open(path, "rb").read()
    return b"5 Asper Meats" not in mod_txt and b"5 Asper Skins" not in mod_txt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the STL writer is byte-faithful, then exit")
    args = ap.parse_args()

    stl_path = os.path.join(args.root, STL_REL)
    if not os.path.isfile(stl_path):
        raise SystemExit("not found: %s" % stl_path)

    paths = {}
    for fname in PLAN:
        p = os.path.join(args.root, QDIR, fname)
        if not os.path.isfile(p):
            raise SystemExit("not found: %s" % p)
        paths[fname] = p

    if args.selftest:
        print("LIST_QUEST_S.STL")
        patch_stl(args.root, stl_path, True, selftest_only=True)
        return

    if args.restore:
        bak = stl_path + ".bak"
        if os.path.isfile(bak):
            shutil.copyfile(bak, stl_path)
            os.remove(bak)
            print("restored LIST_QUEST_S.STL  (NOTE: also reverts the water retitle)")
        else:
            print("no backup for LIST_QUEST_S.STL")
        for fname, p in paths.items():
            bak = p + ".bak"
            if os.path.isfile(bak):
                shutil.copyfile(bak, p)
                os.remove(bak)
                note = ""
                if fname == "QP401.QSD":
                    note = "  (NOTE: also reverts the water and repairs trims)"
                print("restored %s%s" % (fname, note))
            else:
                print("no backup for %s" % fname)
        return

    if args.verify:
        problems = []
        for fname, p in paths.items():
            left, bad = plan_edits(open(p, "rb").read(), PLAN[fname])
            problems += [(fname,) + tuple(x) for x in bad]
            problems += [(fname, l) for l, _, _, _, _ in left]
        if not stl_is_done(stl_path):
            problems.append(("LIST_QUEST_S.STL", "description still says 5 Asper"))
        if problems:
            print("VERIFY FAILED: %r" % problems)
            sys.exit(1)
        print("verify OK -- 2 materials at a 40%% roll, reward is gem:367, "
              "description matches")
        return

    touched = False
    for fname, p in paths.items():
        print(fname)
        touched |= apply(p, PLAN[fname], args.dry_run)
    print("LIST_QUEST_S.STL")
    touched |= patch_stl(args.root, stl_path, args.dry_run)
    if args.dry_run and touched:
        print("(dry run, nothing written)")


if __name__ == "__main__":
    main()
