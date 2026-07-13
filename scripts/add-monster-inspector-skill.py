"""Append the "Monster Inspector" dummy skill to LIST_SKILL.STB + LIST_SKILL_S.STL.

The skill is a client-side "create window" skill (SKILL_TYPE 2) with window type
(SKILL_POWER, game col 9) = 50, which the client maps to the Monster Inspector
panel (see src/client/gamecommon/skill.cpp SKILL_WINDOW_MONSTER_INSPECTOR).
Using it never sends a packet, so it can never aggro the target.

Row layout written (game columns, i.e. excluding the root/ID column -- the
file column is game column + 1; see STBDATA::load which skips the header row
and the first column):
  col0  name              "Monster Inspector" (informational only)
  col1  1lev index        <own row>  (skill has a single level)
  col2  skill level       1
  col3  need skill point  0          (free to learn)
  col4  tab type          1
  col5  SKILL_TYPE        2          (SKILL_CREATE_WINDOW -- no server packet)
  col6  distance          3000       (cosmetic; window skills don't range-check)
  col7  target filter     4          (SKILL_TARGET_FILTER_MOB: monsters only)
  col9  SKILL_POWER       50         (window type -> Monster Inspector)
  col51 icon              148        (placeholder craft icon; the real icon is
                                      added afterwards with add-skill-icon.py:
                                      `python scripts/add-skill-icon.py
                                       monsterinspectoricon.png --skill-row <row>`)
  col86 string key        LSkill<row> (LIST_SKILL_S.STL entry)
All other columns stay empty = 0 = "no requirement / no effect", which passes
Skill_LearnCondition (job/union/skill/ability checks all treat 0 as pass).

Learn it in game with the GM command:  /add skill <row>
(The row number is printed by this script. GM access level 2048 / RIGHT_MASTER;
the SKILL branch lives in Cheat_add, gated by B_Cheater(). "/set skill" does NOT
exist -- Cheat_set has no SKILL branch and fails silently.)

Idempotent: if a type-2 row with power 50 already exists, nothing is written.
Makes .bak backups. Use --dry-run to preview.

After running: restart servers, rebake/deploy client VFS data.
"""
import argparse, io, os, shutil, struct, sys

OURS = "data"
STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
STL_REL = r"3DDATA\STB\LIST_SKILL_S.STL"

SKILL_NAME = b"Monster Inspector"
SKILL_DESC = (b"Inspect the targeted monster: level, HP, combat stats and item drops. "
              b"Using this skill never provokes the monster.")
WINDOW_TYPE = b"50"
ICON = b"148"


# ---------------------------------------------------------------- STB
def stb_read(path):
    with open(path, "rb") as fh:
        d = fh.read()
    f = io.BytesIO(d)
    f.read(4)
    offset, rows, cols = struct.unpack("<III", f.read(12))
    f.seek(offset)
    def pstr():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)
    data = [[pstr() for _ in range(cols - 1)] for _ in range(rows - 1)]
    return d, offset, rows, cols, data


def stb_append_row(path, row_cells, dry):
    d, offset, rows, cols, _ = stb_read(path)
    assert len(row_cells) == cols - 1, (len(row_cells), cols - 1)
    new_id = rows - 1
    row_name = str(new_id).encode("ascii")
    name_block = struct.pack("<H", len(row_name)) + row_name
    cells = b"".join(struct.pack("<H", len(c)) + c for c in row_cells)
    out = (d[0:4]
           + struct.pack("<III", offset + len(name_block), rows + 1, cols)
           + d[16:offset] + name_block + d[offset:] + cells)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out)
    return new_id


# ---------------------------------------------------------------- STL
def read_varint(f):
    n = 0; shift = 0
    while True:
        b = f.read(1)[0]
        n |= (b & 0x7F) << shift
        if b < 0x80:
            return n
        shift += 7


def write_varint(n):
    out = b""
    while n > 0x7F:
        out += bytes([(n & 0x7F) | 0x80])
        n >>= 7
    return out + bytes([n])


def vstr_read(f):
    return f.read(read_varint(f))


def vstr_write(b):
    return write_varint(len(b)) + b


def stl_read(path):
    with open(path, "rb") as fh:
        f = io.BytesIO(fh.read())
    typ = vstr_read(f)
    assert typ == b"ITST01", typ
    keycount, = struct.unpack("<I", f.read(4))
    keys = []
    for _ in range(keycount):
        k = vstr_read(f)
        idx, = struct.unpack("<I", f.read(4))
        keys.append((k, idx))
    langcount, = struct.unpack("<I", f.read(4))
    langpos = struct.unpack("<%dI" % langcount, f.read(4 * langcount))
    langs = []
    for lp in langpos:
        f.seek(lp)
        offsets = struct.unpack("<%dI" % keycount, f.read(4 * keycount))
        entries = []
        for o in offsets:
            f.seek(o)
            entries.append((vstr_read(f), vstr_read(f)))
        langs.append(entries)
    return keys, langs


def stl_append(path, new_key, new_idx, name, desc, dry):
    keys, langs = stl_read(path)
    if any(k == new_key for k, _ in keys):
        print("STL key %s already exists, skipping STL append" % new_key.decode())
        return
    keys.append((new_key, new_idx))
    for entries in langs:
        entries.append((name, desc))
    keycount, langcount = len(keys), len(langs)

    header = vstr_write(b"ITST01") + struct.pack("<I", keycount)
    for k, idx in keys:
        header += vstr_write(k) + struct.pack("<I", idx)
    header += struct.pack("<I", langcount)
    langpos_at = len(header)
    header += b"\x00" * (4 * langcount)

    body = b""
    lang_positions = []
    for entries in langs:
        lang_positions.append(len(header) + len(body))
        base = len(header) + len(body) + 4 * keycount
        offsets, blob = [], b""
        for n, ds in entries:
            offsets.append(base + len(blob))
            blob += vstr_write(n) + vstr_write(ds)
        body += struct.pack("<%dI" % keycount, *offsets) + blob

    out = bytearray(header + body)
    out[langpos_at:langpos_at + 4 * langcount] = struct.pack("<%dI" % langcount, *lang_positions)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(bytes(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    stb_path = os.path.join(OURS, STB_REL)
    stl_path = os.path.join(OURS, STL_REL)
    for p in (stb_path, stl_path):
        if not os.path.exists(p):
            sys.exit("missing %s (run from the repo root)" % p)

    _, _, rows, cols, data = stb_read(stb_path)
    ncols = cols - 1

    # idempotency: window type 50 must be unique
    for i, row in enumerate(data):
        if len(row) > 9 and row[5] == b"2" and row[9] == WINDOW_TYPE:
            print("Monster Inspector skill already present at row %d, nothing to do." % i)
            print("Learn it in game with: /add skill %d" % i)
            return

    new_id = rows - 1
    key = ("LSkill%d" % new_id).encode("ascii")

    cells = [b""] * ncols
    cells[0] = SKILL_NAME
    cells[1] = str(new_id).encode("ascii")
    cells[2] = b"1"
    cells[3] = b"0"
    cells[4] = b"1"
    cells[5] = b"2"
    cells[6] = b"3000"
    cells[7] = b"4"
    cells[9] = WINDOW_TYPE
    cells[51] = ICON
    cells[86] = key

    if not args.dry_run:
        shutil.copyfile(stb_path, stb_path + ".bak")
        shutil.copyfile(stl_path, stl_path + ".bak")

    got_id = stb_append_row(stb_path, cells, args.dry_run)
    assert got_id == new_id
    stl_append(stl_path, key, new_id, SKILL_NAME, SKILL_DESC, args.dry_run)

    if not args.dry_run:
        # verify
        _, _, vrows, _, vdata = stb_read(stb_path)
        assert vrows == rows + 1
        assert vdata[new_id][9] == WINDOW_TYPE and vdata[new_id][86] == key
        vkeys, vlangs = stl_read(stl_path)
        assert any(k == key for k, _ in vkeys)
        print("verified: STB row + STL entry present")

    print("%sMonster Inspector skill row = %d (skill id)" %
          ("[dry-run] " if args.dry_run else "", new_id))
    print("Learn it in game with: /add skill %d" % new_id)


if __name__ == "__main__":
    main()
