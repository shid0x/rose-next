"""Give the Basilisk Captain (LIST_NPC 372) a model entry in LIST_NPC.CHR.

Luna's Crystal Snowfields (LUNAR/LP03) and Freezing Plateau (LP04) spawn 372 at
31 regen points, and its LIST_NPC row is complete (stats, weapon 1094, scale
1300, sounds and effects in cols 30-35) -- but LIST_NPC.CHR entry 372 is a hole,
so the client has no skeleton, parts or motions to build it from. The hole is
in most dumps (139, 667, Evo 137, Jrose, QQ, RoseZA, Rose Brazil, ruff); three
later forks fill it:
  * ruff (second copy): an exact copy of 371, the plain Basilisk;
  * titanRose: 371's skeleton and motions, model `new_basilisk3` (3 parts);
  * tsuki: 371's skeleton and motions, model `BASILISK2` (head, body01, body02).

We take tsuki's look because we already ship it and nothing uses it: PART_NPC.ZSC
objects 406 (BASILISK2 head) and 407 (BASILISK2 body01 + body02) are referenced
by no CHR entry. Their meshes are byte-identical to tsuki's (our textures are our
own copy's skin), and tsuki draws them on BASILISK\\BASILISK_BONE.ZMD -- the same
file as ours (and titanRose's), whose 18-bone hierarchy matches BASILISK2_BONE's.
So entry 372 = entry 371 (skeleton, motion slots 0-9, effects) with parts
[406, 407]. The slot layout is 371's, so the AI's motion numbers keep meaning the
same thing (de_bulcan2.aip casts 3017 on slots 8/9: casting_01 / action_skill01).

The gameserver loads LIST_NPC.CHR too (attack motion timing), so restart it as
well as re-baking for the client.

    python scripts/fix-basilisk-captain-model.py --dry-run
    python scripts/fix-basilisk-captain-model.py
    python scripts/fix-basilisk-captain-model.py --verify
    python scripts/fix-basilisk-captain-model.py --restore

Undo is per entry (build/basilisk-captain-model/manifest.json): --restore puts
the hole back only if entry 372 is still what this script wrote. `data/` is
gitignored, so this file is the record.
"""

import argparse
import copy
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from mapgen import catalogue  # noqa: E402

CHR = os.path.join(REPO, "data", "3DDATA", "NPC", "LIST_NPC.CHR")
STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
PART_ZSC = os.path.join(REPO, "data", "3DDATA", "NPC", "PART_NPC.ZSC")
BACKUP_DIR = os.path.join(REPO, "build", "basilisk-captain-model")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")

ROW = 372            # Basilisk Captain
DONOR = 371          # Basilisk: skeleton, motions, effects
PARTS = [406, 407]   # PART_NPC.ZSC: BASILISK2 head, BASILISK2 body01 + body02
DONOR_SKELETON = b"basilisk\\basilisk_bone.zmd"
PART_FOLDER = "\\basilisk2\\"


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    m = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [fname]
    try:
        spec.loader.exec_module(m)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return m


oro = load("import_oro", "import-oro.py")


def wanted(chr_):
    entry = copy.deepcopy(chr_.chars[DONOR])
    entry["models"] = list(PARTS)
    return entry


def chr_json(entry):
    if entry is None:
        return None
    return json.loads(json.dumps(
        {k: (v.decode("latin-1") if isinstance(v, bytes) else v) for k, v in entry.items()}))


def chr_from_json(d):
    if d is None:
        return None
    return {"skel": d["skel"], "name": d["name"].encode("latin-1"), "models": list(d["models"]),
            "anims": [tuple(x) for x in d["anims"]], "effects": [tuple(x) for x in d["effects"]]}


def check(chr_, stb):
    """(todo, problems): todo is True when entry 372 still has to be written."""
    problems = []
    if stb.get(ROW, 0).strip() != b"Basilisk Captain":
        problems.append("LIST_NPC %d is %r, not the Basilisk Captain" % (ROW, stb.get(ROW, 0)))
    donor = chr_.chars[DONOR] if DONOR < len(chr_.chars) else None
    if donor is None or not chr_.skeletons[donor["skel"]].lower().endswith(DONOR_SKELETON):
        problems.append("CHR %d (donor) is not the Basilisk on %s" % (DONOR, DONOR_SKELETON.decode()))
        return False, problems
    meshes, objects = catalogue.read_zsc(PART_ZSC)
    for p in PARTS:
        if p >= len(objects) or not objects[p]["parts"] or not all(
                PART_FOLDER in meshes[part["mesh"]].lower() for part in objects[p]["parts"]):
            problems.append("PART_NPC %d is not a BASILISK2 part" % p)
    users = [i for i, e in enumerate(chr_.chars) if e and i != ROW and set(PARTS) & set(e["models"])]
    if users:
        problems.append("PART_NPC %s already used by CHR %s -- the look would not be the Captain's own"
                        % (PARTS, users))
    cur = chr_.chars[ROW] if ROW < len(chr_.chars) else None
    if cur == wanted(chr_):
        return False, problems
    if cur is not None:
        problems.append("CHR %d is not a hole and not this fix (%r) -- not a repair target" % (ROW, cur))
    return True, problems


def restore():
    if not os.path.isfile(MANIFEST):
        print("nothing to restore (%s not found)" % MANIFEST)
        return 0
    man = json.load(open(MANIFEST))
    chr_ = oro.Chr(CHR)
    if chr_.chars[ROW] != chr_from_json(man["new"]):
        print("CHR %d changed since the fix -- left alone, manifest kept" % ROW)
        return 1
    chr_.chars[ROW] = chr_from_json(man["old"])
    open(CHR, "wb").write(chr_.to_bytes())
    os.remove(MANIFEST)
    print("restored CHR %d" % ROW)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    if a.restore:
        return restore()

    raw = open(CHR, "rb").read()
    chr_, stb = oro.Chr(CHR), oro.Stb(STB)
    if chr_.to_bytes() != raw:
        print("ABORT: the CHR writer does not round-trip LIST_NPC.CHR byte-for-byte")
        return 1
    todo, problems = check(chr_, stb)
    for p in problems:
        print("   " + p)

    if a.verify:
        if todo or problems:
            print("VERIFY FAILED: entry %d %s, %d problem(s)"
                  % (ROW, "still missing" if todo else "present", len(problems)))
            return 1
        print("ALL CHECKS PASSED -- the Basilisk Captain has its model (CHR %d, parts %s)."
              % (ROW, PARTS))
        return 0
    if problems:
        print("ABORT, unresolved.")
        return 1
    if not todo:
        print("nothing to do -- already applied.")
        return 0

    print("   LIST_NPC.CHR %d <- %d with parts %s (BASILISK2)" % (ROW, DONOR, PARTS))
    if a.dry_run:
        print("dry run: nothing written")
        return 0

    old = chr_.chars[ROW]
    chr_.chars[ROW] = wanted(chr_)
    os.makedirs(BACKUP_DIR, exist_ok=True)
    if not os.path.isfile(MANIFEST):
        json.dump({"old": chr_json(old), "new": chr_json(chr_.chars[ROW])}, open(MANIFEST, "w"), indent=1)
    open(CHR, "wb").write(chr_.to_bytes())
    print("wrote LIST_NPC.CHR (undo manifest in %s)" % BACKUP_DIR)

    todo2, problems2 = check(oro.Chr(CHR), oro.Stb(STB))
    if todo2 or problems2:
        print("VERIFY FAILED after write")
        return 1
    print("verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
