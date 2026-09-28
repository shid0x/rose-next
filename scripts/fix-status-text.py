"""Status names and chat messages in plain English (LIST_STATUS_S.STL).

Part C of the 2026-09-28 tooltip review. LIST_STATUS_S is a QEST01 table, four
fields per key (LIST_STATUS col 20), English block 1. How the client uses them
(CStringManager, CEndurancePack):

  1  name          -- buff bar hover, party frames, the skill tooltip's Effect line
  2  "desc"        -- shown in chat when the status lands on *you*; the code says
                      so (GetStatusStartMsg returns strDesc: "2nd column holds the
                      setting message")
  3  "start msg"   -- shown in chat when it wears off (GetStatusEndMsg)
  4  "end msg"     -- unused

How a status reaches a player:
  * a skill applies LIST_SKILL col 11/12 directly;
  * a use item points LIST_USEITEM col 24 at a *container* row whose cols 5/7
    name the rows actually applied (recvpacket.cpp, AddEnduranceEntity) -- the
    container's own strings never show;
  * another character's icons come from flags, and the client maps each flag to
    one fixed row (CEndurancePack::m_StateSTBIDXTable: every poison shows as row
    7, every slow as 15 ...);
  * the goddess buff is row 61 (CEnduranceGoddess).

Sleep (31) and Fainted (32) differ in one thing (cenduranceproperty.cpp): Sleep
breaks when the target is hit, Fainted does not.

`--dump` writes build/status-text/review.md: every row with its four fields,
type, harmful flag (col 3: 1 harmful, 2 a buff's own drawback) and every route
above that reaches it.

The rewrite (reviewed 2026-09-28): names are short scannable labels ("Defense
Down", "Stunned") because they sit on the buff bar and in the skill tooltip's
Effect line; the flavour goes in the chat lines, second person, a sensation
and then the plain fact. Poison keeps its five tiers as Poison I-V, so a
skill whose poison strengthens with rank shows it in the tooltip. Messages
that were wrong rather than clumsy: the three MP recoveries announced HP;
Defense, Magic Resistance and both decreases announced "increase in attack
power"; Slow Attack announced an increase; row 27 (Critical down) was named
"Cri Increased"; the monster Dodge Up copy announced a decrease; Burning,
Taunt and Reveal printed empty lines. Rows 59/60 (Berserk's and Endure's
drawbacks) share rows 21/15's keys and so their text. Row 61, the goddess
blessing, points at LSTA059, which the STL never had -- a blank buff name and
an empty chat line; the entry is appended. Rows 40-42 and 57 are never shown
and are left alone.

Every entry carries the name it is expected to replace; a name that is neither
that nor ours (a later import) refuses the write. The sidecar
data/3DDATA/STB/status-text.json keeps the replaced fields and the appended
key; `--restore` puts exactly those back. Client data only: re-bake the VFS.

Usage:
    python scripts/fix-status-text.py --dump
    python scripts/fix-status-text.py --dry-run
    python scripts/fix-status-text.py
    python scripts/fix-status-text.py --verify
    python scripts/fix-status-text.py --restore
"""
import argparse
import collections
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
STL = os.path.join(STB_DIR, "LIST_STATUS_S.STL")
REVIEW_DIR = os.path.join(ROOT, "build", "status-text")
REVIEW = os.path.join(REVIEW_DIR, "review.md")
SIDECAR = os.path.join(STB_DIR, "status-text.json")

_POISON = ("Poison burns in your veins, eating away at your HP.",
           "The poison has run its course.")


def _stat(stat, up_line, down_line):
    """(Up entry, Down entry) for a stat status, same shape for every stat."""
    back = "Your %s is back to normal." % stat
    return ((stat + " Up", "%s Your %s is increased." % (up_line, stat), back),
            (stat + " Down", "%s Your %s is decreased." % (down_line, stat), back))


_MAXHP = _stat("Max HP", "Vigor floods your body.", "")[0]
_MAXMP = _stat("Max MP", "Your mind opens wide.", "")[0]
_MOVE = _stat("Movement Speed", "Your stride lightens.", "Your legs turn to lead.")
_ASPD = _stat("Attack Speed", "Your hands blur.", "Your arms grow heavy.")
_ATK = _stat("Attack Power", "Strength surges through you.", "Your strength drains away.")
_DEF = _stat("Defense", "Your guard hardens.", "Your guard falters.")
_RES = _stat("Magic Resistance", "A ward settles over you.", "Your wards unravel.")
_HIT = _stat("Accuracy", "Your aim sharpens.", "Your aim wavers.")
_CRIT = _stat("Critical", "You see every weak spot.", "Your strikes lose their edge.")
_DODGE = _stat("Dodge", "You move like smoke.", "You lose your footing.")
_ADD = ("Additional Damage", "Your attacks strike harder than they should.",
        "Your additional damage has worn off.")

# LIST_STATUS_S key -> (name it replaces, new name, lands on you, wears off)
TEXT = {
    "LSTA001": ("HP Recovery", "HP Recovery",
                "Warmth spreads through your body as your wounds mend.",
                "Your HP recovery has ended."),
    "LSTA004": ("MP Recovery", "MP Recovery",
                "Your mind clears as your mana flows back.",
                "Your MP recovery has ended."),
    "LSTA007": ("Poisoned  1", "Poison I") + _POISON,
    "LSTA008": ("Poisoned 2", "Poison II") + _POISON,
    "LSTA009": ("Poisoned  3", "Poison III") + _POISON,
    "LSTA010": ("Poisoned  4", "Poison IV") + _POISON,
    "LSTA011": ("Poisoned  5", "Poison V") + _POISON,
    "LSTA012": ("MaxHP increased",) + _MAXHP,
    "LSTA013": ("MaxMP increased",) + _MAXMP,
    "LSTA014": ("Rapid Run",) + _MOVE[0],
    "LSTA015": ("Slow Run ",) + _MOVE[1],                 # also row 60, Endure's slow
    "LSTA016": ("Haste Attack",) + _ASPD[0],
    "LSTA017": ("Slow Attack",) + _ASPD[1],
    "LSTA018": ("Atk Power Increased",) + _ATK[0],
    "LSTA019": ("Atk Power Decreased",) + _ATK[1],
    "LSTA020": ("Def Increased",) + _DEF[0],
    "LSTA021": ("Def Decreased ",) + _DEF[1],             # also row 59, Berserk's penalty
    "LSTA022": ("Magick Resistance Increased",) + _RES[0],
    "LSTA023": ("Magick Resistance Decreased",) + _RES[1],
    "LSTA024": ("hitting rate Increased",) + _HIT[0],
    "LSTA025": ("hitting rate Decreased",) + _HIT[1],
    "LSTA026": ("Cri Increased",) + _CRIT[0],
    "LSTA027": ("Cri Increased",) + _CRIT[1],             # type 19: it is a decrease
    "LSTA028": ("Dodge Rate Increased",) + _DODGE[0],
    "LSTA029": ("Dodge Rate Decreased",) + _DODGE[1],
    "LSTA030": ("Dumb", "Silenced", "Your throat seizes. You cannot cast spells.",
                "Your voice returns."),
    "LSTA031": ("Sleep", "Asleep",                         # broken by a hit
                "A heavy sleep drags you under. The next blow will wake you.",
                "You wake up."),
    "LSTA032": ("Fainted", "Stunned",                      # not broken by a hit
                "The blow leaves you reeling. You cannot act.", "Your head clears."),
    "LSTA033": ("Disguise", "Stealth",
                "You melt into your surroundings. Attacking will reveal you.",
                "You are no longer hidden."),
    "LSTA034": ("Invisible", "Invisible", "You fade from sight.", "You are visible again."),
    "LSTA035": ("Shield Damage", "Reflect",
                "Part of every blow you take is turned back on the attacker.",
                "You no longer reflect damage."),
    "LSTA036": ("Additional Damage",) + _ADD,
    "LSTA037": ("Advantages terminate", "Dispelled", "Your blessings are torn away.",
                "The dispel has faded."),
    "LSTA038": ("Disadvantages terminate", "Purified",
                "Holy light burns away every curse on you.", "The purification has faded."),
    "LSTA039": ("Back to normal", "Nullified", "Every effect on you is stripped away.",
                "The nullification has faded."),
    "LSTA043": ("HP Decreased", "Fading", "", ""),       # a summon's lifetime drain
    "LSTA044": ("Max HP Increased",) + _MAXHP,             # 44-54: monster self-buff copies
    "LSTA045": ("Max MP Increased",) + _MAXMP,
    "LSTA046": ("Moving speed Increased",) + _MOVE[0],
    "LSTA047": ("Atk speed Increased",) + _ASPD[0],
    "LSTA048": ("Atk power Increased",) + _ATK[0],
    "LSTA049": ("Def power Increased",) + _DEF[0],
    "LSTA050": ("Magick Resistance Increased",) + _RES[0],
    "LSTA051": ("Hitting rate Increased",) + _HIT[0],
    "LSTA052": ("Cri Increased",) + _CRIT[0],
    "LSTA053": ("Dodge rate Increased",) + _DODGE[0],
    "LSTA054": ("Addition damage",) + _ADD,
    "LSTA055": ("Invisibility termination", "Revealed", "You have been revealed.",
                "You can hide again."),
    "LSTA056": ("Taunt", "Taunted", "You have been provoked.",
                "You shake off the provocation."),
    "LSTA058": ("Flame Heat", "Burning", "Flames cling to you, searing your flesh.",
                "The flames die out."),
}
for _k in ("LSTA002", "LSTA003"):
    TEXT[_k] = TEXT["LSTA001"]
for _k in ("LSTA005", "LSTA006"):
    TEXT[_k] = TEXT["LSTA004"]

# keys the table points at but the STL never had: key -> (id, name, lands, wears off)
APPEND = {
    "LSTA059": (59, "Arua's Blessing", "The goddess Arua watches over you.",
                "Arua's blessing fades."),              # row 61, CEnduranceGoddess
}

ST_TYPE, ST_FLAG, ST_APPLY, ST_KEY = 1, 3, (5, 7), 20
USE_COL_STATUS = 24                     # USEITME_STATUS_STB
SK_STATUS = (11, 12)

# CEndurancePack::m_StateSTBIDXTable -- the row whose name another character's
# icon shows for each ING flag
ICON_ROWS = {1: "HP regen", 4: "MP regen", 7: "poison", 12: "max HP up", 13: "max MP up",
             14: "move speed up", 15: "move speed down", 16: "attack speed up",
             17: "attack speed down", 18: "attack up", 19: "attack down",
             20: "defense up", 21: "defense down", 22: "magic res up", 23: "magic res down",
             24: "accuracy up", 25: "accuracy down", 26: "critical up", 27: "critical down",
             28: "dodge up", 29: "dodge down", 30: "silence", 31: "sleep", 32: "stun",
             33: "disguise", 34: "invisible", 35: "shield damage", 36: "additional damage",
             43: "summon lifetime", 61: "goddess"}


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


def survey():
    rd = load("rose-data-reader")
    ftl = load("fix-tooltip-labels")
    fsd = load("fix-skill-descriptions")

    st = rd.Stb(os.path.join(STB_DIR, "LIST_STATUS.STB"))
    stl = ftl.Stl(STL)
    idx, eng = stl.index(), stl.english()

    def text(r):
        k = st.s(r, ST_KEY).strip()
        if k not in idx:
            return k, None
        return k, [f.decode("utf-8", "replace") for f in eng[idx[k]]]

    # skills: reachable lines from the description survey, everything else monster-side
    sk = rd.Stb(os.path.join(STB_DIR, "LIST_SKILL.STB"))
    recs, _ = fsd.survey(rd)
    reachable = {}
    for rec in recs:
        if "NO-PATH" not in rec["flags"]:
            for r in rec["rows"]:
                reachable[r] = rec["name"]
    player, monster = collections.defaultdict(set), collections.defaultdict(set)
    for r in range(sk.rows):
        for c in SK_STATUS:
            s = sk.i(r, c)
            if s:
                (player[s].add(reachable[r]) if r in reachable else monster[s].add(r))

    # items: col 24 -> container -> applied rows
    use = rd.Stb(os.path.join(STB_DIR, "LIST_USEITEM.STB"))
    use_names = rd.Stl(os.path.join(STB_DIR, "LIST_USEITEM_S.STL"), "cp1252").by_key(1)
    container, applied = collections.defaultdict(set), collections.defaultdict(set)
    for no in range(use.rows):
        s = use.i(no, USE_COL_STATUS)
        if not s:
            continue
        name = use_names.get(use.s(no, use.cols - 1).strip(), ("item %d" % no,))[0]
        container[s].add(name)
        for c in ST_APPLY:
            a = st.i(s, c)
            if a:
                applied[a].add(name)

    out = []
    for r in range(1, st.rows):
        key, fields = text(r)
        if not key and not st.occupied(r):
            continue
        routes = []
        if player[r]:
            routes.append("skills: " + ", ".join(sorted(player[r])))
        if monster[r]:
            routes.append("monster skills: %d rows" % len(monster[r]))
        if applied[r]:
            names = sorted(applied[r])
            routes.append("items (applied): %s%s" % (", ".join(names[:6]),
                                                     " +%d more" % (len(names) - 6) if len(names) > 6 else ""))
        if container[r]:
            routes.append("item container for %d item(s) (own text never shown)" % len(container[r]))
        if r in ICON_ROWS:
            routes.append("icon for others' %s" % ICON_ROWS[r])
        out.append({"row": r, "key": key, "fields": fields, "type": st.i(r, ST_TYPE),
                    "flag": st.i(r, ST_FLAG), "routes": routes,
                    "shown": bool(player[r] or monster[r] or applied[r] or r in ICON_ROWS)})
    return out


def write_review(recs):
    shown = [r for r in recs if r["shown"]]
    L = ["# Status text -- review sheet", "",
         "Generated by `scripts/fix-status-text.py --dump`. Fields: name (buff bar, party",
         "frames, skill tooltip) / chat when it lands on you / chat when it wears off /",
         "unused. Flag: 1 harmful, 2 a buff's own drawback, 0 helpful.", "",
         "%d rows a player can see, %d never shown (containers, unused)." % (
             len(shown), len(recs) - len(shown)), ""]
    for section, rows in (("Shown", shown), ("Never shown", [r for r in recs if not r["shown"]])):
        L.append("## %s (%d)" % (section, len(rows)))
        L.append("")
        for r in rows:
            f = r["fields"] or ["(no STL entry)", "", "", ""]
            L.append("### %d %s -- %r  (type %d, flag %d)" % (r["row"], r["key"], f[0],
                                                              r["type"], r["flag"]))
            for route in r["routes"]:
                L.append("- " + route)
            L.append("- lands: %r" % f[1])
            L.append("- ends:  %r" % f[2])
            if len(f) > 3 and f[3]:
                L.append("- field 4: %r" % f[3])
            L.append("")
    os.makedirs(REVIEW_DIR, exist_ok=True)
    with open(REVIEW, "w", encoding="utf-8") as fh:
        fh.write("\n".join(L))
    return len(shown), len(recs) - len(shown)


def check_style():
    bad = []
    for key, entry in list(TEXT.items()) + [(k, v[1:]) for k, v in APPEND.items()]:
        for s in entry:
            if not all(32 <= ord(c) < 127 for c in s) or s != s.strip() or "  " in s:
                if key in TEXT and s == entry[0]:
                    continue                    # the old name may be messy; it is not written
                bad.append("%s: %r" % (key, s))
    return bad


def plan(stl, side):
    """[(key, current fields, new fields)], [foreign], [keys to append]"""
    idx, eng = stl.index(), stl.english()
    changes, foreign = [], []
    for key, (old, name, lands, ends) in TEXT.items():
        if key not in idx:
            sys.exit("key %s missing from %s" % (key, STL))
        cur = [f.decode("utf-8", "replace") for f in eng[idx[key]]]
        new = [name, lands, ends] + cur[3:]
        if cur == new:
            continue
        if cur[0] not in (old, name, side.get("fields", {}).get(key, [old])[0]):
            foreign.append("%s is %r, expected %r" % (key, cur[0], old))
        changes.append((key, cur, new))
    appends = [k for k in APPEND if k not in idx]
    return changes, foreign, appends


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for flag in ("--dump", "--dry-run", "--verify", "--restore"):
        ap.add_argument(flag, action="store_true")
    args = ap.parse_args()
    if args.dump:
        shown, hidden = write_review(survey())
        print("review sheet: %s (%d shown, %d never shown)" % (REVIEW, shown, hidden))
        return

    bad = check_style()
    if bad:
        sys.exit("text fails the house style:\n  " + "\n  ".join(bad))

    ftl = load("fix-tooltip-labels")
    stl = ftl.Stl(STL)
    if stl.to_bytes() != stl.raw or stl.bad:
        sys.exit("%s does not round-trip -- refusing to touch it" % STL)
    side = {}
    if os.path.isfile(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            side = json.load(fh)

    if args.restore:
        if not side:
            sys.exit("no sidecar -- nothing to restore")
        idx, eng = stl.index(), stl.english()
        for key, fields in side.get("fields", {}).items():
            eng[idx[key]] = [f.encode("utf-8") for f in fields]
        for key in side.get("appended", []):
            if key in stl.index():
                stl.remove(key)
        with open(STL, "wb") as fh:
            fh.write(stl.to_bytes())
        os.remove(SIDECAR)
        print("restored %d entr(ies), removed %d appended key(s); sidecar removed"
              % (len(side.get("fields", {})), len(side.get("appended", []))))
        return

    changes, foreign, appends = plan(stl, side)
    if args.verify:
        for key, cur, new in changes:
            print("DIFF %s: %r != %r" % (key, cur[:3], new[:3]))
        for key in appends:
            print("MISSING %s" % key)
        ok = not changes and not appends
        print("verify:", "OK (%d entries, %d appended)" % (len(TEXT), len(APPEND)) if ok
              else "%d differ, %d missing" % (len(changes), len(appends)))
        sys.exit(0 if ok else 1)

    for key, cur, new in changes:
        print("%s  %r -> %r\n         lands: %r\n         ends:  %r"
              % (key, cur[0], new[0], new[1], new[2]))
    for key in appends:
        print("%s  (new) %r" % (key, APPEND[key][1]))
    if foreign:
        sys.exit("\nchanged since reviewed:\n  " + "\n  ".join(foreign))
    if args.dry_run:
        print("dry run; %d change(s), %d append(s), nothing written"
              % (len(changes), len(appends)))
        return
    if not changes and not appends:
        print("nothing to do; all %d entries in place" % len(TEXT))
        return

    idx, eng = stl.index(), stl.english()
    fields_side = side.setdefault("fields", {})
    for key, cur, new in changes:
        fields_side.setdefault(key, cur)
        eng[idx[key]] = [f.encode("utf-8") for f in new]
    for key in appends:
        num, name, lands, ends = APPEND[key]
        stl.append(key, num, [name, lands, ends, ""])
        side.setdefault("appended", []).append(key)
    with open(STL, "wb") as fh:
        fh.write(stl.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(side, fh, indent=1, sort_keys=True)

    again = ftl.Stl(STL)
    left, _, missing = plan(again, side)
    if left or missing or again.to_bytes() != again.raw:
        sys.exit("VERIFY FAILED after writing: %s %s" % (left, missing))
    print("wrote %d entr(ies), appended %d key(s); verified by re-read. "
          "Re-bake the VFS (client-only)." % (len(changes), len(appends)))


if __name__ == "__main__":
    main()
