"""Give Luna's spawned Dreadnaughts (LIST_NPC 375/376) the Dreadnaught's model and sounds.

In every official dump (139, Evo 137, RoseZA, 667) rows 373/374 are Dreadnaught /
Dreadnaught King on the `trample` skeleton, and rows 375/376 are Mini Mammoth /
Mammoth on `husehorn`. The iROSE rework that re-levelled Luna (Dreadnaught 90 ->
128) wrote a second Dreadnaught / Dreadnaught King over rows 375/376 -- name, STL
key and stats cloned from 373/374 -- but left their LIST_NPC.CHR entries on the
mammoth skeleton and left the presentation columns 30-35 (idle / attack / hit
sounds, hand-hit effect, death effect and sound) blank. Our dump is from that
revision: the Freezing Plateau (LUNAR/LP04) regen points spawn 375 x43 and 376
x20, so every Dreadnaught there is a silent Mini Mammoth, and the real 373/374
spawn nowhere.

The later iROSE forks repaired it three different ways: ruff, QQ and titan point
LP04 back at 373/374 (QQ and titan also moved 376 onto trample), and tsuki moved
both 375 and 376 onto trample (its LP04 backup still spawns 375/376, as ours
does). We take tsuki's route because 375/376 are the rows our passes already
tuned: `rebalance-exp-rewards.py` wrote their EXP (col 17), and their drop table
(col 18) is the one ruff's Dreadnaught uses. Re-pointing the spawns would move
all of that onto rows no pass has looked at.

So this script:
  * copies the LIST_NPC.CHR entry of 373 -> 375 and 374 -> 376 (trample skeleton,
    PART_NPC models 220/221, trample motions). The slot layout is the same 0-9
    as husehorn's, so the AI's motion numbers keep meaning the same thing;
  * copies LIST_NPC.STB cols 30-35 from 373 -> 375 and 374 -> 376, only where the
    target cell is blank.

The gameserver loads LIST_NPC.CHR too (attack motion timing), so restart it as
well as re-baking for the client. The trample attack clip is timed differently
from the mammoth's, so the fight's rhythm changes a little -- intended.

    python scripts/fix-dreadnaught-model.py --dry-run
    python scripts/fix-dreadnaught-model.py
    python scripts/fix-dreadnaught-model.py --verify
    python scripts/fix-dreadnaught-model.py --restore

Undo is per CHR entry and per STB cell (manifest in build/dreadnaught-model/),
never a whole-file copy, so --restore cannot revert another script's later edit
to either file. `data/` is gitignored, so this file is the record.
"""

import argparse
import base64
import copy
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
CHR = os.path.join(REPO, "data", "3DDATA", "NPC", "LIST_NPC.CHR")
STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
BACKUP_DIR = os.path.join(REPO, "build", "dreadnaught-model")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")

DONOR = {
    375: 373,   # Dreadnaught      <- Dreadnaught      (trample)
    376: 374,   # Dreadnaught King <- Dreadnaught King (trample)
}
PRESENTATION_COLS = (30, 31, 32, 33, 34, 35)
DONOR_SKELETON = b"trample"      # what the donor's CHR entry must be on
WRONG_SKELETON = b"husehorn"     # what a not-yet-fixed target is on


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


def skel_path(chr_, entry):
    return chr_.skeletons[entry["skel"]].lower() if entry else b""


def plan(chr_, stb):
    """(chr_todo {row: donor}, stb_todo {(row, col): value}, problems)."""
    chr_todo, stb_todo, problems = {}, {}, []
    for row, donor in sorted(DONOR.items()):
        src, dst = chr_.chars[donor], chr_.chars[row]
        if DONOR_SKELETON not in skel_path(chr_, src):
            problems.append("CHR %d (donor) is not on the %s skeleton: %r"
                            % (donor, DONOR_SKELETON.decode(), skel_path(chr_, src)))
        elif dst != src:
            if WRONG_SKELETON in skel_path(chr_, dst):
                chr_todo[row] = donor
            else:
                problems.append("CHR %d is neither %s nor a copy of %d: %r -- not a repair target"
                                % (row, WRONG_SKELETON.decode(), donor, skel_path(chr_, dst)))
        for c in PRESENTATION_COLS:
            want, cur = stb.get(donor, c).strip(), stb.get(row, c).strip()
            if cur == want:
                continue
            if cur:
                problems.append("LIST_NPC %d col %d is %r, donor has %r -- not blank, left alone"
                                % (row, c, cur, want))
                continue
            stb_todo[(row, c)] = want
    return chr_todo, stb_todo, problems


def restore():
    if not os.path.isfile(MANIFEST):
        print("nothing to restore (%s not found)" % MANIFEST)
        return 0
    man = json.load(open(MANIFEST))
    chr_, stb = oro.Chr(CHR), oro.Stb(STB)
    bad = 0
    for row, e in man["chr"].items():
        row = int(row)
        if chr_.chars[row] != chr_from_json(e["new"]):
            print("   CHR %d changed since the fix -- left alone" % row)
            bad += 1
            continue
        chr_.chars[row] = chr_from_json(e["old"])
    for key, e in man["stb"].items():
        row, col = map(int, key.split(","))
        if stb.get(row, col) != base64.b64decode(e["new"]):
            print("   LIST_NPC %d col %d changed since the fix -- left alone" % (row, col))
            bad += 1
            continue
        stb.set(row, col, base64.b64decode(e["old"]))
    open(CHR, "wb").write(chr_.to_bytes())
    open(STB, "wb").write(stb.to_bytes())
    if bad:
        print("restored with %d entr(ies) skipped; manifest kept" % bad)
        return 1
    os.remove(MANIFEST)
    print("restored %d CHR entries and %d STB cells" % (len(man["chr"]), len(man["stb"])))
    return 0


def chr_json(entry):
    """CHR entry as JSON-safe dict (bytes name -> latin-1, tuples -> lists)."""
    return json.loads(json.dumps(
        {k: (v.decode("latin-1") if isinstance(v, bytes) else v) for k, v in entry.items()}))


def chr_from_json(d):
    return {"skel": d["skel"], "name": d["name"].encode("latin-1"), "models": list(d["models"]),
            "anims": [tuple(x) for x in d["anims"]], "effects": [tuple(x) for x in d["effects"]]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    if a.restore:
        return restore()

    chr_raw, stb_raw = open(CHR, "rb").read(), open(STB, "rb").read()
    chr_, stb = oro.Chr(CHR), oro.Stb(STB)
    if chr_.to_bytes() != chr_raw or stb.to_bytes() != stb_raw:
        print("ABORT: a writer does not round-trip its file byte-for-byte")
        return 1
    chr_todo, stb_todo, problems = plan(chr_, stb)
    for p in problems:
        print("   " + p)

    if a.verify:
        if chr_todo or stb_todo or problems:
            print("VERIFY FAILED: %d CHR entr(ies) and %d cell(s) still to fix, %d problem(s)"
                  % (len(chr_todo), len(stb_todo), len(problems)))
            return 1
        print("ALL CHECKS PASSED -- 375/376 carry the Dreadnaught's model and sounds.")
        return 0
    if problems:
        print("ABORT, unresolved.")
        return 1
    if not chr_todo and not stb_todo:
        print("nothing to do -- already applied.")
        return 0

    for row, donor in sorted(chr_todo.items()):
        print("   LIST_NPC.CHR %d <- %d : %s -> %s" % (row, donor,
              skel_path(chr_, chr_.chars[row]).decode(), skel_path(chr_, chr_.chars[donor]).decode()))
    for (row, c), v in sorted(stb_todo.items()):
        print("   LIST_NPC.STB %d col %d = %s" % (row, c, v.decode("latin-1")))
    if a.dry_run:
        print("dry run: nothing written")
        return 0

    man = {"chr": {}, "stb": {}}
    if os.path.isfile(MANIFEST):
        man = json.load(open(MANIFEST))
    for row, donor in chr_todo.items():
        old = chr_.chars[row]
        chr_.chars[row] = copy.deepcopy(chr_.chars[donor])
        man["chr"].setdefault(str(row), {"old": chr_json(old), "new": chr_json(chr_.chars[row])})
    for (row, c), v in stb_todo.items():
        man["stb"].setdefault("%d,%d" % (row, c), {
            "old": base64.b64encode(stb.get(row, c)).decode("ascii"),
            "new": base64.b64encode(v).decode("ascii")})
        stb.set(row, c, v)

    os.makedirs(BACKUP_DIR, exist_ok=True)
    json.dump(man, open(MANIFEST, "w"), indent=1)
    open(CHR, "wb").write(chr_.to_bytes())
    open(STB, "wb").write(stb.to_bytes())
    print("wrote LIST_NPC.CHR and LIST_NPC.STB (undo manifest in %s)" % BACKUP_DIR)

    c2, s2, p2 = plan(oro.Chr(CHR), oro.Stb(STB))
    if c2 or s2 or p2:
        print("VERIFY FAILED after write")
        return 1
    print("verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
