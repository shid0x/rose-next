"""Bring back the Zombie that quest 122 "Resurrection" spawns at the El Verloon pyramid (2026-11-05).

    python scripts/restore-quest-122-zombie.py --dry-run
    python scripts/restore-quest-122-zombie.py
    python scripts/restore-quest-122-zombie.py --restore       # undo

A tester reported the main-episode quest stuck: sprinkling the medicine at the
pyramid "goes through the motions", but nothing spawns to kill. The chain:

* The JD02 (El Verloon Desert) pyramid carries an event object whose trigger
  is 122-03 (JD02/34_32.IFO). Walking into it runs REWD_029 ``piramid01``
  (data/SCRIPTS/TUTORIAL.LUA -- spelled "piramid"), which opens
  3Ddata/Event/Object005.con, "Elixir of Resurrection".
* Its option fires 122-04: quest 122 selected, the medicine (13:611) taken,
  and -- in retail -- REWD_008 spawning monster 253, the Zombie, beside the
  player (btWho 0, range 1, team 100).
* The Zombie's LIST_NPC column 41 (event-triggered death) is 122-02, so its
  death makes the server send GSV_CHECK_NPC_EVENT; the client runs
  ``piramid02``, which opens Object006.con, "Defeat the Soulless Dead". Its
  option fires 122-01: the necklace and emblem (13:601, 13:612), then quest 123.
  Lost medicine is replaced by Harin (EM02-017, 122-05).

erase-dead-npc-rows.py (2026-10-13) erased row 253 and the REWD_008 on the
belief that no dialog fires 122-04. Nothing in an NPC's dialog does: the
dialog is opened by an area trigger through a system script, which that review
did not follow. Row 253 really was unspawnable, though -- no LIST_NPC.CHR entry
in any dump but tsuki's and Rose Brazil's (Jrose's row is blank and its quest
just as broken) -- so the fix is a model, not a revert.

What this writes:

* LIST_NPC row 253: the retail cells from the erase's undo file
  (build/erase-dead-npc-rows/undo.json), with the columns that belong to the
  model taken from Demoniac Cheney (755) so it moves and sounds like its body:
  .mon file, walk/run speed, no hand weapon, reach, material, death sound.
  Stats, level 35, AI 121 (at_cubs1.aip) and the 122-02 death event stay retail.
* LIST_NPC_S.STL key LNPC0253 ("Zombie"), from the same undo file.
* LIST_NPC.CHR entry 253: a copy of Cheney's (skeleton monster01, models
  386/387, devilchild motions). Cheney, Infernal Shirley and Berserk Arisa are
  the villagers of the cursed-mushroom incident, which quest 123's text ties
  this monster to; tsuki made the same choice ("Zombie Alex").
* QP103.QSD trigger 122-04: the REWD_008 put back after the item removal,
  byte for byte the retail entity (the erase's saved original).

erase-dead-npc-rows.py no longer lists 253, so a re-run leaves all of this
alone. --restore undoes exactly the four edits, and only where the data is
still as this script left it. Server and client both load LIST_NPC and the
CHR: restart the servers and re-bake.
"""
import argparse
import base64
import copy
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from rosefmt import qsd  # noqa: E402
from rosefmt.chr import Chr  # noqa: E402
from rosefmt.stb import Stb  # noqa: E402
from rosefmt.stl import Stl  # noqa: E402

REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data", "3DDATA")
NPC_STB = os.path.join(DATA, "STB", "LIST_NPC.STB")
NPC_STL = os.path.join(DATA, "STB", "LIST_NPC_S.STL")
NPC_CHR = os.path.join(DATA, "NPC", "LIST_NPC.CHR")
QSD_PATH = os.path.join(DATA, "QUESTDATA", "QP103.QSD")
ERASE_UNDO = os.path.join(REPO, "build", "erase-dead-npc-rows", "undo.json")

ROW, TEMPLATE, KEY, TRIGGER = 253, 755, "LNPC0253", "122-04"
STRID_COL = 40
# LIST_NPC columns that describe the body rather than the monster.
MODEL_COLS = {1: "mon file", 2: "walk speed", 3: "run speed", 5: "right-hand weapon",
              26: "reach", 28: "material", 35: "death sound"}
REWD_MOB = 0x01000008


def load_undo():
    if not os.path.exists(ERASE_UNDO):
        raise SystemExit("no %s: the retail row and reward came from it" % ERASE_UNDO)
    with open(ERASE_UNDO, encoding="utf-8") as fh:
        undo = json.load(fh)
    for need in (("rows", str(ROW)), ("stl", KEY)):
        if need[1] not in undo.get(need[0], {}):
            raise SystemExit("%s has no %s %s" % (ERASE_UNDO, need[0], need[1]))
    if "qsd_original" not in undo:
        raise SystemExit("%s has no qsd_original" % ERASE_UNDO)
    return undo


def retail_reward(undo):
    """The REWD_008 the erase removed from 122-04, as its raw entity."""
    for name, ents, _raw in qsd.walk(base64.b64decode(undo["qsd_original"])):
        if name == TRIGGER:
            for et, e in ents:
                if et == REWD_MOB and struct.unpack_from("<i", e, 8)[0] == ROW:
                    return e
    raise SystemExit("the saved QP103 has no REWD_008 for %d in %s" % (ROW, TRIGGER))


def trigger(q):
    found = [t for _pn, trigs in q["patterns"] for t in trigs if qsd.trigger_name(t["name"]) == TRIGGER]
    if len(found) != 1:
        raise SystemExit("QP103.QSD: %d triggers named %s, expected 1" % (len(found), TRIGGER))
    return found[0]


def is_spawn(e):
    return qsd.entity_type(e) == REWD_MOB and struct.unpack_from("<i", e, 8)[0] == ROW


def target_cells(npc, undo):
    cells = [bytes.fromhex(h) for h in undo["rows"][str(ROW)]]
    if len(cells) != npc.cols:
        raise SystemExit("undo row has %d cells, LIST_NPC has %d columns" % (len(cells), npc.cols))
    for c in MODEL_COLS:
        cells[c] = npc.get(TEMPLATE, c)
    return cells


def target_chr(chr_):
    e = chr_.chars[TEMPLATE]
    if e is None:
        raise SystemExit("LIST_NPC.CHR %d (the template) is a hole" % TEMPLATE)
    e = copy.deepcopy(e)
    e["name"] = b"Zombie"
    return e


def load():
    npc, stl, chr_ = Stb(NPC_STB), Stl(NPC_STL), Chr(NPC_CHR)
    with open(QSD_PATH, "rb") as fh:
        blob = fh.read()
    q = qsd.parse(blob)
    if qsd.build(q) != blob:
        raise SystemExit("QP103.QSD does not round-trip: refusing to edit it")
    return npc, stl, chr_, q


def row_cells(npc):
    return [npc.get(ROW, c) for c in range(npc.cols)]


def run(dry):
    undo = load_undo()
    npc, stl, chr_, q = load()
    cells, entry, spawn = target_cells(npc, undo), target_chr(chr_), retail_reward(undo)
    t = trigger(q)
    todo = []

    cur = row_cells(npc)
    if cur == cells:
        print("  LIST_NPC %d already restored" % ROW)
    elif any(v.strip() for v in cur):
        raise SystemExit("refusing: LIST_NPC %d holds %r now (reused since the erase?)" % (ROW, npc.s(ROW, 0)))
    else:
        for c, v in enumerate(cells):
            npc.set(ROW, c, v)
        todo.append("npc")
        print("  LIST_NPC %d <- %s, lv %s, AI %s, death event %s; model columns from %d (%s)"
              % (ROW, npc.s(ROW, 0).strip(), npc.s(ROW, 7).strip(), npc.s(ROW, 16).strip(),
                 npc.s(ROW, 41).strip(), TEMPLATE, ", ".join(MODEL_COLS.values())))

    if stl.has(KEY):
        print("  LIST_NPC_S.STL %s already present (%r)" % (KEY, stl.name(KEY, 1)))
    else:
        rec = undo["stl"][KEY]
        stl.keys.append((KEY.encode("latin-1"), rec["idx"]))
        for li, fields in enumerate(rec["langs"]):
            stl.langs[li].append([bytes.fromhex(f) for f in fields])
        stl.count = len(stl.keys)
        todo.append("stl")
        print("  LIST_NPC_S.STL += %s (%r)" % (KEY, stl.name(KEY, 1)))

    if chr_.chars[ROW] == entry:
        print("  LIST_NPC.CHR %d already restored" % ROW)
    elif chr_.chars[ROW] is not None:
        raise SystemExit("refusing: LIST_NPC.CHR %d is in use (%r)" % (ROW, chr_.chars[ROW]["name"]))
    else:
        chr_.chars[ROW] = entry
        todo.append("chr")
        print("  LIST_NPC.CHR %d <- copy of %d (skeleton %d, models %s, %d motions)"
              % (ROW, TEMPLATE, entry["skel"], entry["models"], len(entry["anims"])))

    if any(is_spawn(e) for e in t["rewds"]):
        print("  QP103.QSD %s already spawns %d" % (TRIGGER, ROW))
    else:
        t["rewds"].append(spawn)
        todo.append("qsd")
        print("  QP103.QSD %s += REWD_008 (spawn %d x1 beside the player)" % (TRIGGER, ROW))

    if not todo:
        print("  nothing to do")
        return
    if dry:
        print("  dry run, nothing written")
        return
    save(npc, stl, chr_, q, todo)
    print("  done -- restart the servers and re-bake")


def restore():
    undo = load_undo()
    npc, stl, chr_, q = load()
    cells, entry = target_cells(npc, undo), target_chr(chr_)
    t = trigger(q)
    todo = []
    if row_cells(npc) == cells:
        for c in range(npc.cols):
            npc.set(ROW, c, b"")
        todo.append("npc")
    else:
        print("  LIST_NPC %d is not as this script left it: left alone" % ROW)
    users = [r for r in range(npc.rows) if npc.s(r, STRID_COL).strip() == KEY]
    if stl.has(KEY) and not users:
        stl.remove([KEY])
        todo.append("stl")
    if chr_.chars[ROW] == entry:
        chr_.chars[ROW] = None
        todo.append("chr")
    else:
        print("  LIST_NPC.CHR %d is not as this script left it: left alone" % ROW)
    keep = [e for e in t["rewds"] if not is_spawn(e)]
    if len(keep) != len(t["rewds"]):
        t["rewds"] = keep
        todo.append("qsd")
    save(npc, stl, chr_, q, todo)
    print("  undone: %s" % (", ".join(todo) or "nothing"))


def save(npc, stl, chr_, q, todo):
    if "npc" in todo:
        npc.save(False, keep_backup=False)
    if "stl" in todo:
        stl.save(False, keep_backup=False)
    if "chr" in todo:
        chr_.save(False, keep_backup=False)
    if "qsd" in todo:
        with open(QSD_PATH, "wb") as fh:
            fh.write(qsd.build(q))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--restore", action="store_true", help="undo this script's four edits")
    a = ap.parse_args()
    if a.restore:
        restore()
    else:
        run(a.dry_run)


if __name__ == "__main__":
    main()
