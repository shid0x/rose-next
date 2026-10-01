#!/usr/bin/env python3
"""Mark weapons as boss loot: a red item name.

An item's name colour comes from one cell. The column before the STL key in the
five equipment tables (LIST_WEAPON game col 44) is a STR_ITEMPREFIX id, read by
`CItem::GetItemRareType`:

    1-20   cyan name, with the prefix word in front (Demon, Dark ... Grand)
    21     violet name, no word -- the retail uniques (rows 901-993)
    22     red name, no word -- boss loot (added 2026-10-01)
    blank  yellow

22 needs the client that knows it (`CItem::GetItemNameColor`); an older client
draws the name yellow, nothing worse. STR_ITEMPREFIX.STL entry 22 is blank and
must stay blank, or the word is glued in front of every boss-loot name. The
server never reads the column.

First use, and a test of the tier itself: the 13 weapons of Jrose's dark
Japanese-legend set (doc/jrose-weapon-batch3.txt), parked as loot for the Karkia
catacombs boss. Their stats are still placeholders.

`data/` is gitignored, so this script is the committed record. Idempotent; the
sidecar keeps each cell's previous bytes so --restore is exact.

    python scripts/set-boss-loot-tier.py --dry-run
    python scripts/set-boss-loot-tier.py
    python scripts/set-boss-loot-tier.py --verify
    python scripts/set-boss-loot-tier.py --restore
"""
import argparse, importlib.util, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
STB = os.path.join("data", "3DDATA", "STB", "LIST_WEAPON.STB")
SIDECAR = os.path.join("build", "boss-loot-tier", "LIST_WEAPON.json")

PREFIX_COL = 44
BOSS_LOOT = b"22"
ROWS = list(range(1456, 1469))      # Kunihoroboshi .. Kekkando

def load(name, fn):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fn))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(STB):
        sys.exit("run from the repo root (%s not found)" % STB)
    ii = load("import_item", "import-item.py")

    _, _, rows, cols, data = ii.stb_read(STB)
    if cols - 1 != 46:
        sys.exit("LIST_WEAPON has %d data cols, expected 46 -- col 44 may no longer "
                 "be the prefix column" % (cols - 1))
    for r in ROWS:
        if r >= rows - 1 or not data[r][0]:
            sys.exit("row %d is missing or unnamed -- import the set first" % r)

    if args.restore:
        if not os.path.exists(SIDECAR):
            sys.exit("nothing to restore (%s not found)" % SIDECAR)
        saved = json.load(open(SIDECAR))
        for r, old in sorted(saved.items(), key=lambda kv: int(kv[0])):
            print("row %s: %r -> %r" % (r, data[int(r)][PREFIX_COL], old))
            ii.stb_set_cell(STB, int(r), PREFIX_COL, old.encode("latin-1"), args.dry_run)
        if not args.dry_run:
            os.remove(SIDECAR)
        print("restored %d cell(s)%s" % (len(saved), " (dry run)" if args.dry_run else ""))
        return 0

    todo = [r for r in ROWS if data[r][PREFIX_COL] != BOSS_LOOT]
    if args.verify:
        for r in todo:
            print("row %d (%s): prefix is %r, expected %r"
                  % (r, data[r][0].decode("latin-1"), data[r][PREFIX_COL], BOSS_LOOT))
        print("OK: all %d rows are boss loot" % len(ROWS) if not todo
              else "FAILED: %d row(s) differ" % len(todo))
        return 1 if todo else 0

    saved = json.load(open(SIDECAR)) if os.path.exists(SIDECAR) else {}
    for r in todo:
        print("row %d (%s): %r -> %r"
              % (r, data[r][0].decode("latin-1"), data[r][PREFIX_COL], BOSS_LOOT))
        saved.setdefault(str(r), data[r][PREFIX_COL].decode("latin-1"))
    if todo and not args.dry_run:
        os.makedirs(os.path.dirname(SIDECAR), exist_ok=True)
        json.dump(saved, open(SIDECAR, "w"), indent=1)
        for r in todo:
            ii.stb_set_cell(STB, r, PREFIX_COL, BOSS_LOOT, False)
        _, _, _, _, after = ii.stb_read(STB)
        bad = [r for r in ROWS if after[r][PREFIX_COL] != BOSS_LOOT]
        other = sum(1 for r in range(len(data)) for c in range(len(data[r]))
                    if after[r][c] != data[r][c] and not (r in ROWS and c == PREFIX_COL))
        if bad or other:
            sys.exit("post-write check failed: rows %s, %d unrelated cell(s) changed" % (bad, other))
    print("%d row(s) %s, %d already set"
          % (len(todo), "would change" if args.dry_run else "changed", len(ROWS) - len(todo)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
