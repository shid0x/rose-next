"""Give every item that has no displayed name the name that is already in its STB.

The client resolves an item name in exactly two steps
(`CStringManager::GetItemStringData`, src/client/gamecommon/stringmanager.cpp):

    key  = LIST_<TYPE>.STB.value(item_no, col_count - 1)   # the last column
    name = LIST_<TYPE>_S.STL[key], language block 1 (LANGUAGE_USA)

Either step failing returns `m_strNull`, so the item renders with an **empty
name** -- no warning, no log line, nothing in `error.txt`. 257 real items were in
that state, in two distinct flavours:

  * **NOKEY** (136 rows) -- the STB's last column is blank, so there is no key to
    look up. Whole appended blocks are like this: LIST_JEWEL 252-370 is a
    complete accessory set (socket rings/necklaces/earrings, plus Bronze ->
    Diamond in all six stats) that someone added without ever writing the STL
    link.
  * **NOSTL** (121 rows) -- the STB names a key such as `LPAT007`, but the STL
    has no entry under it. LIST_PAT ships 73 keys against 149 items.

Both are fixed from **our own data**: the English name is already sitting in STB
column 0, which is what the *server* uses (`ITEM_NAME` = `get_cstr(row, 0)`) and
why these items have always had names in GM output while showing blank in the
client. So this is a client-data repair; no server change, no code change.

What it does NOT touch
----------------------
  * **Mob weapons** -- LIST_WEAPON 1001-1153 and LIST_SUBWPN 301-305 carry a
    model and a hit sound but no Type, no price and no icon. They are NPC
    equipment, never enter an inventory, and are *correctly* nameless. The Type
    column separates them cleanly: no row below 1001 lacks a Type, and every row
    that lacks one is in that block.
  * **`Naked01` / `Barefoot`** -- rows 0/1 of CAP/BODY/ARMS/FOOT, the placeholders
    an unequipped slot points at.
  * **Rows that share a key on purpose.** ~1150 rows point at another row's key
    (`LWEA005` is on both `Long Sword` and `Golden Long Sword`). That is the rare
    system: `GetItemName` prepends `STR_ITEMPREFIX[rare type]`, so the displayed
    string is assembled at runtime. Sharing a key is correct here, and a script
    that "fixed" it would rename half the game.

Things that made this less obvious than it looks
------------------------------------------------
  * **The reference dumps do not align by row.** Every client has its own item
    tables -- our LIST_PAT row 7 is `Clover Frame`, 667's is `Gray Racing Frame`.
    Copying names by row number yields plausible, silently wrong text. Names here
    come from our own STB; the dumps are used only to *corroborate* (70 of the 97
    cart parts appear in a dump with a byte-identical STL string, which is what
    establishes that column 0 holds canonical retail text) and to recover
    descriptions by exact name match.
  * **LIST_QUESTITEM 31-40 already had their STL entries.** `LQIT031`-`LQIT040`
    are present and correct (`Ripe Apple` ... `Ripe Plum`); only the STB's key
    column was blanked, orphaning them. Those ten need no new STL entry -- and
    their STB column 0 is mojibake (`??? ??`, literal `?` bytes from a lossy
    re-encode upstream), so the repair runs the other way: the STL name is
    written back into the STB.
  * **The STL key convention is `L<PREFIX><row>` with `id == row`.** Verified at
    100% across PAT/JEWEL/WEAPON/CAP/USEITEM before generating a single key.

Known-inert, deliberately left alone
------------------------------------
LIST_QUESTITEM 400-401, 700-706 and 950-965 have *only* column 0 filled -- no
Type, no icon, no quest link. They are named here for completeness and because a
named stub is easier to find than a blank one, but they cannot be granted or
displayed properly until the rest of the row exists. Naming them changes nothing
at runtime: the STL is consulted for name and description only, and validity
(`tagBaseITEM::Init` -> `IsValidITEM`) is decided from the type range and the STB
row count, never from the STL.

Likewise the 99 jewels are currently unobtainable -- they appear in no drop
table, no shop and no craft recipe. Naming them is correct and harmless; making
them reachable is a separate data change.

Idempotent, verifiable and reversible through a sidecar next to the STBs. `data/`
is gitignored, so this file is the only committed record of the change.

Usage:
    python scripts/fix-item-names.py --dry-run
    python scripts/fix-item-names.py
    python scripts/fix-item-names.py --verify
    python scripts/fix-item-names.py --restore
"""
import argparse
import importlib.util
import io
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SIDECAR = os.path.join(STB_DIR, "item-names.json")

# Reference dumps, used only for descriptions and only on an exact name match.
# Absent is fine -- names come from our own tables either way.
DUMP_ROOT = r"C:\Users\Thomas\Desktop\Testclients"
DUMPS = [
    ("667",    os.path.join(DUMP_ROOT, "667", "extracted data", "3DDATA", "STB"), "latin-1", "utf-8"),
    ("roseza", os.path.join(DUMP_ROOT, "RoseZA test client", "data", "3DDATA", "STB"), "cp949", "cp949"),
    ("ruff",   os.path.join(DUMP_ROOT, "ruff", "extracted data", "3DDATA", "STB"), "latin-1", "utf-8"),
    ("qq",     os.path.join(DUMP_ROOT, "QQ-iROSE Online", "QQiroseData", "3DDATA", "STB"), "cp949", "cp949"),
]

# item type -> (STB base name, STL key prefix). Mirrors _ItemTypeToItemTable in
# src/client/gamecommon/stringmanager.cpp; the prefixes are read off our own
# files, not guessed (LIST_JEWEL is LJEW -- import-item.py had LJEM).
TABLES = {
    1:  ("LIST_FACEITEM", "LFAC"),
    2:  ("LIST_CAP",      "LCAP"),
    3:  ("LIST_BODY",     "LBOD"),
    4:  ("LIST_ARMS",     "LARM"),
    5:  ("LIST_FOOT",     "LFOO"),
    6:  ("LIST_BACK",     "LBAC"),
    7:  ("LIST_JEWEL",    "LJEW"),
    8:  ("LIST_WEAPON",   "LWEA"),
    9:  ("LIST_SUBWPN",   "LSUB"),
    10: ("LIST_USEITEM",  "LUSE"),
    11: ("LIST_JEMITEM",  "LJEM"),
    12: ("LIST_NATURAL",  "LNAT"),
    13: ("LIST_QUESTITEM","LQIT"),
    14: ("LIST_PAT",      "LPAT"),
}

# The client reads language block 1 for a Latin charset (LANGUAGE_USA = 1,
# stringmanager.h). Blocks 0/2/3/4 are KOR/JPN/CHS/CHT.
LANG_USA = 1

COL_TYPE = 4    # ITEM_TYPE in rose/io/stb.h -- 0/blank marks a mob weapon

WEAPON_TYPES = (8, 9)
ARMOUR_TYPES = (2, 3, 4, 5)


def load_reader():
    spec = importlib.util.spec_from_file_location(
        "rose_data_reader", os.path.join(HERE, "rose-data-reader.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ------------------------------------------------------------------ STB write
#
# Only cell data is rewritten. Everything before `data_offset` (column widths,
# column titles, row names) is copied through byte for byte, so `data_offset`
# stays valid and an STB editor sees the file it wrote. Proven byte-identical on
# a no-op pass over every table this script touches.

class StbFile:
    def __init__(self, path, encoding="latin-1"):
        self.path, self.encoding = path, encoding
        raw = open(path, "rb").read()
        if raw[:4] != b"STB1":
            raise ValueError("%s: not an STB1 file" % path)
        self.offset, raw_rows, raw_cols = struct.unpack_from("<III", raw, 4)
        self.rows, self.cols = raw_rows - 1, raw_cols - 1
        self.header = raw[:self.offset]

        f = io.BytesIO(raw)
        f.seek(self.offset)

        def cell():
            n, = struct.unpack("<H", f.read(2))
            return f.read(n)

        self.d = [[cell() for _ in range(self.cols)] for _ in range(self.rows)]
        if f.tell() != len(raw):
            raise ValueError("%s: %d trailing bytes after the cell data"
                             % (path, len(raw) - f.tell()))

    def get(self, r, c):
        return self.d[r][c].decode(self.encoding, "replace")

    def set(self, r, c, val):
        self.d[r][c] = val.encode(self.encoding)

    def key_col(self):
        """What the client reads: STBDATA::value(row, col_count - 1)."""
        return self.cols - 1

    def to_bytes(self):
        out = bytearray(self.header)
        for row in self.d:
            for c in row:
                out += struct.pack("<H", len(c)) + c
        return bytes(out)


# ------------------------------------------------------------------ STL write
#
# ITST01: pstr8 tag | u32 keycount | keycount x (pstr8 key, u32 id) |
#         u32 langcount | langcount x u32 block offset |
#         per block: keycount x u32 entry offset, then the entries back to back.
#
# Entry strings use a 7-bit varint length. The client ignores the per-entry
# offset table and reads the entries sequentially, so key order and entry order
# must stay in lockstep -- which is why entries are appended, never inserted.

def _varint(n):
    out = b""
    while n > 0x7F:
        out += bytes([(n & 0x7F) | 0x80])
        n >>= 7
    return out + bytes([n])


def _read_varint(f):
    n, shift = 0, 0
    while True:
        b = f.read(1)[0]
        n |= (b & 0x7F) << shift
        if b < 0x80:
            return n
        shift += 7


def _vstr_read(f):
    return f.read(_read_varint(f))


def _vstr(b):
    return _varint(len(b)) + b


class StlFile:
    def __init__(self, path):
        self.path = path
        f = io.BytesIO(open(path, "rb").read())
        self.tag = _vstr_read(f)
        if self.tag != b"ITST01":
            raise ValueError("%s: unexpected STL tag %r" % (path, self.tag))
        keycount, = struct.unpack("<I", f.read(4))
        self.keys = []
        for _ in range(keycount):
            k = _vstr_read(f)
            idx, = struct.unpack("<I", f.read(4))
            self.keys.append((k, idx))
        langcount, = struct.unpack("<I", f.read(4))
        langpos = struct.unpack("<%dI" % langcount, f.read(4 * langcount))
        self.langs = []
        for lp in langpos:
            f.seek(lp)
            offsets = struct.unpack("<%dI" % keycount, f.read(4 * keycount))
            entries = []
            for o in offsets:
                f.seek(o)
                entries.append((_vstr_read(f), _vstr_read(f)))
            self.langs.append(entries)

    def has(self, key):
        kb = key.encode("latin-1")
        return any(k == kb for k, _ in self.keys)

    def name(self, key):
        """The name the client would show, or None if the key is absent."""
        kb = key.encode("latin-1")
        for i, (k, _) in enumerate(self.keys):
            if k == kb:
                return self.langs[LANG_USA][i][0].decode("utf-8", "replace")
        return None

    def append(self, key, idx, name, desc):
        if self.has(key):
            raise ValueError("%s: key %s already present" % (self.path, key))
        self.keys.append((key.encode("latin-1"), idx))
        nb, db = name.encode("utf-8"), desc.encode("utf-8")
        for entries in self.langs:
            entries.append((nb, db))

    def remove(self, keys):
        drop = {k.encode("latin-1") for k in keys}
        keep = [i for i, (k, _) in enumerate(self.keys) if k not in drop]
        self.keys = [self.keys[i] for i in keep]
        self.langs = [[e[i] for i in keep] for e in self.langs]

    def to_bytes(self):
        keycount, langcount = len(self.keys), len(self.langs)
        header = _vstr(self.tag) + struct.pack("<I", keycount)
        for k, idx in self.keys:
            header += _vstr(k) + struct.pack("<I", idx)
        header += struct.pack("<I", langcount)
        langpos_at = len(header)
        header += b"\x00" * (4 * langcount)

        body, positions = b"", []
        for entries in self.langs:
            positions.append(len(header) + len(body))
            base = len(header) + len(body) + 4 * keycount
            offsets, blob = [], b""
            for n, d in entries:
                offsets.append(base + len(blob))
                blob += _vstr(n) + _vstr(d)
            body += struct.pack("<%dI" % keycount, *offsets) + blob

        out = bytearray(header + body)
        out[langpos_at:langpos_at + 4 * langcount] = struct.pack(
            "<%dI" % langcount, *positions)
        return bytes(out)


# ------------------------------------------------------------------ selection

def is_mojibake(name):
    """A name destroyed by a lossy re-encode: '?' filler and no real letters."""
    return "?" in name and not re.search(r"[A-Za-z0-9]", name)


def is_real_item(item_type, stb, row):
    """Can a player ever hold this row?

    Mob weapons carry a model and a hit sound but no Type; the split is exact
    (LIST_WEAPON 1001-1153 and LIST_SUBWPN 301-305, and nothing below 1001).
    Rows 0/1 of the armour tables are the Naked/Barefoot avatar placeholders.
    """
    if item_type in WEAPON_TYPES:
        try:
            if int(stb.get(row, COL_TYPE).strip() or 0) <= 0:
                return False
        except ValueError:
            return False
    if item_type in ARMOUR_TYPES and row <= 1:
        return False
    return True


def broken_rows(item_type, stb, stl, prefix):
    """Rows the client would render with an empty name, as (row, kind, key)."""
    kc = stb.key_col()
    out = []
    for r in range(stb.rows):
        if not stb.get(r, 0).strip():
            continue
        if not is_real_item(item_type, stb, r):
            continue
        key = stb.get(r, kc).strip()
        if not key:
            out.append((r, "NOKEY", "%s%03d" % (prefix, r)))
            continue
        shown = stl.name(key)
        if shown is None:
            out.append((r, "NOSTL", key))
        elif not shown.strip():
            out.append((r, "BLANK", key))
    return out


# ------------------------------------------------------- description recovery

def _ascii(s):
    return bool(s) and all(ord(c) < 128 for c in s)


def description_index(rd, base):
    """name(lowercased) -> (dump tag, description), from exact name matches only.

    Row numbers are meaningless across dumps, so nothing is looked up by row: a
    description is adopted only when some dump has an item whose *displayed name*
    is byte-identical to ours.
    """
    idx = {}
    for tag, path, stb_enc, stl_enc in DUMPS:
        f_stb = os.path.join(path, base + ".STB")
        f_stl = os.path.join(path, base + "_S.STL")
        if not (os.path.exists(f_stb) and os.path.exists(f_stl)):
            continue
        try:
            s = rd.Stb(f_stb, stb_enc)
            stl = rd.Stl(f_stl, stl_enc)
            kc = s.key_column()
            if kc is None:
                kc = s.cols - 1
            # Pick whichever language block reads as English in this dump.
            best, best_score = 0, -1
            nlang = len(stl.lang_off) if stl.lang_off else 1
            for li in range(nlang):
                try:
                    block = stl.lang(li)
                except Exception:
                    continue
                score = sum(1 for v in block[:300] if _ascii(v[0]))
                if score > best_score:
                    best, best_score = li, score
            names = {k.decode("latin-1"): v
                     for (k, _), v in zip(stl.keys, stl.lang(best))}
            for r in range(s.rows):
                n0 = s.s(r, 0).strip()
                if not n0:
                    continue
                v = names.get(s.s(r, kc).strip())
                if not v or len(v) < 2:
                    continue
                nm, ds = v[0].strip(), v[1].strip()
                if nm.lower() == n0.lower() and _ascii(ds) and len(ds) > 8:
                    idx.setdefault(n0.lower(), (tag, ds))
        except Exception:
            continue    # a dump we cannot parse is not a reason to fail
    return idx


# ----------------------------------------------------------------------- main

def plan(rd, want_desc):
    """Work out every edit without touching a file."""
    jobs = []
    for item_type, (base, prefix) in sorted(TABLES.items()):
        stb_path = os.path.join(STB_DIR, base + ".STB")
        stl_path = os.path.join(STB_DIR, base + "_S.STL")
        if not (os.path.exists(stb_path) and os.path.exists(stl_path)):
            continue
        stb, stl = StbFile(stb_path), StlFile(stl_path)
        rows = broken_rows(item_type, stb, stl, prefix)
        if not rows:
            continue
        descs = description_index(rd, base) if want_desc else {}

        stb_keys, stb_names, stl_add = {}, {}, []
        for row, kind, key in rows:
            name = stb.get(row, 0).strip()
            existing = stl.name(key)
            if existing and existing.strip():
                # The STL already has the right text and only the STB link was
                # lost (LIST_QUESTITEM 31-40). Adopt it, and repair column 0 if
                # a lossy re-encode destroyed it.
                if is_mojibake(name):
                    stb_names[row] = stb.get(row, 0)
                name = existing.strip()
            else:
                stl_add.append((key, row, name,
                                descs.get(name.lower(), ("", ""))[1]))
            if kind == "NOKEY":
                stb_keys[row] = stb.get(row, stb.key_col())
        jobs.append(dict(type=item_type, base=base, prefix=prefix,
                         stb=stb, stl=stl, rows=rows,
                         stb_keys=stb_keys, stb_names=stb_names,
                         stl_add=stl_add, descs=descs))
    return jobs


def apply(jobs):
    for j in jobs:
        stb, stl = j["stb"], j["stl"]
        for row in j["stb_keys"]:
            stb.set(row, stb.key_col(), "%s%03d" % (j["prefix"], row))
        for row in j["stb_names"]:
            name = stl.name("%s%03d" % (j["prefix"], row))
            stb.set(row, 0, name.strip())
        for key, idx, name, desc in j["stl_add"]:
            stl.append(key, idx, name, desc)


def write(jobs):
    for j in jobs:
        with open(j["stb"].path, "wb") as fh:
            fh.write(j["stb"].to_bytes())
        with open(j["stl"].path, "wb") as fh:
            fh.write(j["stl"].to_bytes())


def report(jobs):
    total = sum(len(j["rows"]) for j in jobs)
    print("%d items resolve to an empty name\n" % total)
    print("  %-16s %6s %6s %6s %8s" % ("table", "total", "NOKEY", "NOSTL", "+desc"))
    for j in jobs:
        nokey = sum(1 for _, k, _ in j["rows"] if k == "NOKEY")
        nostl = len(j["rows"]) - nokey
        withd = sum(1 for _, _, _, d in j["stl_add"] if d)
        print("  %-16s %6d %6d %6d %8d"
              % (j["base"], len(j["rows"]), nokey, nostl, withd))
    print("\n  %d STB key cells written, %d STB names repaired, %d STL entries appended"
          % (sum(len(j["stb_keys"]) for j in jobs),
             sum(len(j["stb_names"]) for j in jobs),
             sum(len(j["stl_add"]) for j in jobs)))
    for j in jobs:
        print("\n  --- %s" % j["base"])
        for key, idx, name, desc in j["stl_add"][:6]:
            print("      %-10s %-34s %s" % (key, name[:34], (desc[:38] + "...") if desc else ""))
        if len(j["stl_add"]) > 6:
            print("      ... and %d more" % (len(j["stl_add"]) - 6))
        for row in sorted(j["stb_names"])[:12]:
            print("      row %-5d STB name repaired from the STL" % row)


def verify(rd):
    """Re-run the client's own resolution and count what still comes back blank."""
    bad = []
    for item_type, (base, prefix) in sorted(TABLES.items()):
        stb_path = os.path.join(STB_DIR, base + ".STB")
        stl_path = os.path.join(STB_DIR, base + "_S.STL")
        if not (os.path.exists(stb_path) and os.path.exists(stl_path)):
            continue
        stb = rd.Stb(stb_path, "latin-1")
        names = rd.Stl(stl_path, "utf-8").by_key(LANG_USA)
        kc = stb.cols - 1
        wrapper = StbFile(stb_path)
        for r in range(stb.rows):
            n0 = stb.s(r, 0).strip()
            if not n0 or not is_real_item(item_type, wrapper, r):
                continue
            key = stb.s(r, kc).strip()
            if not key or key not in names or not names[key][0].strip():
                bad.append((base, r, n0))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--no-descriptions", action="store_true",
                    help="skip the reference-dump description lookup")
    args = ap.parse_args()

    rd = load_reader()

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for base, rec in saved.items():
            stb = StbFile(os.path.join(STB_DIR, base + ".STB"))
            for row, old in rec["stb_keys"].items():
                stb.set(int(row), stb.key_col(), old)
            for row, old in rec["stb_names"].items():
                stb.set(int(row), 0, old)
            stl = StlFile(os.path.join(STB_DIR, base + "_S.STL"))
            stl.remove(rec["stl_added"])
            with open(stb.path, "wb") as fh:
                fh.write(stb.to_bytes())
            with open(stl.path, "wb") as fh:
                fh.write(stl.to_bytes())
        os.remove(SIDECAR)
        print("restored %d tables; sidecar removed" % len(saved))
        return

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the fix has not been applied")
        bad = verify(rd)
        recorded = sum(len(r["stl_added"]) + len(r["stb_keys"]) for r in saved.values())
        print("%d edits recorded across %d tables; %d items still resolve blank"
              % (recorded, len(saved), len(bad)))
        for base, row, name in bad[:20]:
            print("   %-16s row %-5d %s" % (base, row, name))
        sys.exit(1 if bad else 0)

    if saved:
        print("already applied to %d tables -- nothing to do." % len(saved))
        print("re-run with --restore first if you want to redo it.")
        return

    jobs = plan(rd, not args.no_descriptions)
    if not jobs:
        print("nothing to fix")
        return
    report(jobs)
    apply(jobs)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    write(jobs)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({j["base"]: dict(
            stb_keys={str(k): v for k, v in j["stb_keys"].items()},
            stb_names={str(k): v for k, v in j["stb_names"].items()},
            stl_added=[k for k, _, _, _ in j["stl_add"]],
        ) for j in jobs}, fh, indent=1)

    bad = verify(rd)
    if bad:
        print("\nFAILED: %d items still resolve to an empty name" % len(bad))
        for base, row, name in bad[:20]:
            print("   %-16s row %-5d %s" % (base, row, name))
        sys.exit(1)
    print("\nwritten and verified: every real item now resolves to a name")
    print("sidecar: %s" % os.path.relpath(SIDECAR, ROOT))


if __name__ == "__main__":
    main()
