#!/usr/bin/env python3
"""The Ikaness Greater Assassin: shadows, marks, and circles you must stand in (2026-11-13).

The body is `catacomb_pool.py`'s (LIST_NPC 2560: Jrose's black Ikaness under a round hat,
one red eye, a black Death Expeler); the fight is the controller's (src/sho_gameserver/src/
catacombs.cpp, FightAssassin / AssassinMechanic: **the constants there and here must
agree**). Applied by `import-catacombs.py`. Shared tools: `catacomb_kit.py`.

The last of the crypt's four sword fights, in violet. Ferdinando's floor is lines and
Gladiomurus's is walls; his is **where you are told to be**. Every other boss's shapes are
red and mean "get out". Two of his are green and mean "get in" (the user's soaking
mechanic, 2026-11-13): a shape that somebody must be standing in when it lands.

    move              damage      anim   slots    cast     the floor
    Shadowstep         7235       7242    6 / 7   1.56 s   a circle under one player, he is in it
                                                           a second later; then the next player,
                                                           and a third. Later each strike leaves
                                                           a cross of cuts behind
    Fan of Knives      7236       7243    8 / 9   1.67 s   three volleys, each a narrow cone at
                                                           every player from where he stands
    Black Sun          7237/7238  7244   12 / 13  2.0 s    GREEN circles (one, later up to three,
                                                           never more than there are players):
                                                           each must have someone in it as it
                                                           lands, who takes a moderate hit
                                                           (7237). An empty one bursts over the
                                                           whole room (7238)
    Backstab           7239       7245   14 / 15  1.0 s    he is behind whoever he is fighting
                                                           and cuts back through them: a cone
    Execution          7240/7241  7246   16 / 17  2.0 s    his answer to each wave of the dead:
                                                           one GREEN mark, everything else in the
                                                           room red. The blow in the mark (7240)
                                                           is shared by whoever stands in it and
                                                           made for three; outside it is 7241

Where it comes from
-------------------
  * **His skeleton is the avatar's** (Icanes_G: the same 21 `b1_*` bones), so SlopRose's
    player clips go onto it with no bone map: IAI_DASH3 (the three steps), IAI_RIFT (three
    draw cuts 1.17 s apart: the knife volleys), IAI_SUMMON (the raised blade: the suns),
    IAI_UPPER (the cut from behind), IAI_STOP / IAI_SEVERANCE (the execution).
  * **The effects** are the katana kit in violet (`catacomb_kit.Kit`), with a line, a wide
    fan and a narrow one built from its slashes.

    python scripts/catacomb_assassin.py            # the plan
    python scripts/catacomb_assassin.py --sheet    # contact sheets of the clips on him
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_kit as kit  # noqa: E402
import catacomb_monsters as mon  # noqa: E402
import catacomb_pool as pool  # noqa: E402

BUILD = os.path.join(ROOT, "build", "catacombs", "assassin")
SK = boss
ASSN = pool.ASSASSIN
DST_SKEL = r"3Ddata\NPC\etc\Icanes_G\Icanes_G.ZMD"
FILL = r"3Ddata\MOTION\NPC\Icanes03\warning_01.ZMO"
CLIP_DIR = r"3Ddata\MOTION\NPC\ASSASSIN"
NPC_CHR_REL = mon.NPC_CHR_REL

# our clip -> (SlopRose's player clip, frames [a, b) or None for all of it, {frame: event})
CLIPS = {
    "ASSN_STEP_C": ("IAI_DASH3", (0, 14), {0: 44}),
    "ASSN_STEP_A": ("IAI_DASH3", (14, 75), {5: 64, 19: 64, 33: 64}),        # the three strikes
    "ASSN_KNIFE_C": ("IAI_RIFT", (0, 20), {0: 44}),
    "ASSN_KNIFE_A": ("IAI_RIFT", (20, 112), {7: 74, 42: 74, 77: 74}),       # the three volleys
    "ASSN_ORB_C": ("IAI_SUMMON", (0, 12), {2: 44}),
    "ASSN_ORB_A": ("IAI_SUMMON", (12, 23), {}),
    "ASSN_STAB_C": ("IAI_UPPER", (0, 12), {0: 44}),
    "ASSN_STAB_A": ("IAI_UPPER", (12, 44), {4: 74}),                        # the cut
    "ASSN_EXEC_C": ("IAI_STOP", (0, 30), {0: 44}),
    # 64 starts the storm, whose shatter (2.25 s in) lands on frame 89 with the blow
    "ASSN_EXEC_A": ("IAI_SEVERANCE", None, {21: 64}),
}
SLOTS = {6: "ASSN_STEP_C", 7: "ASSN_STEP_A", 8: "ASSN_KNIFE_C", 9: "ASSN_KNIFE_A", 12: "ASSN_ORB_C", 13: "ASSN_ORB_A",
         14: "ASSN_STAB_C", 15: "ASSN_STAB_A", 16: "ASSN_EXEC_C", 17: "ASSN_EXEC_A"}

# ---- the effects: the kit's KATANA_* as VIOLET_*, then those built here; FILE_EFFECT rows from
# FX_FIRST in this order (catacombs.cpp names RIFT_CUT_1, RIFT_CUT_FINISHER, LINE, FAN and KNIFE by row)
FX_FIRST = 2926
KIT_FX = ("DRAW_FLASH", "RIFT_1", "RIFT_CUT_1", "RIFT_CUT_FINISHER", "GALE_GUST", "PHANTOM_WAKE", "SEVERANCE",
          "SUMMON_FAN", "UPPER_SLASH")
BUILT_FX = ("LINE", "FAN", "KNIFE")
FX = KIT_FX + BUILT_FX


def fx(name):
    return FX_FIRST + FX.index(name)


SND_CUT_HIT = 43                    # Sword_H01
anim = kit.anim


def damage(power):
    return kit.damage(power, fx("RIFT_1"), SND_CUT_HIT)


SKILLS = (  # row, source row, name, cells, description
    (7235, kit.DAMAGE_SRC, "Shadowstep", damage(440), "He is where you stand. Do not be."),
    (7236, kit.DAMAGE_SRC, "Fan of Knives", damage(380), "A knife for each of you, three times."),
    (7237, kit.DAMAGE_SRC, "Black Sun", damage(330), "Someone must stand under it as it falls."),
    (7238, kit.DAMAGE_SRC, "Black Sun unleashed", damage(680), "Nobody stood under it."),
    (7239, kit.DAMAGE_SRC, "Backstab", damage(520), "He is behind you, and the blade comes back through."),
    (7240, kit.DAMAGE_SRC, "Execution", damage(1250), "One mark, one blow. Share it."),
    (7241, kit.DAMAGE_SRC, "Night Without End", damage(720), "Everywhere but the mark."),
    (7242, kit.ANIM_SRC, "The Assassin steps", anim(6, 30, fx("DRAW_FLASH"), fx("PHANTOM_WAKE")), "The Assassin's steps."),
    (7243, kit.ANIM_SRC, "The Assassin throws", anim(8, 40, fx("DRAW_FLASH"), None, fx("GALE_GUST")),
     "The Assassin's knives."),
    (7244, kit.ANIM_SRC, "The Assassin calls the sun", anim(12, 20, fx("SUMMON_FAN")), "The Assassin's black sun."),
    (7245, kit.ANIM_SRC, "The Assassin stabs", anim(14, 40, fx("DRAW_FLASH"), None, fx("UPPER_SLASH")),
     "The Assassin's backstab."),
    (7246, kit.ANIM_SRC, "The Assassin executes", anim(16, 50, fx("DRAW_FLASH"), fx("SEVERANCE")),
     "The Assassin's execution."),
)

P = kit.P


def clip_rel(name):
    return CLIP_DIR + "\\" + name + ".ZMO"


# ---------------------------------------------------------------------- the clips
def build_clips(ours):
    out = {}
    for name, (stem, cut, events) in CLIPS.items():
        blob = kit.retargeted(stem, mon.find_ci(ours, DST_SKEL), mon.find_ci(ours, FILL), BUILD)
        out[clip_rel(name)] = kit.cut_clip(blob, cut, events)
    return out


def slot_clips():
    return {slot: clip_rel(name) for slot, name in SLOTS.items()}


def chr_entry(chr_):
    return kit.chr_entry(chr_, ASSN, slot_clips())


# ---------------------------------------------------------------------- the effects, in violet
def shadowed(hi, lo):
    """A colour as shadow: white becomes lilac (224, 171, 255), a pure tint deep violet
    (148, 31, 255)."""
    return hi * 0.58 + lo * 0.30, hi * 0.12 + lo * 0.55, hi


VIOLET = kit.Kit("VIOLET", shadowed)


def effect_files(ours):
    """{data-relative path: bytes} of the violet kit and what is built here."""
    files, k = {}, {}
    for name in KIT_FX:
        k[name] = VIOLET.effect(name, files)
    gust = k["GALE_GUST"]
    VIOLET.built("LINE", kit.line_effect(gust), files)
    # the backstab's cone: 10 m, wide
    VIOLET.built("FAN", kit.fan_effect(gust, angles=(-36, -18, 0, 18, 36), reach=500, length=900), files)
    # a knife's cone: 24 m, narrow
    VIOLET.built("KNIFE", kit.fan_effect(gust, angles=(-7, 7), reach=1200, length=1980, dust_angles=(0,)), files)
    kit.check_meshes(files)
    return files


# ---------------------------------------------------------------------- apply
def apply(ours, dry):
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in build_clips(ours).items())
    print(f"    {'the clips':26s} {len(CLIPS)} on his skeleton, {changed} written")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    want = chr_entry(chr_)
    if chr_.chars[ASSN] != want:
        chr_.chars[ASSN] = want
        chr_.save(dry, keep_backup=False)
        print(f"    {'LIST_NPC.CHR':26s} entry {ASSN}: slots {sorted(SLOTS)}")
    files = effect_files(ours)
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in files.items())
    print(f"    {'the violet kit':26s} {len(files)} files, {changed} written")
    kit.apply_effect_rows(ours, FX_FIRST, FX, "VIOLET", dry)
    kit.apply_skill_rows(ours, SKILLS, dry)


def verify(ours, check):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    before = len(chr_.motions)
    check(chr_.chars[ASSN] == chr_entry(chr_) and len(chr_.motions) == before, f"LIST_NPC.CHR {ASSN} and its clips")
    check(all(os.path.isfile(mon.find_ci(ours, clip_rel(n))) for n in CLIPS), f"{len(CLIPS)} ASSN_* clips")
    kit.verify_effect_rows(ours, FX_FIRST, FX, "VIOLET", check)
    kit.verify_skill_rows(ours, SKILLS, check)


def remove_files(ours):
    """What this module wrote that no table restore takes back (--restore). The kit's own
    meshes are Ferdinando's too: his module removes those."""
    n = 0
    for rel in [r for r in effect_files(ours) if "VIOLET_" in r.upper()] + [clip_rel(name) for name in CLIPS]:
        path = mon.find_ci(ours, rel)
        if os.path.isfile(path):
            os.remove(path)
            n += 1
    print(f"    removed {n} Assassin files")


def main():
    ours = os.path.join(ROOT, "data")
    if "--sheet" in sys.argv:
        kit.sheets(ours, ASSN, slot_clips(), build_clips(ours), os.path.join(BUILD, "sheets"),
                   sys.argv[sys.argv.index("--sheet") + 1:], cols=8)
        return 0
    for name, (src, cut, events) in CLIPS.items():
        print("%-14s %-16s frames %-10s events %s" % (name, src, cut or "all", events))
    for row, _src, name, cells, _d in SKILLS:
        print(row, name, "power", cells.get(SK.SK_POWER, "-"), "cast slot", cells.get(SK.SK_CAST_MOTION, "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
