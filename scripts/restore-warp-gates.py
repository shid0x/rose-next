"""Restore the missing Eldeon zone-to-zone warp gates in the map .IFO files.

Symptom: on Eldeon you cannot walk from one zone to another. Arriving in Refuge
Xita (EJT01, zone 61) leaves you stuck in town, and Forest of Wandering / Marsh of
Ghosts / Sikuku Underground Prison have no way back.

Cause: nothing is wrong with the warp *tables*. `WARP.STB` already has correct
rows for every Eldeon link (126-135: destination zone + destination event-position
name), and every destination event position exists in the matching `.ZON`
LUMP_EVENT_OBJECT block. What is missing is the trigger object the player walks
into: an entry in the map `.IFO`'s LUMP_TERRAIN_WARP (type 10) block.

  client  CMAP::ReadObjINFO case LUMP_TERRAIN_WARP -> CObjectMANAGER::Add_WARP
          builds an (invisible) SPECIAL_WARP_OBJECT trigger and tags it with
          warp_id + WARP_OBJECT_INDEX_OFFSET. Walking into it makes
          CWarpObjectActionProcessor::ProcessChain send cli_TELEPORT_REQ.
  server  classUSER::Recv_cli_TELEPORT_REQ looks the id up in WARP.STB and
          resolves the destination through g_pZoneLIST->Get_EventPOS.

The server never reads the .IFO, so this is purely client-side map data. Junon and
Lunar are fully wired, which is why warping works everywhere else.

Our Eldeon maps instead carry *dead* LUMP_TERRAIN_EVENT_OBJECT (type 12) entries
at roughly the same spots -- visible `warpbox` models (EVENT_OBJECT.STB row 10)
whose trigger names ("FOW-Shadi", "Marsh-Shady", "PrisonToFOW", "FOWtoPRI") appear
nowhere in the quest data, so walking into them does nothing. All three reference
dumps (QQ-iROSE, RoseZA test client, titanRose) have the warp trigger and no box,
so this script removes the dead box and restores the trigger.

Warp objects are copied **verbatim** out of a reference client's .IFO, so
position, rotation and scale are exactly retail rather than guessed.

Restored (identical file + warp id in all three references):
  EJT01/32_31.IFO  warp 126 -> Shady Jungle          (leave town, 11 o'clock)
  EJT01/36_31.IFO  warp 127 -> Shady Jungle          (leave town, 1 o'clock)
  EJ02 /38_32.IFO  warp 131 -> Shady Jungle          (- dead box "FOW-Shadi")
  EJ02 /33_37.IFO  warp 134 -> Sikuku Underground Pr.(- dead box "FOWtoPRI")
  EJ03 /31_31.IFO  warp 133 -> Shady Jungle          (- dead box "Marsh-Shady")
  EZ01 /34_31.IFO  warp 135 -> Forest of Wandering   (- dead box "PrisonToFOW")

EJ01 (Shady Jungle) already has all four of its outbound gates (128, 129, 130,
132), so this completes the Eldeon graph:
  EJT01 <-> EJ01 <-> {EJ02, EJ03},  EJ02 <-> EZ01

Deliberately NOT touched: the dead event objects "FowToSr" (EJ02/38_39) and
"PriToSea" (EZ01/33_31). No reference has a warp there and they would point at
Eldeon zones (66-68) that our LIST_ZONE.STB does not have.

Known still-missing elsewhere, same defect, out of scope here: warp 40
(JG03 -> Desert of the Dead), 43 (JD04 -> Anima Lake), 70 (SUM_EVENT -> Zant) are
recoverable from the references; 37 (JG03 -> Breezy Hills), 121/122 (LZ02 ->
Crystal Snowfields) and 90-98 (JGF01/JGF02, maps absent from our data) are not.

Idempotent: a warp id already present is left alone, an already-removed box is
skipped. Makes .bak backups. Use --dry-run to preview and --selftest to prove the
rewriter is byte-faithful before it touches anything.

After running: rebake/deploy the client VFS data. No server restart needed.

.IFO layout, which this script rewrites:
    i32 lump_count
    lump_count x { i32 type; i32 absolute_offset }
    lump data blocks, laid out contiguously in table order
Object record shared by every object lump:
    u8 name_len + name | i16 warp_id | i16 event_id | i32 obj_type | i32 obj_id
    i32 map_x | i32 map_y | 16B quaternion | 12B position | 12B scale
LUMP_TERRAIN_EVENT_OBJECT (12) appends two pascal strings (trigger, con file);
LUMP_TERRAIN_WARP (10) appends nothing. Only lumps 10 and 12 are decoded -- every
other block is copied byte-for-byte, so unparsed lumps (OCEAN, WATER, REGEN...)
cannot be corrupted.
"""
import argparse, os, shutil, struct, sys

MAPS_REL = os.path.join("data", "3DDATA", "Maps", "ELDEON")
DEFAULT_REF = r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data\3DDATA\Maps\ELDEON"

LUMP_WARP = 10
LUMP_EVENT_OBJECT = 12

# (zone, ifo basename, warp id to restore, dead event trigger to drop or None)
PLAN = [
    ("EJT01", "32_31", 126, None),
    ("EJT01", "36_31", 127, None),
    ("EJ02",  "38_32", 131, "FOW-Shadi"),
    ("EJ02",  "33_37", 134, "FOWtoPRI"),
    ("EJ03",  "31_31", 133, "Marsh-Shady"),
    ("EZ01",  "34_31", 135, "PrisonToFOW"),
]

OBJ_FIXED = 2 + 2 + 4 + 4 + 4 + 4 + 16 + 12 + 12   # everything after the name


# ------------------------------------------------------------------ primitives
class Reader:
    def __init__(self, buf, o=0):
        self.b = buf
        self.o = o

    def i32(self):
        v, = struct.unpack_from("<i", self.b, self.o); self.o += 4; return v

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def take(self, n):
        s = self.b[self.o:self.o + n]
        if len(s) != n:
            raise ValueError("truncated .IFO")
        self.o += n
        return s

    def bstr(self):
        return self.take(self.u8())

    def pstr(self):
        b = self.u8()
        if b & 0x80:
            b2 = self.u8()
            n = (b2 << 7) | (b - 0x80)
        else:
            n = b
        return self.take(n)


def put_bstr(s):
    if len(s) > 255:
        raise ValueError("name too long")
    return bytes([len(s)]) + s


def put_pstr(s):
    n = len(s)
    if n < 0x80:
        return bytes([n]) + s
    if n < 0x4000:
        return bytes([(n & 0x7F) | 0x80, n >> 7]) + s
    raise ValueError("string too long")


# ------------------------------------------------------------------ object lump
def parse_object_lump(buf, start, end, lump_type):
    """Decode an object lump; raises unless it consumes exactly [start, end)."""
    r = Reader(buf, start)
    count = r.i32()
    objs = []
    for _ in range(count):
        name = r.bstr()
        fixed = r.take(OBJ_FIXED)
        warp_id, event_id = struct.unpack_from("<hh", fixed, 0)
        extra = b""
        if lump_type == LUMP_EVENT_OBJECT:
            trigger = r.pstr()
            confile = r.pstr()
            extra = put_pstr(trigger) + put_pstr(confile)
        else:
            trigger = confile = b""
        objs.append(dict(name=name, fixed=fixed, extra=extra, warp_id=warp_id,
                         event_id=event_id, trigger=trigger, confile=confile))
    if r.o != end:
        raise ValueError(f"lump {lump_type}: parsed to {r.o}, block ends at {end}")
    return objs


def build_object_lump(objs):
    out = [struct.pack("<i", len(objs))]
    for o in objs:
        out.append(put_bstr(o["name"]))
        out.append(o["fixed"])
        out.append(o["extra"])
    return b"".join(out)


# ------------------------------------------------------------------ ifo
def read_ifo(path):
    buf = open(path, "rb").read()
    r = Reader(buf)
    n = r.i32()
    lumps = [(r.i32(), r.i32()) for _ in range(n)]
    header_end = r.o
    offsets = [o for _, o in lumps]
    if offsets != sorted(offsets):
        raise ValueError(f"{path}: lump offsets are not in table order")
    if offsets and offsets[0] != header_end:
        raise ValueError(f"{path}: first lump at {offsets[0]}, header ends at {header_end}")
    bounds = []
    for i, (t, off) in enumerate(lumps):
        end = lumps[i + 1][1] if i + 1 < len(lumps) else len(buf)
        bounds.append((t, off, end))
    return buf, bounds


def write_ifo(path, bounds, buf, replacements):
    """replacements: {lump_type: raw block bytes}"""
    blocks = []
    for t, off, end in bounds:
        blocks.append(replacements.get(t, buf[off:end]))
    n = len(bounds)
    header_size = 4 + 8 * n
    out = [struct.pack("<i", n)]
    cur = header_size
    for (t, _, _), blk in zip(bounds, blocks):
        out.append(struct.pack("<ii", t, cur))
        cur += len(blk)
    out.extend(blocks)
    data = b"".join(out)
    with open(path, "wb") as fh:
        fh.write(data)
    return data


def block(bounds, lump_type):
    for t, off, end in bounds:
        if t == lump_type:
            return off, end
    return None, None


# ------------------------------------------------------------------ operations
def selftest(paths):
    """Rebuilding with no edits must reproduce the file byte-for-byte."""
    ok = True
    for p in paths:
        buf, bounds = read_ifo(p)
        n = len(bounds)
        rebuilt = [struct.pack("<i", n)]
        cur = 4 + 8 * n
        blocks = [buf[off:end] for _, off, end in bounds]
        for (t, _, _), blk in zip(bounds, blocks):
            rebuilt.append(struct.pack("<ii", t, cur))
            cur += len(blk)
        rebuilt.extend(blocks)
        same = b"".join(rebuilt) == buf
        # and the two lumps we decode must round-trip through parse/build
        rt = True
        for lt in (LUMP_WARP, LUMP_EVENT_OBJECT):
            off, end = block(bounds, lt)
            if off is None:
                continue
            objs = parse_object_lump(buf, off, end, lt)
            rt = rt and build_object_lump(objs) == buf[off:end]
        print(f"  {os.path.basename(os.path.dirname(p))}/{os.path.basename(p)}: "
              f"container={'OK' if same else 'FAIL'} lump-roundtrip={'OK' if rt else 'FAIL'}")
        ok = ok and same and rt
    return ok


def find_ref_warp(ref_dir, zone, base, warp_id):
    p = os.path.join(ref_dir, zone, base + ".IFO")
    if not os.path.isfile(p):
        raise SystemExit(f"reference not found: {p}")
    buf, bounds = read_ifo(p)
    off, end = block(bounds, LUMP_WARP)
    if off is None:
        raise SystemExit(f"{p}: no WARP lump")
    for o in parse_object_lump(buf, off, end, LUMP_WARP):
        if o["warp_id"] == warp_id:
            return o
    raise SystemExit(f"{p}: no warp object with id {warp_id}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the rewriter is byte-faithful, then exit")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--ref", default=DEFAULT_REF, help="reference client ELDEON maps dir")
    args = ap.parse_args()

    maps = os.path.join(args.root, MAPS_REL)
    if not os.path.isdir(maps):
        raise SystemExit(f"not found: {maps} (run from the repo root or pass --root)")
    targets = [os.path.join(maps, z, b + ".IFO") for z, b, _, _ in PLAN]
    for p in targets:
        if not os.path.isfile(p):
            raise SystemExit(f"not found: {p}")

    if args.selftest:
        print("self-test (rebuild with no edits must be byte-identical):")
        ok = selftest(targets)
        print("PASS" if ok else "FAIL")
        return 0 if ok else 1

    if not os.path.isdir(args.ref):
        raise SystemExit(f"reference maps dir not found: {args.ref}\n"
                         f"pass --ref <client>/3DDATA/Maps/ELDEON")

    print("self-test:")
    if not selftest(targets):
        raise SystemExit("rewriter self-test FAILED -- refusing to write")

    changed_files = 0
    for zone, base, warp_id, dead_trigger in PLAN:
        path = os.path.join(maps, zone, base + ".IFO")
        buf, bounds = read_ifo(path)
        repl = {}
        notes = []

        woff, wend = block(bounds, LUMP_WARP)
        warps = parse_object_lump(buf, woff, wend, LUMP_WARP)
        if any(w["warp_id"] == warp_id for w in warps):
            notes.append(f"warp {warp_id} already present")
        else:
            ref = find_ref_warp(args.ref, zone, base, warp_id)
            warps.append(ref)
            repl[LUMP_WARP] = build_object_lump(warps)
            px, py, pz = struct.unpack_from("<3f", ref["fixed"], 2 + 2 + 4 + 4 + 4 + 4 + 16)
            notes.append(f"+ warp {warp_id} at ({px:.0f}, {py:.0f}, {pz:.0f})")

        if dead_trigger:
            eoff, eend = block(bounds, LUMP_EVENT_OBJECT)
            if eoff is None:
                notes.append(f"no EVENT_OBJECT lump, cannot drop {dead_trigger!r}")
            else:
                evts = parse_object_lump(buf, eoff, eend, LUMP_EVENT_OBJECT)
                want = dead_trigger.encode("latin-1")
                keep = [e for e in evts if e["trigger"] != want]
                if len(keep) == len(evts):
                    notes.append(f"dead box {dead_trigger!r} already gone")
                else:
                    repl[LUMP_EVENT_OBJECT] = build_object_lump(keep)
                    notes.append(f"- dead box {dead_trigger!r} ({len(evts) - len(keep)})")

        tag = f"{zone}/{base}.IFO"
        print(f"  {tag:18s} " + "; ".join(notes))
        if not repl or args.dry_run:
            continue

        shutil.copyfile(path, path + ".bak")
        write_ifo(path, bounds, buf, repl)

        # verify by re-reading through the same decoder
        vbuf, vbounds = read_ifo(path)
        voff, vend = block(vbounds, LUMP_WARP)
        vwarps = parse_object_lump(vbuf, voff, vend, LUMP_WARP)
        if not any(w["warp_id"] == warp_id for w in vwarps):
            raise SystemExit(f"VERIFY FAILED: {tag} has no warp {warp_id} after write")
        if dead_trigger:
            veoff, veend = block(vbounds, LUMP_EVENT_OBJECT)
            vevts = parse_object_lump(vbuf, veoff, veend, LUMP_EVENT_OBJECT)
            if any(e["trigger"] == dead_trigger.encode("latin-1") for e in vevts):
                raise SystemExit(f"VERIFY FAILED: {tag} still has {dead_trigger!r}")
        changed_files += 1

    if args.dry_run:
        print("dry run: nothing written")
    else:
        print(f"wrote {changed_files} file(s); .bak backups alongside; verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
