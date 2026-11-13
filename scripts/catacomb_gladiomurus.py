#!/usr/bin/env python3
"""Gladiomurus, the Wall of Swords: a valkyrie who cuts the room up (2026-11-13).

The body is `catacomb_pool.py`'s (LIST_NPC 2559: Jrose's "valkyrie goblin", a red horned
woman with a long blade); the fight is the controller's (src/sho_gameserver/src/
catacombs.cpp, FightGladiomurus / GladiomurusMechanic: **the constants there and here
must agree**). Applied by `import-catacombs.py`. Shared tools: `catacomb_kit.py`.

Her name is the fight: *gladius*, *murus*. Where Ferdinando's cuts are over in a blink,
hers stay: a line of great swords falls across the whole room and stands there for
eleven seconds, hurting whoever is in it or goes through it, so every other move is dodged in a room that
has just changed shape. Gold where he is blood.

    move              damage  anim   slots    cast     the floor
    Sword Wall         7225   7230   12 / 13  1.33 s   a line of swords across the room through
                                                       one player, standing 11 s; later a cross
    Valkyrie's Lance   7226   7231   14 / 15  1.33 s   four thrusts, each a long thin line at a
                                                       different player (she turns to each);
                                                       later each is a trident
    Crescent Wheel     7227   7232    8 / 9   1.83 s   her blade goes round like a clock hand:
                                                       eight wedges, one after the other; later
                                                       it comes back the other way
    Skyfall            7228   7233    6 / 7   1.83 s   she leaps onto the farthest player (and is
                                                       there: GSV_MOB_BLINK), then two rings roll
                                                       out from where she landed
    Valhalla's Gate    7229   7234   16 / 17  2.0 s    her answer to each wave of the dead: a
                                                       square of sword walls round her, and
                                                       everything outside it burns as she lands.
                                                       The cage stands 13 s and she fights in it

Where it comes from
-------------------
  * **Her own clips** for Skyfall (Gob_girl_skill01: a flip and the blade planted in the
    ground) and the Wheel (Gob_girl_skill02: low sweeps), as copies with our events.
  * **SlopRose's player clips** for the rest, moved onto her skeleton by EVO-REFORGED's
    retargeter: IAI_SUMMON (the blade raised: the wall), RAPIER_WARNING / RAPIER_4ATTACK
    (the fencing stance and four thrusts, on frames 5, 15, 44 and 81: one-two, three,
    and a long wait before the fourth), TWOSWD_CHARGE / TWOSWD_AERIAL (a crouch, then a
    leap with blows in the air and a slam on frame 86: the gate). **Her skeleton is a
    3ds Max biped under its default names** ("Bip01 L Thigh"), which the retargeter
    cannot pair with the avatar's `b1_*` by itself: BONE_MAP is its map file.
  * **The effects** are SlopRose's katana kit in gold (`catacomb_kit.Kit`), a line and a
    wedge of cuts built from its slashes, and **the wall**: the kit's spectral sword made
    a great sword (2.4 times as long, 5 times as wide: GOLD_GREATSWORD.ZMS and its bob,
    scaled copies) and stood point down every 1.1 m, flat in the wall's plane, so the
    line reads as a palisade from both sides. Each bobs seven times (11.2 s) and the
    effect ends: the controller's wall lasts exactly as long.

    python scripts/catacomb_gladiomurus.py            # the plan
    python scripts/catacomb_gladiomurus.py --sheet    # contact sheets of the clips on her
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import zmo as rfzmo  # noqa: E402
from rosefmt import zms as rfzms  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_kit as kit  # noqa: E402
import catacomb_monsters as mon  # noqa: E402
import catacomb_pool as pool  # noqa: E402

BUILD = os.path.join(ROOT, "build", "catacombs", "gladiomurus")
SK = boss
GLAD = pool.GLADIOMURUS
DST_SKEL = r"3Ddata\NPC\npc\Gob_girl\Gob_girl.ZMD"
OWN_DIR = r"3Ddata\MOTION\NPC\Gob_girl"
FILL = OWN_DIR + r"\Gob_girl_stand.ZMO"
CLIP_DIR = r"3Ddata\MOTION\NPC\GLADIOMURUS"
NPC_CHR_REL = mon.NPC_CHR_REL
BONE_MAP = """# Gob_girl (a 3ds Max biped under its default names) <- the avatar
Bip01 Spine = b1_belly
Bip01 Spine1 = b1_chest
Bip01 Neck = b1_neck
Bip01 Head = b1_head
Bip01 L Clavicle = b1_lclavicle
Bip01 L UpperArm = b1_lupperarm
Bip01 L Forearm = b1_lforearm
Bip01 L Hand = b1_lhand
Bip01 R Clavicle = b1_rclavicle
Bip01 R UpperArm = b1_rupperarm
Bip01 R Forearm = b1_rforearm
Bip01 R Hand = b1_rhand
Bip01 L Thigh = b1_lthigh
Bip01 L Calf = b1_lcalf
Bip01 L Foot = b1_lfoot
Bip01 L Toe0 = b1_ltoe0
Bip01 R Thigh = b1_rthigh
Bip01 R Calf = b1_rcalf
Bip01 R Foot = b1_rfoot
Bip01 R Toe0 = b1_rtoe0
"""

# our clip -> (where from, frames [a, b) or None for all of it, {frame: event})
#   "own:<file>" = her own clip in OWN_DIR, anything else = SlopRose's player clip
CLIPS = {
    "GLAD_SKY_C": ("own:Gob_girl_casting.ZMO", None, {0: 44}),
    # she leaves the ground on frame 31 and the blade is in the floor on 44
    "GLAD_SKY_A": ("own:Gob_girl_skill01.ZMO", None, {28: 64, 44: 74}),
    "GLAD_WHEEL_C": ("own:Gob_girl_casting.ZMO", None, {0: 44}),
    "GLAD_WHEEL_A": ("own:Gob_girl_skill02.ZMO", None, {6: 74, 20: 74, 35: 74}),
    "GLAD_WALL_C": ("IAI_SUMMON", (0, 12), {2: 44}),
    "GLAD_WALL_A": ("IAI_SUMMON", (12, 23), {}),
    "GLAD_LANCE_C": ("RAPIER_WARNING", (0, 12), {0: 44}),
    "GLAD_LANCE_A": ("RAPIER_4ATTACK", (0, 100), {5: 64, 15: 64, 44: 64, 81: 64}),     # the four thrusts
    "GLAD_GATE_C": ("TWOSWD_CHARGE", (100, 112), {0: 44}),
    "GLAD_GATE_A": ("TWOSWD_AERIAL", None, {86: 74}),                                  # the slam
}
SLOTS = {6: "GLAD_SKY_C", 7: "GLAD_SKY_A", 8: "GLAD_WHEEL_C", 9: "GLAD_WHEEL_A", 12: "GLAD_WALL_C", 13: "GLAD_WALL_A",
         14: "GLAD_LANCE_C", 15: "GLAD_LANCE_A", 16: "GLAD_GATE_C", 17: "GLAD_GATE_A"}

# ---- the effects: the kit's KATANA_* as GOLD_*, then those built here; FILE_EFFECT rows from
# FX_FIRST in this order (catacombs.cpp names RIFT_CUT_FINISHER, LINE, WEDGE, WALL and CAGE by row)
FX_FIRST = 2917
KIT_FX = ("DRAW_FLASH", "RIFT_1", "RIFT_CUT_FINISHER", "GALE_GUST", "SUMMON_FAN")
BUILT_FX = ("LINE", "WEDGE", "WALL", "CAGE")
FX = KIT_FX + BUILT_FX
WALL_CM, CAGE_CM = 6000, 2700       # a wall across the room; one side of the gate's square
SWORD_STEP = 110                    # cm between two swords of a wall
SWORD_LONG, SWORD_WIDE = 2.4, 5.0   # the kit's 2.6 m spectral sword made a 6.3 m great sword
SWORD_BURIED = 50                   # cm of its point in the floor
SWORD_BOBS = 7                      # x 1.6 s: catacombs.cpp's GLAD_WALL_MS is 11200
SWORD_MESH = kit.MESH_DIR + "GOLD_GREATSWORD.ZMS"
SWORD_BOB = kit.MESH_DIR + "GOLD_GREATSWORD_STAND.ZMO"
SWORD_LIGHT = 1.8                   # the kit's sword is see-through: these are a wall


def fx(name):
    return FX_FIRST + FX.index(name)


SND_CUT_HIT = 43                    # Sword_H01
anim = kit.anim


def damage(power):
    return kit.damage(power, fx("RIFT_1"), SND_CUT_HIT)


SKILLS = (  # row, source row, name, cells, description
    (7225, kit.DAMAGE_SRC, "Sword Wall", damage(170), "A line of great swords across the room. It stands, and it cuts."),
    (7226, kit.DAMAGE_SRC, "Valkyrie's Lance", damage(430), "Four thrusts, each for someone else."),
    (7227, kit.DAMAGE_SRC, "Crescent Wheel", damage(400), "Her blade goes all the way round. Go round with it."),
    (7228, kit.DAMAGE_SRC, "Skyfall", damage(470), "She comes down where the farthest of you stands, and the floor rolls out from her."),
    (7229, kit.DAMAGE_SRC, "Valhalla's Gate", damage(620), "A hall of swords closes round her. Be inside it."),
    (7230, kit.ANIM_SRC, "Gladiomurus raises a wall", anim(12, 30, fx("SUMMON_FAN")), "Gladiomurus's sword wall."),
    (7231, kit.ANIM_SRC, "Gladiomurus thrusts", anim(14, 30, fx("DRAW_FLASH"), fx("DRAW_FLASH")), "Gladiomurus's thrusts."),
    (7232, kit.ANIM_SRC, "Gladiomurus wheels", anim(8, 20, fx("DRAW_FLASH"), None, fx("GALE_GUST")), "Gladiomurus's wheel."),
    (7233, kit.ANIM_SRC, "Gladiomurus leaps", anim(6, 20, fx("DRAW_FLASH"), fx("DRAW_FLASH"), fx("GALE_GUST")),
     "Gladiomurus's leap."),
    (7234, kit.ANIM_SRC, "Gladiomurus closes the gate", anim(16, 20, fx("SUMMON_FAN"), None, fx("GALE_GUST")),
     "Gladiomurus's gate."),
)

P = kit.P


def clip_rel(name):
    return CLIP_DIR + "\\" + name + ".ZMO"


# ---------------------------------------------------------------------- the clips
def map_file():
    os.makedirs(BUILD, exist_ok=True)
    path = os.path.join(BUILD, "gob_girl.map")
    if not os.path.isfile(path) or open(path).read() != BONE_MAP:
        with open(path, "w") as fh:
            fh.write(BONE_MAP)
    return path


def build_clips(ours):
    out = {}
    for name, (src, cut, events) in CLIPS.items():
        if src.startswith("own:"):
            blob = open(mon.find_ci(ours, OWN_DIR + "\\" + src[4:]), "rb").read()
        else:
            blob = kit.retargeted(src, mon.find_ci(ours, DST_SKEL), mon.find_ci(ours, FILL), BUILD, map_file())
        out[clip_rel(name)] = kit.cut_clip(blob, cut, events)
    return out


def slot_clips():
    return {slot: clip_rel(name) for slot, name in SLOTS.items()}


def chr_entry(chr_):
    return kit.chr_entry(chr_, GLAD, slot_clips())


# ---------------------------------------------------------------------- the effects, in gold
def gilded(hi, lo):
    """A colour as gold: white becomes pale gold (255, 235, 168), a pure tint deep gold
    (255, 173, 46)."""
    return hi, hi * 0.68 + lo * 0.24, hi * 0.18 + lo * 0.48


GOLD = kit.Kit("GOLD", gilded)


def great_sword():
    """(mesh bytes, bob bytes): the kit's spectral sword and its hover, longer and wider.
    The sword lies along its own +X, flat, its width along +Z."""
    m = rfzms.parse(kit.kit_file(kit.MESH_DIR + "KATANA_SPECTRAL_SWORD.ZMS"))
    scale = (SWORD_LONG, 1.0, SWORD_WIDE)
    m.positions = (m.positions * scale).astype(m.positions.dtype)
    m.bbox_min = tuple(float(v * k) for v, k in zip(m.bbox_min, scale))
    m.bbox_max = tuple(float(v * k) for v, k in zip(m.bbox_max, scale))
    bob = bytearray(kit.kit_file(kit.MESH_DIR + "KATANA_SUMMONED_SWORD_HOVER.ZMO"))
    start, stride, (_, frames, chans) = rfzmo.frame_layout(bytes(bob))
    for f in range(frames):
        o = start + stride * f
        for ctype, _ in chans:
            if ctype == 2:              # a vertex position, cm
                x, y, z = struct.unpack_from("<3f", bob, o)
                struct.pack_into("<3f", bob, o, x * scale[0], y * scale[1], z * scale[2])
            o += rfzmo.FIELD_SIZE[ctype]
    return rfzms.build(m), bytes(bob)


def sword_texture():
    """The kit's spectral sword texture in gold, brighter: it is see-through as shipped."""
    import numpy as np
    from PIL import Image
    from rosefmt import dds
    a = np.asarray(dds.read_bgra(GOLD.texture(mon.find_ci(kit.SLOP, r"3DDATA\EFFECT\PARTICLES\TEXTURE\KATANA_SPECTRAL.DDS")))
                   .convert("RGBA"), dtype=np.float32)
    a[..., :3] *= SWORD_LIGHT
    return dds.bgra_bytes(Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA"))


def wall_effect(gust, fade, length_cm):
    """Great swords point down along +X, `length_cm` from end to end, flat in the wall's
    plane; dust where they land. `fade` is the kit's fading sword, for its mesh entry."""
    _, dust, glint = kit.gust_parts(gust)
    sword = fade["meshes"][0]
    half = 1.31 * 100 * SWORD_LONG          # the sword's half length, cm
    n = int(length_cm // SWORD_STEP) + 1
    x0 = -(n - 1) * SWORD_STEP / 2.0
    swords = []
    for k in range(n):
        lean = (-90 + 5, -90 - 4, -90 + 2, -90 - 6)[k % 4]
        swords.append(dict(sword, zms=SWORD_MESH, zmo=SWORD_BOB, texture=r"3DDATA\EFFECT\PARTICLES\TEXTURE\GOLD_GREATSWORD.DDS",
                           pos=(x0 + k * SWORD_STEP, 0.0, half - SWORD_BURIED + (0, 35, -20, 15)[k % 4]),
                           rot=(0.0, 0.0, float(lean), 0.0), delay=0, repeat=SWORD_BOBS))
    step = 500
    puffs = [kit.at(dust, x, delay=0) for x in range(int(-length_cm / 2), int(length_cm / 2) + 1, step)]
    glints = [kit.at(glint, x, z=350, delay=60) for x in range(int(-length_cm / 2) + 250, int(length_cm / 2), step * 2)]
    return dict(gust, meshes=swords, particles=puffs + glints)


def effect_files(ours):
    """{data-relative path: bytes} of the gold kit and what is built here."""
    files, k = {}, {}
    for name in KIT_FX:
        k[name] = GOLD.effect(name, files)
    fade = GOLD.recoloured(kit.eft.read(kit.kit_file(r"3DDATA\EFFECT\KATANA_SUMMONED_SWORD_FADE.EFT")), {})
    gust = k["GALE_GUST"]
    files[SWORD_MESH], files[SWORD_BOB] = great_sword()
    files[r"3DDATA\EFFECT\PARTICLES\TEXTURE\GOLD_GREATSWORD.DDS"] = sword_texture()
    GOLD.built("LINE", kit.line_effect(gust), files)
    # a wedge of the wheel: 45 degrees, 14 m
    GOLD.built("WEDGE", kit.fan_effect(gust, angles=(-12, 12), reach=700, length=1200, dust_angles=(0,)), files)
    GOLD.built("WALL", wall_effect(gust, fade, WALL_CM), files)
    GOLD.built("CAGE", wall_effect(gust, fade, CAGE_CM), files)
    kit.check_meshes(files)
    return files


# ---------------------------------------------------------------------- apply
def apply(ours, dry):
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in build_clips(ours).items())
    print(f"    {'the clips':26s} {len(CLIPS)} on her skeleton, {changed} written")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    want = chr_entry(chr_)
    if chr_.chars[GLAD] != want:
        chr_.chars[GLAD] = want
        chr_.save(dry, keep_backup=False)
        print(f"    {'LIST_NPC.CHR':26s} entry {GLAD}: slots {sorted(SLOTS)}")
    files = effect_files(ours)
    changed = sum(kit.write(ours, rel, blob, dry) for rel, blob in files.items())
    print(f"    {'the gold kit':26s} {len(files)} files, {changed} written")
    kit.apply_effect_rows(ours, FX_FIRST, FX, "GOLD", dry)
    kit.apply_skill_rows(ours, SKILLS, dry)


def verify(ours, check):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    before = len(chr_.motions)
    check(chr_.chars[GLAD] == chr_entry(chr_) and len(chr_.motions) == before, f"LIST_NPC.CHR {GLAD} and its clips")
    check(all(os.path.isfile(mon.find_ci(ours, clip_rel(n))) for n in CLIPS), f"{len(CLIPS)} GLAD_* clips")
    kit.verify_effect_rows(ours, FX_FIRST, FX, "GOLD", check)
    kit.verify_skill_rows(ours, SKILLS, check)


def remove_files(ours):
    """What this module wrote that no table restore takes back (--restore). The kit's own
    meshes are Ferdinando's too: his module removes those."""
    n = 0
    for rel in [r for r in effect_files(ours) if "GOLD_" in r.upper()] + [clip_rel(name) for name in CLIPS]:
        path = mon.find_ci(ours, rel)
        if os.path.isfile(path):
            os.remove(path)
            n += 1
    print(f"    removed {n} Gladiomurus files")


def main():
    ours = os.path.join(ROOT, "data")
    if "--sheet" in sys.argv:
        kit.sheets(ours, GLAD, slot_clips(), build_clips(ours), os.path.join(BUILD, "sheets"),
                   sys.argv[sys.argv.index("--sheet") + 1:], cols=8)
        return 0
    for name, (src, cut, events) in CLIPS.items():
        print("%-14s %-26s frames %-10s events %s" % (name, src, cut or "all", events))
    for row, _src, name, cells, _d in SKILLS:
        print(row, name, "power", cells.get(SK.SK_POWER, "-"), "cast slot", cells.get(SK.SK_CAST_MOTION, "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
