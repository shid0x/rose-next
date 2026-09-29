"""Strip AI skill casts (AIACT24) that name a skill missing from LIST_SKILL.STB.

Sibling of `audit-ai-monster-refs.py`, which it imports for the .aip container
codec (parse/build/round-trip self-test). One AI action type carries a skill
id: **24 "use skill on target/self"** --

    struct AIACT24 { DWORD dwSize; AITYPE Type; BYTE btTarget; short nSkill; short nMotion; };

default packing, so `nSkill` is the `<h` at offset 10 and `nMotion` at 12; every
record we ship is 16 bytes. Nothing between the .aip and the server validated the
id -- `F_AIACT24` handed it straight to `SetCMD_Skill2OBJ` -- so an imported AI
that names a skill nobody imported with it *casts a skill that does not exist*.

Why this is not cosmetic
------------------------
Found 2026-09-14 on Fearsome Terrasaurus King (LIST_NPC 2265). Its AI is RoseZA's
`OR_THORNIE.AIP`, deliberately kept by the 667 Oro import, and it casts 3603
"Charge" and 3604 "Fireball" from its attack-move and when-damaged patterns. Both
are blank rows in our LIST_SKILL. What happened:

  * The server accepted the cast, broadcast GSV_TARGET_SKILL, and `Skill_START`
    switched on SKILL_TYPE 0 -- no effect. Every client played the boss's casting
    motion for nothing: "a different animation that doesn't deal any damage".
  * That cast replaced the boss's *attack* motion on the client. The server had
    already applied that swing's damage (Attack_START applies at frame 0), so the
    client's 3 s orphan sweep discarded the event and folded the HP silently.
    Two consecutive discards (611 + 475 HP) followed by the killing blow (545)
    presented the avatar's death from a visible 1323 HP.

The code side is fixed on both ends: the client now presents a swing that the
attacker's own skill command pre-empts (`CObjCHAR::PresentPreemptedCombatSwing`),
and `F_AIACT24` refuses a skill id whose row is blank or out of range, warning
once. This script removes the *cause*: an action that can only ever be refused is
dead weight, and leaving it in reads as a kit the monster never actually has.
`import-oro-667.py` step 3h checks that every cast *animates* on the model
(`aip_skill_motions` / `chr_anim_audit`) but never that the skill row exists --
this is the missing half of that check.

What the dangling ids were (RoseZA / 667 LIST_SKILL) -- all imported as of 2026-09-15
--------------------------------------------------------------------------------------
  or_thornie.aip      3603 Charge        type 3 (attack-motion change), range 500, power 2000
                      3604 Fireball      type 6 (projectile magic), power 3500, bullet 476,
                                         casting fx 1882, skill fx 1883, hit fx 95, sfx 93/148
  or_gmdevourer1.aip  3609 AOE movement-speed reduction (667: "Dispell (3-8) Buffs"), type 12/8
                      3610 Slow (AOE)    type 8, duration 30
                      3611 Stun + Damage AOE, type 17, power 400
                      3613 Range Attack, type 6 projectile, power 300 -- collides with our
                                         Karkia Stun (Jrose 3613); imported at 7013 and
                                         re-pointed with --remap (2026-09-14)
  inguz.aip           3044 (type 7 area magic, power 200, Korean name)
  kh_2676.aip         871  Voltage Jolt  type 6, Jrose power 700 -- Karkia's AI is Jrose's,
                                         so the id is Jrose's (RoseZA's 871 is a different row)
  pengun.aip          2979 jump attack (long range), type 6, power 250
  penart.aip          2980 stun-damage jump attack (long range), type 6, power 200

`scripts/import-monster-skills.py` is the importer (Charge/Fireball were the
first, 2026-09-14): it resolves the effect/bullet/sound chain, writes the rows in
place at the source index (our tables are the same lineage as RoseZA's, so the
indices line up) and re-bases SKILL_POWER to our scale. **Ordering dependency:**
after importing, `--restore` this script's backups so the casts come back, then
re-run it -- with the rows present only the remaining dangling casts are
stripped. The restore is whole-file, so do it before re-running, not after.

The third failure mode: a cast that can never present itself
--------------------------------------------------------------
A monster casts with the AI action's nMotion: CHR slot nMotion plays the wind-up
and slot nMotion+1 the release, on the client (CObjMOB::GetANI_Casting/GetANI_Skill)
and on the server (CObjNPC) alike -- 1498 of the 1565 casts we ship follow it. The
server sends the skill's result (GSV_DAMAGE_OF_SKILL / GSV_EFFECT_OF_SKILL) when
its own casting motion ends, and the client parks that payload on the caster
until the release clip's action frame: 24/34 (ActionSkill -- for a projectile
skill this is what launches the bullet), or 25/35/26/10/20/56/66
(ActionImmediateSkill). A release clip with none of them never presents:

  * projectile skill -> the bullet never fires; the damage lands on a later melee
    frame with no visual and the status payload times out (Mukuroji, whose
    release is the pig attack clip; Orgeid, whose Jrose CHR held the event-less
    casting clip in both slots -- fixed by a CHR slot, see import-karkia.py
    CHR_MOTION_OVERRIDE / fix-chr-skill-slots.py);
  * any other skill -> the parked payload is resolved *silently* after the 3 s
    abandon grace (CObjCHAR::ProcTimeOutEffectedSkill): HP folds with no digit,
    no hit effect, and the status lands late with no cast to explain it.

Two shapes cause it. A CHR slot holding the wrong clip (Orgeid) is fixed in the
CHR. An AI authored against a different slot layout is fixed in the AI: Grand
Master Devourer's model has (charge, skill01, charge, skill02) in slots 6-9 --
two wind-up/release pairs at 6/7 and 8/9 -- and RoseZA's OR_GMdevourer1.aip
casts on 7 and 9 (release = the event-less charge clip, and slot 10 does not
exist) and its self-buffs on 2 (release = the hit clip). `--remotion
FILE.aip:OLD=NEW` re-points every cast on nMotion OLD to NEW (7=6, 9=8, 2=6 for
the Devourer), recorded in the manifest like --remap. Mini-Devourer 2225 is the
model with no skill clip at all (slots 0-5) casting 3042 on 8; with no 3D
artist to add one, its casts are stripped on request (--strip, below) and it
fights with normal attacks (2026-09-15).

The audit walks every monster's AI casts and WARNS about both classes (it never
strips them). Retail ships ~110 such casts (self-buffs on Smoulys, Kaiman, the
Salamander Flames, ...) whose release clip has no event -- their status arrives
~3 s late and silently; listed, not fixed.

Warnings do not fail --verify unless --strict-motions is given.

Re-pointing casts (both recorded in build/ai-skill-refs/manifest.json, both undone
by --restore; `--restore --only FILE.aip` undoes one file and leaves the rest of
the manifest alone -- the whole-manifest restore also reverts KH_2676's 923 remap):

    python scripts/audit-ai-skill-refs.py --remap    or_gmdevourer1.aip:3613=7013
    python scripts/audit-ai-skill-refs.py --remotion or_gmdevourer1.aip:7=6 \
                                          --remotion or_gmdevourer1.aip:9=8 \
                                          --remotion or_gmdevourer1.aip:2=6

Note `--restore --only` puts back the file's pre-edit *original*, i.e. it undoes
the strip, the remap and the remotion of that file together -- redo them after.

Testing a low-roll cast: `--rechance FILE.aip:SKILL=PCT` sets the random-chance
condition (type 0x04000008, percent byte at offset 8) of every event that casts
SKILL. The Devourer's damage casts sit behind 8% and 5%:

    python scripts/audit-ai-skill-refs.py --rechance or_gmdevourer1.aip:7013=100 \
                                          --rechance or_gmdevourer1.aip:3611=100
    ... test ...
    python scripts/audit-ai-skill-refs.py --rechance or_gmdevourer1.aip:7013=8 \
                                          --rechance or_gmdevourer1.aip:3611=5

The manifest records each change with the percentages it replaced, so the undo
is the same command with the old value (not --restore, see above). The other
gates on the event (a buff on you, a second attacker, an enemy in reach) still
apply.

The fifth failure mode: a cast behind a gate nobody can pass
------------------------------------------------------------
Condition 02 "N characters within D metres whose level, relative to mine, lies
in [lo, hi]" is read by our server through `AICOND02 { int iDistance; BYTE
btIsAllied; short nLevelDiff; short nLevelDiff2; WORD wChrNum; }` (the 2004
layout; the older one packed two chars where the pad and nLevelDiff now sit).
RoseZA's OR_GMdevourer1.aip gates its two damage casts on `00 cd 64 00 64 00
01 00` -- enemy, [100, 100], one of them: a target exactly 100 levels below the
boss, which no character near level 200 is. At 100% roll the casts still never
fired (2026-09-14). Every other window in that file decodes sensibly, so this
is an editor default RoseZA's server read as "any"; 20 such windows exist in 17
files but only these two gate a cast. `--rewindow FILE.aip:SKILL=LO,HI` rewrites
the window of every condition 02 on an event that casts SKILL, recorded like
the others; the audit warns about any cast behind a lo >= hi window.

    python scripts/audit-ai-skill-refs.py --rewindow or_gmdevourer1.aip:7013=-100,100 \
                                          --rewindow or_gmdevourer1.aip:3611=-100,100

Stripping a cast on request: `--strip FILE.aip:SKILL` removes every action that
casts SKILL from that file, recorded like the others. Used for Mini-Devourer
2225 (or_minidevourer1.aip, 3042): its model has no skill clip at all (slots
0-5), so the cast can never present and, without a 3D artist, it fights with
normal attacks only (2026-09-15).

    python scripts/audit-ai-skill-refs.py --strip or_minidevourer1.aip:3042

What it does
------------
Removes the offending action record from its event and decrements that event's
action count; everything else in the file is copied through byte-for-byte (the
codec's `--selftest` proves the rewrite round-trips every .aip before any is
touched). Backups go to `build/ai-skill-refs/manifest.json`, never beside the
.aip -- `pack.rs` filters only hidden entries and `pack.ps1` hard-errors on any
`.bak` under data/.

`data/` is gitignored, so this file is the only committed record of the change.
"""

import argparse
import base64
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

AIACT24 = 0x19 | 0x0B000000  # type index 25 == "act 24" in the tool's 1-based numbering
AICOND_CHANCE = 0x08 | 0x04000000  # cond 07 "random percent"; BYTE cPercent at offset 8
CHANCE_OFF = 8
AICOND_NEAR = 0x03 | 0x04000000    # cond 02 "N chars within D m, level diff in [lo, hi]"
NEAR_FMT = "<iBxhhH"               # iDistance, btIsAllied, nLevelDiff, nLevelDiff2, wChrNum @ 8
TARGET_OFF, SKILL_OFF, MOTION_OFF = 8, 10, 12

SKILL_STB = os.path.join("data", "3DDATA", "STB", "LIST_SKILL.STB")
BACKUP_DIR = os.path.join("build", "ai-skill-refs")
MANIFEST = "manifest.json"

# Imported AI files carry their SOURCE dump's skill ids. A blank row is the loud
# failure; the quiet one is an id that is occupied here by a DIFFERENT skill --
# Nigaki (kh_2676.aip, Jrose) cast 923, which is Jrose's ally heal (type 11) and
# OUR player Healing (type 10, self): the monsters spam-cast it on each other and
# never fought back. Files are matched to their dump by filename prefix and the
# cast is flagged when SKILL_TYPE differs between the dump's row and ours. Rows
# the importers ported or re-pointed on purpose are allowlisted per dump.
SOURCE_DUMPS = {
    "Jrose": (r"C:\Users\Thomas\Desktop\Testclients\Jrose\3Ddata\STB\LIST_SKILL.STB", "cp932"),
    "RoseZA": (r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data\3DDATA\STB\LIST_SKILL.STB", "cp949"),
}
PREFIX_DUMP = {"kh_": "Jrose", "kak_": "Jrose", "ks_": "Jrose", "or_": "RoseZA",
               # SHIBUYA's Deaders (import-shibuya.py stage 4)
               "sw_sby": "Jrose", "sw109_": "Jrose", "zombie_police": "Jrose"}
# import-karkia.py SKILL_PORTS (row -> row) and SKILL_REPOINT targets: our rows,
# deliberately different from Jrose's at those ids. 7015-7018: import-shibuya.py's
# CAST_REMAP, which rebuilds those AI files with the casts re-pointed at our tail.
DELIBERATE = {"Jrose": {361, 1090, 3613, 3616, 3627, 3686, 3711, 3771, 3779, 3780, 3781,
                        7015, 7016, 7017, 7018},
              "RoseZA": set()}

TARGET_LABEL = {0: "cond-char", 1: "cur-target", 2: "self"}

NPC_STB = os.path.join("data", "3DDATA", "STB", "LIST_NPC.STB")
AI_STB = os.path.join("data", "3DDATA", "STB", "FILE_AI.STB")
NPC_CHR = os.path.join("data", "3DDATA", "NPC", "LIST_NPC.CHR")
NPC_AI_COL = 16
PRESENTER_FRAMES = {24, 34, 26, 25}                    # frames that launch a projectile skill
PAYLOAD_FRAMES = {24, 34, 25, 35, 26, 10, 20, 56, 66}  # frames that drain a parked skill payload


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


mon = load("audit-ai-monster-refs")  # parse_aip / build_aip / aip_files / selftest
rd = load("rose-data-reader")


# --------------------------------------------------------------------------- #
# LIST_SKILL


def load_skill_rows(root):
    """True per row when the row is entirely blank (every cell empty)."""
    stb = rd.Stb(os.path.join(root, SKILL_STB))
    load_skill_rows.stb = stb
    return [all(not stb.get(r, c) for c in range(stb.cols)) for r in range(stb.rows)]


def skill_problem(blank, idx):
    if idx < 1 or idx >= len(blank):
        return "out of range (table has %d rows)" % len(blank)
    if blank[idx]:
        return "BLANK ROW"
    return None


_dump_cache = {}


def dump_for(path):
    fn = os.path.basename(path).lower()
    for prefix, dump in PREFIX_DUMP.items():
        if fn.startswith(prefix):
            return dump
    return None


def dump_type(dump, idx):
    if dump not in _dump_cache:
        p, enc = SOURCE_DUMPS[dump]
        _dump_cache[dump] = rd.Stb(p, enc) if os.path.isfile(p) else None
    t = _dump_cache[dump]
    if t is None or idx >= t.rows:
        return None
    v = t.get(idx, 5).strip()
    return int(v) if v.isdigit() else 0


REMAPPED_TO = set()   # ids that --remap pointed casts at: OUR rows, outside the dump's namespace


def load_remapped(root):
    mpath = os.path.join(root, BACKUP_DIR, MANIFEST)
    if os.path.isfile(mpath):
        for rec in json.load(open(mpath))["files"].values():
            for r in rec.get("remapped", []):
                REMAPPED_TO.add(r["to"])


def mismatch_problem(ours, dump, idx):
    """'MISMATCH ...' when the dump's row at idx is a different kind of skill."""
    if dump is None or idx in DELIBERATE.get(dump, ()) or idx in REMAPPED_TO:
        return None
    st = dump_type(dump, idx)
    if st is None:
        return None
    ov = ours.get(idx, 5).strip()
    ot = int(ov) if ov.isdigit() else 0
    if st != ot:
        return "MISMATCH: type %d here, type %d in %s (%r here)" % (ot, st, dump, ours.get(idx, 0)[:24])
    return None


# --------------------------------------------------------------------------- #
# actions


def act_skill(a):
    """(target, skill, motion) if this is a skill-use action, else None."""
    if len(a) < MOTION_OFF + 2:
        return None
    typ, = struct.unpack_from("<I", a, 4)
    if typ != AIACT24:
        return None
    skill, motion = struct.unpack_from("<hh", a, SKILL_OFF)
    return (a[TARGET_OFF], skill, motion)


def scan(root, blank):
    """[(path, [(pat_i, ev_i, act_i, target, skill, motion, why)])]"""
    todo = []
    for p in mon.aip_files(root):
        b = open(p, "rb").read()
        _hdr, _title, pats, _tail = mon.parse_aip(b)
        hits = []
        for pi, (_pn, evs) in enumerate(pats):
            for ei, (_en, _cs, acts) in enumerate(evs):
                for ai, a in enumerate(acts):
                    s = act_skill(a)
                    if not s:
                        continue
                    tgt, skill, motion = s
                    why = skill_problem(blank, skill)
                    if why is None:
                        why = mismatch_problem(load_skill_rows.stb, dump_for(p), skill)
                    if why is None:
                        continue
                    hits.append((pi, ei, ai, tgt, skill, motion, why))
        if hits:
            todo.append((p, hits))
    return todo


def strip(b, hits):
    hdr, title, pats, tail = mon.parse_aip(b)
    drop = {}
    for pi, ei, ai, *_rest in hits:
        drop.setdefault((pi, ei), set()).add(ai)
    newpats = []
    for pi, (pn, evs) in enumerate(pats):
        newevs = []
        for ei, (en, cs, acts) in enumerate(evs):
            kill = drop.get((pi, ei), set())
            newevs.append((en, cs, [a for ai, a in enumerate(acts) if ai not in kill]))
        newpats.append((pn, newevs))
    return mon.build_aip(hdr, title, newpats, tail)


def remap(b, old_id, new_id):
    """Rewrite every AIACT24 nSkill == old_id to new_id. Returns (bytes, count)."""
    hdr, title, pats, tail = mon.parse_aip(b)
    n = 0
    newpats = []
    for pn, evs in pats:
        newevs = []
        for en, cs, acts in evs:
            out = []
            for a in acts:
                s = act_skill(a)
                if s and s[1] == old_id:
                    a = a[:SKILL_OFF] + struct.pack("<h", new_id) + a[SKILL_OFF + 2:]
                    n += 1
                out.append(a)
            newevs.append((en, cs, out))
        newpats.append((pn, newevs))
    return mon.build_aip(hdr, title, newpats, tail), n


def remotion(b, old_motion, new_motion):
    """Rewrite every AIACT24 nMotion == old_motion to new_motion. Returns (bytes, count)."""
    hdr, title, pats, tail = mon.parse_aip(b)
    n = 0
    newpats = []
    for pn, evs in pats:
        newevs = []
        for en, cs, acts in evs:
            out = []
            for a in acts:
                s = act_skill(a)
                if s and s[2] == old_motion:
                    a = a[:MOTION_OFF] + struct.pack("<h", new_motion) + a[MOTION_OFF + 2:]
                    n += 1
                out.append(a)
            newevs.append((en, cs, out))
        newpats.append((pn, newevs))
    return mon.build_aip(hdr, title, newpats, tail), n


def rechance(b, skill, pct):
    """Set the random-chance condition of every event that casts `skill` to pct.
    Returns (bytes, [old percentages])."""
    hdr, title, pats, tail = mon.parse_aip(b)
    olds = []
    newpats = []
    for pn, evs in pats:
        newevs = []
        for en, cs, acts in evs:
            if any((x := act_skill(a)) and x[1] == skill for a in acts):
                out = []
                for c in cs:
                    if len(c) > CHANCE_OFF and struct.unpack_from("<I", c, 4)[0] == AICOND_CHANCE:
                        olds.append(c[CHANCE_OFF])
                        c = c[:CHANCE_OFF] + bytes([pct]) + c[CHANCE_OFF + 1:]
                    out.append(c)
                cs = out
            newevs.append((en, cs, acts))
        newpats.append((pn, newevs))
    return mon.build_aip(hdr, title, newpats, tail), olds


def rewindow(b, skill, window):
    """Set the level window of every condition 02 on an event that casts `skill`.
    Returns (bytes, [old (lo, hi)])."""
    lo, hi = window
    hdr, title, pats, tail = mon.parse_aip(b)
    olds = []
    newpats = []
    for pn, evs in pats:
        newevs = []
        for en, cs, acts in evs:
            if any((x := act_skill(a)) and x[1] == skill for a in acts):
                out = []
                for c in cs:
                    if len(c) >= 8 + struct.calcsize(NEAR_FMT) \
                            and struct.unpack_from("<I", c, 4)[0] == AICOND_NEAR:
                        d, al, olo, ohi, n = struct.unpack_from(NEAR_FMT, c, 8)
                        olds.append((olo, ohi))
                        c = c[:8] + struct.pack(NEAR_FMT, d, al, lo, hi, n) + c[8 + struct.calcsize(NEAR_FMT):]
                    out.append(c)
                cs = out
            newevs.append((en, cs, acts))
        newpats.append((pn, newevs))
    return mon.build_aip(hdr, title, newpats, tail), olds


def window_warnings(root):
    """[(aip, skill, lo, hi)] for casts gated by a condition 02 whose level window
    is empty (lo > hi) or a single value (lo == hi) -- unsatisfiable in practice."""
    out = []
    for p in mon.aip_files(root):
        _h, _t, pats, _tail = mon.parse_aip(open(p, "rb").read())
        for _pn, evs in pats:
            for _en, cs, acts in evs:
                casts = [x[1] for a in acts if (x := act_skill(a))]
                if not casts:
                    continue
                for c in cs:
                    if len(c) >= 8 + struct.calcsize(NEAR_FMT) \
                            and struct.unpack_from("<I", c, 4)[0] == AICOND_NEAR:
                        _d, _al, lo, hi, _n = struct.unpack_from(NEAR_FMT, c, 8)
                        if lo >= hi:
                            out.extend((os.path.basename(p).lower(), sk, lo, hi) for sk in casts)
    return out


def report_window_warnings(warns):
    if not warns:
        return
    print("\nWARNING: %d cast(s) gated by a condition-02 level window nobody can pass "
          "(fix = --rewindow FILE.aip:SKILL=LO,HI):" % len(warns))
    for fn, sk, lo, hi in warns:
        print("   %-28s skill %4d  window [%d, %d]" % (fn, sk, lo, hi))


def strip_skill(b, skill, _unused=None):
    """Remove every AIACT24 casting `skill`. Returns (bytes, count)."""
    hdr, title, pats, tail = mon.parse_aip(b)
    hits = []
    for pi, (_pn, evs) in enumerate(pats):
        for ei, (_en, _cs, acts) in enumerate(evs):
            for ai, a in enumerate(acts):
                x = act_skill(a)
                if x and x[1] == skill:
                    hits.append((pi, ei, ai))
    return strip(b, hits), len(hits)


def do_remap(root, specs, motion_specs, dry, chance_specs=(), window_specs=(), strip_specs=()):
    """specs: ['file.aip:OLD=NEW', ...] -- re-point casts whose id collides with one
    of our own skills to the row the importer put the real skill on.
    motion_specs: the same syntax for nMotion -- re-point casts authored against a
    slot layout the model does not have (see the docstring).
    chance_specs / window_specs: 'file.aip:SKILL=PCT' / 'file.aip:SKILL=LO,HI'."""
    bdir = os.path.join(root, BACKUP_DIR)
    mpath = os.path.join(bdir, MANIFEST)
    man = json.load(open(mpath)) if os.path.isfile(mpath) else {"files": {}}
    files = {os.path.basename(p).lower(): p for p in mon.aip_files(root)}
    jobs = [("remapped", remap, "skill", sp) for sp in specs] + \
           [("remotioned", remotion, "motion", sp) for sp in motion_specs] + \
           [("rechanced", rechance, "chance", sp) for sp in chance_specs] + \
           [("rewindowed", rewindow, "window", sp) for sp in window_specs] + \
           [("stripped_on_request", strip_skill, "strip", sp) for sp in strip_specs]
    for key, fn_apply, what, spec in jobs:
        fn, ids = spec.split(":")
        if what == "strip":
            lhs, rhs = ids, "0"
        else:
            lhs, rhs = ids.split("=")
        old_v = int(lhs)
        new_v = tuple(int(x) for x in rhs.split(",")) if what == "window" else int(rhs)
        p = files.get(fn.lower())
        if not p:
            print("no such AI file: %s" % fn)
            return 1
        b = open(p, "rb").read()
        out, n = fn_apply(b, old_v, new_v)
        if what == "chance":
            olds, n = n, len(n)
            if not 0 <= new_v <= 100:
                print("chance must be 0-100")
                return 1
            print("   %-28s skill %d chance %s -> %d%% : %d condition(s)%s"
                  % (fn, old_v, "/".join("%d%%" % o for o in olds) or "-", new_v, n,
                     "  (dry run)" if dry else ""))
            rec = {"skill": old_v, "from": olds, "to": new_v}
        elif what == "window":
            olds, n = n, len(n)
            if len(new_v) != 2 or new_v[0] > new_v[1]:
                print("window must be LO,HI with LO <= HI")
                return 1
            print("   %-28s skill %d level window %s -> [%d, %d] : %d condition(s)%s"
                  % (fn, old_v, "/".join("[%d, %d]" % o for o in olds) or "-", new_v[0], new_v[1], n,
                     "  (dry run)" if dry else ""))
            rec = {"skill": old_v, "from": olds, "to": list(new_v)}
        elif what == "strip":
            print("   %-28s skill %d : %d cast(s) removed%s"
                  % (fn, old_v, n, "  (dry run)" if dry else ""))
            rec = {"skill": old_v, "count": n}
        else:
            print("   %-28s %s %d -> %d : %d record(s)%s"
                  % (fn, what, old_v, new_v, n, "  (dry run)" if dry else ""))
            rec = {"from": old_v, "to": new_v, "count": n}
        if dry or n == 0:
            continue
        rel = os.path.relpath(p, root).replace("\\", "/")
        man["files"].setdefault(rel, {})
        man["files"][rel].setdefault("original", base64.b64encode(b).decode("ascii"))
        man["files"][rel].setdefault(key, []).append(rec)
        open(p, "wb").write(out)
    if not dry:
        os.makedirs(bdir, exist_ok=True)
        json.dump(man, open(mpath, "w"), indent=1)
    return 0


# --------------------------------------------------------------------------- #
# release-clip check


def projectile_presented(stb, sid):
    """Mirror of Rose::Combat::is_projectile_presented_skill for a LIST_SKILL row."""
    t = stb.get(sid, 5).strip()
    t = int(t) if t.isdigit() else 0
    bl = stb.get(sid, 71).strip()
    bl = int(bl) if bl.isdigit() else 0
    return t in (5, 6) or (t in (3, 19) and bl > 0)


def zmo_events(path):
    """The set of action-frame event ids a .ZMO carries (EZMO/3ZMO trailer)."""
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if d[-4:] not in (b"EZMO", b"3ZMO"):
        return set()
    off, = struct.unpack_from("<I", d, len(d) - 8)
    n, = struct.unpack_from("<H", d, off)
    return {e for e in struct.unpack_from("<%dh" % n, d, off + 2) if e}


def resolve_ci(root, rel):
    """data/ is case-inconsistent on disk; resolve a data-relative path case-insensitively."""
    cur = root
    for part in rel.replace("\\", "/").split("/"):
        if not os.path.isdir(cur):
            return None
        hit = next((e for e in os.listdir(cur) if e.lower() == part.lower()), None)
        if hit is None:
            return None
        cur = os.path.join(cur, hit)
    return cur if os.path.isfile(cur) else None


def motion_warnings(root):
    """[(npc, name, aip, skill, type, kind, slot, clip, events)] for casts whose
    release clip (CHR slot nMotion+1) carries no frame that presents the skill.
    kind is "projectile" (bullet never launches) or "payload" (the parked result
    resolves silently ~3 s late). Read-only; the fix is a CHR slot or --remotion."""
    oro = load("import-oro")
    npc = rd.Stb(os.path.join(root, NPC_STB))
    ai = rd.Stb(os.path.join(root, AI_STB))
    skills = load_skill_rows.stb
    chr_ = oro.Chr(os.path.join(root, NPC_CHR))
    files = {os.path.basename(p).lower(): p for p in mon.aip_files(root)}
    casts_cache, ev_cache, out = {}, {}, []
    for r in range(npc.rows):
        a = npc.get(r, NPC_AI_COL).strip()
        if not a.isdigit() or not int(a):
            continue
        fn = os.path.basename(ai.get(int(a), 0).decode("latin-1")).lower()
        p = files.get(fn)
        if not p:
            continue
        if p not in casts_cache:
            _h, _t, pats, _tail = mon.parse_aip(open(p, "rb").read())
            casts = set()
            for _pn, evs in pats:
                for _en, _cs, acts in evs:
                    for act in acts:
                        s = act_skill(act)
                        if s:
                            casts.add((s[1], s[2]))
            casts_cache[p] = casts
        entry = chr_.chars[r] if r < len(chr_.chars) else None
        anims = dict(entry["anims"]) if entry else {}
        name = npc.get(r, 0).decode("latin-1", "replace")
        for sid, motion in sorted(casts_cache[p]):
            if sid <= 0 or sid >= skills.rows:
                continue
            t = skills.get(sid, 5).strip()
            t = int(t) if t.isdigit() else 0
            if t == 0:
                continue                      # a dangling cast: the strip pass reports it
            proj = projectile_presented(skills, sid)
            kind, need = ("projectile", PRESENTER_FRAMES) if proj else ("payload", PAYLOAD_FRAMES)
            slot = motion + 1
            if slot not in anims:
                out.append((r, name, fn, sid, t, kind, slot, "<no clip in slot>", ()))
                continue
            rel = chr_.motions[anims[slot]].decode("latin-1")
            if rel not in ev_cache:
                path = resolve_ci(os.path.join(root, "data"), rel)
                ev_cache[rel] = zmo_events(path) if path else None
            ev = ev_cache[rel]
            if ev is None:
                out.append((r, name, fn, sid, t, kind, slot, os.path.basename(rel), "<file missing>"))
            elif not (ev & need):
                out.append((r, name, fn, sid, t, kind, slot, os.path.basename(rel), tuple(sorted(ev))))
    return out


def report_motion_warnings(warns):
    if not warns:
        print("\nrelease clips: every cast's release clip carries a frame that presents it.")
        return
    nproj = sum(1 for w in warns if w[5] == "projectile")
    print("\nWARNING: %d cast(s) whose release clip (slot nMotion+1) cannot present the skill "
          "-- %d projectile (bullet never fires), %d payload (result resolves silently ~3 s late).\n"
          "   fix = CHR slot (import-karkia.py CHR_MOTION_OVERRIDE) or --remotion; never stripped:"
          % (len(warns), nproj, len(warns) - nproj))
    for r, name, fn, sid, t, kind, slot, clip, ev in warns:
        print("   npc %4d %-24s %-22s skill %4d type %2d %-10s slot %2d = %-34s %s"
              % (r, name[:24] or "(nameless)", fn, sid, t, kind, slot, clip,
                 ev if isinstance(ev, str) else "frames %s" % list(ev)))


def report(todo):
    for p, hits in todo:
        for _pi, _ei, _ai, tgt, skill, motion, why in hits:
            print("   %-28s skill %-5d (target %-10s motion %2d) %s"
                  % (os.path.basename(p), skill, TARGET_LABEL.get(tgt, tgt), motion, why))


def do_restore(root, only=None):
    """Put the manifest's originals back. `only` restores one file (matched by
    basename) and keeps the manifest for the others -- the whole-file restore
    would also undo every other file's strip and remap."""
    bdir = os.path.join(root, BACKUP_DIR)
    mpath = os.path.join(bdir, MANIFEST)
    if not os.path.isfile(mpath):
        print("nothing to restore (%s not found)" % mpath)
        return 0
    man = json.load(open(mpath))
    n = 0
    for rel, rec in sorted(man["files"].items()):
        if only and os.path.basename(rel).lower() != only.lower():
            continue
        open(os.path.join(root, rel), "wb").write(base64.b64decode(rec["original"]))
        del man["files"][rel]
        n += 1
    if only and n == 0:
        print("no such file in the manifest: %s" % only)
        return 1
    if man["files"]:
        json.dump(man, open(mpath, "w"), indent=1)
    else:
        os.remove(mpath)
    print("restored %d file(s) from %s" % (n, bdir))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--verify", action="store_true", help="check that no dangling refs remain")
    ap.add_argument("--restore", action="store_true", help="undo from build/ai-skill-refs/")
    ap.add_argument("--selftest", action="store_true", help="prove the rewriter round-trips")
    ap.add_argument("--remap", action="append", default=[], metavar="FILE.aip:OLD=NEW",
                    help="re-point a cast whose id collides with one of our skills")
    ap.add_argument("--remotion", action="append", default=[], metavar="FILE.aip:OLD=NEW",
                    help="re-point every cast on nMotion OLD to NEW (model slot layout)")
    ap.add_argument("--rechance", action="append", default=[], metavar="FILE.aip:SKILL=PCT",
                    help="set the random-chance condition of every event casting SKILL (testing)")
    ap.add_argument("--rewindow", action="append", default=[], metavar="FILE.aip:SKILL=LO,HI",
                    help="set the level window of every condition 02 on events casting SKILL")
    ap.add_argument("--strip", action="append", default=[], metavar="FILE.aip:SKILL",
                    help="remove every cast of SKILL from the file (a model that cannot present it)")
    ap.add_argument("--only", default=None, metavar="FILE.aip",
                    help="with --restore: restore just this file, keep the rest of the manifest")
    ap.add_argument("--strict-motions", action="store_true",
                    help="make release-clip warnings fail --verify")
    a = ap.parse_args()
    root = os.path.abspath(a.root)

    if a.restore:
        return do_restore(root, a.only)
    if a.remap or a.remotion or a.rechance or a.rewindow or a.strip:
        print("self-test (the rewriter must reproduce every file byte-identically):")
        if not mon.selftest(root):
            return 1
        return do_remap(root, a.remap, a.remotion, a.dry_run, a.rechance, a.rewindow, a.strip)

    print("self-test (the rewriter must reproduce every file byte-identically):")
    if not mon.selftest(root):
        print("\nABORT: rewriter is not byte-exact; nothing was touched.")
        return 1
    if a.selftest:
        return 0

    blank = load_skill_rows(root)
    load_remapped(root)
    todo = scan(root, blank)

    warns = motion_warnings(root)
    wwarns = window_warnings(root)

    if a.verify:
        if todo:
            print("\nVERIFY FAILED: %d file(s) still carry dangling skill refs" % len(todo))
            report(todo)
            return 1
        print("\nALL CHECKS PASSED -- no AI action casts a missing skill.")
        report_motion_warnings(warns)
        report_window_warnings(wwarns)
        return 1 if ((warns or wwarns) and a.strict_motions) else 0

    if not todo:
        print("\nnothing to do -- no AI action casts a missing skill.")
        report_motion_warnings(warns)
        report_window_warnings(wwarns)
        return 0

    nrefs = sum(len(h) for _p, h in todo)
    print("\n%d dangling skill cast(s) in %d file(s):" % (nrefs, len(todo)))
    report(todo)
    report_motion_warnings(warns)
    report_window_warnings(wwarns)

    if a.dry_run:
        print("\ndry run: nothing written")
        return 0

    bdir = os.path.join(root, BACKUP_DIR)
    os.makedirs(bdir, exist_ok=True)
    mpath = os.path.join(bdir, MANIFEST)
    man = json.load(open(mpath)) if os.path.isfile(mpath) else {"files": {}}

    for p, hits in todo:
        b = open(p, "rb").read()
        rel = os.path.relpath(p, root).replace("\\", "/")
        # Keep the *first* original seen, so a second run stays undoable to the
        # true pre-edit bytes rather than to an already-stripped file.
        man["files"].setdefault(rel, {})
        man["files"][rel].setdefault("original", base64.b64encode(b).decode("ascii"))
        man["files"][rel]["stripped"] = [
            {"pattern": pi, "event": ei, "action": ai, "target": tgt, "skill": skill,
             "motion": motion, "why": why}
            for pi, ei, ai, tgt, skill, motion, why in hits
        ]
        out = strip(b, hits)
        # Re-parse the result: exactly the flagged records must be gone and the
        # header / trailing bytes untouched.
        h0, _t0, pats_before, tail0 = mon.parse_aip(b)
        h1, _t1, pats_after, tail1 = mon.parse_aip(out)
        removed = sum(len(evs_b[ei][2]) - len(evs_a[ei][2])
                      for (_pn, evs_b), (_pn2, evs_a) in zip(pats_before, pats_after)
                      for ei in range(len(evs_b)))
        if removed != len(hits) or tail1 != tail0 or h1 != h0:
            print("ABORT: rewrite of %s did not remove exactly the flagged records" % rel)
            return 1
        open(p, "wb").write(out)
        print("   wrote %s (-%d action record(s))" % (rel, len(hits)))

    json.dump(man, open(mpath, "w"), indent=1)
    print("\nbackups + manifest: %s" % mpath)

    if scan(root, blank):
        print("VERIFY FAILED after write")
        return 1
    print("verified: no dangling skill casts remain.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
