"""Step 2 of the Eldeon rebuild: import EJ04, the Eldeon Clan Field (zone 66).

EJ04 is the third clan field -- we already have Junon (zone 11, JG08) and Luna
(zone 59, LPVP01); Eldeon's was simply absent. QQ-iROSE names zone 66 "Eldeon
Clan Field" and ruff's row carries join trigger "Clan-003" plus ECF_PvPK /
ECF_PvPD, which confirms it.

Note up front: **no reference client has a warp gate leading INTO any clan
field** -- no WARP.STB row in ours, ruff's or QQ's targets zone 11, 59 or 66.
Clan fields are entered through the clan/guild system, which this server does not
implement yet. EJ04 will therefore be reachable only by `/mm 66 <x> <y>` until it
is, exactly like the two clan fields we already ship. The map itself is complete:
72 cells, 105 spawn points, 6 monster types.

What this imports:
  * 3DDATA/Maps/ELDEON/EJ04/     -- 72 map cells, .ZON, lightmaps, minimap
  * LIST_ZONE.STB row 66         -- appended (our table ends at 65)
  * LIST_NPC.STB rows 1597-1599, 1601-1603
  * LIST_NPC.CHR entries for those six
  * WARP.STB row 136             -- EJ04's exit gate

Two deliberate deviations from ruff, both forced:

1. LIST_ZONE column mapping. Ruff's table is 37 columns to our 36; comparing row
   61 shows cols 0-35 align and ruff's col 36 is an appended copy of the zone
   number, which our schema has no slot for -- so only cols 0-35 are copied.
   Ruff also leaves the revive-point columns (31-33) empty and uses its own join
   trigger in col 22; we take ruff's row as-is for the new zone since we have no
   better values, but the same mapping must never be applied to an existing row,
   which would clobber our revive points and PvP triggers.

2. Warp 136's destination. Ruff points it at event position "WARP-EZ01-EJ02",
   which does not exist in our EJ02.ZON (we have "WARP-EJ01-EJ02" and
   "WARP-EJ02-EZ01-6"). Left as ruff has it, the exit gate would resolve to
   nothing and the server would reject the teleport. It is repointed at
   "WARP-EJ01-EJ02", a real arrival point in Forest of Wandering.

Verified before writing this:
  * All 12 PART_NPC.ZSC model indices the six NPCs use are byte-identical
    between our table and ruff's, so their CHR entries import without remapping
    models.
  * All 57 skeleton/motion files they reference already exist in our data/, and
    every path is already present in our CHR string tables -- the CHR merge is
    pure index remapping, nothing is appended.
  * Our rows 1601-1603 are name-only "ButterFly" stubs: no stats, no model, no
    drops, no quest references, and spawned in none of our maps. Overwriting
    them costs nothing. Rows 1597-1599 are entirely empty.

Idempotent, makes a backup under build/ (outside data/, since pack.rs bakes
anything left in the data tree into the .vfs), and verifies after writing.

Afterwards run, to give the zone its on-screen name:
    python scripts/add-zone-name.py --zone 66
(ruff/QQ key the name as LZON069, which this script writes into col 26.)

Then rebake/deploy the VFS and restart the servers -- unlike step 1 this changes
tables the gameserver caches at startup.
"""
import argparse, filecmp, os, shutil, struct, sys, time

OUR_DATA = "data"
DEFAULT_SOURCE = r"C:\Users\Thomas\Desktop\Testclients\ruff\extracted data"

ZONE_STB = os.path.join("3DDATA", "STB", "LIST_ZONE.STB")
NPC_STB = os.path.join("3DDATA", "STB", "LIST_NPC.STB")
WARP_STB = os.path.join("3DDATA", "STB", "WARP.STB")
AI_STB = os.path.join("3DDATA", "STB", "FILE_AI.STB")
NPC_CHR = os.path.join("3DDATA", "NPC", "LIST_NPC.CHR")
MAP_REL = os.path.join("3DDATA", "Maps", "ELDEON", "EJ04")

# NPC_AI_TYPE (LIST_NPC col 16) indexes FILE_AI.STB, whose rows name a .aip.
# The six EJ04 monsters use rows 242-248, which are blank in our table. That is
# not benign: CAI_LIST::Load leaves m_ppAI[i] NULL for a blank row, and
# CAI_LIST::AI_Created range-checks the index but not the pointer (the _ASSERT
# is compiled out in release), so the gameserver null-derefs the moment the
# first one spawns -- which is exactly what happened after the initial import.
NPC_AI_COL = 16

ZONE_ROW = 66
ZONE_COLS = 36                       # ours; ruff has 37, col 36 is a zone-number copy
ZONE_NAME = b"Eldeon Clan Field"     # QQ's wording; ruff says "Eldeon CF"
NPC_ROWS = [1597, 1598, 1599, 1601, 1602, 1603]
NPC_SHARED_COLS = 43                 # ruff has 43; our col 43 (PVP state) is ours to keep
WARP_ROW = 136
WARP_DEST_ZONE = b"63"
WARP_DEST_POS = b"WARP-EJ01-EJ02"    # ruff's "WARP-EZ01-EJ02" does not exist in our EJ02.ZON
WARP_NAME = b"EJ04->EJ02 (Eldeon Clan Field exit)"


# ------------------------------------------------------------------ STB
def stb_read(path):
    raw = open(path, "rb").read()
    off, rows, cols = struct.unpack_from("<III", raw, 4)
    o = off
    data = []
    for _ in range(rows - 1):
        row = []
        for _ in range(cols - 1):
            n, = struct.unpack_from("<H", raw, o); o += 2
            row.append(raw[o:o + n]); o += n
        data.append(row)
    return raw, off, rows, cols, data


def stb_write(path, raw, off, rows, cols, data):
    out = [raw[:off]]
    for row in data:
        for c in row:
            out.append(struct.pack("<H", len(c)) + c)
    open(path, "wb").write(b"".join(out))


def stb_append_row(raw, off, rows, cols, data, cells):
    """append one row: a name goes at the end of the header block, cells at the end"""
    assert len(cells) == cols - 1, (len(cells), cols - 1)
    name = str(rows - 1).encode("ascii")
    name_block = struct.pack("<H", len(name)) + name
    new_raw = (raw[:4] + struct.pack("<III", off + len(name_block), rows + 1, cols)
               + raw[16:off] + name_block)
    return new_raw, off + len(name_block), rows + 1, data + [list(cells)]


# ------------------------------------------------------------------ CHR
def chr_read(path):
    d = open(path, "rb").read()
    o = 0

    def u16():
        nonlocal o
        v, = struct.unpack_from("<H", d, o); o += 2; return v

    def u8():
        nonlocal o
        v = d[o]; o += 1; return v

    def cstr():
        nonlocal o
        e = d.index(b"\0", o)
        s = d[o:e]; o = e + 1; return s

    skel = [cstr() for _ in range(u16())]
    motion = [cstr() for _ in range(u16())]
    effect = [cstr() for _ in range(u16())]
    chars = []
    for _ in range(u16()):
        if not u8():
            chars.append(None); continue
        e = dict(skel=u16(), name=cstr())
        e["models"] = [u16() for _ in range(u16())]
        e["anims"] = [(u16(), u16()) for _ in range(u16())]
        e["effects"] = [(u16(), u16()) for _ in range(u16())]
        chars.append(e)
    # Our LIST_NPC.CHR carries ~2KB past the declared character count, holding
    # further (unreferenced) entries -- the client reads exactly the declared
    # count and stops, so this is dead data. Preserve it verbatim rather than
    # silently truncating the file.
    return skel, motion, effect, chars, d[o:]


def chr_write(path, skel, motion, effect, chars, tail=b""):
    out = []

    def table(lst):
        out.append(struct.pack("<H", len(lst)))
        for s in lst:
            out.append(s + b"\0")

    table(skel); table(motion); table(effect)
    out.append(struct.pack("<H", len(chars)))
    for c in chars:
        if c is None:
            out.append(b"\0"); continue
        out.append(b"\1")
        out.append(struct.pack("<H", c["skel"]))
        out.append(c["name"] + b"\0")
        out.append(struct.pack("<H", len(c["models"])))
        for m in c["models"]:
            out.append(struct.pack("<H", m))
        out.append(struct.pack("<H", len(c["anims"])))
        for t, i in c["anims"]:
            out.append(struct.pack("<HH", t, i))
        out.append(struct.pack("<H", len(c["effects"])))
        for b, i in c["effects"]:
            out.append(struct.pack("<HH", b, i))
    out.append(tail)
    open(path, "wb").write(b"".join(out))


def index_of(table, value, label):
    low = {s.lower(): i for i, s in enumerate(table)}
    i = low.get(value.lower())
    if i is None:
        raise SystemExit(f"{label} {value!r} is not in our CHR string table "
                         f"(pre-flight said it would be; refusing to guess)")
    return i


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the STB/CHR writers are byte-faithful, then exit")
    args = ap.parse_args()

    root = os.path.join(args.root, OUR_DATA)

    if args.selftest:
        import tempfile
        tmp = tempfile.mkdtemp()
        ok = True
        sk, mo, ef, ch, tail = chr_read(os.path.join(root, NPC_CHR))
        out = os.path.join(tmp, "a.chr")
        chr_write(out, sk, mo, ef, ch, tail)
        same = filecmp.cmp(os.path.join(root, NPC_CHR), out, shallow=False)
        print(f"  LIST_NPC.CHR round-trip: {'OK' if same else 'FAIL'}")
        ok = ok and same
        for rel in (ZONE_STB, NPC_STB, WARP_STB):
            p = os.path.join(root, rel)
            raw, off, rows, cols, data = stb_read(p)
            o2 = os.path.join(tmp, os.path.basename(rel))
            stb_write(o2, raw, off, rows, cols, data)
            same = filecmp.cmp(p, o2, shallow=False)
            print(f"  {os.path.basename(rel):16} round-trip: {'OK' if same else 'FAIL'}")
            ok = ok and same
        shutil.rmtree(tmp, ignore_errors=True)
        print("PASS" if ok else "FAIL")
        return 0 if ok else 1
    src = args.source
    for p in (root, src, os.path.join(src, MAP_REL)):
        if not os.path.isdir(p):
            raise SystemExit(f"not found: {p}")

    actions = []

    # --- maps
    dst_map = os.path.join(root, MAP_REL)
    src_map = os.path.join(src, MAP_REL)
    files = []
    for r, _, fs in os.walk(src_map):
        for f in fs:
            if f.lower() == "thumbs.db":
                continue
            p = os.path.join(r, f)
            files.append(os.path.relpath(p, src_map))
    todo = [f for f in files
            if not os.path.exists(os.path.join(dst_map, f))
            or not filecmp.cmp(os.path.join(dst_map, f), os.path.join(src_map, f), shallow=False)]
    actions.append(f"map cells: {len(files)} files in source, {len(todo)} to copy")

    # --- zone row
    zraw, zoff, zrows, zcols, zdata = stb_read(os.path.join(root, ZONE_STB))
    _, _, _, srcols, srdata = stb_read(os.path.join(src, ZONE_STB))
    zone_needed = ZONE_ROW >= len(zdata) or not zdata[ZONE_ROW][1]
    zone_cells = list(srdata[ZONE_ROW][:ZONE_COLS])
    zone_cells[0] = ZONE_NAME
    actions.append(f"LIST_ZONE: rows={len(zdata)}; row {ZONE_ROW} "
                   + ("APPEND" if zone_needed else "already present"))

    # --- npc rows
    nraw, noff, nrows, ncols, ndata = stb_read(os.path.join(root, NPC_STB))
    _, _, _, sncols, sndata = stb_read(os.path.join(src, NPC_STB))
    npc_todo = [i for i in NPC_ROWS
                if list(ndata[i][:NPC_SHARED_COLS]) != list(sndata[i][:NPC_SHARED_COLS])]
    actions.append(f"LIST_NPC: {len(npc_todo)} of {len(NPC_ROWS)} rows to update "
                   f"(cols 0-{NPC_SHARED_COLS-1}, our col {NPC_SHARED_COLS} kept)")

    # --- AI rows + their .aip files (NPC col 16 -> FILE_AI.STB -> 3DDATA/AI/*.aip)
    araw, aoff, arows, acols, adata = stb_read(os.path.join(root, AI_STB))
    _, _, _, sacols, sadata = stb_read(os.path.join(src, AI_STB))
    ai_needed, aip_needed = [], []
    for i in NPC_ROWS:
        v = sndata[i][NPC_AI_COL]
        if not v.isdigit() or int(v) <= 0:
            continue
        idx = int(v)
        if idx >= len(sadata) or not sadata[idx][0]:
            raise SystemExit(f"source FILE_AI.STB row {idx} (NPC {i}) is empty")
        want = sadata[idx][0]
        if idx >= len(adata):
            raise SystemExit(f"our FILE_AI.STB has no row {idx}")
        if adata[idx][0] != want:
            ai_needed.append((idx, want))
        rel = want.decode("latin-1").replace(chr(92), os.sep)
        if not os.path.exists(os.path.join(root, rel)):
            aip_needed.append(rel)
    actions.append(f"FILE_AI: {len(ai_needed)} row(s) to set, {len(aip_needed)} .aip file(s) to copy")

    # --- warp row
    wraw, woff, wrows, wcols, wdata = stb_read(os.path.join(root, WARP_STB))
    warp_needed = wdata[WARP_ROW][1] != WARP_DEST_ZONE or wdata[WARP_ROW][2] != WARP_DEST_POS
    actions.append(f"WARP row {WARP_ROW}: " + ("set" if warp_needed else "already set"))

    # --- chr entries
    sk, mo, ef, ch, chr_tail = chr_read(os.path.join(root, NPC_CHR))
    ssk, smo, sef, sch, _ = chr_read(os.path.join(src, NPC_CHR))
    new_entries = {}
    for i in NPC_ROWS:
        s = sch[i]
        if s is None:
            raise SystemExit(f"source CHR has no entry for NPC {i}")
        new_entries[i] = dict(
            skel=index_of(sk, ssk[s["skel"]], "skeleton"),
            name=s["name"],
            models=list(s["models"]),
            anims=[(t, index_of(mo, smo[a], "motion")) for t, a in s["anims"]],
            effects=[(b, index_of(ef, sef[e], "effect")) for b, e in s["effects"]],
        )
    chr_todo = [i for i in NPC_ROWS if ch[i] != new_entries[i]]
    actions.append(f"LIST_NPC.CHR: {len(chr_todo)} of {len(NPC_ROWS)} entries to write "
                   f"(no new skeleton/motion/effect strings needed)")

    print("plan:")
    for a in actions:
        print("   " + a)

    if not (todo or zone_needed or npc_todo or warp_needed or chr_todo
            or ai_needed or aip_needed):
        print("\nnothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    # --- backup
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(args.root, "build", f"ej04-import-backup-{stamp}")
    os.makedirs(backup, exist_ok=True)
    for rel in (ZONE_STB, NPC_STB, WARP_STB, AI_STB, NPC_CHR):
        dst = os.path.join(backup, os.path.basename(rel))
        shutil.copy2(os.path.join(root, rel), dst)
    print(f"\nbacked up 4 tables -> {backup}")

    # --- write maps
    for f in todo:
        s, d = os.path.join(src_map, f), os.path.join(dst_map, f)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(s, d)
    print(f"copied {len(todo)} map file(s)")

    # --- write zone row
    if zone_needed:
        while len(zdata) < ZONE_ROW:
            zraw, zoff, zrows, zdata = stb_append_row(zraw, zoff, zrows, zcols, zdata,
                                                      [b""] * (zcols - 1))
        zraw, zoff, zrows, zdata = stb_append_row(zraw, zoff, zrows, zcols, zdata, zone_cells)
        stb_write(os.path.join(root, ZONE_STB), zraw, zoff, zrows, zcols, zdata)
        print(f"appended LIST_ZONE row {ZONE_ROW}")

    # --- write npc rows
    if npc_todo:
        for i in npc_todo:
            for c in range(NPC_SHARED_COLS):
                ndata[i][c] = sndata[i][c]
        stb_write(os.path.join(root, NPC_STB), nraw, noff, nrows, ncols, ndata)
        print(f"updated LIST_NPC rows {npc_todo}")

    # --- write AI rows + copy .aip files
    for rel in aip_needed:
        s_, d_ = os.path.join(src, rel), os.path.join(root, rel)
        if not os.path.exists(s_):
            raise SystemExit(f"source .aip missing: {s_}")
        os.makedirs(os.path.dirname(d_), exist_ok=True)
        shutil.copy2(s_, d_)
    if aip_needed:
        print(f"copied {len(aip_needed)} .aip file(s)")
    if ai_needed:
        for idx, want in ai_needed:
            adata[idx][0] = want
        stb_write(os.path.join(root, AI_STB), araw, aoff, arows, acols, adata)
        print(f"set FILE_AI rows {[i for i, _ in ai_needed]}")

    # --- write warp row
    if warp_needed:
        wdata[WARP_ROW][0] = WARP_NAME
        wdata[WARP_ROW][1] = WARP_DEST_ZONE
        wdata[WARP_ROW][2] = WARP_DEST_POS
        stb_write(os.path.join(root, WARP_STB), wraw, woff, wrows, wcols, wdata)
        print(f"set WARP row {WARP_ROW} -> zone {WARP_DEST_ZONE.decode()} "
              f"{WARP_DEST_POS.decode()}")

    # --- write chr
    if chr_todo:
        for i in chr_todo:
            ch[i] = new_entries[i]
        chr_write(os.path.join(root, NPC_CHR), sk, mo, ef, ch, chr_tail)
        print(f"wrote LIST_NPC.CHR entries {chr_todo}")

    # --- verify
    print("\nverifying")
    _, _, _, _, zv = stb_read(os.path.join(root, ZONE_STB))
    if zv[ZONE_ROW][1] != zone_cells[1]:
        raise SystemExit("VERIFY FAILED: zone row 66 path wrong")
    _, _, _, _, nv = stb_read(os.path.join(root, NPC_STB))
    for i in NPC_ROWS:
        if list(nv[i][:NPC_SHARED_COLS]) != list(sndata[i][:NPC_SHARED_COLS]):
            raise SystemExit(f"VERIFY FAILED: NPC row {i}")
    _, _, _, _, wv = stb_read(os.path.join(root, WARP_STB))
    if wv[WARP_ROW][1] != WARP_DEST_ZONE:
        raise SystemExit("VERIFY FAILED: warp row")
    vsk, vmo, vef, vch, vtail = chr_read(os.path.join(root, NPC_CHR))
    if vtail != chr_tail:
        raise SystemExit("VERIFY FAILED: CHR trailing block was not preserved")
    for i in NPC_ROWS:
        if vch[i] != new_entries[i]:
            raise SystemExit(f"VERIFY FAILED: CHR entry {i}")
        c = vch[i]
        for p in [vsk[c["skel"]]] + [vmo[a] for _, a in c["anims"]]:
            if not os.path.exists(os.path.join(root, p.decode("latin-1").replace("\\", "/"))):
                raise SystemExit(f"VERIFY FAILED: NPC {i} references missing file {p}")
    _, _, _, _, av = stb_read(os.path.join(root, AI_STB))
    for i in NPC_ROWS:
        v = nv[i][NPC_AI_COL]
        if not v.isdigit() or int(v) <= 0:
            continue
        idx = int(v)
        path = av[idx][0]
        if not path:
            raise SystemExit(f"VERIFY FAILED: FILE_AI row {idx} (NPC {i}) still empty")
        full = os.path.join(root, path.decode("latin-1").replace(chr(92), os.sep))
        if not os.path.exists(full):
            raise SystemExit(f"VERIFY FAILED: .aip missing for NPC {i}: {path}")
    bad = [f for f in todo
           if not filecmp.cmp(os.path.join(dst_map, f), os.path.join(src_map, f), shallow=False)]
    if bad:
        raise SystemExit(f"VERIFY FAILED: {len(bad)} map file(s) differ after copy")
    print("verified: zone row, 6 NPC rows, 6 CHR entries, 6 AI rows + .aip files, "
          "warp row, map files (all referenced files present)")
    print(f"\nbackup: {backup}")
    print("next: python scripts/add-zone-name.py --zone 66   (adds the LZON069 string)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
