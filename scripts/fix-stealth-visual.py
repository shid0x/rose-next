"""Give the Stealth status something the player can actually see.

    python scripts/fix-stealth-visual.py --apply
    python scripts/fix-stealth-visual.py --restore

Stealth is not broken
---------------------
It was reported as "nothing happens -- no status, no transparent texture, the
animation plays and that's it". The skill is in fact working. Traced end to end:

    client   SKILL_TYPE 12 (SELF_STATE_DURATION) -> CSelfBoundSkill
             -> Send_cli_SELF_SKILL                                    OK
    server   Is_SelfSKILL(12) = true -> Skill_ChangeIngSTATUS(self)    OK
             Skill_IsPassFilter: col 7 = 0 = SKILL_TARGET_FILTER_SELF  passes
             SKILL_SUCCESS_RATIO = 0, so the success roll is skipped
             entirely -- it cannot fail                                OK
             IsEnableApplay -> UpdateIngSTATUS -> SetFLAG(FLAG_ING_DISGUISE)

and the mechanic behind it is fully implemented server-side: `cobjai.cpp` (488
and 834) makes monsters refuse to target a disguised character, and
`cobjchar.cpp` (877-953) drops the flag the moment you attack or take a hit.

What is missing is every channel that would tell the player any of that
happened. `FLAG_ING_DISGUISE` appears **only** in server code -- there are zero
occurrences anywhere under `src/client/` -- so the client cannot fade the
character out even in principle. And in `LIST_STATUS.STB`, Disguise's "Phase
effects" column is **empty**, where Sleep has 1510 and Haste Attack 181. Nothing
is drawn on the character because there is nothing to draw.

So a working stealth looks exactly like a dead button.

What this changes
-----------------
One cell: `LIST_STATUS.STB` row 33 (Disguise), column 10, set to effect 2013
(`3DData\\Effect\\_smoke_black_01.eft`) -- black smoke, which reads as "hidden"
and is the closest shipped effect to the intent.

Row 34 (Invisible, ING_TRANSPARENT) has the same empty column and is left alone:
no skill in LIST_SKILL.STB applies it, so there is nothing to see it with.

Verifying an effect index before using one
------------------------------------------
An `.eft` is a small binary wrapper naming a `.ptl` particle file, and **the
basenames do not have to match** -- `_smoke_black_01.eft` points at
`@smoke_02.ptl`. Checking for a same-named `.ptl` reports a false "missing" and
would have ruled this effect out wrongly. Resolve the reference out of the file
itself; this script does that and refuses to write if any asset is absent.

What it does not fix
--------------------
The character still does not turn translucent. That genuinely needs client code,
since the client has no knowledge of the flag at all, and it is deliberately not
attempted here. A smoke aura is arguably the better read anyway: it tells you
*and* anyone watching that you are hidden, where transparency alone is easy to
miss on your own screen.
"""

import argparse
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STATUS_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_STATUS.STB")
EFFECT_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "FILE_EFFECT.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_STATUS.stealth-visual.json")

COL_NAME = 0
COL_TYPE = 1
COL_STEP_EFFECT = 10        # STATE_STEP_EFFECT -- "Phase effects"

ROW_DISGUISE = 33
ING_DISGUISE = 25           # t_AbilityINDEX / eING_TYPE, datatype.h
EFFECT_BLACK_SMOKE = 2013


def load_stb_module():
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def gi(stb, r, c):
    v = stb.get(r, c).strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def check_effect_assets(oro, index):
    """Resolve an effect index to its files and confirm every one is present.

    The .eft names its .ptl internally and the basenames need not match, so the
    reference has to be read out of the file rather than guessed from the name.
    """
    fx = oro.Stb(EFFECT_STB)
    rel = fx.get(index, 1).strip().decode("utf-8", "replace")
    if not rel:
        return None, ["FILE_EFFECT row %d has no path" % index]
    eft = os.path.join(ROOT, "data", rel.replace("\\", "/"))
    if not os.path.exists(eft):
        return rel, ["%s (the .eft itself)" % rel]
    blob = open(eft, "rb").read()
    missing = []
    for ref in re.findall(rb"[ -~]{5,}", blob):
        text = ref.decode("latin-1")
        if "\\" not in text:
            continue
        # some paths are stored with doubled separators
        path = os.path.join(ROOT, "data", text.replace("\\\\", "/").replace("\\", "/"))
        if not os.path.exists(path):
            missing.append(text)
    return rel, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_stb_module()
    stb = oro.Stb(STATUS_STB)

    saved = None
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        stb.set(ROW_DISGUISE, COL_STEP_EFFECT, saved["was"])
        with open(STATUS_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print("restored Disguise's phase effect to %r; sidecar removed"
              % (saved["was"] or "(empty)"))
        return

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- not applied")
        got = gi(stb, ROW_DISGUISE, COL_STEP_EFFECT)
        ok = got == EFFECT_BLACK_SMOKE
        print("Disguise phase effect = %d, expected %d -- %s"
              % (got, EFFECT_BLACK_SMOKE, "OK" if ok else "MISMATCH"))
        sys.exit(0 if ok else 1)

    # sanity: the row really is the one we think it is
    name = stb.get(ROW_DISGUISE, COL_NAME).strip().decode("utf-8", "replace")
    if gi(stb, ROW_DISGUISE, COL_TYPE) != ING_DISGUISE:
        sys.exit("row %d is %r with type %d, expected ING_DISGUISE (%d) -- "
                 "LIST_STATUS.STB has moved; refusing to write"
                 % (ROW_DISGUISE, name, gi(stb, ROW_DISGUISE, COL_TYPE), ING_DISGUISE))

    rel, missing = check_effect_assets(oro, EFFECT_BLACK_SMOKE)
    if missing:
        sys.exit("effect %d (%s) is missing assets, refusing to write:\n  %s"
                 % (EFFECT_BLACK_SMOKE, rel, "\n  ".join(missing)))

    was = stb.get(ROW_DISGUISE, COL_STEP_EFFECT).strip().decode("ascii", "replace")
    print("row %d %r (type %d = ING_DISGUISE)" % (ROW_DISGUISE, name, ING_DISGUISE))
    print("  phase effect: %r -> %d  (%s)" % (was or "(empty)", EFFECT_BLACK_SMOKE, rel))
    print("  all referenced assets present")

    if args.dry_run or not args.apply:
        print("\ndry run -- nothing written (pass --apply to write)")
        return

    stb.set(ROW_DISGUISE, COL_STEP_EFFECT, str(EFFECT_BLACK_SMOKE))
    with open(STATUS_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"row": ROW_DISGUISE, "col": COL_STEP_EFFECT, "was": was}, fh, indent=1)

    chk = oro.Stb(STATUS_STB)
    if gi(chk, ROW_DISGUISE, COL_STEP_EFFECT) != EFFECT_BLACK_SMOKE:
        sys.exit("verify failed after write")
    print("\ndone -- written and verified. Sidecar: %s" % os.path.basename(SIDECAR))
    print("Restart the game server and rebake the VFS. No client rebuild needed.")


if __name__ == "__main__":
    main()
