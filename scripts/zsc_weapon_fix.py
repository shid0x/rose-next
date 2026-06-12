#!/usr/bin/env python3
"""
zsc_weapon_fix.py — populate empty LIST_WEAPON.ZSC / LIST_SUBWPN.ZSC objects that
NPCs reference, sourcing the object definition + meshes from a working reference
client (RoseZA). Fixes "invisible weapon" monsters (e.g. Ikaness Worker NPC 1576).

Byte-faithful: unmodified objects and table entries are re-emitted verbatim. Only
the targeted empty objects are rebuilt, and new mesh/texture entries are appended
(which never shifts existing indices). A zero-modification round-trip reproduces
the input byte-for-byte (see --selftest).

Usage:
  python scripts/zsc_weapon_fix.py --selftest          # validate round-trip only
  python scripts/zsc_weapon_fix.py --dry-run           # report what would change
  python scripts/zsc_weapon_fix.py --apply             # write ZSCs + copy meshes
"""
import struct, sys, os, ntpath, shutil, argparse

SEP = chr(92)
OUR_ROOT = "data/3DDATA"
REF_ROOT = "c:/Users/Thomas/Desktop/RoseZA test client/data/3DDATA"
NPC_STB = OUR_ROOT + "/STB/LIST_NPC.STB"
REF_NPC_STB = REF_ROOT + "/STB/LIST_NPC.STB"

# (npc weapon column raw-cell index, our zsc, ref zsc)  -- raw cell = game col + 1
TARGETS = [
    ("RIGHT", 6, "/WEAPON/LIST_WEAPON.ZSC"),
    ("LEFT",  7, "/WEAPON/LIST_SUBWPN.ZSC"),
]

# ---------------------------------------------------------------- ZSC byte model
class Reader:
    def __init__(s, d): s.d = d; s.o = 0
    def i16(s): v = struct.unpack_from('<h', s.d, s.o)[0]; s.o += 2; return v
    def i32(s): v = struct.unpack_from('<i', s.d, s.o)[0]; s.o += 4; return v
    def u8(s):  v = s.d[s.o]; s.o += 1; return v
    def take(s, n): v = s.d[s.o:s.o+n]; s.o += n; return v
    def cstr_raw(s):
        st = s.o
        while s.d[s.o] != 0: s.o += 1
        s.o += 1
        return s.d[st:s.o]            # includes trailing NUL
    def prop_block_raw(s):
        st = s.o
        while True:
            t = s.u8()
            if t == 0: break
            sz = s.u8(); s.o += sz
        return s.d[st:s.o]            # includes terminating 0x00

class Zsc:
    """Byte-faithful representation. Tables hold raw entry bytes; objects hold
    either raw bytes (unmodified) or a structured rebuild."""
    def __init__(s, path):
        r = Reader(open(path, 'rb').read())
        s.models_raw = [r.cstr_raw() for _ in range(r.i16())]
        s.tex_raw = []
        nt = r.i16()
        for _ in range(nt):
            st = r.o
            r.cstr_raw(); r.take(36)          # path + 9*i16 + f32 + i16 + 3*f32
            s.tex_raw.append(r.d[st:r.o])
        s.eff_raw = [r.cstr_raw() for _ in range(r.i16())]
        s.objects = []                         # list of dict
        nobj = r.i16()
        for i in range(nobj):
            st = r.o
            r.i32(); r.i32(); r.i32()          # cylinder
            parts = []
            pc = r.i16()
            if pc > 0:
                for _ in range(pc):
                    m = r.i16(); t = r.i16(); prop = r.prop_block_raw()
                    parts.append([m, t, prop])
                ec = r.i16()
                for _ in range(ec):
                    r.i16(); r.i16(); r.prop_block_raw()
                r.take(24)                     # bbox
            s.objects.append(dict(idx=i, raw=r.d[st:r.o], empty=(pc == 0), parts=parts))
        s.models = [m[:-1].decode('euc-kr', 'replace') for m in s.models_raw]
        # decoded model path of a populated object's first part (for matching)
    def model_basename(s, oi):
        ob = s.objects[oi]
        if ob['empty'] or not ob['parts']: return None
        m = ob['parts'][0][0]
        return ntpath.basename(s.models[m].replace('/', SEP)).lower() if m < len(s.models) else None

    def serialize(s):
        out = bytearray()
        out += struct.pack('<h', len(s.models_raw))
        for m in s.models_raw: out += m
        out += struct.pack('<h', len(s.tex_raw))
        for t in s.tex_raw: out += t
        out += struct.pack('<h', len(s.eff_raw))
        for e in s.eff_raw: out += e
        out += struct.pack('<h', len(s.objects))
        for ob in s.objects:
            out += ob['raw']
        return bytes(out)

# ------------------------------------------------------------------- STB reader
def stb_rows(path):
    d = open(path, 'rb').read(); o = 8
    rc = struct.unpack_from('<i', d, o)[0]; o += 4
    cc = struct.unpack_from('<i', d, o)[0]; o += 4
    o += 4; o += 2; o += 2 * cc
    def rss():
        nonlocal o
        ln = struct.unpack_from('<H', d, o)[0]; o += 2
        s = d[o:o+ln]; o += ln; return s.decode('euc-kr', 'replace')
    rss()
    for _ in range(cc): rss()
    n = rc - 1
    rows = [[None]*cc for _ in range(n)]
    for i in range(n): rows[i][0] = rss()
    for i in range(n):
        for j in range(1, cc): rows[i][j] = rss()
    return rows

def ti(s):
    try: return int(s)
    except: return 0

# --------------------------------------------------------------- patch building
def build_object_raw(refzsc, ref_oi, our: Zsc, model_add, tex_add, eff_add):
    """Rebuild target object's bytes using ref's object, remapping model/texture/
    effect indices into our tables (appending where missing)."""
    rr = Reader(refzsc_raw_of(refzsc, ref_oi))
    rad = rr.i32(); cx = rr.i32(); cy = rr.i32()
    pc = rr.i16()
    out = bytearray()
    out += struct.pack('<iiih', rad, cx, cy, pc)
    for _ in range(pc):
        rm = rr.i16(); rt = rr.i16(); prop = rr.prop_block_raw()
        nm = remap_model(refzsc, rm, our, model_add)
        nt = remap_tex(refzsc, rt, our, tex_add)
        out += struct.pack('<hh', nm, nt) + prop
    ec = rr.i16()
    out += struct.pack('<h', ec)
    for _ in range(ec):
        rty = rr.i16(); re = rr.i16(); prop = rr.prop_block_raw()
        ne = remap_eff(refzsc, re, our, eff_add)
        out += struct.pack('<hh', rty, ne) + prop
    out += rr.take(24)   # bbox
    return bytes(out)

def refzsc_raw_of(refzsc, oi):
    return refzsc.objects[oi]['raw']

def remap_model(refzsc, ri, our, model_add):
    path_raw = refzsc.models_raw[ri]
    if path_raw in our._model_index: return our._model_index[path_raw]
    idx = len(our.models_raw)
    our.models_raw.append(path_raw); our.models.append(path_raw[:-1].decode('euc-kr','replace'))
    our._model_index[path_raw] = idx; model_add.append(path_raw[:-1].decode('euc-kr','replace'))
    return idx

def remap_tex(refzsc, ri, our, tex_add):
    rec = refzsc.tex_raw[ri]
    if rec in our._tex_index: return our._tex_index[rec]
    idx = len(our.tex_raw)
    our.tex_raw.append(rec); our._tex_index[rec] = idx
    tex_add.append(rec.split(b'\x00',1)[0].decode('euc-kr','replace'))
    return idx

def remap_eff(refzsc, ri, our, eff_add):
    rec = refzsc.eff_raw[ri] if 0 <= ri < len(refzsc.eff_raw) else None
    if rec is None: return ri
    if rec in our._eff_index: return our._eff_index[rec]
    idx = len(our.eff_raw)
    our.eff_raw.append(rec); our._eff_index[rec] = idx
    eff_add.append(rec[:-1].decode('euc-kr','replace'))
    return idx

# ---------------------------------------------------------------------- helpers
def find_file_ci(root, basename):
    bl = basename.lower()
    for dp, _, files in os.walk(root):
        for f in files:
            if f.lower() == bl: return os.path.join(dp, f)
    return None

def selftest():
    ok = True
    for _, _, rel in TARGETS:
        p = OUR_ROOT + rel
        z = Zsc(p)
        same = z.serialize() == open(p, 'rb').read()
        print("  round-trip %-28s %s" % (ntpath.basename(rel), "OK (byte-identical)" if same else "MISMATCH!"))
        ok = ok and same
    return ok

def plan():
    rows = stb_rows(NPC_STB)
    jobs = []  # (label, rel, objidx, npcs, ref)
    for label, col, rel in TARGETS:
        our = Zsc(OUR_ROOT + rel)
        ref = Zsc(REF_ROOT + rel)
        ref.objects_full = ref  # ref already byte-faithful
        used = {}
        for i, row in enumerate(rows):
            v = ti(row[col])
            if v > 0: used.setdefault(v, []).append(i)
        for objidx in sorted(used):
            our_ok = 0 <= objidx < len(our.objects) and not our.objects[objidx]['empty']
            if our_ok: continue
            if objidx >= len(ref.objects) or ref.objects[objidx]['empty']:
                continue  # ref has nothing either -> truly no weapon
            jobs.append((label, rel, objidx, used[objidx]))
    return jobs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        print("Zero-modification round-trip validation:")
        sys.exit(0 if selftest() else 1)

    print("Round-trip safety check:")
    if not selftest():
        print("  ABORT: parser is not byte-faithful on current files."); sys.exit(1)
    print()

    rows = stb_rows(NPC_STB)
    ref_rows = stb_rows(REF_NPC_STB)
    copied = set()
    for label, col, rel in [(l, c, r) for (l, c, r) in TARGETS]:
        our = Zsc(OUR_ROOT + rel)
        ref = Zsc(REF_ROOT + rel)
        our._model_index = {m: i for i, m in enumerate(our.models_raw)}
        our._tex_index = {t: i for i, t in enumerate(our.tex_raw)}
        our._eff_index = {e: i for i, e in enumerate(our.eff_raw)}
        # Target = union of objects referenced by our STB and RoseZA's STB
        # (robust to the 1576->122 test edit; over-populating an empty object is harmless).
        used = {}
        for i, row in enumerate(rows):
            v = ti(row[col])
            if v > 0: used.setdefault(v, []).append(i)
        for i, row in enumerate(ref_rows):
            v = ti(row[col])
            if v > 0 and v not in used: used.setdefault(v, []).append(i)
        targets = []
        for objidx in sorted(used):
            our_ok = 0 <= objidx < len(our.objects) and not our.objects[objidx]['empty']
            if our_ok: continue
            if objidx >= len(ref.objects) or ref.objects[objidx]['empty']: continue
            targets.append(objidx)
        print("### %s -> %s : %d empty objects to populate ###" % (label, ntpath.basename(rel), len(targets)))
        model_add, tex_add, eff_add = [], [], []
        for objidx in targets:
            mesh = ref.models[ref.objects[objidx]['parts'][0][0]]
            new_raw = build_object_raw(ref, objidx, our, model_add, tex_add, eff_add)
            our.objects[objidx]['raw'] = new_raw
            our.objects[objidx]['empty'] = False
            # copy mesh + texture files for EVERY part (objects can have >1 part)
            files = []
            for (pm, pt, _prop) in ref.objects[objidx]['parts']:
                files.append(ntpath.basename(ref.models[pm].replace('/', SEP)))
                texpath = ref.tex_raw[pt].split(b'\x00', 1)[0].decode('euc-kr', 'replace')
                files.append(ntpath.basename(texpath.replace('/', SEP)))
            seen = set(); files = [f for f in files if not (f in seen or seen.add(f))]
            copy_notes = []
            for fn in files:
                src = find_file_ci(REF_ROOT + "/WEAPON", fn)
                if not src: copy_notes.append("MISSING:%s" % fn); continue
                rel_under = os.path.relpath(src, REF_ROOT).replace('\\','/')
                dst = OUR_ROOT + "/" + rel_under
                if a.apply:
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    if not os.path.exists(dst): shutil.copy2(src, dst)
                copy_notes.append(ntpath.basename(dst))
                copied.add(dst)
            print("   obj %-5d <- %-45s NPCs=%s files=%s" % (objidx, mesh, used[objidx][:5], copy_notes))
        if a.apply:
            outp = OUR_ROOT + rel
            shutil.copy2(outp, outp + ".bak")
            open(outp, 'wb').write(our.serialize())
            print("   WROTE %s (+%d models, +%d textures; backup .bak)" % (rel, len(model_add), len(tex_add)))
        else:
            print("   [dry-run] would add %d models, %d textures, populate %d objects" % (len(model_add), len(tex_add), len(targets)))
        print()
    if a.apply:
        print("Applied. Copied %d mesh/texture files. NOTE: set NPC 1576 right weapon back to 1122 (undo the 122 test)." % len(copied))
    else:
        print("Dry-run only. Re-run with --apply to write.")

if __name__ == '__main__':
    main()
