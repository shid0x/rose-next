"""Cut "In Need of Repairs" (Oro 4415) from six salvage spots to three, and make
each spot pay out on the first touch.

The quest wants six Salvageable Equipment and three Captured Venomous Hooded
Aspers. The capture half is fine -- it is a kill drop from NPC 2177 (dead-event
`4415-31`, a one-in-three roll), the Aspers spawn 142 times across the Golden
Ring, and the Net item is decoration that nothing checks. The salvage half is
the problem:

  * six spots, all required, spread over ODD04 (four) and ODD05 (two)
  * each is an invisible `warpbox` trigger with **nothing beside it** -- the
    nearest scenery is 811 to 7,965 units away, where the "In Need of Water"
    cacti had a unique cactus model 42 to 157 units away. There is no way to
    find them without coordinates.
  * every spot rolls COND_010 at 50%, and the event object will not re-fire for
    ~10 seconds (CEventObjectActionProcessor sets a 300-frame cooldown), so half
    the time you stand around and walk back in.

So: three spots instead of six, and the roll removed.

What this changes in `QP401.QSD`
--------------------------------
  * `4415-02` (hand-in)   Salvageable Equipment `>= 6` -> `>= 3`
  * `4415_EQ1`..`EQ6`     Salvageable Equipment `< 6`  -> `< 3`   (the pickup cap)
  * `4415_EQ1`..`EQ6`     COND_010 high percentile 50 -> 100

COND_010 is checked client-side only as `iRand = rand() % 101; fail if iRand <
low || iRand > high`, so low 0 / high 100 can never fail. All thirteen edits are
fixed-size pokes -- an i32 for the counts, a single byte for the percentile --
so the file length and every offset in it are unchanged.

The six spots keep their own switches, so none can be farmed twice; you simply
need any three of them. The capture requirement is left alone.

The `LIST_QUEST_S.STL` description does not state a number for the equipment
("...combing The Wasteland desert for damaged equipment"), so there is no text
to retitle.

Note that the *dialog* gates on these same numbers in its own compiled Lua --
see `scripts/patch-ega-repairs-dialog.py`, which has to be applied with this or
the hand-in stays hidden.

`data/` is gitignored, so this script is the committed record of the change.
Idempotent; `--dry-run` previews, `--verify` re-reads and checks, `--restore`
puts the `.bak` back.
"""
import argparse, os, shutil, struct, sys

QSD_REL = os.path.join("data", "3DDATA", "QUESTDATA", "QP401.QSD")

SALVAGE = 13181                    # questitem:181, packed type*1000 + no
OLD_COUNT, NEW_COUNT = 6, 3
OLD_PCT, NEW_PCT = 50, 100

PICKUPS = {"4415_EQ%d" % n for n in range(1, 7)}
HANDIN = {"4415-02"}
TARGETS = PICKUPS | HANDIN


def scan(blob):
    """-> (counts, chances)

    counts  = [(trigger, offset_of_iRequestCnt, value, op)] for COND_004 rows
              naming the Salvageable Equipment
    chances = [(trigger, offset_of_btHighPcnt, low, high)] for COND_010

    Walks the file in order; the format carries no internal offsets.
    """
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

    counts, chances = [], []
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
                esz = u32()
                etype = struct.unpack_from("<i", b, o)[0] & 0xFFFFFF; o += 4
                body = ent + 8
                if idx < ncond and etype == 4:
                    n, = struct.unpack_from("<i", b, body)
                    stride = (esz - 12) // n if n > 0 else 0
                    for i in range(max(0, n)):
                        rec = body + 4 + i * stride
                        sn, where, cnt = struct.unpack_from("<iii", b, rec)
                        if sn == SALVAGE:
                            counts.append((name, rec + 8, cnt, b[rec + 12]))
                elif idx < ncond and etype == 10:
                    chances.append((name, body + 1, b[body], b[body + 1]))
                o = ent + esz
    return counts, chances


def patch(path, dry):
    blob = open(path, "rb").read()
    counts, chances = scan(blob)

    todo, unexpected = [], []
    for name, off, val, op in counts:
        if name not in TARGETS:
            unexpected.append(("count", name, val))
        elif val == NEW_COUNT:
            pass
        elif val == OLD_COUNT:
            todo.append(("count", name, off, op))
        else:
            unexpected.append(("count", name, val))

    for name, off, low, high in chances:
        if name not in PICKUPS:
            continue                       # other quests' rolls are none of our business
        if high == NEW_PCT:
            pass
        elif low == 0 and high == OLD_PCT:
            todo.append(("chance", name, off, None))
        else:
            unexpected.append(("chance", name, "%d..%d" % (low, high)))

    print("    Salvageable Equipment conditions: %d" % len(counts))
    print("    COND_010 rolls on the six spots:  %d"
          % len([c for c in chances if c[0] in PICKUPS]))
    if unexpected:
        raise SystemExit("    unexpected entries, refusing: %r" % unexpected)
    if not todo:
        print("    already trimmed, nothing to do")
        return False

    for kind, name, off, op in sorted(todo, key=lambda t: (t[0], t[1])):
        if kind == "count":
            sym = "<" if op == 3 else ">="
            print("        %-9s  %s %d -> %s %d" % (name, sym, OLD_COUNT, sym, NEW_COUNT))
        else:
            print("        %-9s  roll 0..%d%% -> 0..%d%% (always)" % (name, OLD_PCT, NEW_PCT))
    if dry:
        return True

    out = bytearray(blob)
    for kind, name, off, op in todo:
        if kind == "count":
            struct.pack_into("<i", out, off, NEW_COUNT)
        else:
            out[off] = NEW_PCT
    if len(out) != len(blob):
        raise SystemExit("    length changed, refusing to write")
    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(bytes(out))

    c2, ch2 = scan(bytes(out))
    bad = [(n, v) for n, _, v, _ in c2 if n in TARGETS and v != NEW_COUNT]
    bad += [(n, h) for n, _, l, h in ch2 if n in PICKUPS and h != NEW_PCT]
    if bad:
        raise SystemExit("    VERIFY FAILED: %r" % bad)
    print("    %d edits written and verified" % len(todo))
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    path = os.path.join(args.root, QSD_REL)
    if not os.path.isfile(path):
        raise SystemExit("not found: %s" % path)

    if args.restore:
        bak = path + ".bak"
        if not os.path.isfile(bak):
            raise SystemExit("no backup at %s" % bak)
        shutil.copyfile(bak, path)
        os.remove(bak)
        print("restored %s -- NOTE this also reverts simplify-oro-water-quest.py"
              % os.path.basename(path))
        return

    if args.verify:
        counts, chances = scan(open(path, "rb").read())
        bad = [(n, v) for n, _, v, _ in counts if n in TARGETS and v != NEW_COUNT]
        bad += [(n, h) for n, _, l, h in chances if n in PICKUPS and h != NEW_PCT]
        if bad:
            print("VERIFY FAILED: %r" % bad)
            sys.exit(1)
        print("verify OK -- three salvage spots, no roll")
        return

    print("QP401.QSD")
    patch(path, args.dry_run)
    if args.dry_run:
        print("(dry run, nothing written)")


if __name__ == "__main__":
    main()
