"""Make every skill book's name the name of the skill it teaches.

Alpha test #2: testers bought *Rain of Arrows* and learned **Range Bow Shot**,
bought *Knuckle Mastery* and learned **Combat Mastery**. A skill book is a
LIST_USEITEM row of class 314 (`USE_ITEM_SKILL_LEARN`) whose col 20 names the
LIST_SKILL row it teaches, and the two strings a player sees are resolved
independently: the book's name from LIST_USEITEM_S.STL (key in the STB's last
column) and the skill's from LIST_SKILL_S.STL (key in LIST_SKILL col 86, one key
shared by every rank of a line). The tooltip never prints the taught skill's
name (`CItem::GetToolTip`, item.cpp `USE_ITEM_SKILL_LEARN`, prints only the
class / level / prerequisite lines), so the book's name is the only thing that
tells a player what they are buying.

Surveyed 2026-09-22 over the 203 books: 97 agreed, 61 disagreed, 45 point at a
skill with no client-side name at all (see "left alone"). The 61 are three
different mistakes, and the reference dumps of the same lineage (ruff,
QQ-iROSE, titanRose) settle which side drifted:

  A. The book names a different skill outright -- Rain of Arrows / Range Bow
     Shot, Fire Shot / Taunt Shot, Magic Shot / Heavy Bow Shot, Focus Shot / Slow
     Shot, Knuckle Mastery / Combat Mastery, Market Research / Economy Research,
     Discount / Buying Trick, Hire Mercenary / Employ Troops, eight emote books
     ... Retail shipped those books *as* Taunt Shot, Heavy Bow Shot, Range
     Shot, Combat Mastery, Employ Troops with the same col-20 targets we have,
     so the names are our translation drift (a few -- Shield Protect / Berserk,
     Interrogate / Vanish -- are retail's own). Col 20 is right everywhere: only
     strings move, no book is re-pointed. The book takes the skill's name.
  B. Same skill, the *skill* string is broken -- Lightening, Puri, Defese Aura,
     Luna Strone, Combat Matery, Rest Matery, BoneFire, Natura lItem Craft,
     MagicArmsMastery, " Sub Weapon Craft", "Kiss ", "Sniping ", Poison Pang.
     The skill is corrected and the book follows.
  C. Same skill, both readable, the book's English is better -- Gather / Pick
     Up, Floating Air / Levitation, One-Hand / One-Handed Mastery, Blessing Mind
     / Blessed Mind, Calling Hawk / Call Hawk, Magickal Knife / Magic Knife,
     Smash Gun / Gun Smash, Castle Gear / Castle Gear Craft ... Decided
     2026-09-22: the skill takes the book's name (visible in the skill window
     and tree for every rank -- that is the point).

B and C are the `SKILL_FIXES` table, applied first; after that the rule is
mechanical: a book whose name is not byte-for-byte its skill's is renamed.
`EXPECTED` carries the reviewed outcome so a future import cannot be renamed
silently -- a rename outside it refuses to write unless `--allow-new`.

Descriptions: books do not reuse the skill's description (0 of 203 do; the
books carry good-English one-liners, the skills the original "You can damage
your tartet" text), and most of the renamed books' descriptions already
describe the right skill under the wrong name. The eight that do not are in
`BOOK_DESCRIPTIONS`: Shield Protect's shield text on the Berserk book, the
four Scout shots, Ice Pole's text on Ice Bang, and the two emote books with no
description at all. (Ice Bang was first rewritten here as single target; it is
not -- LIST_SKILL type 7 is an area around the target -- and the entry was
corrected on 2026-09-28.) It also carries books that keep their name:

  * three that shipped a literal `???` where an apostrophe was lost (Shield
    Barrier, Impact Wave, Champion Hit: "skill???s duration");
  * eight whose text contradicts the skill, found while rewriting the skill
    descriptions (`fix-skill-descriptions.py --dump`): Lightning puts the
    target to sleep (status 31), not a stun; the Butterfly lowers Accuracy and
    confuses nothing; the Bonfire heals every player near it, not the party
    only; the Phantom Sword is melee (reach 2.5 m), not ranged; Battle Mastery
    raises Attack Speed only; Vanish is a Sleep, nothing runs away; Power
    Attack needs a katar or dual swords and Power Burst a katar, where both
    books said "a Knuckle or a Melee Weapon"; Economy Research gives far more
    than "Charm by 2" past rank 1; Power Gun Shot read as a passive;
  * two that left out a later bonus: Craft Mastery (gun Attack Speed from rank
    6) and Weapon Research (Critical from rank 11).

Only the English language block is touched
------------------------------------------
LIST_SKILL_S.STL block 0 is real Korean for 228 of its 234 keys, and blocks
2-4 hold Japanese / Chinese. So every write goes to `LANG_USA` (block 1) alone,
and the sidecar records the **previous bytes** of each field so `--restore`
puts back exactly what was there (the `fix-item-descriptions.py` rule).
LIST_SKILL.STB col 0 -- the server's `SKILL_NAME`, used only as a blank check
in `ai_action.cpp` -- is mirrored for every row whose col 86 is a fixed key,
per the "STL key + STB col 0 mirror" rule for new skills; it changes nothing
in play.

Left alone, on purpose:
  * the 45 books whose target skill has no STL key: the dead / stub rows
    `prune-dead-skill-books.py` already handles (Advance Crossbow Mastery,
    Brave Howl, Concussor ...), the GM block 871-896 (skills 3201-3221, Korean
    col 0, sold by nobody), 608 Mount -> 23 "Pure Me", 671 Religious Study ->
    451 "Beam Blade", 614 Alchemy Craft -> 132. Reported by `--dry-run`.
  * rows 851-853: no STL key, name "Name".
  * duplicates: 663 becomes a second Berserk book (662), 634 a second Hand
    Clapping (640), 641/642 a second Hi! / Bye (631/632). All are unsold and
    undropped; they get the true name and are listed as duplicates. 752 and
    810 both read "Combat Mastery" afterwards and teach *different* skills
    (Raider 1421, Dealer 2141) -- retail named both lines that; the class
    line in the tooltip tells them apart, and it is not this script's call.

Interaction with other passes: `fix-item-names.py` appended LUSE608/641/642;
if it is restored first those keys vanish and this restore skips them. Run
every `--verify` afterwards (whole-file backup hazard, CLAUDE.md).

Idempotent. Sidecar data/3DDATA/STB/skill-book-names.json; whole-file copies
of the three tables in build/skill-book-names-backup/ for forensics only (a
.bak beside the data would be baked into the .vfs by pack.rs). Both STLs live
in the VFS: re-bake and ship the client; restart the game server for the STB.

Usage:
    python scripts/fix-skill-book-names.py --dry-run
    python scripts/fix-skill-book-names.py
    python scripts/fix-skill-book-names.py --verify
    python scripts/fix-skill-book-names.py --restore
"""
import argparse
import collections
import importlib.util
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
USEITEM_STB = os.path.join(STB_DIR, "LIST_USEITEM.STB")
USEITEM_STL = os.path.join(STB_DIR, "LIST_USEITEM_S.STL")
SKILL_STB = os.path.join(STB_DIR, "LIST_SKILL.STB")
SKILL_STL = os.path.join(STB_DIR, "LIST_SKILL_S.STL")
NPC_STB = os.path.join(STB_DIR, "LIST_NPC.STB")
NPC_STL = os.path.join(STB_DIR, "LIST_NPC_S.STL")
SELL_STB = os.path.join(STB_DIR, "LIST_SELL.STB")
SIDECAR = os.path.join(STB_DIR, "skill-book-names.json")
BACKUP = os.path.join(ROOT, "build", "skill-book-names-backup")

LANG_USA = 1                # stringmanager.h; block 0 is Korean
T_USE = 10
USE_ITEM_SKILL_LEARN = 314  # datatype.h
USE_COL_CLASS = 4
USE_COL_SKILL = 20          # USEITEM_SCROLL_LEARN_SKILL
SK_COL_NAME = 0             # server SKILL_NAME
SK_COL_STL_KEY = 86         # client name key

# LIST_SKILL_S.STL key -> the one name the line should carry (kinds B and C).
SKILL_FIXES = {
    # B: typos, stray spaces, run-together words
    "LSkill0030": "Recovery Kiss",       # "Kiss " (STB col 0 already says so)
    "LSkill0271": "Crossbow Mastery",    # CrossBow Mastery
    "LSkill0801": "Magic Arms Mastery",  # MagicArmsMastery
    "LSkill0951": "Lightning",           # Lightening
    "LSkill1011": "Defense Aura",        # Defese Aura
    "LSkill1041": "Purify",              # Puri
    "LSkill1091": "Luna Stone",          # Luna Strone (the book said Lunar; docs say Luna)
    "LSkill1161": "Bonfire",             # BoneFire
    "LSkill1421": "Combat Mastery",      # Combat Matery
    "LSkill1441": "Rest Mastery",        # Rest Matery
    "LSkill2211": "Sniping Shot",        # "Sniping "
    "LSkill2271": "Poison Fang",         # Poison Pang
    "LSkill2401": "Natural Item Craft",  # Natura lItem Craft
    "LSkill2411": "Sub Weapon Craft",    # " Sub Weapon Craft"
    "LSkill2631": "Refine Item",         # Refine item
    # B, found 2026-09-28 by `fix-skill-descriptions.py --dump`: rank 6+ keys
    # (no book of their own, so the book survey never saw them) and two more
    # stray spaces
    "LSkill0396": "Divine Lightning",    # Divine Lightening (Divine Force ranks 6-10)
    "LSkill0956": "Lightning Shock",     # Lightening Shock (Lightning ranks 6-10)
    "LSkill2431": "Sword Craft",         # " Sword Craft"
    "LSkill2471": "Bow Craft",           # " Bow Craft"
    # also 2026-09-28: a name that disagreed with the table, and three emote
    # names left in the original translation's broken English
    "LSkill0033": "Toss 10 Zuly",        # Throwing 10 Zuly
    "LSkill0034": "Toss 50 Zuly",        # Throwing 100 Zuly -- costs and gives 50
    "LSkill0046": "Cheer",               # Fighting (its text was "Cheer Up")
    "LSkill0048": "Anger",               # Get Anger
    "LSkill0049": "Anger",               # Get Anger
    # C: the book's better English wins
    "LSkill0012": "Pick Up",             # Gather
    "LSkill0014": "Levitation",          # Floating Air
    "LSkill0020": "Trade",               # Trading
    "LSkill0021": "Vending",             # Open Market
    "LSkill0025": "Ride Request",        # Request Riding
    "LSkill0041": "Hi!",                 # Hi (book 631 and STB col 0 say Hi!)
    "LSkill0211": "One-Handed Mastery",  # One-Hand Mastery
    "LSkill0221": "Two-Handed Mastery",  # Two-Hand Mastery
    "LSkill0941": "Staff Stun",          # Staff Stun Attack
    "LSkill1051": "Blessed Mind",        # Blessing Mind
    "LSkill1151": "Call Butterfly",      # ButterFly
    "LSkill1181": "Call Elemental",      # Elemental
    "LSkill1191": "Call Firegon",        # Calling Firegon
    "LSkill1221": "Hit Support",         # Hitting Support
    "LSkill1671": "Detect",              # Detecting
    "LSkill1711": "Call Hawk",           # Calling Hawk
    "LSkill1871": "Magic Knife",         # Magickal Knife
    "LSkill2281": "Gun Smash",           # Smash Gun
    "LSkill2571": "Magic Armor Craft",   # Magick Armor Craft
    "LSkill2601": "Castle Gear Craft",   # Castle Gear
}

# LIST_USEITEM row -> new English description, for the renamed books whose old
# text described the old name rather than the skill. Everything else keeps its
# description. Same voice as the surrounding books: one imperative sentence.
BOOK_DESCRIPTIONS = {
    641: "Learn emotive expression.",
    642: "Learn emotive expression.",
    663: "Increase Attack Power of caster while decreasing caster's Defense "
         "for skill's duration.",                                   # = 662
    672: "Inflict damage on a target and provoke it into attacking the caster.",
    673: "Gather strength before firing to hit a target with great force.",
    674: "Inflict damage on a target while decreasing its Movement Speed.",
    675: "Fire a powerful shot at a target from a long distance.",
    727: "Inflict ice damage on a target and enemies near it, with a chance to "
         "decrease their Movement Speed.",
    # not renamed -- the apostrophe was lost to a lossy conversion and three
    # literal '?' bytes shipped in its place (found 2026-09-28)
    664: "Increase Magic Resistance of the caster and party members for "
         "skill's duration. Requires a Shield.",
    676: "Thrust a sword into the ground and crack the ground's surface to do "
         "splash damage to many foes.",
    692: "Enables its caster to leap high into the air and strike the top of "
         "the enemy's skull.",
    # not renamed -- the text contradicted the skill (2026-09-28)
    712: "Cast Lightning on a target, with a chance to put it to sleep.",
    732: "Summon a magic butterfly that fights beside the caster and lowers "
         "its targets' Accuracy.",
    733: "Cast a magic campfire that restores the HP and MP of everyone nearby.",
    734: "Summon a magic sword to attack a target at close range.",
    755: "Increase Attack Speed of Katar, Dual Wield and Bow weapons.",
    757: "Hit a target with mighty force while using a Katar or Dual Swords.",
    767: "Advanced Power Attack skill. Hit a target with awesome might while "
         "using a Katar.",
    777: "Put a monster to sleep, and from rank 6 the monsters around it too.",
    801: "Study economy. Increase Charm with every rank of this skill.",
    808: "Allows its user to learn weapon crafting skills. Increase "
         "Concentration, and from rank 11 Critical.",
    811: "Learn the essentials of crafting. Increase Maximum MP, and from "
         "rank 6 Attack Speed with Guns.",
    816: "Fire a powerful shot at a target with a Gun or Launcher.",
}

# LIST_USEITEM row -> the name it ends up with. The reviewed outcome of the rule
# on 2026-09-22; a rename the rule produces outside this set refuses to write.
EXPECTED = {
    634: "Hand Clapping", 635: "Chuckle", 636: "Cheer", 637: "Begging",
    638: "Anger", 639: "Anger", 641: "Hi!", 642: "Bye",
    663: "Berserk",
    672: "Taunt Shot", 673: "Heavy Bow Shot", 674: "Slow Shot",
    675: "Range Bow Shot",
    701: "Magic Arms Mastery", 726: "Luna Stone", 727: "Ice Bang",
    740: "Sharpen Support",
    752: "Combat Mastery", 759: "Double Attack", 762: "Holding Arrow",
    777: "Vanish",
    801: "Economy Research", 803: "Pack Mastery", 804: "Buying Trick",
    805: "Sell Trick", 806: "Gathering", 807: "Employ Troops",
    820: "Aim Point", 829: "Employ Warrior", 830: "Employ Hunter",
    832: "Natural Item Craft", 833: "Sub Weapon Craft",
    835: "Blunt Weapon Craft", 846: "Castle Gear Craft", 849: "Refine Item",
    850: "Jewel Item Craft",
}


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


def key_index(stl):
    return {k.decode("latin-1"): i for i, (k, _) in enumerate(stl.keys)}


def utf8(b):
    return b.decode("utf-8", "surrogateescape")


def bytes_of(s):
    return s.encode("utf-8", "surrogateescape")


# ------------------------------------------------------------------ survey

def sold_by(rd, pr):
    """LIST_USEITEM row -> sorted NPC names that sell it."""
    npc = rd.Stb(NPC_STB)
    sell = rd.Stb(SELL_STB)
    names = rd.Stl(NPC_STL, "utf-8").by_key(LANG_USA)
    kc = npc.key_column()
    if kc is None:
        kc = npc.cols - 1
    out = collections.defaultdict(set)
    for tab, rows in pr.npc_tabs(npc).items():
        if tab >= sell.rows:
            continue
        who = {names.get(npc.s(r, kc).strip(), (str(r),))[0] for r in rows}
        for slot in range(pr.SLOTS):
            v = sell.i(tab, pr.FIRST_SLOT_COL + slot)
            if v <= 0:
                continue
            ty, no = pr.decode_store_item(v)
            if ty == T_USE:
                out[no].update(who)
    return {k: sorted(v) for k, v in out.items()}


def survey(rd, pr, skill_names=None):
    """One record per class-314 row.

    `skill_names` lets the dry run evaluate the rule against the *post-fix*
    skill names before anything is written.
    """
    use = rd.Stb(USEITEM_STB)
    sk = rd.Stb(SKILL_STB)
    books = rd.Stl(USEITEM_STL, "utf-8").by_key(LANG_USA)
    if skill_names is None:
        skill_names = {k: v[0] for k, v in
                       rd.Stl(SKILL_STL, "utf-8").by_key(LANG_USA).items()}
    sellers = sold_by(rd, pr)
    out = []
    for no in range(use.rows):
        if use.i(no, USE_COL_CLASS) != USE_ITEM_SKILL_LEARN:
            continue
        key = use.s(no, use.cols - 1).strip()
        target = use.i(no, USE_COL_SKILL)
        skey = sk.s(target, SK_COL_STL_KEY).strip() if target < sk.rows else ""
        entry = books.get(key)
        rec = {"row": no, "key": key, "target": target, "skey": skey,
               "name": entry[0] if entry else None,
               "desc": entry[1] if entry and len(entry) > 1 else "",
               "skill": skill_names.get(skey), "sold": sellers.get(no, [])}
        if not key or entry is None:
            rec["kind"] = "nameless"
        elif not rec["skill"]:
            rec["kind"] = "no-skill-name"
        elif rec["skill"].strip() == rec["name"].strip():
            rec["kind"] = "match"
        else:
            rec["kind"] = "rename"
        out.append(rec)
    return out


def planned_skill_names(rd):
    names = {k: v[0] for k, v in
             rd.Stl(SKILL_STL, "utf-8").by_key(LANG_USA).items()}
    missing = [k for k in SKILL_FIXES if k not in names]
    if missing:
        sys.exit("SKILL_FIXES keys absent from LIST_SKILL_S.STL: %s" % missing)
    names.update(SKILL_FIXES)
    return names


def report(rd, recs):
    cur = {k: v[0] for k, v in
           rd.Stl(SKILL_STL, "utf-8").by_key(LANG_USA).items()}
    fixes = [(k, cur[k], v) for k, v in SKILL_FIXES.items() if cur[k] != v]
    print("skill names to correct (LIST_SKILL_S.STL block 1 + STB col 0): %d"
          % len(fixes))
    for k, old, new in fixes:
        print("  %-11s %-22r -> %r" % (k, old, new))

    ren = [r for r in recs if r["kind"] == "rename"]
    print("\nskill books to rename (LIST_USEITEM_S.STL block 1): %d" % len(ren))
    for r in ren:
        d = "  +desc" if r["row"] in BOOK_DESCRIPTIONS else ""
        print("  %3d %-9s %-22r -> %-22r (skill %4d)%s  sold by %s"
              % (r["row"], r["key"], r["name"], r["skill"], r["target"], d,
                 ", ".join(r["sold"]) or "nobody"))

    final = collections.defaultdict(list)
    for r in recs:
        if r["kind"] in ("rename", "match"):
            final[(r["skill"] if r["kind"] == "rename" else r["name"]).strip()].append(r["row"])
    dup = {k: v for k, v in final.items() if len(v) > 1}
    if dup:
        print("\nbooks sharing a name afterwards:")
        for k, rows in sorted(dup.items()):
            print("  %-20s rows %s" % (k, rows))

    rest = [r for r in recs if r["kind"] in ("no-skill-name", "nameless")]
    print("\nleft alone -- target skill has no client name (%d):" % len(rest))
    for r in rest:
        print("  %3d %-9s %-30r -> skill %4d %s  sold by %s"
              % (r["row"], r["key"], r["name"], r["target"], r["skey"] or "(no key)",
                 ", ".join(r["sold"]) or "nobody"))


# ------------------------------------------------------------------ write

def apply(rd, fix, oro, recs, dry):
    side = {"skill_stl": {}, "skill_stb": {}, "book_stl": {}}
    if os.path.isfile(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            side = json.load(fh)
    n_skill = n_stb = n_book = 0

    # 1. skills: STL block 1 name, STB col 0 on every rank sharing the key
    sstl = fix.StlFile(SKILL_STL)
    sidx = key_index(sstl)
    entries = sstl.langs[LANG_USA]
    for key, name in SKILL_FIXES.items():
        i = sidx[key]
        old_name, old_desc = entries[i]
        if old_name == bytes_of(name):
            continue
        side["skill_stl"].setdefault(key, utf8(old_name))
        entries[i] = (bytes_of(name), old_desc)
        n_skill += 1
    sstb = oro.Stb(SKILL_STB)
    for r in range(sstb.rows):
        key = sstb.get(r, SK_COL_STL_KEY).decode("latin-1").strip()
        if key in SKILL_FIXES:
            new = SKILL_FIXES[key].encode("latin-1")
            old = sstb.get(r, SK_COL_NAME)
            if old != new:
                side["skill_stb"].setdefault(str(r), old.decode("latin-1"))
                sstb.set(r, SK_COL_NAME, new)
                n_stb += 1

    # 2. books: STL block 1 name (+ description where listed)
    bstl = fix.StlFile(USEITEM_STL)
    bidx = key_index(bstl)
    bentries = bstl.langs[LANG_USA]
    for r in recs:
        if r["kind"] != "rename" and r["row"] not in BOOK_DESCRIPTIONS:
            continue
        i = bidx[r["key"]]
        old_name, old_desc = bentries[i]
        new_name = bytes_of(r["skill"]) if r["kind"] == "rename" else old_name
        new_desc = old_desc
        if r["row"] in BOOK_DESCRIPTIONS:
            new_desc = bytes_of(BOOK_DESCRIPTIONS[r["row"]])
        if (old_name, old_desc) == (new_name, new_desc):
            continue
        side["book_stl"].setdefault(
            r["key"], {"name": utf8(old_name), "desc": utf8(old_desc)})
        bentries[i] = (new_name, new_desc)
        n_book += 1

    counts = (n_book, n_skill, n_stb)
    if dry:
        return counts
    os.makedirs(BACKUP, exist_ok=True)
    for path in (SKILL_STL, SKILL_STB, USEITEM_STL):
        bak = os.path.join(BACKUP, os.path.basename(path))
        if not os.path.exists(bak):
            shutil.copyfile(path, bak)
    with open(SKILL_STL, "wb") as fh:
        fh.write(sstl.to_bytes())
    with open(SKILL_STB, "wb") as fh:
        fh.write(sstb.to_bytes())
    with open(USEITEM_STL, "wb") as fh:
        fh.write(bstl.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(side, fh, indent=1, ensure_ascii=False)
    return counts


def restore(fix, oro):
    if not os.path.isfile(SIDECAR):
        sys.exit("no sidecar -- nothing to restore")
    with open(SIDECAR, encoding="utf-8") as fh:
        side = json.load(fh)
    n = 0
    sstl = fix.StlFile(SKILL_STL)
    sidx = key_index(sstl)
    for key, old in side["skill_stl"].items():
        i = sidx.get(key)
        if i is None:
            continue
        sstl.langs[LANG_USA][i] = (bytes_of(old), sstl.langs[LANG_USA][i][1])
        n += 1
    sstb = oro.Stb(SKILL_STB)
    for r, old in side["skill_stb"].items():
        sstb.set(int(r), SK_COL_NAME, old.encode("latin-1"))
        n += 1
    bstl = fix.StlFile(USEITEM_STL)
    bidx = key_index(bstl)
    for key, old in side["book_stl"].items():
        i = bidx.get(key)
        if i is None:
            continue                     # fix-item-names.py --restore ran first
        bstl.langs[LANG_USA][i] = (bytes_of(old["name"]), bytes_of(old["desc"]))
        n += 1
    with open(SKILL_STL, "wb") as fh:
        fh.write(sstl.to_bytes())
    with open(SKILL_STB, "wb") as fh:
        fh.write(sstb.to_bytes())
    with open(USEITEM_STL, "wb") as fh:
        fh.write(bstl.to_bytes())
    os.remove(SIDECAR)
    return n


def verify(rd, pr):
    bad = []
    cur = {k: v[0] for k, v in
           rd.Stl(SKILL_STL, "utf-8").by_key(LANG_USA).items()}
    for key, name in SKILL_FIXES.items():
        if cur.get(key) != name:
            bad.append("skill STL %s is %r, expected %r" % (key, cur.get(key), name))
    sk = rd.Stb(SKILL_STB)
    for r in range(sk.rows):
        key = sk.s(r, SK_COL_STL_KEY).strip()
        if key in SKILL_FIXES and sk.s(r, SK_COL_NAME) != SKILL_FIXES[key]:
            bad.append("skill STB row %d col 0 is %r, expected %r"
                       % (r, sk.s(r, SK_COL_NAME), SKILL_FIXES[key]))
    recs = survey(rd, pr)
    for r in recs:
        if r["kind"] == "rename":
            bad.append("book %d %s still %r, skill %d is %r"
                       % (r["row"], r["key"], r["name"], r["target"], r["skill"]))
        elif r["row"] in EXPECTED and r["name"] != EXPECTED[r["row"]]:
            bad.append("book %d is %r, expected %r"
                       % (r["row"], r["name"], EXPECTED[r["row"]]))
        if r["row"] in BOOK_DESCRIPTIONS and r["desc"] != BOOK_DESCRIPTIONS[r["row"]]:
            bad.append("book %d description is %r" % (r["row"], r["desc"]))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--allow-new", action="store_true",
                    help="also rename books the rule finds outside the reviewed set")
    args = ap.parse_args()
    # the GM rows carry Korean in col 86; a cp1252 console must not abort the report
    sys.stdout.reconfigure(errors="replace")

    rd = load("rose-data-reader")
    pr = load("prune-dead-skill-books")
    fix = load("fix-item-names")
    oro = load("import-oro")

    if args.restore:
        n = restore(fix, oro)
        print("restored %d field(s); sidecar removed" % n)
        return 0

    if args.verify:
        bad = verify(rd, pr)
        print("%d skill fixes, %d expected book names; %d problem(s)"
              % (len(SKILL_FIXES), len(EXPECTED), len(bad)))
        for b in bad:
            print("  " + b)
        return 1 if bad else 0

    recs = survey(rd, pr, planned_skill_names(rd))
    report(rd, recs)

    ren = {r["row"]: r["skill"] for r in recs if r["kind"] == "rename"}
    new = {no: nm for no, nm in ren.items() if EXPECTED.get(no) != nm}
    if new and not args.allow_new:
        print("\n%d rename(s) outside the reviewed set: %s -- review and re-run "
              "with --allow-new" % (len(new), new))
        return 1
    gone = sorted(no for no in EXPECTED if no not in ren)
    if gone:
        print("\nalready in place (expected, no longer renamed): %s" % gone)

    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    n_book, n_skill, n_stb = apply(rd, fix, oro, recs, dry=False)
    bad = verify(rd, pr)
    if bad:
        print("VERIFY FAILED after writing:")
        for b in bad:
            print("  " + b)
        return 1
    print("\nwrote %d book entry(ies), %d skill name(s) and %d STB col-0 cell(s); "
          "verified. Re-bake the VFS and restart the game server."
          % (n_book, n_skill, n_stb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
