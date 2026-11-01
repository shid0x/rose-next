"""Revived Quarantine Officers cast Tornado and Berserk with the player's own animations (2026-11-01).

The officer (LIST_NPC 2703, lv 223) and its Alpha (2692, lv 236) come from Jrose with two
skills, which import-karkia.py re-points to our player rows (SKILL_REPOINT): Berserk
(716 -> 361, a self-buff, from their idle pattern) and Tornado (846 -> 1090, rank 10, from
their when-hit pattern). Jrose's sordmaster/warrior1 set has no cast or skill clip, so
a86dbbff filled slots 6/7 -- the pair both skills cast on -- with the alert pose and the
plain swing, whose only event is 21. A payload drains on 24/34/25/35/26/10/20/56/66, so
neither skill presented: Berserk's buff showed at the end of a 2 s swing ("payload
cancelled ... cast abandoned" in the client log), and Tornado was "damage with no visible
cast, and a run of blank hits ending in a death" -- switched off since 2026-09-07
(AIP_DISABLE_SKILLS, the event's chance byte zeroed).

Both now cast with the **player's own clips, moved onto the officer's skeleton** by
EVO-REFORGED's retargeter (Rose_Engine/TOOLS/AnimRetarget/retarget.py, used with their
authorisation; first tried on a Red Jelly Bean the same day). sordmaster_bone is the male
avatar's 21-bone biped (hips 74 cm vs 73) plus a 4-bone plume on the head, Bone01-04,
whose rotations come from the officer's own alert clip (--fill). The two skills get a
pair each, so they no longer share one:

    slot 6 (Berserk cast)    self_casting01_m1: 28 frames, 0.9 s; event 44 at 9 (casting
                             effect 0, _self_casting_01)
    slot 7 (Berserk release) self_skill01_m1: 21 frames; 34 at 0 (the buff lands), 74 at 1
                             (casting effect 2, _self_skill_01), 24 at 18
    slot 8 (Tornado cast)    scope_casting01_m1 + scope_rotate01_m1 x 2: 33 frames, 1.1 s;
                             64 at 11 / 22 (casting effect 1, _scope_rotate_01, the charge)
    slot 9 (Tornado spin)    s_twistattack01_m1: 71 frames; 74 at 0 (casting effect 2,
                             _wind_01, the tornado), 34 / 24 at 8 / 10 (the area hit)

Berserk's AI and row are unchanged: it still casts 361 on slot 6. Tornado moves:

* **Its numbers.** 1090 is the player's power 501 and 14 m: measured with the combat sim's
  formulas on average-gear builds of every second job at lv 223 / 236, one cast took
  ~1,000-1,250 HP (700 from a Cleric) -- 12-35% of a character -- while the officer's
  normal swing does 4-35 (101 on a Mage). The officers cast **LIST_SKILL 7202**, a monster
  row of their own: a copy of 3069 (Area Blast, our monster type-17 template) with
  Tornado's effects, hit effect, icon and name key (LSkill1086), **radius 7 m** (Jrose's
  own Tornado) and **power 200**, ~290-510 a cast (Cleric to Champion; 474-514 on the
  user's Knight in game). The player's 1090 is untouched.
* **How often.** kak_livingdead.aip (2703) runs its when-hit pattern on 100% of the hits
  the officer takes and rolled Tornado at 10% -- one every ten hits. ks_2692.aip runs it
  on 40%. Both now land at **4% of hits taken**: 4% on 2703, 10% on the Alpha. Each cast is
  gated on the target standing within 6 m (AICOND04), as every cast should be
  (doc/making-monsters.md): a type 17 hits around the caster.

The Tornado event is found by its cast (AIACT24 skill 1090, or 7202 once done) in the
DAMAGED pattern; the original bytes go to the manifest. 7202 is in audit-ai-skill-refs.py's
DELIBERATE set: Jrose's row 7202 is blank, and without that entry the audit (run without
--dry-run) strips the cast as a MISMATCH -- it did once, the day this was written.

Re-running import-karkia.py's CHR or AI stages rebuilds these entries and files from the
Jrose source (slots 6/7 the plain pair, Tornado off): run this script again afterwards.
The retargeter is only needed to apply; --verify checks the clips with our own codec.

    python scripts/fix-quarantine-officer-skills.py --dry-run
    python scripts/fix-quarantine-officer-skills.py
    python scripts/fix-quarantine-officer-skills.py --verify
    python scripts/fix-quarantine-officer-skills.py --restore

--restore puts back the two AI events and CHR entries and removes what this script
appended (LIST_SKILL 7202, the four motion-list entries, the four clips) when they are
still the last rows and unused. LIST_SKILL, LIST_NPC.CHR, the .aip files and the clips are
read by the server and the client: restart the game server and re-bake.
"""

import argparse
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rosefmt import aip, chr as rchr, stb as rstb, zmo  # noqa: E402

DATA = os.path.join(REPO, "data")
BUILD = os.path.join(REPO, "build", "quarantine-officer-skills")
MANIFEST = os.path.join(BUILD, "manifest.json")
RETARGET = r"C:\Users\Thomas\Desktop\Rose\EVO-REFORGED\Rose_Engine\TOOLS\AnimRetarget\retarget.py"

SKILL_STB = r"3DDATA\STB\LIST_SKILL.STB"
NPC_CHR = r"3DDATA\NPC\LIST_NPC.CHR"

OFFICERS = {2703: (r"3DDATA\AI\kak_livingdead.aip", 4),   # npc: (AI file, Tornado chance %)
            2692: (r"3DDATA\AI\ks_2692.aip", 10)}
OLD_SKILL = 1090                    # the player's Tornado rank 10, re-pointed by import-karkia
SKILL = 7202                        # one past the end of LIST_SKILL
TEMPLATE = 3069                     # Area Blast: monster type 17
TORNADO = 1086                      # the player's Tornado: effects, icon, name key
NAME = "Tornado (Revived Quarantine Officer)"
COPY_FROM_TORNADO = [51] + list(range(56, 68)) + [70, 74, 75, 76, 86]
CELLS = {8: 700, 9: 200}            # radius cm, power
CAST_SLOT, OLD_SLOT = 8, 6          # Tornado casts on 8 (released on 9); it was on Berserk's 6
REACH_M = 6

SKEL_SRC = r"3DDATA\AVATAR\MALE.ZMD"
SKEL_DST = r"3Ddata\NPC\npc\sordmaster\sordmaster_bone.ZMD"
FILL = r"3Ddata\MOTION\NPC\warrior1\warrior1_warring_01.ZMO"
AV = "3DDATA\\MOTION\\AVATAR\\"
OUT = "3Ddata\\MOTION\\NPC\\warrior1\\"
# slot: (our clip, player clips joined in order, frames, events)
CLIPS = {
    6: (OUT + "quarantine_berserk_cast.ZMO", [AV + "self_casting01_m1.zmo"], 28, [(9, 44)]),
    7: (OUT + "quarantine_berserk_release.ZMO", [AV + "self_skill01_m1.zmo"], 21, [(0, 34), (1, 74), (18, 24)]),
    8: (OUT + "quarantine_tornado_cast.ZMO",
        [AV + "scope_casting01_m1.zmo", AV + "scope_rotate01_m1.zmo", AV + "scope_rotate01_m1.zmo"],
        33, [(11, 64), (22, 64)]),
    9: (OUT + "quarantine_tornado_spin.ZMO", [AV + "s_twistattack01_m1.ZMO"], 71, [(0, 74), (8, 34), (10, 24)]),
}

DAMAGED = 3
ACT_SKILL, COND_CHANCE, COND_RANGE = 24, 7, 4


def find_ci(rel):
    """The file for a data-relative path, matched case-insensitively per component."""
    cur = DATA
    for part in rel.replace("\\", "/").split("/"):
        hit = [f for f in os.listdir(cur) if f.lower() == part.lower()] if os.path.isdir(cur) else []
        cur = os.path.join(cur, hit[0] if hit else part)
    return cur


def events(blob):
    return [(i, e) for i, e in enumerate(zmo.events(blob)[1] or []) if e]


# ------------------------------------------------------------------ clips
def retarget(src_rel, out):
    """One player clip onto the officer's skeleton (bones by name, foot IK, the plume filled)."""
    if not os.path.isfile(RETARGET):
        raise SystemExit(f"the retargeter is not at {RETARGET} -- it is needed to build the clips")
    cmd = [sys.executable, "-I", RETARGET, "--src-skel", find_ci(SKEL_SRC), "--src", find_ci(src_rel),
           "--dst-skel", find_ci(SKEL_DST), "--fill", find_ci(FILL), "--out", out, "--quiet"]
    subprocess.run(cmd, check=True)
    return open(out, "rb").read()


def clip_problems(slot, blob):
    """Why a clip is not the one this script builds (empty: it is)."""
    _rel, _src, want_frames, want_events = CLIPS[slot]
    own = zmo.frame_layout(open(find_ci(FILL), "rb").read())[2]
    fps, frames, chans = zmo.frame_layout(blob)[2]
    out = []
    if (fps, chans) != (own[0], own[2]):
        out.append("channels are not the officer's own clips'")
    if frames != want_frames:
        out.append(f"{frames} frames, expected {want_frames}")
    if events(blob) != want_events:
        out.append(f"events {events(blob)}, expected {want_events}")
    return out


def build_clips():
    """{slot: bytes} for the four clips."""
    work = os.path.join(BUILD, "retarget")
    os.makedirs(work, exist_ok=True)
    sources = dict.fromkeys(s for _r, srcs, _f, _e in CLIPS.values() for s in srcs)
    parts = {s: retarget(s, os.path.join(work, os.path.basename(s))) for s in sources}
    built = {}
    for slot, (rel, srcs, _f, _e) in CLIPS.items():
        blob = parts[srcs[0]]
        for s in srcs[1:]:
            blob = zmo.joined(blob, parts[s])
        bad = clip_problems(slot, blob)
        if bad:
            raise SystemExit(f"{rel}: {'; '.join(bad)} -- refusing")
        built[slot] = blob
    return built


# ------------------------------------------------------------------ CHR
def our_motions():
    return [CLIPS[s][0].encode("latin-1") for s in sorted(CLIPS)]


def motion_index(chr_, rel, add):
    key = rel.encode("latin-1")
    hit = [i for i, m in enumerate(chr_.motions) if m.lower() == key.lower()]
    if hit:
        return hit[0]
    if not add:
        return None
    chr_.motions.append(key)
    return len(chr_.motions) - 1


def officer_anims(chr_, npc, add):
    want = {slot: motion_index(chr_, CLIPS[slot][0], add) for slot in sorted(CLIPS)}
    if None in want.values():
        return None
    anims = [(s, i) for s, i in chr_.chars[npc]["anims"] if s not in want]
    return sorted(anims + list(want.items()))


# ------------------------------------------------------------------ skill row
def skill_row(sk):
    row = list(sk.d[TEMPLATE])
    row[0] = NAME.encode("latin-1")
    row[1] = str(SKILL).encode()
    for c in COPY_FROM_TORNADO:
        row[c] = sk.d[TORNADO][c]
    for c, v in CELLS.items():
        row[c] = str(v).encode()
    return row


# ------------------------------------------------------------------ AI
def kind(rec):
    return (aip.record_type(rec) & 0xFF) - 1


def cast_of(acts):
    """(skill, motion) of the event's AIACT24, or None."""
    for a in acts:
        if kind(a) == ACT_SKILL:
            return struct.unpack_from("<hh", a, 10)
    return None


def tornado_event(pats, skill):
    hits = [k for k, (_n, _c, acts) in enumerate(pats[DAMAGED][1]) if (cast_of(acts) or (None,))[0] == skill]
    return hits[0] if len(hits) == 1 else None


def c_target_within(metres):        # AICOND04 cMoreLess 1 (import-hebarn.py's layout)
    return struct.pack("<II", 16, 0x04000000 | (COND_RANGE + 1)) + struct.pack("<iB", metres, 1) + b"\xcd" * 3


def rewired(blob, chance):
    """(new bytes, (index, original event)): the Tornado event on 7202 / slot 8, chance, range gate."""
    header, title, pats, tail = aip.parse(blob)
    k = tornado_event(pats, OLD_SKILL)
    if k is None:
        raise SystemExit("no single DAMAGED event casting %d -- refusing" % OLD_SKILL)
    pname, evs = pats[DAMAGED]
    evs = list(evs)
    name, conds, acts = evs[k]
    if [kind(c) for c in conds] != [COND_CHANCE] or cast_of(acts) != (OLD_SKILL, OLD_SLOT):
        raise SystemExit(f"the {OLD_SKILL} event is not the one-chance cast on slot {OLD_SLOT} this was written for")
    roll = bytearray(conds[0])
    roll[8] = chance
    new_acts = []
    for a in acts:
        if kind(a) == ACT_SKILL:
            a = bytearray(a)
            struct.pack_into("<hh", a, 10, SKILL, CAST_SLOT)
            a = bytes(a)
        new_acts.append(a)
    evs[k] = (name, [bytes(roll), c_target_within(REACH_M)], new_acts)
    pats[DAMAGED] = (pname, evs)
    return aip.build(header, title, pats, tail), (k, (name, conds, acts))


def is_rewired(blob, chance):
    _h, _t, pats, _tail = aip.parse(blob)
    k = tornado_event(pats, SKILL)
    if k is None or tornado_event(pats, OLD_SKILL) is not None:
        return False
    _n, conds, acts = pats[DAMAGED][1][k]
    return (cast_of(acts) == (SKILL, CAST_SLOT) and [kind(c) for c in conds] == [COND_CHANCE, COND_RANGE]
            and conds[0][8] == chance and conds[1] == c_target_within(REACH_M))


def unwired(blob, k, orig):
    header, title, pats, tail = aip.parse(blob)
    pname, evs = pats[DAMAGED]
    evs = list(evs)
    if tornado_event(pats, SKILL) != k:
        raise SystemExit("the rewired event is not where the manifest says -- refusing")
    evs[k] = orig
    pats[DAMAGED] = (pname, evs)
    return aip.build(header, title, pats, tail)


def ev_json(ev):
    return [ev[0].hex(), [c.hex() for c in ev[1]], [a.hex() for a in ev[2]]]


def ev_from(j):
    return (bytes.fromhex(j[0]), [bytes.fromhex(c) for c in j[1]], [bytes.fromhex(a) for a in j[2]])


# ------------------------------------------------------------------ main
def load():
    out = []
    for rel, cls in ((SKILL_STB, rstb.Stb), (NPC_CHR, rchr.Chr)):
        path = find_ci(rel)
        t = cls(path)
        if t.to_bytes() != open(path, "rb").read():
            raise SystemExit(f"ABORT: the codec does not round-trip {rel} byte for byte")
        out.append(t)
    ais = {}
    for npc, (rel, _chance) in OFFICERS.items():
        raw = open(find_ci(rel), "rb").read()
        if aip.build(*aip.parse(raw)) != raw:
            raise SystemExit(f"ABORT: {rel} does not round-trip through the codec")
        ais[npc] = raw
    return out[0], out[1], ais


def restore(sk, chr_, ais, man):
    for npc, (rel, _chance) in OFFICERS.items():
        k, ev = man["ai"][str(npc)]
        if tornado_event(aip.parse(ais[npc])[2], SKILL) is not None:
            open(find_ci(rel), "wb").write(unwired(ais[npc], k, ev_from(ev)))
        chr_.chars[npc]["anims"] = [tuple(a) for a in man["anims"][str(npc)]]
    used = {m for e in chr_.chars if e for _s, m in e["anims"]}
    ours = [m.lower() for m in our_motions()]
    n, tail = len(chr_.motions), len(ours)
    if [m.lower() for m in chr_.motions[-tail:]] == ours and not used & set(range(n - tail, n)):
        chr_.motions = chr_.motions[:-tail]
    if sk.rows == SKILL + 1 and sk.d[SKILL] == skill_row(sk) and man.get("grew_skill"):
        if not (sk.header.endswith(b"\x00\x00") and (not sk.rownames or sk.rownames[-1] == b"")):
            raise SystemExit("LIST_SKILL's last row label is not the blank one this script added -- refusing")
        sk.d.pop()
        sk.rows -= 1
        sk.header = sk.header[:-2]
        if sk.rownames:
            sk.rownames.pop()
    elif sk.rows > SKILL:
        sk.d[SKILL] = [b""] * sk.cols
    sk.save(False, keep_backup=False)
    chr_.save(False, keep_backup=False)
    listed = {m.lower() for m in chr_.motions}
    for rel in our_motions():
        if rel.lower() not in listed and os.path.isfile(find_ci(rel.decode("latin-1"))):
            os.remove(find_ci(rel.decode("latin-1")))
    os.remove(MANIFEST)
    print(f"restored the two Tornado events (skill {OLD_SKILL}, chance 0) and LIST_NPC.CHR "
          f"{sorted(OFFICERS)}; removed LIST_SKILL {SKILL} and the four clips")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    sk, chr_, ais = load()
    man = json.load(open(MANIFEST)) if os.path.isfile(MANIFEST) else None
    if sk.get(TORNADO, 0).strip() != b"Tornado" or sk.get(OLD_SKILL, 0).strip() != b"Tornado" or sk.i(TEMPLATE, 5) != 17:
        print(f"ABORT: LIST_SKILL {TORNADO}/{OLD_SKILL} are not Tornado or {TEMPLATE} is not type 17")
        return 1
    for npc in OFFICERS:
        e = chr_.chars[npc] if npc < len(chr_.chars) else None
        if not e or chr_.skeletons[e["skel"]].lower() != SKEL_DST.encode("latin-1").lower():
            print(f"ABORT: LIST_NPC.CHR entry {npc} is not on {SKEL_DST}")
            return 1

    if args.restore:
        if not man:
            print("nothing to restore")
            return 0
        restore(sk, chr_, ais, man)
        return 0

    if args.verify:
        checks = [(man is not None, "manifest"),
                  (sk.rows > SKILL and sk.d[SKILL] == skill_row(sk), f"LIST_SKILL {SKILL} {NAME}")]
        for npc, (rel, chance) in OFFICERS.items():
            checks.append((is_rewired(ais[npc], chance), f"{rel}: Tornado on {SKILL}, slot {CAST_SLOT}, {chance}%, {REACH_M} m"))
            checks.append((officer_anims(chr_, npc, add=False) == chr_.chars[npc]["anims"],
                           f"LIST_NPC.CHR {npc} slots {sorted(CLIPS)}"))
        for slot, (rel, _s, _f, _e) in CLIPS.items():
            ok = os.path.isfile(find_ci(rel)) and not clip_problems(slot, open(find_ci(rel), "rb").read())
            checks.append((ok, rel))
        ok = True
        for good, what in checks:
            if not good:
                print(f"  NOT OK: {what}")
                ok = False
        print("ALL CHECKS PASSED -- the officers berserk and spin." if ok else "VERIFY FAILED")
        return 0 if ok else 1

    if man:
        print("already applied; --restore first to redo")
        return 0
    if sk.rows > SKILL and any(sk.d[SKILL]):
        print(f"ABORT: LIST_SKILL {SKILL} is {sk.get(SKILL, 0)!r}, expected a free row")
        return 1
    built = build_clips()
    new_ai = {npc: rewired(ais[npc], chance) for npc, (_rel, chance) in OFFICERS.items()}
    for slot, blob in built.items():
        print(f"    slot {slot}  {os.path.basename(CLIPS[slot][0]):32s} {zmo.read_header(blob)[1]:3d} frames, "
              f"events {events(blob)}")
    print(f"    LIST_SKILL {SKILL} {NAME}: copy of {TEMPLATE} + Tornado's effects, "
          f"radius {CELLS[8] / 100:g} m, power {CELLS[9]}")
    for npc, (rel, chance) in OFFICERS.items():
        print(f"    {rel}: Tornado {OLD_SKILL} -> {SKILL}, slot {OLD_SLOT} -> {CAST_SLOT}, chance 0 -> {chance}%, "
              f"target within {REACH_M} m; LIST_NPC.CHR {npc} slots {sorted(CLIPS)}")
    if args.dry_run:
        print("dry run: nothing written")
        return 0

    os.makedirs(BUILD, exist_ok=True)
    grew = sk.rows <= SKILL
    json.dump(dict(ai={str(n): [k, ev_json(ev)] for n, (_b, (k, ev)) in new_ai.items()},
                   anims={str(n): chr_.chars[n]["anims"] for n in OFFICERS}, grew_skill=grew),
              open(MANIFEST, "w"), indent=1)
    for slot, blob in built.items():
        with open(find_ci(CLIPS[slot][0]), "wb") as fh:
            fh.write(blob)
    for npc, (rel, _chance) in OFFICERS.items():
        with open(find_ci(rel), "wb") as fh:
            fh.write(new_ai[npc][0])
        chr_.chars[npc]["anims"] = officer_anims(chr_, npc, add=True)
    if grew:
        sk.grow_to(SKILL + 1)
    sk.d[SKILL] = skill_row(sk)
    sk.save(False, keep_backup=False)
    chr_.save(False, keep_backup=False)
    print(f"wrote LIST_SKILL {SKILL}, LIST_NPC.CHR {sorted(OFFICERS)}, {len(built)} clips and 2 AI files "
          f"(undo: --restore)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
