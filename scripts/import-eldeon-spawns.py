"""Step 3 of the Eldeon rebuild: import a zone's monster spawns from a reference
client, without disturbing anything else in the map.

Spawns live in the map .IFO's LUMP_TERRAIN_REGEN (type 8), alongside the deco,
construction, collision, event and warp objects. Copying whole .IFO files would
drag all of those along -- comparing our EJT01 against ruff's, the OBJECT lump
differs in 40 of 42 cells, COLLISION in 23, CNST in 8, EVENT_OBJECT in 7, MOB in
6 and WARP in 1 -- so this splices **only** lump 8 and rewrites the container.

EJT01 (Xita Refuge) measured, ours vs ruff:
    ours  489 regen entries, 22 NPC types,  547 mobs
    ruff 1771 regen entries, 17 NPC types, 1799 mobs

Ruff is ~3.3x denser but narrower, and three of the species it drops live
nowhere else in our world: 502 Warbiz, 571 Rotten Tree, 572 Rotten Elemental --
and 502 is referenced by PVP10.QSD / PVP13-01.QSD. Replacing outright would
delete them from the game and break a quest, so by default the script re-appends
our own regen points for exactly those species, with their mob lists filtered
down so nothing else is duplicated (+53 mobs, about +3%). Pass --pure to take the
reference's set verbatim instead.

Safe because the terrain matches: every EJT01 .HIM differs from ruff's only in
float bit patterns (max height delta 0.000) and the trailing AABB block, so the
heightfield is numerically identical and regen coordinates transfer as-is.

Pre-flight refuses to write unless every NPC id in the resulting spawn set
resolves in our LIST_NPC.STB *and* its NPC_AI_TYPE row in FILE_AI.STB names a
.aip that exists. That check exists because importing NPC rows without their AI
crashed the gameserver during step 2: CAI_LIST::Load leaves m_ppAI[i] NULL for a
blank row and CAI_LIST::AI_Created dereferenced it.

Backup goes to build/ (outside data/, which pack.rs would otherwise bake into the
.vfs). --selftest proves the container rewrite is byte-faithful, --dry-run
previews. Idempotent. Client and server both read this, so rebake the VFS and
restart the servers afterwards.
"""
import argparse, glob, os, shutil, struct, sys, time

OUR_DATA = "data"
DEFAULT_SOURCE = r"C:\Users\Thomas\Desktop\Testclients\ruff\extracted data"
MAPS = os.path.join("3DDATA", "Maps", "ELDEON")
NPC_STB = os.path.join("3DDATA", "STB", "LIST_NPC.STB")
AI_STB = os.path.join("3DDATA", "STB", "FILE_AI.STB")
LUMP_REGEN = 8
NPC_AI_COL = 16

# species the reference drops that exist nowhere else in our world
KEEP_SPECIES = {502, 571, 572}


class R:
    def __init__(self, d):
        self.d, self.o = d, 0

    def i16(self):
        v, = struct.unpack_from("<h", self.d, self.o); self.o += 2; return v

    def i32(self):
        v, = struct.unpack_from("<i", self.d, self.o); self.o += 4; return v

    def u8(self):
        v = self.d[self.o]; self.o += 1; return v

    def take(self, n):
        s = self.d[self.o:self.o + n]; self.o += n; return s

    def bstr(self):
        return self.take(self.u8())


def put_bstr(s):
    return bytes([len(s)]) + s


# ------------------------------------------------------------------ regen lump
def parse_regen(buf, start, end):
    """decode lump 8; must consume exactly [start,end) or we do not understand it"""
    r = R(buf); r.o = start
    count = r.i32()
    out = []
    for _ in range(count):
        name = r.bstr()
        fixed = r.take(2 + 2 + 4 + 4 + 4 + 4 + 16 + 12 + 12)
        point_name = r.bstr()
        lists = []
        for _ in range(2):                       # basic list, then tactics list
            n = r.i32()
            entries = []
            for _ in range(n):
                nm = r.bstr()
                idx = r.i32(); cnt = r.i32()
                entries.append((nm, idx, cnt))
            lists.append(entries)
        tail = r.take(16)                        # interval, limit, range, tactic point
        out.append(dict(name=name, fixed=fixed, point=point_name,
                        basic=lists[0], tactics=lists[1], tail=tail))
    if r.o != end:
        raise ValueError(f"regen lump: parsed to {r.o}, block ends at {end}")
    return out


def build_regen(points):
    out = [struct.pack("<i", len(points))]
    for p in points:
        out.append(put_bstr(p["name"]))
        out.append(p["fixed"])
        out.append(put_bstr(p["point"]))
        for key in ("basic", "tactics"):
            out.append(struct.pack("<i", len(p[key])))
            for nm, idx, cnt in p[key]:
                out.append(put_bstr(nm) + struct.pack("<ii", idx, cnt))
        out.append(p["tail"])
    return b"".join(out)


def species(point):
    return {idx for _, idx, cnt in point["basic"] + point["tactics"] if idx > 0 and cnt > 0}


def filter_point(point, keep):
    p = dict(point)
    p["basic"] = [e for e in point["basic"] if e[1] in keep and e[2] > 0]
    p["tactics"] = [e for e in point["tactics"] if e[1] in keep and e[2] > 0]
    return p


# ------------------------------------------------------------------ ifo
def read_ifo(path):
    buf = open(path, "rb").read()
    r = R(buf)
    n = r.i32()
    lumps = [(r.i32(), r.i32()) for _ in range(n)]
    header_end = r.o
    offs = [o for _, o in lumps]
    if offs != sorted(offs) or (offs and offs[0] != header_end):
        raise ValueError(f"{path}: unexpected lump layout")
    bounds = [(t, o, lumps[i + 1][1] if i + 1 < len(lumps) else len(buf))
              for i, (t, o) in enumerate(lumps)]
    return buf, bounds


def write_ifo(path, buf, bounds, replacement):
    blocks = [replacement if t == LUMP_REGEN else buf[o:e] for t, o, e in bounds]
    n = len(bounds)
    out = [struct.pack("<i", n)]
    cur = 4 + 8 * n
    for (t, _, _), blk in zip(bounds, blocks):
        out.append(struct.pack("<ii", t, cur))
        cur += len(blk)
    out.extend(blocks)
    data = b"".join(out)
    open(path, "wb").write(data)
    return data


# ------------------------------------------------------------------ tables
def read_stb(path):
    d = open(path, "rb").read()
    off, rows, cols = struct.unpack_from("<III", d, 4)
    o = off
    data = []
    for _ in range(rows - 1):
        row = []
        for _ in range(cols - 1):
            n, = struct.unpack_from("<H", d, o); o += 2
            row.append(d[o:o + n]); o += n
        data.append(row)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--zone", default="EJT01", help="Eldeon zone folder (default EJT01)")
    ap.add_argument("--root", default=".")
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--pure", action="store_true",
                    help="take the reference spawn set verbatim, dropping species "
                         "that exist nowhere else")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    root = os.path.join(args.root, OUR_DATA)
    ours_dir = os.path.join(root, MAPS, args.zone)
    src_dir = os.path.join(args.source, MAPS, args.zone)
    for p in (ours_dir, src_dir):
        if not os.path.isdir(p):
            raise SystemExit(f"not found: {p}")

    cells = sorted({os.path.basename(p).lower(): os.path.basename(p)
                    for p in glob.glob(ours_dir + "/*.IFO")
                    + glob.glob(ours_dir + "/*.ifo")}.values())

    if args.selftest:
        ok = True
        for c in cells:
            buf, bounds = read_ifo(os.path.join(ours_dir, c))
            off, end = next(((o, e) for t, o, e in bounds if t == LUMP_REGEN), (None, None))
            if off is None:
                continue
            pts = parse_regen(buf, off, end)
            if build_regen(pts) != buf[off:end]:
                print(f"  {c}: regen round-trip FAIL"); ok = False
            blocks = [buf[o:e] for _, o, e in bounds]
            n = len(bounds)
            rebuilt = [struct.pack("<i", n)]
            cur = 4 + 8 * n
            for (t, _, _), blk in zip(bounds, blocks):
                rebuilt.append(struct.pack("<ii", t, cur)); cur += len(blk)
            rebuilt.extend(blocks)
            if b"".join(rebuilt) != buf:
                print(f"  {c}: container round-trip FAIL"); ok = False
        print(f"  {len(cells)} cells checked")
        print("PASS" if ok else "FAIL")
        return 0 if ok else 1

    # --- build the new spawn set per cell
    plan, new_species, kept = {}, set(), 0
    for c in cells:
        sp = os.path.join(src_dir, c)
        if not os.path.exists(sp):
            continue
        sbuf, sbounds = read_ifo(sp)
        soff, send = next(((o, e) for t, o, e in sbounds if t == LUMP_REGEN), (None, None))
        src_pts = parse_regen(sbuf, soff, send) if soff is not None else []

        obuf, obounds = read_ifo(os.path.join(ours_dir, c))
        ooff, oend = next(((o, e) for t, o, e in obounds if t == LUMP_REGEN), (None, None))
        our_pts = parse_regen(obuf, ooff, oend) if ooff is not None else []

        pts = list(src_pts)
        if not args.pure:
            src_species = {s for p in src_pts for s in species(p)}
            keep = KEEP_SPECIES - src_species
            for p in our_pts:
                if species(p) & keep:
                    f = filter_point(p, keep)
                    if f["basic"] or f["tactics"]:
                        pts.append(f); kept += 1
        for p in pts:
            new_species |= species(p)
        plan[c] = (obuf, obounds, pts)

    # --- pre-flight: every spawned NPC must resolve, AI included
    npc = read_stb(os.path.join(root, NPC_STB))
    ai = read_stb(os.path.join(root, AI_STB))
    bad = []
    for i in sorted(new_species):
        if i >= len(npc) or not npc[i][0]:
            bad.append(f"NPC {i}: no row in LIST_NPC.STB"); continue
        v = npc[i][NPC_AI_COL].decode("latin-1")
        if v.isdigit() and int(v) > 0:
            j = int(v)
            if j >= len(ai) or not ai[j][0]:
                bad.append(f"NPC {i} ({npc[i][0].decode('latin-1')}): FILE_AI row {j} is blank")
            else:
                rel = ai[j][0].decode("latin-1").replace(chr(92), os.sep)
                if not os.path.exists(os.path.join(root, rel)):
                    bad.append(f"NPC {i}: .aip missing ({ai[j][0].decode('latin-1')})")

    total_pts = sum(len(p) for _, _, p in plan.values())
    total_mobs = sum(cnt for _, _, pts in plan.values()
                     for p in pts for _, idx, cnt in p["basic"] + p["tactics"] if idx > 0)
    print(f"zone {args.zone}: {len(plan)} cells")
    print(f"   new spawn set: {total_pts} regen points, {len(new_species)} NPC types, "
          f"{total_mobs} mobs" + ("" if args.pure else f"  (incl. {kept} carried-over points)"))
    print(f"   pre-flight: {len(bad)} unresolved NPC/AI reference(s)")
    for b in bad[:10]:
        print(f"      !! {b}")
    if bad:
        raise SystemExit("refusing to write")

    changed = [c for c, (buf, bounds, pts) in plan.items()
               if build_regen(pts) != buf[next(o for t, o, e in bounds if t == LUMP_REGEN):
                                          next(e for t, o, e in bounds if t == LUMP_REGEN)]]
    print(f"   cells whose regen lump changes: {len(changed)}")
    if not changed:
        print("\nnothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(args.root, "build", f"spawns-{args.zone}-backup-{stamp}")
    os.makedirs(backup, exist_ok=True)
    for c in cells:
        shutil.copy2(os.path.join(ours_dir, c), os.path.join(backup, c))
    print(f"\nbacked up {len(cells)} .IFO -> {backup}")

    for c in changed:
        buf, bounds, pts = plan[c]
        write_ifo(os.path.join(ours_dir, c), buf, bounds, build_regen(pts))
    print(f"rewrote {len(changed)} cell(s)")

    # --- verify
    seen_species, seen_pts = set(), 0
    for c in cells:
        buf, bounds = read_ifo(os.path.join(ours_dir, c))
        off, end = next(((o, e) for t, o, e in bounds if t == LUMP_REGEN), (None, None))
        if off is None:
            continue
        pts = parse_regen(buf, off, end)
        seen_pts += len(pts)
        for p in pts:
            seen_species |= species(p)
        # every other lump must be byte-identical to the backup
        b0, bd0 = read_ifo(os.path.join(backup, c))
        for (t, o, e), (t2, o2, e2) in zip(bounds, bd0):
            if t != t2:
                raise SystemExit(f"VERIFY FAILED: {c} lump order changed")
            if t != LUMP_REGEN and buf[o:e] != b0[o2:e2]:
                raise SystemExit(f"VERIFY FAILED: {c} lump {t} changed unexpectedly")
    if seen_pts != total_pts or seen_species != new_species:
        raise SystemExit("VERIFY FAILED: spawn set not as planned after write")
    print(f"verified: {seen_pts} regen points, {len(seen_species)} NPC types, "
          f"all other lumps byte-identical")
    print(f"backup: {backup}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
