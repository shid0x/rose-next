#!/usr/bin/env python3
"""Measure what the hit-reaction ("flinch") actually costs a monster's attack cadence.

Why this exists
---------------
`CObjCHAR::Apply_DAMAGE` (server) and `CObjCHAR::ApplyPresentedCombatFeedback`
(client) both interrupt a damaged character's current motion with its hit
animation:

    if (sDamage.m_wACTION & DMG_ACT_HITTED)
        if (!(pTarget->Get_STATE() & CS_BIT_INT2)) {
            pTarget->Set_MOTION(pTarget->GetANI_Hit());   // fAniSpeed defaults to 1.0
            pTarget->Set_STATE(CS_HIT);
        }

`CS_ATTACK` does not carry `CS_BIT_INT2`, so a monster mid-swing *is*
interruptible.  The catch is where monster damage is actually delivered:

    CObjAI::Start_ATTACK -> Set_MOTION(GetANI_Attack(), 0, atkSpeed/100, true)
                         -> Attack_START -> Apply_DAMAGE + Send_combat_swing

i.e. **at frame 0 of the attack motion**.  So a flinch never cancels a hit --
that hit already landed when the motion started.  What it does is replace the
*remainder* of the attack motion with the hit motion, after which
`ProcCMD_ATTACK` restarts the attack and immediately deals damage again.

    interval, not flinched : A      = attackFrames / (attackFPS * atkSpeed/100)
    interval, flinched at f: f + H  H = hitFrames    / (hitFPS * 1.0)

So flinching SHORTENS the monster's damage interval whenever `f + H < A`.
Break-even elapsed time is `A - H`; under a uniformly-timed interrupt the
expected interval is `A/2 + H`, which beats `A` whenever `H < A/2`.

Note the asymmetry the code creates: the attack motion is scaled by attack
speed (`atkSpeed/100`), the hit motion is not (`Set_MOTION` defaults
`fAniSpeed = 1.0f`).  Fast monsters therefore lose more to a flinch than slow
ones.

This script reads the real data (`LIST_NPC.STB`, `LIST_NPC.CHR`, the `.ZMO`
motions) and reports A, H and the resulting cadence change per monster, so the
"should the flinch be crit-only?" decision is made on numbers rather than
intuition.  Read-only: it never writes to data/.

Formats
-------
LIST_NPC.CHR:
    u16 skelCount;      skelCount   * cstr
    u16 aniCount;       aniCount    * cstr          (.ZMO paths, relative to data/)
    u16 boneFxCount;    boneFxCount * cstr
    u16 modelCount;
      per model:
        u8  isValid                                 (0 -> absent, no further bytes)
        u16 skelIndex
        cstr name
        u16 dataIdxCount;  skip dataIdxCount * 2 bytes
        u16 aniCount;      aniCount * (u16 aniIdx, u16 aniFileIdx)
        u16 boneFxCount;   skip boneFxCount * 4 bytes
    Strings are NUL-terminated (CStr::ReadString).

ZMO ("ZMO0002"):
    cstr magic; u32 fps; u32 frameCount; ... channel data ...
    last 4 bytes: "EZMO" (v2) or "3ZMO" (v3) marker
    bytes [-8:-4]: u32 offset of the extended block
    extended block: u16 totalFrames; i16 frameEvent[totalFrames]
    Frame-event ids counted as attack points (server io_motion.cpp):
        10, 20, 21..28, 56, 57, 66, 67
    The server decrements m_wTotalFrame after loading; we mirror that.
"""

import argparse
import os
import struct
import sys

MOB_ANI_ATTACK = 2
MOB_ANI_HIT = 3

ATTACK_FRAME_EVENTS = {10, 20, 21, 22, 23, 24, 25, 26, 27, 28, 56, 57, 66, 67}

NPC_COL_NAME = 0
NPC_COL_LEVEL = 7
NPC_COL_HP = 8
NPC_COL_ATK_SPEED = 14


def read_stb(path):
    d = open(path, "rb").read()
    if d[:4] != b"STB1":
        raise SystemExit(f"{path}: not an STB1 file")
    off, rows, cols = struct.unpack_from("<III", d, 4)
    o = off
    data = []
    for _ in range(rows - 1):
        row = []
        for _ in range(cols - 1):
            n, = struct.unpack_from("<H", d, o)
            o += 2
            row.append(d[o:o + n].decode("cp949", "replace"))
            o += n
        data.append(row)
    return data


class Reader:
    def __init__(self, buf):
        self.d = buf
        self.o = 0

    def u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def u16(self):
        v, = struct.unpack_from("<H", self.d, self.o)
        self.o += 2
        return v

    def u32(self):
        v, = struct.unpack_from("<I", self.d, self.o)
        self.o += 4
        return v

    def cstr(self):
        end = self.d.index(b"\0", self.o)
        s = self.d[self.o:end].decode("cp949", "replace")
        self.o = end + 1
        return s


def read_npc_chr(path):
    """-> (ani_paths, {model_index: {ani_idx: ani_file_idx}})"""
    r = Reader(open(path, "rb").read())

    for _ in range(r.u16()):          # skeleton files
        r.cstr()

    ani_paths = [r.cstr() for _ in range(r.u16())]

    for _ in range(r.u16()):          # bone effect files
        r.cstr()

    models = {}
    for model_idx in range(r.u16()):
        if r.u8() == 0:
            continue                  # model absent; loader returns immediately
        r.u16()                       # skeleton index
        r.cstr()                      # name
        # NB: read the count into a local first -- `r.o += r.u16() * 2` would
        # capture r.o *before* u16() advances it and silently lose those 2 bytes.
        n_data = r.u16()
        r.o += n_data * 2             # data indices
        anis = {}
        for _ in range(r.u16()):
            ani_idx = r.u16()
            file_idx = r.u16()
            if ani_idx < 0x8000:      # loader skips negative indices
                anis[ani_idx] = file_idx
        n_bonefx = r.u16()
        r.o += n_bonefx * 4           # bone effects
        models[model_idx] = anis
    return ani_paths, models


def read_zmo(path):
    """-> (fps, total_frames_after_server_decrement, attack_frame_count) or None"""
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if len(d) < 16:
        return None

    r = Reader(d)
    if r.cstr() != "ZMO0002":
        return None
    fps = r.u32()
    frames = r.u32()

    attack_frames = 0
    tag = d[-4:]
    if tag in (b"EZMO", b"3ZMO"):
        block_off, = struct.unpack_from("<I", d, len(d) - 8)
        if 0 <= block_off < len(d) - 2:
            n, = struct.unpack_from("<H", d, block_off)
            end = block_off + 2 + n * 2
            if end <= len(d):
                events = struct.unpack_from("<" + str(n) + "h", d, block_off + 2)
                attack_frames = sum(1 for e in events if e in ATTACK_FRAME_EVENTS)

    return fps, max(1, frames - 1), attack_frames


def resolve(data_root, rel):
    """data/ is case-inconsistent; find the file case-insensitively."""
    p = os.path.join(data_root, rel.replace("\\", os.sep))
    if os.path.isfile(p):
        return p
    parts = rel.replace("\\", "/").split("/")
    cur = data_root
    for part in parts:
        if not os.path.isdir(cur):
            return None
        match = next((e for e in os.listdir(cur) if e.lower() == part.lower()), None)
        if match is None:
            return None
        cur = os.path.join(cur, match)
    return cur if os.path.isfile(cur) else None


def median(vals):
    s = sorted(vals)
    return s[len(s) // 2]


def flinch_and_crit_rates(level, critical, atk, defence):
    """Exact mirror of CCal::Get_BasicDAMAGE's two gates (integer division and all).

        iCriSuc     = ((1 + rand(100)) * 3 + LEVEL + 30) * 16 / (CRITICAL + 70)
        iHitActRATE = (28 - iCriSuc) * (ATK + 20) / (DEF + 5)
        flinch iff iHitActRATE >= 10        crit iff iCriSuc < 20

    RANDOM(100) is `rand() % 100`, so the roll is uniform over 0..99 and the
    exact rates come from enumerating it rather than sampling.

    Note both gates key off the same crit roll: the flinch band is "crit, or
    just short of crit, provided you outgun the target".  That is why moving
    the flinch to crit-only is a tightening rather than a redesign.
    """
    flinch = crit = 0
    for r in range(100):
        cri_suc = ((1 + r) * 3 + level + 30) * 16 // (critical + 70)
        if cri_suc < 20:
            crit += 1
        if (28 - cri_suc) * (atk + 20) // (defence + 5) >= 10:
            flinch += 1
    return flinch, crit


def critical_stat(sense, level):
    """cuserdata.cpp:522 -- m_iCritical = SENSE * 0.8 + LEVEL * 0.3."""
    return int(sense * 0.8 + level * 0.3)


def expected_interval(A, H, period, q, samples=400):
    """Expected gap between two monster damage events under player pressure.

    Damage lands at frame 0 of the attack motion.  The monster is then in that
    motion for A seconds unless a flinch replaces the remainder with the hit
    motion, after which the attack restarts and damage lands again.

    `period` is the player's attack interval, `q` the chance a hit carries
    DMG_BIT_HITTED.  A second flinch *during* a hit motion does not extend it:
    Chg_CurMOTION only resets the frame counter when the motion pointer
    actually changes, and hit -> hit is the same pointer.  So the flinch cost
    is bounded at H no matter how fast the player swings.
    """
    if period <= 0 or q <= 0:
        return A

    total = 0.0
    for i in range(samples):
        phase = period * (i + 0.5) / samples   # deterministic sweep over hit phase
        # First *flinching* hit after the attack started.
        t = phase
        k = 0
        f = None
        while t < A:
            # P(this hit is the first flinching one) folded in analytically below;
            # walk the hit train and take the expectation over the geometric draw.
            k += 1
            t += period
        # expectation over which hit in the train flinches
        hits = [phase + j * period for j in range(k)]
        p_none = 1.0
        acc = 0.0
        for h in hits:
            acc += p_none * q * (h + H)
            p_none *= (1.0 - q)
        acc += p_none * A                      # no hit flinched before A elapsed
        total += acc
    return total / samples


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--min-level", type=int, default=1)
    ap.add_argument("--max-level", type=int, default=999)
    ap.add_argument("--csv", help="write the per-monster table here")
    ap.add_argument("--limit", type=int, default=25, help="sample rows to print (0 = none)")
    ap.add_argument("--keep-equal-motions", action="store_true",
                    help="keep rows whose attack and hit motion are the same length "
                         "(town NPCs share one motion and never fight; excluded by default)")
    args = ap.parse_args()

    data_root = os.path.join(args.root, "data")
    npc_stb = resolve(data_root, "3DDATA/STB/LIST_NPC.STB")
    npc_chr = resolve(data_root, "3DDATA/NPC/LIST_NPC.CHR")
    if not npc_stb or not npc_chr:
        raise SystemExit("could not find LIST_NPC.STB / LIST_NPC.CHR under data/")

    stb = read_stb(npc_stb)
    ani_paths, models = read_npc_chr(npc_chr)
    print("LIST_NPC.STB rows=%d  LIST_NPC.CHR motions=%d models=%d"
          % (len(stb), len(ani_paths), len(models)), file=sys.stderr)

    zmo_cache = {}

    def motion(file_idx):
        if file_idx not in zmo_cache:
            rel = ani_paths[file_idx] if file_idx < len(ani_paths) else None
            p = resolve(data_root, rel) if rel else None
            zmo_cache[file_idx] = read_zmo(p) if p else None
        return zmo_cache[file_idx]

    def as_int(row, col):
        try:
            return int(row[col])
        except (ValueError, IndexError):
            return 0

    rows = []
    skipped = {"no_model": 0, "no_motion": 0, "bad_zmo": 0, "level": 0}
    for npc_id, row in enumerate(stb):
        anis = models.get(npc_id)
        if not anis:
            skipped["no_model"] += 1
            continue
        if MOB_ANI_ATTACK not in anis or MOB_ANI_HIT not in anis:
            skipped["no_motion"] += 1
            continue
        level = as_int(row, NPC_COL_LEVEL)
        if level <= 0 or not (args.min_level <= level <= args.max_level):
            skipped["level"] += 1
            continue
        atk_speed = as_int(row, NPC_COL_ATK_SPEED)
        if atk_speed <= 0:
            skipped["level"] += 1
            continue

        atk = motion(anis[MOB_ANI_ATTACK])
        hit = motion(anis[MOB_ANI_HIT])
        if not atk or not hit or atk[0] <= 0 or hit[0] <= 0:
            skipped["bad_zmo"] += 1
            continue

        A = atk[1] / (atk[0] * (atk_speed / 100.0))
        H = hit[1] / (hit[0] * 1.0)
        rows.append({
            "id": npc_id,
            "name": row[NPC_COL_NAME] if row else "",
            "level": level,
            "hp": as_int(row, NPC_COL_HP),
            "atk_speed": atk_speed,
            "A": A,
            "H": H,
            "attack_points": atk[2],
        })

    if not args.keep_equal_motions:
        before = len(rows)
        rows = [r for r in rows if abs(r["A"] - r["H"]) > 1e-9]
        skipped["equal_motions"] = before - len(rows)

    print("skipped: %s" % skipped, file=sys.stderr)
    if not rows:
        raise SystemExit("no monsters resolved -- check the CHR parse")

    for r in rows:
        r["breakeven"] = r["A"] - r["H"]
        r["expected"] = r["A"] / 2.0 + r["H"]
        r["ratio"] = r["expected"] / r["A"]

    rows.sort(key=lambda r: (r["level"], r["id"]))

    faster = [r for r in rows if r["ratio"] < 1.0]
    print()
    print("monsters resolved            : %d" % len(rows))
    print("flinch SPEEDS UP the monster : %d  (%.0f%%)"
          % (len(faster), 100.0 * len(faster) / len(rows)))
    print("  criterion: H < A/2, i.e. expected interval A/2 + H < A")
    print()
    print("median A (attack interval)   : %.2f s" % median([r["A"] for r in rows]))
    print("median H (hit motion)        : %.2f s" % median([r["H"] for r in rows]))
    mR = median([r["ratio"] for r in rows])
    print("median expected/normal ratio : %.2f  (%s)"
          % (mR, "monsters damage MORE often when flinched" if mR < 1
             else "monsters damage LESS often when flinched"))
    print()

    print("by level band (medians):")
    print("  %10s  %4s  %6s  %6s  %6s  %8s" % ("band", "n", "A", "H", "ratio", "faster%"))
    bands = [(1, 30), (31, 60), (61, 100), (101, 140), (141, 180), (181, 220), (221, 999)]
    for lo, hi in bands:
        sel = [r for r in rows if lo <= r["level"] <= hi]
        if not sel:
            continue
        pf = 100.0 * sum(1 for r in sel if r["ratio"] < 1.0) / len(sel)
        print("  %4d-%-5d  %4d  %6.2f  %6.2f  %6.2f  %7.0f%%"
              % (lo, hi, len(sel), median([r["A"] for r in sel]),
                 median([r["H"] for r in sel]), median([r["ratio"] for r in sel]), pf))

    # How often the flinch actually fires. Baseline player stats per level are
    # rough (no refines/gems -- see doc/balance-analysis.md "Model caveats"),
    # but the gate is dominated by the crit roll, which depends only on
    # LEVEL and CRITICAL, so the flinch/crit split is robust to ATK/DEF error.
    print()
    print("how often the flinch fires (exact enumeration of the rand roll)")
    print("  %6s %7s %6s %7s | %8s %7s" % ("level", "CRIT", "ATK", "mobDEF", "flinch%", "crit%"))
    baseline = [
        (30, 40, 180, 60), (60, 60, 330, 110), (100, 90, 520, 170),
        (140, 120, 760, 230), (180, 150, 1000, 280), (216, 175, 1201, 320),
        (240, 200, 1350, 360),
    ]
    flinch_by_level = {}
    for lv, sense, atk, dfn in baseline:
        c = critical_stat(sense, lv)
        fl, cr = flinch_and_crit_rates(lv, c, atk, dfn)
        flinch_by_level[lv] = fl / 100.0
        print("  %6d %7d %6d %7d | %7d%% %6d%%" % (lv, c, atk, dfn, fl, cr))

    print()
    print("monster damage rate vs player attack speed")
    print("  (interval between monster damage events; 1.00x = flinch changes nothing)")
    print("  q = chance a player hit carries DMG_BIT_HITTED")
    print()
    print("  %14s  %10s  %10s  %10s" % ("player swing", "q=1.0", "q=0.5", "q=0.25"))
    med_A = median([r["A"] for r in rows])
    med_H = median([r["H"] for r in rows])
    for period in (0.8, 1.2, 1.6, 2.0, 3.0):
        cells = []
        for q in (1.0, 0.5, 0.25):
            vals = [expected_interval(r["A"], r["H"], period, q) / r["A"] for r in rows]
            m = median(vals)
            cells.append("%.2fx" % (1.0 / m))
        print("  %13.1fs  %10s  %10s  %10s" % (period, cells[0], cells[1], cells[2]))
    print()
    print("  median monster: A=%.2fs  H=%.2fs" % (med_A, med_H))

    if args.limit:
        print()
        print("sample (%d monsters, evenly spread by level):" % args.limit)
        print("  %5s  %4s  %5s  %6s  %6s  %6s  %6s  %s"
              % ("id", "lv", "aspd", "A", "H", "A-H", "ratio", "name"))
        step = max(1, len(rows) // args.limit)
        for r in rows[::step][:args.limit]:
            print("  %5d  %4d  %5d  %6.2f  %6.2f  %6.2f  %6.2f  %s"
                  % (r["id"], r["level"], r["atk_speed"], r["A"], r["H"],
                     r["breakeven"], r["ratio"], r["name"]))

    if args.csv:
        import csv
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
        print("\nwrote %s" % args.csv)


if __name__ == "__main__":
    main()
