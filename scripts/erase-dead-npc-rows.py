"""Erase the 219 LIST_NPC rows that can never spawn and that nothing live uses (2026-10-13).

    python scripts/erase-dead-npc-rows.py --dry-run
    python scripts/erase-dead-npc-rows.py
    python scripts/audit-ai-monster-refs.py        # then strip the two dead AI files' refs
    python scripts/erase-dead-npc-rows.py --verify
    python scripts/erase-dead-npc-rows.py --restore

The GM item browser listed 223 broken monster rows (210 with town NPCs
hidden). Every one is a row the server can never put in the world: a blank
column 0 (the server's NPC_NAME -- `get_cstr` is nullptr for a blank cell and
every spawn path refuses on it), an empty model column, level 0, an HP column
past 20,000, or no LIST_NPC.CHR entry. Four of them were live content and were
given names instead (fix-nameless-npc-rows.py: the Junon Ent 322, the Woopie
Hooligans 306-308). The other 219 are erased, as the user asked:

* **206 that nothing references** -- name-only stubs (Baboa Ghost x13,
  Christmas Tree x15, the "1-10" ... "241-250" labels, town-NPC names with no
  columns), nameless retail rows (the jellybean ladder 230-249, the wards
  226-229, the event snowmen and penguin 293-298, the mummies 943-963, the
  sabers 1760-1770, the unused Dreadnaught/King 373/374 -- LP04 spawns 375/376,
  which fix-dreadnaught-model.py gave their model), and the 13-million-HP Moss
  Golem / Nepenthes / Turak copies 996-998.
* **13 that only dead content references**, erased with the reference:
  Zombie 253 (no CHR entry; spawned only by QP103.QSD trigger 122-04, a quest
  chain no dialog fires -- the REWD_008 reward is removed from the trigger),
  **wrong, and taken back out on 2026-11-05**: no *NPC* dialog fires 122-04,
  but the JD02 pyramid's area trigger does (122-03 -> REWD_029 ``piramid01``
  in TUTORIAL.LUA -> Object005.con), so quest 122 stalled with nothing to
  kill. restore-quest-122-zombie.py gave 253 a model and its spawn back; the
  list below is now 218 rows. Before calling a quest chain dead, follow the
  IFO event-object triggers and the REWD_029 system scripts too.
  Dragon Egg / Hatchling 678/679 (CL_SEED.AIP, which no FILE_AI row loads) and
  the candle-ghost forms 741-750 (Change_CHAR targets in GHOST_M3.AIP, whose only
  user is GhostSeed 704). "Nothing spawns 704" was wrong too (checked 2026-11-05):
  eleven AIs summon it on death (AIACT37, e.g. Penguin General, Hell Mag), but
  741-750 had blank column-0 names, so the server refused every Change_CHAR into
  them -- the seed never grew in our build, and erasing them changed nothing. The AI actions are stripped by
  audit-ai-monster-refs.py once the rows are blank -- its job.

Cleaning the references is not tidiness: an erased row looks free, importers
pick free rows by hand, and a leftover reference would then silently name
whatever lands there.

"Erased" means fully, the rule since Ruven / the Little Angel (fix-spawned-npc-
gaps.py item 7): every LIST_NPC cell blanked, the LIST_NPC.CHR entry made a
hole, and the LIST_NPC_S.STL key removed -- **only when no surviving row
shares it** (twelve do: 226/227 carry the key of monster 41, 298 Mr Baggie's,
373/374 the Dreadnaughts', ...). PART_NPC.ZSC models stay; other rows share
most of them. Rows are not removed -- the id is the index.

Guards: the run refuses a listed row that is no longer dead (someone repaired
it since 2026-10-13), and re-scans every place an id can be named -- REGEN and
MOB lumps of every map, every .aip's monster actions and NPC-select
conditions, every .qsd's COND_013/017 and REWD_008/031, LIST_SKILL type-14
summons -- refusing any reference but the three above. Undo is cell-level
(`build/erase-dead-npc-rows/undo.json`: the old cells, CHR entries and STL
entries; the QSD as its original bytes), and --restore puts back only what is
still as this script left it. Server and client load LIST_NPC and the CHR:
restart the servers and re-bake.
"""
import argparse
import base64
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import rosedata as oro  # noqa: E402
from rosefmt import aip, ifo, qsd  # noqa: E402

REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data")
BUILD = os.path.join(REPO, "build", "erase-dead-npc-rows")
UNDO = os.path.join(BUILD, "undo.json")

NPC_STB = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL = r"3DDATA\STB\LIST_NPC_S.STL"
SKILL_STB = r"3DDATA\STB\LIST_SKILL.STB"
NPC_MON_COL, NPC_LEVEL_COL, NPC_HP_COL = 1, 7, 8
NPC_STRID_COL = oro.NPC_STRID_COL            # 40
SERVER_MAX_HP = 20000                        # MobRowRefusalReason, cheatcmd.cpp
SKILL_TYPE_COL, SKILL_SUMMON_COL = 4, 28     # SKILL_SUMMON_PET, read for SKILL_TYPE_14 only
SKILL_TYPE_SUMMON = 14

_RANGES = [(226, 249), (293, 295), (297, 298), 335, 350, 367, (373, 374), 377, 380, 411,
           (587, 599), (607, 609), (676, 679), (694, 697), 700, (741, 750), 939, 941, (943, 959),
           (961, 963), 965, (970, 972), (974, 977), (996, 1000), (1022, 1023), 1042, 1065, 1074,
           1108, 1120, 1124, (1126, 1128), (1136, 1137), 1152, 1174, 1189, 1194, 1209, 1269,
           (1475, 1489), (1600, 1604), 1608, (1612, 1616), (1618, 1632), (1635, 1642), (1644, 1650),
           (1660, 1684), (1760, 1770)]
ERASE = sorted({i for r in _RANGES for i in (range(r[0], r[1] + 1) if isinstance(r, tuple) else [r])})
assert len(ERASE) == 218, len(ERASE)          # the reviewed set, less 253 (see the docstring)

# The references the run accepts: dead content, cleaned with the rows. The one
# QSD entry is kept only so --restore can find the file; 253 is no longer in
# ERASE, so the run neither accepts nor strips it.
QSD_DEAD = (r"3DDATA\QUESTDATA\QP103.QSD", "122-04", 253)      # file, trigger, REWD_008 monster
AIP_DEAD = {"cl_seed.aip": {678, 679}, "ghost_m3.aip": set(range(741, 751))}

G = 0x0B000000
MONSTER_ACTS = {0x0A | G, 0x0B | G, 0x13 | G, 0x15 | G, 0x25 | G, 0x26 | G}   # WORD id at offset 8
AICOND_NPC = 0x12 | 0x04000000               # AICOND_17 "select NPC", int at offset 8
REWD = 0x01000000
COND_NPC, COND_NPCVAR, REWD_MOB, REWD_MOBVAR = 13, 17, REWD | 8, REWD | 31


def P(rel):
    return oro.rel_path(DATA, rel)


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha1(fh.read()).hexdigest()


# ------------------------------------------------------------------ checks
def dead_reason(npc, chr_, i):
    """Why the server can never put row ``i`` in the world, or None."""
    if not npc.s(i, 0).strip():
        return "no column-0 name"
    if not npc.s(i, NPC_MON_COL).strip():
        return "no model column"
    if npc.i(i, NPC_LEVEL_COL) <= 0:
        return "level 0"
    if npc.i(i, NPC_HP_COL) > SERVER_MAX_HP:
        return "junk HP column"
    if i >= len(chr_.chars) or chr_.chars[i] is None:
        return "no LIST_NPC.CHR entry"
    return None


def references(ids):
    """[(id, where, allowed)] for every place that names one of ``ids``."""
    out = []

    def add(i, where, allowed=False):
        if i in ids:
            out.append((i, where, allowed))

    for dp, _d, fs in os.walk(os.path.join(DATA, "3DDATA")):
        for f in fs:
            path, low = os.path.join(dp, f), f.lower()
            rel = os.path.relpath(path, DATA)
            if low.endswith(".ifo"):
                with open(path, "rb") as fh:
                    m = ifo.parse(fh.read())
                for g in m.lump(ifo.REGEN) or []:
                    for mob in list(g.basic) + list(g.tactic):
                        if mob.count > 0:
                            add(mob.mob, "regen %s" % rel)
                for rec in m.lump(ifo.MOB) or []:
                    add(rec.obj_id, "placement %s" % rel)
            elif low.endswith(".aip"):
                with open(path, "rb") as fh:
                    _h, _t, pats, _tail = aip.parse(fh.read())
                allowed = AIP_DEAD.get(low, set())
                for _pn, evs in pats:
                    for _en, conds, acts in evs:
                        for a in acts:
                            if len(a) >= 10 and struct.unpack_from("<I", a, 4)[0] in MONSTER_ACTS:
                                mid = struct.unpack_from("<H", a, 8)[0]
                                add(mid, "ai action %s" % rel, mid in allowed)
                        for c in conds:
                            if len(c) >= 12 and struct.unpack_from("<I", c, 4)[0] == AICOND_NPC:
                                add(struct.unpack_from("<i", c, 8)[0], "ai select-NPC %s" % rel)
            elif low.endswith(".qsd"):
                with open(path, "rb") as fh:
                    blob = fh.read()
                for trig, ents, _raw in qsd.walk(blob):
                    for et, e in ents:
                        if et == COND_NPC:
                            add(struct.unpack_from("<i", e, 8)[0], "quest COND_013 %s %s" % (rel, trig))
                        elif et == COND_NPCVAR:
                            for off in (8, 16):
                                add(struct.unpack_from("<i", e, off)[0], "quest COND_017 %s %s" % (rel, trig))
                        elif et in (REWD_MOB, REWD_MOBVAR):
                            mid = struct.unpack_from("<i", e, 8)[0]
                            dead = (et == REWD_MOB and oro.norm(rel) == oro.norm(QSD_DEAD[0])
                                    and trig == QSD_DEAD[1] and mid == QSD_DEAD[2])
                            add(mid, "quest reward %s %s" % (rel, trig), dead)
    skills = oro.Stb(P(SKILL_STB))
    for r in range(skills.rows):
        if skills.i(r, SKILL_TYPE_COL) == SKILL_TYPE_SUMMON:
            add(skills.i(r, SKILL_SUMMON_COL), "summon skill %d %s" % (r, skills.s(r, 0).strip()))
    return out


# ------------------------------------------------------------------ CHR json
def chr_json(e):
    return None if e is None else {"skel": e["skel"], "name": e["name"].decode("latin-1"),
                                   "models": e["models"], "anims": e["anims"], "effects": e["effects"]}


def chr_from_json(d):
    return None if d is None else dict(skel=d["skel"], name=d["name"].encode("latin-1"),
                                       models=list(d["models"]), anims=[tuple(x) for x in d["anims"]],
                                       effects=[tuple(x) for x in d["effects"]])


def hx(b):
    return b.hex()


# ------------------------------------------------------------------ run
def strip_qsd_reward(blob):
    """QP103 without the REWD_008 that spawns the Zombie; (blob, removed?)."""
    q = qsd.parse(blob)
    if qsd.build(q) != blob:
        raise SystemExit("%s does not round-trip: refusing to edit it" % QSD_DEAD[0])
    removed = False
    for _pn, trigs in q["patterns"]:
        for t in trigs:
            if qsd.trigger_name(t["name"]) != QSD_DEAD[1]:
                continue
            keep = []
            for e in t["rewds"]:
                if (qsd.entity_type(e) == REWD_MOB and struct.unpack_from("<i", e, 8)[0] == QSD_DEAD[2]):
                    removed = True
                    continue
                keep.append(e)
            t["rewds"] = keep
    return qsd.build(q), removed


def run(dry):
    npc = oro.Stb(P(NPC_STB))
    chr_ = oro.Chr(P(oro.NPC_CHR_REL))
    stl = oro.Stl(P(NPC_STL))
    todo = [i for i in ERASE if any(npc.get(i, c).strip() for c in range(npc.cols))
            or (i < len(chr_.chars) and chr_.chars[i] is not None)]
    alive = [i for i in todo if dead_reason(npc, chr_, i) is None]
    if alive:
        raise SystemExit("refusing: rows %s were repaired since the 2026-10-13 review "
                         "(they can spawn now) -- take them out of ERASE" % alive)
    refs = references(set(ERASE))
    live = [(i, w) for i, w, ok in refs if not ok]
    if live:
        for i, w in live:
            print("  REFERENCED %d %r <- %s" % (i, npc.s(i, 0).strip(), w))
        raise SystemExit("refusing: %d live reference(s) to rows in ERASE" % len(live))
    print("  %d rows listed, %d still to erase; %d dead reference(s) accepted"
          % (len(ERASE), len(todo), len(refs)))

    undo = json.load(open(UNDO)) if os.path.exists(UNDO) else {"rows": {}, "chr": {}, "stl": {}}
    erased = set(ERASE)
    survivors = {npc.s(r, NPC_STRID_COL).strip() for r in range(npc.rows) if r not in erased}
    todo_keys = {npc.s(i, NPC_STRID_COL).strip() for i in todo} - {""}
    for i in todo:
        print("  erase %4d %-40s (%s)" % (i, (npc.s(i, 0).strip() or npc.s(i, NPC_MON_COL).strip())[:40],
                                          dead_reason(npc, chr_, i)))
        undo["rows"].setdefault(str(i), [hx(npc.get(i, c)) for c in range(npc.cols)])
        key = npc.s(i, NPC_STRID_COL).strip()
        if key and key not in survivors and stl.has(key) and key not in undo["stl"]:
            j = stl.index(key)
            undo["stl"][key] = {"idx": stl.keys[j][1], "langs": [[hx(f) for f in rows[j]] for rows in stl.langs]}
        for c in range(npc.cols):
            npc.set(i, c, b"")
        if i < len(chr_.chars) and chr_.chars[i] is not None:
            undo["chr"].setdefault(str(i), chr_json(chr_.chars[i]))
            chr_.chars[i] = None
    gone = [k for k in undo["stl"] if stl.has(k)]
    if gone:
        stl.remove(gone)
        print("  LIST_NPC_S.STL -= %d keys (%d kept: a surviving row shares them)"
              % (len(gone), len(todo_keys & survivors)))

    qpath = P(QSD_DEAD[0])
    with open(qpath, "rb") as fh:
        qblob = fh.read()
    new_q, removed = strip_qsd_reward(qblob) if QSD_DEAD[2] in ERASE else (qblob, False)
    if removed:
        print("  %s trigger %s: REWD_008 (spawn %d) removed" % (os.path.basename(qpath), QSD_DEAD[1], QSD_DEAD[2]))
    if not todo and not removed:
        print("  already done")
        return

    if dry:
        print("  dry run, nothing written")
        return
    os.makedirs(BUILD, exist_ok=True)
    if removed:
        undo.setdefault("qsd_original", base64.b64encode(qblob).decode("ascii"))
        with open(qpath, "wb") as fh:
            fh.write(new_q)
        undo["qsd_after"] = sha(qpath)
    with open(UNDO, "w", encoding="utf-8") as fh:
        json.dump(undo, fh)
    npc.save(dry, keep_backup=False)
    chr_.save(dry, keep_backup=False)
    if gone:
        stl.save(dry, keep_backup=False)
    print("  done -- now run audit-ai-monster-refs.py, then --verify; restart the servers and re-bake")


def verify():
    npc = oro.Stb(P(NPC_STB))
    chr_ = oro.Chr(P(oro.NPC_CHR_REL))
    stl = oro.Stl(P(NPC_STL))
    ok = True
    left = [i for i in ERASE if any(npc.get(i, c).strip() for c in range(npc.cols))]
    holes = [i for i in ERASE if i < len(chr_.chars) and chr_.chars[i] is not None]
    erased = set(ERASE)
    users = {}
    for r in range(npc.rows):
        k = npc.s(r, NPC_STRID_COL).strip()
        if k:
            users.setdefault(k, []).append(r)
    undo = json.load(open(UNDO)) if os.path.exists(UNDO) else {"stl": {}}
    stale = [k for k in undo["stl"] if stl.has(k) and not any(u not in erased for u in users.get(k, []))]
    refs = references(erased)
    for what, bad in (("LIST_NPC rows not blank", left), ("CHR entries not holes", holes),
                      ("STL keys still present", stale), ("references left", refs)):
        print("  %s %-24s %d" % ("ok  " if not bad else "FAIL", what, len(bad)))
        for b in bad[:12]:
            print("         %s" % (b,))
        ok &= not bad
    print("verify:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def restore():
    if not os.path.exists(UNDO):
        raise SystemExit("nothing to restore (no %s)" % UNDO)
    undo = json.load(open(UNDO))
    npc = oro.Stb(P(NPC_STB))
    chr_ = oro.Chr(P(oro.NPC_CHR_REL))
    stl = oro.Stl(P(NPC_STL))
    rows = chrs = keys = 0
    for i, cells in undo["rows"].items():
        i = int(i)
        if any(npc.get(i, c).strip() for c in range(npc.cols)):
            print("  LIST_NPC %d was reused since (%r): left alone" % (i, npc.s(i, 0)))
            continue
        for c, v in enumerate(cells):
            npc.set(i, c, bytes.fromhex(v))
        rows += 1
    for i, e in undo["chr"].items():
        i = int(i)
        if chr_.chars[i] is not None:
            print("  LIST_NPC.CHR %d was reused since: left alone" % i)
            continue
        chr_.chars[i] = chr_from_json(e)
        chrs += 1
    for key, rec in undo["stl"].items():
        if stl.has(key):
            print("  LIST_NPC_S.STL %s exists again: left alone" % key)
            continue
        stl.keys.append((key.encode("latin-1"), rec["idx"]))
        for li, fields in enumerate(rec["langs"]):
            stl.langs[li].append([bytes.fromhex(f) for f in fields])
        stl.count = len(stl.keys)
        keys += 1
    if "qsd_original" in undo:
        qpath = P(QSD_DEAD[0])
        if sha(qpath) != undo.get("qsd_after"):
            print("  %s changed since: left alone" % QSD_DEAD[0])
        else:
            with open(qpath, "wb") as fh:
                fh.write(base64.b64decode(undo["qsd_original"]))
            print("  %s restored" % QSD_DEAD[0])
    npc.save(False, keep_backup=False)
    chr_.save(False, keep_backup=False)
    stl.save(False, keep_backup=False)
    os.remove(UNDO)
    print("  restored %d rows, %d CHR entries, %d STL keys -- audit-ai-monster-refs.py --restore puts "
          "the AI actions back" % (rows, chrs, keys))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    if a.verify:
        return verify()
    if a.restore:
        restore()
        return 0
    run(a.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
