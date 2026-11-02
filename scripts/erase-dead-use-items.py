"""Erase the 73 use items that cannot do anything (2026-11-02).

Tester report: the Halloween Tree (10:441) "doesn't work -- you use it, it is not
consumed and nothing happens". `scripts/audit-use-items.py` follows the server's
use-item decision (classUSER::Use_InventoryITEM) for every named LIST_USEITEM row
and found 73 that cannot work:

    26  class 313 (cast a skill)   the LIST_SKILL row in col 20 is blank, so the
                                   server keeps the item and does nothing
    21  class 314 (skill book)     teaches a blank LIST_SKILL row
    17  class 322 (box)            a package id the server does not declare
                                   (FindUseItemPackage), so the box stays shut
     1  class 316 (event scroll)   a blank LIST_EVENT row
     8  classes 324/325/363/602    no handler, no status, no ability: consumed
                                   for nothing (medals, MallPoint coins, Reroll
                                   Hammer, a 2016 winter box)

None is sold, dropped or given by a box: testers only had them from the GM item
browser. The user's call: retire them all ("if any interest me in another client
I'll pick them later"), and "retire" here means fully erased, no stub.

**The first run erased five boxes that work** (Camo Cart 520, Katayoku
Half-Wing 525, White Pink Santa 1043, Inmate Set 1060, Black Beachwear 1090) and
flagged the SW Mascot Costume Box (995): the audit's package parser lost every
Package(...) that followed a comment line in gs_user.cpp. The user caught it;
all were restored the same day and the list here is the corrected one. Each
row below was re-checked against the code directly from the original table.

    LIST_USEITEM.STB     the 73 rows blanked (every cell; row numbers are ids and
                         stay, the table is not compacted)
    LIST_USEITEM_S.STL   their LUSE keys removed (every language block), unless
                         a row that stays shares the key

Their icons stay in ITEM1.TSI and their ground models in LIST_FIELDITEM.ZSC:
both are shared indices, never renumbered. The skill books' dead LIST_SKILL
stubs (541/561/571/581 ...) are not touched.

Refused if anything still names one of them, in the legacy (10NNN) or wide
(10000NNN) item encoding: an item-code STB cell (shops, drops, crafting,
breakdown), a QSD item condition or reward, an AI drop (action 17), a server
package reward. Holdings are checked read-only in the database named by
Exes/server.toml; a character holding one would keep a nameless white icon.

    python scripts/erase-dead-use-items.py --dry-run
    python scripts/erase-dead-use-items.py
    python scripts/erase-dead-use-items.py --verify
    python scripts/erase-dead-use-items.py --restore

Undo in build/erase-dead-use-items/: the blanked cells and removed STL entries
are recorded, and the two files are kept whole as a safety net. `data/` is
gitignored, so this file is the record. Restart the servers (they read
LIST_USEITEM), re-bake the VFS.
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rosefmt import aip, qsd  # noqa: E402
from rosefmt.stb import Stb  # noqa: E402
from rosefmt.stl import Stl  # noqa: E402

DATA = os.path.join(REPO, "data", "3DDATA")
STB = os.path.join(DATA, "STB", "LIST_USEITEM.STB")
STL = os.path.join(DATA, "STB", "LIST_USEITEM_S.STL")
GS_USER = os.path.join(REPO, "src", "sho_gameserver", "src", "gs_user.cpp")
SERVER_TOML = os.path.join(REPO, "Exes", "server.toml")
PSQL = r"C:\Program Files\PostgreSQL\18\bin\psql.exe"
BACKUP_DIR = os.path.join(REPO, "build", "erase-dead-use-items")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")

T_USE, LEGACY_MAX, WIDE_BASE = 10, 999, 100000
# Tables whose cells hold item codes. Other tables hold plain numbers (HP,
# prices ...) that can equal a 10NNN code by chance.
ITEM_CODE_TABLES = ("LIST_SELL.STB", "ITEM_DROP.STB", "LIST_PRODUCT.STB", "LIST_BREAK.STB",
                    "LIST_NPC.STB", "LIST_QUESTITEM.STB", "LIST_USEITEM.STB")

ROWS = {
    # class 313: cast a blank skill
    144: "Woopie Chef paw charm", 146: "King beans", 148: "Cowboy pumpkin hat",
    205: "Shout Scroll", 207: "World Scroll Junon", 208: "World Scroll Lunar",
    209: "World Scroll Eldeon", 210: "World Scroll Orlo", 320: "Purple Icicle",
    324: "Black Icicle", 325: "Red Icicle", 327: "Rock", 342: "HP/MP Scroll (Party)",
    343: "Dexterity Scroll (Party)", 344: "Strength Scroll (Party)",
    345: "Defense Scroll (Party)", 346: "Accuracy Scroll (Party)",
    347: "Damage Scroll (Party)", 398: "Halloween Dance Beginner",
    399: "Halloween Dance Intermediate", 400: "Halloween Dance Refined",
    440: "Candle Ghost", 441: "Halloween Tree", 1030: "Mistletoe", 1040: "Fireworks",
    1113: "Net",
    # class 314: teach a blank skill
    608: "Mount", 612: "Union Skill", 613: "Skill Tester", 614: "Alchemy Craft",
    648: "Crossbow Speed Mastery", 649: "Double Shot", 650: "Advance Crossbow Mastery",
    669: "Brave Howl", 671: "Religious Study", 679: "Trap Arrow", 680: "Poisoned Shot",
    681: "Critical Shot", 686: "Championship Study", 687: "Sharpen Blade",
    688: "Raging Blood", 689: "Champion Clamor", 771: "Natural Instinct",
    786: "Shuriken Mastery", 787: "Sharp Claw", 819: "Union Armor", 823: "Concussor",
    # class 316: blank event
    203: "No exp scroll",
    # class 322: undeclared package
    256: "Halloween Candy-bag", 257: "Halloween Grab-bag", 267: "Halloween Car Box",
    268: "Halloween Mask Box", 290: "Halloween t-shirt Costume Box",
    527: "God of Egypt Masks Box", 1000: "Goody Bag", 1001: "Medal Bag",
    1029: "Silver Whistle Box", 1037: "Small Christmas Present",
    1038: "Medium Christmas Present", 1039: "Large Christmas Present",
    1094: "Seasonal Summer Box",
    1104: "Tropical Snacks Box", 1105: "Beach Beverages Box", 1106: "Festival Menu Box",
    1112: "Swimming Wear Box",
    # classes 324 / 325 / 363 / 602: no handler, consumed for nothing
    206: "Reroll Hammer", 228: "Seasonal Winter Box 2016", 271: "100 MallPoint Coin",
    272: "200 MallPoint Coin", 275: "2 MallPoint Coin", 200: "Trainee Medal",
    201: "Brave Medal", 202: "Hero Medal",
}


def codes(rows):
    """{code string: row} in both item encodings."""
    out = {}
    for r in rows:
        out[str(T_USE * WIDE_BASE + r)] = r
        if r <= LEGACY_MAX:
            out[str(T_USE * 1000 + r)] = r
    return out


def references(rows):
    """Everything that still names one of ``rows``: [description]."""
    want = codes(rows)
    hits = []
    for name in ITEM_CODE_TABLES:
        p = os.path.join(DATA, "STB", name)
        if not os.path.exists(p):
            continue
        t = Stb(p)
        for r in range(t.rows):
            if name == "LIST_USEITEM.STB" and r in rows:
                continue
            for c in range(t.cols):
                v = t.s(r, c).strip()
                if v in want:
                    hits.append("%s row %d col %d = %s (%s)" % (name, r, c, v, ROWS[want[v]]))
    for p in glob.glob(os.path.join(DATA, "QUESTDATA", "**", "*.QSD"), recursive=True):
        for _off, sn, kind in qsd.item_refs(p):
            if str(sn) in want:
                hits.append("%s: quest %s %d (%s)" % (os.path.relpath(p, DATA), kind, sn, ROWS[want[str(sn)]]))
    for p in glob.glob(os.path.join(DATA, "AI", "**", "*.aip"), recursive=True):
        _h, _t, pats, _tail = aip.parse(open(p, "rb").read())
        for _pn, events in pats:
            for _en, _conds, acts in events:
                for a in acts:
                    if aip.record_type(a) != 17:
                        continue
                    for i in range(5):
                        v = int.from_bytes(a[8 + 2 * i:10 + 2 * i], "little", signed=True)
                        if str(v) in want:
                            hits.append("%s: AI drop %d (%s)" % (os.path.relpath(p, DATA), v, ROWS[want[str(v)]]))
    src = open(GS_USER, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"Reward\(ITEM_TYPE_USE,\s*(\d+)", src):
        if int(m.group(1)) in rows:
            hits.append("gs_user.cpp package reward 10:%s" % m.group(1))
    return hits


def holdings(rows):
    """[(where, owner, item)] held in the servers' database; None if it cannot be read.

    The connection string is read from Exes/server.toml at run time and handed to
    psql through the environment, never printed or stored."""
    try:
        toml = open(SERVER_TOML, encoding="utf-8").read()
    except OSError as e:
        print("   database not checked: %s" % e)
        return None
    m = re.search(r'^\s*connection_string\s*=\s*"([^"]+)"', toml, re.M)
    if not m or not os.path.exists(PSQL):
        print("   database not checked: no connection string or no psql")
        return None
    ids = ",".join(str(r) for r in sorted(rows))
    sql = ("SELECT 'inventory', c.name, i.game_data_id FROM inventory v JOIN item i ON i.id = v.item_id "
           "JOIN character c ON c.id = v.owner_id WHERE i.type_id = %d AND i.game_data_id IN (%s)"
           % (T_USE, ids))
    env = dict(os.environ, ROSE_DB_URL=m.group(1))
    try:
        out = subprocess.run([PSQL, "-At", "-F", "\t", "-c", sql, "-d", env["ROSE_DB_URL"]],
                             capture_output=True, text=True, timeout=30, env=env)
    except (OSError, subprocess.TimeoutExpired) as e:
        print("   database not checked: %s" % type(e).__name__)
        return None
    if out.returncode:
        print("   database not checked: psql exit %d" % out.returncode)
        return None
    return [line.split("\t") for line in out.stdout.splitlines() if line.strip()]


def rows_left(stb):
    return [r for r in ROWS if any(c.strip() for c in stb.d[r])]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--verify", action="store_true")
    mode.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    if a.restore:
        return restore()

    stb, stl = Stb(STB), Stl(STL)
    if stb.to_bytes() != open(STB, "rb").read() or stl.to_bytes() != open(STL, "rb").read():
        sys.exit("ABORT: LIST_USEITEM.STB / _S.STL does not round-trip")

    left = rows_left(stb)
    man = json.load(open(MANIFEST)) if os.path.exists(MANIFEST) else {"cells": {}, "stl": {}, "stl_keys": []}
    if a.verify:
        stl_left = [k for k in man["stl_keys"] if stl.has(k)]
        hits = references(set(ROWS))
        held = holdings(set(ROWS))
        for h in hits:
            print("   still referenced: " + h)
        for h in held or []:
            print("   still held: %s" % "  ".join(h))
        print("rows still holding cells: %s" % left)
        print("STL keys still present:   %s" % stl_left)
        if left or stl_left or hits or held:
            print("VERIFY FAILED")
            return 1
        print("ALL CHECKS PASSED -- the %d use items are erased." % len(ROWS))
        return 0
    if not left:
        print("nothing to do.")
        return 0

    for r in left:
        cur = re.sub(r" +", " ", stb.s(r, 0).strip())
        key = stb.s(r, stb.cols - 1).strip()
        shown = stl.field(key, 0, 0).strip() if key and stl.has(key) else ""
        if ROWS[r] not in (cur, shown):
            sys.exit("ABORT: LIST_USEITEM %d is %r / %r, expected %r" % (r, cur, shown, ROWS[r]))
    hits = references(set(left))
    if hits:
        sys.exit("ABORT: still referenced:\n  " + "\n  ".join(hits))
    held = holdings(set(left))
    for h in held or []:
        print("   held in the database: %s" % "  ".join(h))

    # an STL key goes only when no surviving row uses it
    kept_keys = {stb.s(r, stb.cols - 1).strip() for r in range(stb.rows) if r not in ROWS}
    mine = {stb.s(r, stb.cols - 1).strip() for r in left} - {""}
    keys = sorted(k for k in mine - kept_keys if stl.has(k))
    shared = sorted(mine & kept_keys)
    print("blank %d row(s), remove %d STL key(s)%s"
          % (len(left), len(keys), (", keep %d shared: %s" % (len(shared), shared)) if shared else ""))
    if a.dry_run:
        print("dry run: nothing written")
        return 0

    os.makedirs(BACKUP_DIR, exist_ok=True)
    for path in (STB, STL):
        keep = os.path.join(BACKUP_DIR, os.path.basename(path) + ".orig")
        if not os.path.exists(keep):
            shutil.copy2(path, keep)
    for r in left:
        man["cells"][str(r)] = [c.decode("latin-1") for c in stb.d[r]]
        stb.d[r] = [b"" for _ in stb.d[r]]
    for k in keys:
        j = stl.index(k)
        man["stl"][k] = {"id": stl.keys[j][1],
                         "langs": [[f.decode("latin-1") for f in rows[j]] for rows in stl.langs]}
    man["stl_keys"] = sorted(set(man["stl_keys"]) | set(keys))
    stl.remove(keys)
    open(STB, "wb").write(stb.to_bytes())
    open(STL, "wb").write(stl.to_bytes())
    json.dump(man, open(MANIFEST, "w"), indent=1)

    stb2, stl2 = Stb(STB), Stl(STL)
    bad_rows, bad_keys = rows_left(stb2), [k for k in man["stl_keys"] if stl2.has(k)]
    if bad_rows or bad_keys:
        print("FAILED: rows %s keys %s" % (bad_rows, bad_keys))
        return 1
    print("erased %d row(s) and %d STL key(s) (undo: --restore). Restart the servers and "
          "re-bake the VFS." % (len(left), len(keys)))
    return 0


def restore():
    if not os.path.isfile(MANIFEST):
        print("nothing to restore")
        return 0
    man = json.load(open(MANIFEST))
    stb, stl = Stb(STB), Stl(STL)
    for r, cells in man["cells"].items():
        if any(c.strip() for c in stb.d[int(r)]):
            sys.exit("ABORT: LIST_USEITEM %s holds cells again -- restore by hand" % r)
        stb.d[int(r)] = [c.encode("latin-1") for c in cells]
    for k, e in sorted(man["stl"].items(), key=lambda kv: kv[1]["id"]):
        if stl.has(k):
            continue
        stl.append(k, e["id"], *[f.encode("latin-1") for f in e["langs"][0]])
        j = stl.index(k)
        for li, fields in enumerate(e["langs"]):
            stl.langs[li][j] = [f.encode("latin-1") for f in fields]
    open(STB, "wb").write(stb.to_bytes())
    open(STL, "wb").write(stl.to_bytes())
    os.remove(MANIFEST)
    print("restored %d row(s) and %d STL key(s)" % (len(man["cells"]), len(man["stl"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
