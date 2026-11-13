#!/usr/bin/env python3
"""Ferdinando, the Eternal: an iai swordsman in blood (2026-11-12).

The body is `catacomb_pool.py`'s (LIST_NPC 2557, Jrose's vampire lord on the Brinto rig);
the fight is the controller's (src/sho_gameserver/src/catacombs.cpp, FightFerdinando /
FerdinandoMechanic: **the constants there and here must agree**). Applied by
`import-catacombs.py`. The user's brief: "a fancy swordsman with bloody effects".

Where it comes from
-------------------
SlopRose's iai katana kit (C:\\Users\\Thomas\\Desktop\\Rose\\sloprose\\overrides, the
SlopStudio toolkit's work; ours to use): player clips IAI_* and 23 KATANA_* effects.

  * **The clips** are the player's, so they are moved onto Ferdinando's skeleton with
    EVO-REFORGED's retargeter (Rose_Engine/TOOLS/AnimRetarget/retarget.py, run as a
    subprocess, as fix-quarantine-officer-skills.py does). The Brinto rig has the
    avatar's 21 `b1_*` bones under the same names, so the mapping is by name; its two
    extra bones take his own idle (--fill). Every source event is cleared (the kit's
    101-106 and 95-99 are its own client's) and ours written: 31 / 21 on his swing, and
    the casting-effect events 44 / 64 / 74 / 84 that fire a move's effects on him.
  * **A move is a cast clip and an action clip** (a monster casts on slot N, releases on
    N + 1). The cast is the move's wind-up cut off and played slowly -- the time the
    floor warning is read in -- and the action the rest. Six moves need six pairs, and a
    monster had two: MAX_MOB_ANI went from 11 to 24 (datatype.h) for this, slots 12-23.
  * **The effects** are the kit's, recoloured: every colour key of the particle files and
    every pixel of the kit's own textures goes to blood (the brightest channel kept as
    red, the rest crushed), written as BLOOD_*; the azure originals are not installed.
    Meshes and their animations are copied as they are. Two kinds are left out or
    changed: the kit's *held* effects never end by themselves (its client takes them
    away), so the hovering swords and the charge glow are not used, and the ring of
    swords is given two turns to live. Three effects are built here from the kit's
    slash meshes: BLOOD_LINE (a 20 m cut and two shorter ones along +X), BLOOD_DASH (a
    14 m one) and BLOOD_FAN (five cuts fanned from the origin). The floor layer turns an
    effect's +X down its strip or cone (groundtelegraph.cpp, Erupt).

The fight (catacombs.cpp: FightFerdinando, FerdinandoMechanic)
-------------------------------------------------------------
He walks and swings like any monster. Between swings, on the controller's clock, one
move at a time; each is an animation row (a self "skill" that is only its clips and
their casting effects) and shapes on the floor that hit by a damage row (never cast:
catacomb_argentia.py explains). A move with a front is aimed at whoever he is fighting.

    move              damage  anim   slots    cast     the floor
    Crimson Draw       7213   7219    6 / 7   1.67 s   three lines in a fan, one per draw;
                                                       later each goes through him
    Night Step         7214   7220    8 / 9   2.33 s   a zig-zag through three players, all
                                                       shown at once; he is at the end of
                                                       each line as it fires (GSV_MOB_BLINK)
    Sanguine Rain      7215   7221   12 / 13  1.0 s    a cut opening under every player,
                                                       then again, then a third time
    Severance          7216   7222   14 / 15  2.0 s    six lines, each through one player,
                                                       while a storm builds 17 m round him:
                                                       out of it before it breaks. His
                                                       answer to each wave of the dead
    Spiral of Blades   7217   7223   16 / 17  2.17 s   everything from 5 to 19 m round him;
                                                       later a cut round himself a second
                                                       after
    Rising Crescent    7218   7224   18 / 19  1.33 s   a 120-degree cone in front; later
                                                       the same behind

**The cast speeds are multiples of 10 %** because the engine plays a clip at a whole
number of frames a second (30 x speed): 25 % would run at 7 fps on the client and 7.5
on the server, and the floor would fire a seventh of a second off the blade.

    python scripts/catacomb_ferdinando.py            # the plan
    python scripts/catacomb_ferdinando.py --sheet    # contact sheets of the clips on him
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

BUILD = os.path.join(ROOT, "build", "catacombs", "ferdinando")
SK = boss
FERD = pool.FERDINANDO
DST_SKEL = r"3Ddata\NPC\animal\Brinto1\Brinto1_bone.ZMD"
FILL = r"3Ddata\MOTION\NPC\Brinto1\Brinto1_warning.ZMO"
CLIP_DIR = r"3Ddata\MOTION\NPC\FERDINANDO"
NPC_CHR_REL = mon.NPC_CHR_REL

# our clip -> (the kit's clip, frames [a, b) or None for all of it, {frame: event})
CLIPS = {
    "FERD_ATTACK": ("IAI_ATTACK1", None, {22: 31, 25: 21}),
    "FERD_DRAW_C": ("IAI_3ATTACK", (0, 15), {0: 44}),
    "FERD_DRAW_A": ("IAI_3ATTACK", (15, 75), {4: 64, 18: 74, 32: 84}),      # the three draws
    "FERD_DASH_C": ("IAI_DASH3", (0, 14), {0: 44}),
    "FERD_DASH_A": ("IAI_DASH3", (14, 75), {5: 64, 19: 64, 33: 64}),        # the three dashes' cuts
    "FERD_RAIN_C": ("IAI_SUMMON", (0, 12), {2: 44}),
    "FERD_RAIN_A": ("IAI_SUMMON", (12, 23), {}),
    "FERD_SEVER_C": ("IAI_STOP", (0, 30), {0: 44}),                         # the sheathed stance, held
    # 74 on each of the six cuts; 64 starts the storm, whose shatter (2.25 s in) lands on frame 89
    "FERD_SEVER_A": ("IAI_SEVERANCE", None, {8: 74, 17: 74, 21: 64, 26: 74, 35: 74, 44: 74, 53: 74}),
    "FERD_SPIRAL_C": ("IAI_SPIRAL", (0, 13), {0: 44}),
    "FERD_SPIRAL_A": ("IAI_SPIRAL", (13, 36), {0: 74}),
    "FERD_UPPER_C": ("IAI_UPPER", (0, 12), {0: 44}),
    "FERD_UPPER_A": ("IAI_UPPER", (12, 44), {4: 74}),
}
SLOTS = {2: "FERD_ATTACK", 6: "FERD_DRAW_C", 7: "FERD_DRAW_A", 8: "FERD_DASH_C", 9: "FERD_DASH_A",
         12: "FERD_RAIN_C", 13: "FERD_RAIN_A", 14: "FERD_SEVER_C", 15: "FERD_SEVER_A",
         16: "FERD_SPIRAL_C", 17: "FERD_SPIRAL_A", 18: "FERD_UPPER_C", 19: "FERD_UPPER_A"}

# ---- the effects: the kit's KATANA_* as BLOOD_*, then the three built here; FILE_EFFECT rows
# from FX_FIRST in this order (catacombs.cpp names RIFT_CUT_1, LINE, DASH and FAN by row)
FX_FIRST = 2903
KIT_FX = ("DRAW_FLASH", "RIFT_1", "RIFT_CUT_1", "PHANTOM_WAKE", "FINISHER_CUT", "SEVERANCE", "SUMMON_FAN",
          "SPIRAL_SWORDS", "SPIRAL_BURST", "UPPER_SLASH", "GALE_GUST")
BUILT_FX = ("LINE", "DASH", "FAN")
FX = KIT_FX + BUILT_FX


def fx(name):
    return FX_FIRST + FX.index(name)


SND_CUT, SND_CUT_HIT = 38, 43       # Sword_002, Sword_H01 (stock, until his own are made)

DAMAGE_SRC, ANIM_SRC = kit.DAMAGE_SRC, kit.ANIM_SRC
anim = kit.anim


def damage(power):
    """A damage row: what a player caught by the floor takes, sees (cuts through the body)
    and hears."""
    return kit.damage(power, fx("RIFT_1"), SND_CUT_HIT)


SKILLS = (  # row, source row, name, cells, description
    (7213, DAMAGE_SRC, "Crimson Draw", damage(430), "Three draws of the blade, each a line of blood across the room."),
    (7214, DAMAGE_SRC, "Night Step", damage(460), "He is where you stood before you saw him move."),
    (7215, DAMAGE_SRC, "Sanguine Rain", damage(320), "The air is cut open where the living stand."),
    (7216, DAMAGE_SRC, "Severance", damage(440), "Six cuts no eye follows, and a storm of them round him for whoever stayed."),
    (7217, DAMAGE_SRC, "Spiral of Blades", damage(380), "A ring of swords thrown out from him. Against him is the only shelter."),
    (7218, DAMAGE_SRC, "Rising Crescent", damage(450), "A rising cut that opens everything in front of him."),
    (7219, ANIM_SRC, "Ferdinando draws", anim(6, 30, fx("DRAW_FLASH"), fx("GALE_GUST"), fx("GALE_GUST"), fx("GALE_GUST")),
     "Ferdinando's draw cuts."),
    (7220, ANIM_SRC, "Ferdinando steps", anim(8, 20, fx("DRAW_FLASH"), fx("PHANTOM_WAKE")), "Ferdinando's dashes."),
    (7221, ANIM_SRC, "Ferdinando calls the rain", anim(12, 40, fx("SUMMON_FAN")), "Ferdinando's rain of cuts."),
    (7222, ANIM_SRC, "Ferdinando severs", anim(14, 50, fx("DRAW_FLASH"), fx("SEVERANCE"), fx("FINISHER_CUT")),
     "Ferdinando's Severance."),
    (7223, ANIM_SRC, "Ferdinando spins", anim(16, 20, fx("SPIRAL_SWORDS"), None, fx("SPIRAL_BURST")), "Ferdinando's spiral."),
    (7224, ANIM_SRC, "Ferdinando rises", anim(18, 30, fx("DRAW_FLASH"), None, fx("UPPER_SLASH")), "Ferdinando's rising cut."),
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
    return kit.chr_entry(chr_, FERD, slot_clips())


# ---------------------------------------------------------------------- the effects, in blood
BLOOD_GREEN, BLOOD_BLUE = 0.17, 0.20     # white becomes (255, 43, 56): crimson, not pink


def bloody(hi, lo):
    """A colour as blood: its brightest channel becomes the red, the others are crushed
    (white keeps a little, so a core still reads as hot)."""
    return hi, lo * BLOOD_GREEN, lo * BLOOD_BLUE + hi * 0.02


BLOOD = kit.Kit("BLOOD", bloody)


def effect_files(ours):
    """{data-relative path: bytes} of the blood kit: effects, particle files, textures, and
    the kit's meshes and mesh animations as they are."""
    files, k = {}, {}
    for name in KIT_FX:
        k[name] = BLOOD.effect(name, files)
    gust, wake = k["GALE_GUST"], k["PHANTOM_WAKE"]
    slash, dust, _ = kit.gust_parts(gust)
    BLOOD.built("LINE", kit.line_effect(gust), files)
    BLOOD.built("DASH", dict(
        gust,
        meshes=[kit.cut(slash, 1440), kit.cut(slash, 1200, z=140, twist=45, tilt=4, delay=50)],
        particles=[kit.at(wake["particles"][0], x, z=100, delay=20 * n) for n, x in enumerate((-500, 0, 500))]
        + [kit.at(dust, x, delay=30) for x in (-400, 400)]), files)
    BLOOD.built("FAN", kit.fan_effect(gust), files)
    kit.check_meshes(files)
    return files


# ---------------------------------------------------------------------- apply
def apply(ours, dry):
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in build_clips(ours).items())
    print(f"    {'the clips':26s} {len(CLIPS)} on his skeleton, {changed} written")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    want = chr_entry(chr_)
    if chr_.chars[FERD] != want:
        chr_.chars[FERD] = want
        chr_.save(dry, keep_backup=False)
        print(f"    {'LIST_NPC.CHR':26s} entry {FERD}: slots {sorted(SLOTS)}")
    files = effect_files(ours)
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in files.items())
    print(f"    {'the blood kit':26s} {len(files)} files, {changed} written")
    kit.apply_effect_rows(ours, FX_FIRST, FX, "BLOOD", dry)
    kit.apply_skill_rows(ours, SKILLS, dry)


def verify(ours, check):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    before = len(chr_.motions)
    check(chr_.chars[FERD] == chr_entry(chr_) and len(chr_.motions) == before, f"LIST_NPC.CHR {FERD} and its sword clips")
    check(all(os.path.isfile(mon.find_ci(ours, clip_rel(n))) for n in CLIPS), f"{len(CLIPS)} FERD_* clips")
    kit.verify_effect_rows(ours, FX_FIRST, FX, "BLOOD", check)
    kit.verify_skill_rows(ours, SKILLS, check)


def remove_files(ours):
    """What this module wrote that no table restore takes back (--restore); the kit's
    meshes stay only if something else of ours names them, which nothing does."""
    n = 0
    for rel in list(effect_files(ours)) + [clip_rel(name) for name in CLIPS]:
        path = mon.find_ci(ours, rel)
        if os.path.isfile(path):
            os.remove(path)
            n += 1
    print(f"    removed {n} Ferdinando files")


def main():
    ours = os.path.join(ROOT, "data")
    if "--sheet" in sys.argv:
        kit.sheets(ours, FERD, slot_clips(), build_clips(ours), os.path.join(BUILD, "sheets"),
                   sys.argv[sys.argv.index("--sheet") + 1:])
        return 0
    for name, (stem, cut, events) in CLIPS.items():
        print("%-14s %-14s frames %-9s events %s" % (name, stem, cut or "all", events))
    for row, _src, name, cells, _d in SKILLS:
        print(row, name, "power", cells.get(SK.SK_POWER, "-"), "cast slot", cells.get(SK.SK_CAST_MOTION, "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
