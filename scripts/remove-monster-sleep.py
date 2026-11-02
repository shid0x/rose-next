"""Monsters no longer put players to sleep (2026-11-02).

Report (the user and a tester, on the Dark Tower): the monster casts sleep, you lose your
target, and its next hit wakes you. That is all a monster's sleep can do here. Any attack
on a sleeper wakes it, hit or miss (Apply_DAMAGE), and the monster that cast it is the next
one to attack; falling asleep runs SetCMD_STOP, so what is left is a dropped attack order (a
stun has resumed the attack since 63921339; sleep never did). The only players it really
holds are the ones nobody is hitting -- a healer standing back stays asleep for up to 20 s,
and players cannot hit each other to wake them. Either nothing, or a party member out of the
fight. Players' own sleep skills (Lightning, Vanish, ...) stay: against a monster the player
chooses when to break it.

Five monster skills applied Sleep (LIST_STATUS 31), cast by 13 monsters:

    2968 Slumber          one target, 14 s, 70%  Goblin Mage 281, Little Goblin Magician 1400
    3062 Drowsy Roar      25 m, 15 s             Turak 558, Alpha Turak 559/560
    3598 Prison Slumber   30 m, 20 s             Sikuku Jailer 1714, Guard 1715, Warden 1716,
                                                 Infiltrator 1721; Revived Veteran Warrior 2688
    3557 Deep Slumber     18 m, 15 s             Cursed Ant Vagabond 1572, Dark Tower 2720,
                                                 Element Battler 2721

* **The first ten lose the cast** (STRIP: 12 casts in 7 files). Each keeps two to six other
  skills. Only the AIACT24 record goes; its event stays with its conditions and its other
  actions (the cast counter AIACT35 everywhere but the Goblins, the Turak's retarget
  AIACT06). Events are first-match-wins, so where the sleep event used to fire the monster
  now just keeps swinging, and every other skill fires exactly as often as before -- the
  Turak's Stunning Roar, a stun, in particular. Deleting the event would have handed its
  share to the next one.
* **The three trees keep a cast**: Deep Slumber was their only skill. LIST_SKILL 3557
  becomes **Torpor** -- Movement Speed Down 40% (LIST_STATUS 15) and Attack Speed Down 30%
  (17), the monster Frost Bolt's numbers (2997), on the same 18 m around the caster, 15 s
  and success 200. Rates only, flat columns blank; state slot n reads ability slot n
  (Skill_ApplyIngSTATUS). The percentage is off the player's pre-buff speed
  (CObjAVT::Get_BaseAbilityValue), so a recast refreshes and cannot stack. The name follows
  (LIST_SKILL_S.STL LMobSkill3557, every language; name-monster-skills-players.py's NAMES
  carries "Torpor" too, so a re-run keeps it), and so does the tool label in col 0: this
  script keeps name-monster-skills.py's manifest entry for that cell (build/monster-skill-
  names/) in step, so its --verify and --restore still hold.

2968, 3062 and 3598 stay in LIST_SKILL, cast by nobody. --verify also scans every .aip for a
cast of any skill that applies a sleep state, so a new import that brings one back fails it.

Re-running import-karkia.py's AI stage rebuilds KS_2688.AIP from the Jrose source: run this
again afterwards. LIST_SKILL, LIST_SKILL_S.STL and the .aip files are read by the server and
the client: restart the game server and re-bake. data/ is gitignored, so this docstring is
the committed record of the change.

    python scripts/remove-monster-sleep.py --dry-run
    python scripts/remove-monster-sleep.py
    python scripts/remove-monster-sleep.py --verify
    python scripts/remove-monster-sleep.py --restore

--restore puts back each AI file, cell and name that nobody has edited since (whole files from
the manifest in build/remove-monster-sleep/). Put NAMES[3557] in name-monster-skills-players.py
back to "Deep Slumber" with it, or that script's --verify fails.
"""

import argparse
import base64
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rosefmt import aip, stb as rstb, stl as rstl  # noqa: E402

DATA = os.path.join(REPO, "data", "3DDATA")
BUILD = os.path.join(REPO, "build", "remove-monster-sleep")
MANIFEST = os.path.join(BUILD, "manifest.json")
LABELS = os.path.join(REPO, "build", "monster-skill-names", "manifest.json")  # name-monster-skills.py's

# AI file: (sleep skill it casts, how many casts)
STRIP = {
    "DA_MAGIC1.AIP": (2968, 1),         # Goblin Mage 281, Little Goblin Magician 1400
    "ED_TURAK.AIP": (3062, 4),          # Turak 558, Alpha Turak 559/560
    "ED_S_WARDER01.AIP": (3598, 1),     # Sikuku Jailer 1714
    "ED_S_WOMAN01.AIP": (3598, 1),      # Sikuku Guard 1715
    "ED_S_WARDER01W.AIP": (3598, 1),    # Sikuku Warden 1716
    "ED_S_WOMAN02.AIP": (3598, 2),      # Sikuku Infiltrator 1721
    "KS_2688.AIP": (3598, 2),           # Revived Veteran Warrior 2688
}

SKILL = 3557
NAME_KEY = "LMobSkill3557"
OLD_NAME, NAME = "Deep Slumber", "Torpor"
LABEL_KEY = f"{SKILL},0"
OLD_LABEL = "Self Effect: Asleep (Cursed Ant Vagabond, Dark Tower, Element Battler)"
LABEL = ("Self Effect: Movement Speed Down, Attack Speed Down "
         "(Cursed Ant Vagabond, Dark Tower, Element Battler)")
# game col: (was, now). 11/12 state slots; 21-23 / 24-26 ability, flat, rate per slot.
CELLS = {
    0: (OLD_LABEL, LABEL),
    11: ("31", "15"),                   # Asleep -> Movement Speed Down
    12: ("", "17"),                     #        + Attack Speed Down
    21: ("", "23"), 22: ("", ""), 23: ("", "40"),     # AT_SPEED, -40%
    24: ("", "24"), 25: ("", ""), 26: ("", "30"),     # AT_ATK_SPD, -30%
}

ACT_SKILL = 24
ING_SLEEP = 23                          # LIST_STATUS col 1 (STATE_TYPE)


def find_ci(*parts):
    cur = DATA
    for part in parts:
        hit = [f for f in os.listdir(cur) if f.lower() == part.lower()] if os.path.isdir(cur) else []
        cur = os.path.join(cur, hit[0] if hit else part)
    return cur


def cast_skill(rec):
    """The skill an AIACT24 record casts, else None."""
    if (aip.record_type(rec) & 0xFF) - 1 != ACT_SKILL:
        return None
    return struct.unpack_from("<h", rec, 10)[0]


def stripped(blob, skill):
    """(bytes, count): every AIACT24 casting `skill` removed, its event kept."""
    header, title, pats, tail = aip.parse(blob)
    n, out = 0, []
    for pname, evs in pats:
        new = []
        for ename, conds, acts in evs:
            keep = [a for a in acts if cast_skill(a) != skill]
            n += len(acts) - len(keep)
            new.append((ename, conds, keep))
        out.append((pname, new))
    return aip.build(header, title, out, tail), n


def sleep_casts(sk, st):
    """{file: [skill, ...]} for every .aip cast of a skill that applies a sleep state."""
    states = {r for r in range(1, st.rows) if st.i(r, 1) == ING_SLEEP}
    skills = {r for r in range(1, sk.rows) if sk.i(r, 11) in states or sk.i(r, 12) in states}
    out = {}
    ai_dir = find_ci("AI")
    for f in sorted(os.listdir(ai_dir)):
        if not f.lower().endswith(".aip"):
            continue
        _h, _t, pats, _tail = aip.parse(open(os.path.join(ai_dir, f), "rb").read())
        hits = [s for _p, evs in pats for _e, _c, acts in evs for s in map(cast_skill, acts) if s in skills]
        if hits:
            out[f] = hits
    return out


def names(t):
    return [t.field(NAME_KEY, 0, lang) for lang in range(len(t.langs))]


def load():
    sk_path, st_path, stl_path = (find_ci("STB", n) for n in ("LIST_SKILL.STB", "LIST_STATUS.STB",
                                                              "LIST_SKILL_S.STL"))
    sk, st, t = rstb.Stb(sk_path), rstb.Stb(st_path), rstl.Stl(stl_path)
    for obj, path in ((sk, sk_path), (t, stl_path)):
        if obj.to_bytes() != open(path, "rb").read():
            raise SystemExit(f"ABORT: the codec does not round-trip {os.path.basename(path)} byte for byte")
    ais = {}
    for fn in STRIP:
        raw = open(find_ci("AI", fn), "rb").read()
        if aip.build(*aip.parse(raw)) != raw:
            raise SystemExit(f"ABORT: {fn} does not round-trip through the codec")
        ais[fn] = raw
    labels = json.load(open(LABELS, encoding="utf-8")) if os.path.isfile(LABELS) else None
    return sk, st, t, ais, labels


def cell(sk, c):
    return sk.get(SKILL, c).strip().decode("latin-1")


def save_labels(labels, written):
    labels["cells"][LABEL_KEY][1] = written
    json.dump(labels, open(LABELS, "w", encoding="utf-8"), indent=1, ensure_ascii=False)


def restore(sk, t, ais, labels, man):
    for fn, b64 in man["ai"].items():
        orig = base64.b64decode(b64)
        if ais[fn] == stripped(orig, STRIP[fn][0])[0]:
            open(find_ci("AI", fn), "wb").write(orig)
        else:
            print(f"   skip {fn}: edited since")
    for c, (old, new) in CELLS.items():
        if cell(sk, c) == new:
            sk.set(SKILL, c, old.encode("latin-1"))
        else:
            print(f"   skip LIST_SKILL {SKILL} col {c}: edited since")
    if set(names(t)) == {NAME}:
        t.set_name(NAME_KEY, OLD_NAME)
    if labels and labels["cells"].get(LABEL_KEY, [None, None])[1] == LABEL:
        save_labels(labels, OLD_LABEL)
    sk.save(False, keep_backup=False)
    t.save(False, keep_backup=False)
    os.remove(MANIFEST)
    print(f"restored {len(man['ai'])} AI files, LIST_SKILL {SKILL} and its name "
          f"(put NAMES[{SKILL}] in name-monster-skills-players.py back to {OLD_NAME!r})")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    sk, st, t, ais, labels = load()
    man = json.load(open(MANIFEST)) if os.path.isfile(MANIFEST) else None

    if args.restore:
        if not man:
            print("nothing to restore")
            return 0
        restore(sk, t, ais, labels, man)
        return 0

    if args.verify:
        checks = [(man is not None, "manifest")]
        for fn, (skill, _n) in STRIP.items():
            ok = man is not None and ais[fn] == stripped(base64.b64decode(man["ai"][fn]), skill)[0]
            checks.append((ok, f"{fn}: the {skill} casts gone, every event kept"))
        for c, (_old, new) in CELLS.items():
            checks.append((cell(sk, c) == new, f"LIST_SKILL {SKILL} col {c} = {new!r}"))
        checks.append((set(names(t)) == {NAME}, f"{NAME_KEY} = {NAME!r} in every language"))
        if labels:
            checks.append((labels["cells"].get(LABEL_KEY, [None, None])[1] == LABEL,
                           "name-monster-skills.py's manifest knows the new label"))
        left = sleep_casts(sk, st)
        checks.append((not left, f"no AI file casts a sleep skill (still: {left})"))
        ok = True
        for good, what in checks:
            if not good:
                print(f"  NOT OK: {what}")
                ok = False
        print("ALL CHECKS PASSED -- no monster puts a player to sleep." if ok else "VERIFY FAILED")
        return 0 if ok else 1

    if man:
        print("already applied; --restore first to redo")
        return 0
    bad = [f"col {c} is {cell(sk, c)!r}, expected {old!r}" for c, (old, _new) in CELLS.items()
           if cell(sk, c) != old]
    if set(names(t)) != {OLD_NAME}:
        bad.append(f"{NAME_KEY} is {names(t)}, expected {OLD_NAME!r}")
    if labels and labels["cells"].get(LABEL_KEY, [None, None])[1] != OLD_LABEL:
        bad.append(f"name-monster-skills.py's manifest has {labels['cells'].get(LABEL_KEY)} for {LABEL_KEY}")
    new_ai = {}
    for fn, (skill, want) in STRIP.items():
        new_ai[fn], n = stripped(ais[fn], skill)
        if n != want:
            bad.append(f"{fn} casts {skill} {n} time(s), expected {want}")
    if bad:
        print("ABORT: the data is not what this script was written against:")
        for b in bad:
            print(f"   {b}")
        return 1

    for fn, (skill, n) in STRIP.items():
        print(f"    {fn:20s} {n} cast(s) of {skill} removed, events kept")
    print(f"    LIST_SKILL {SKILL} {OLD_NAME} -> {NAME}: states 31 -> 15 + 17, "
          f"move -{CELLS[23][1]}%, attack speed -{CELLS[26][1]}% (18 m, 15 s unchanged)")
    if args.dry_run:
        print("dry run: nothing written")
        return 0

    os.makedirs(BUILD, exist_ok=True)
    json.dump({"ai": {fn: base64.b64encode(raw).decode("ascii") for fn, raw in ais.items()}},
              open(MANIFEST, "w"), indent=1)
    for fn, blob in new_ai.items():
        open(find_ci("AI", fn), "wb").write(blob)
    for c, (_old, new) in CELLS.items():
        sk.set(SKILL, c, new.encode("latin-1"))
    t.set_name(NAME_KEY, NAME)
    sk.save(False, keep_backup=False)
    t.save(False, keep_backup=False)
    if labels:
        save_labels(labels, LABEL)
    print(f"wrote {len(new_ai)} AI files, LIST_SKILL {SKILL} and its name (undo: --restore)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
