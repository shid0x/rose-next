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
import copy
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import dds, eft, ptl  # noqa: E402
from rosefmt import zmo as rfzmo  # noqa: E402
from rosefmt import zms as rfzms  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_monsters as mon  # noqa: E402
import catacomb_pool as pool  # noqa: E402

SLOP = r"C:\Users\Thomas\Desktop\Rose\sloprose\overrides"
RETARGET = r"C:\Users\Thomas\Desktop\Rose\EVO-REFORGED\Rose_Engine\TOOLS\AnimRetarget\retarget.py"
BUILD = os.path.join(ROOT, "build", "catacombs", "ferdinando")
SK = boss
FERD = pool.FERDINANDO
SRC_SKEL = r"3DDATA\AVATAR\MALE.ZMD"                    # the kit's own (two more dummies)
SRC_CLIPS = r"3DDATA\MOTION\AVATAR"
DST_SKEL = r"3Ddata\NPC\animal\Brinto1\Brinto1_bone.ZMD"
FILL = r"3Ddata\MOTION\NPC\Brinto1\Brinto1_warning.ZMO"
CLIP_DIR = r"3Ddata\MOTION\NPC\FERDINANDO"
SKILL_STB_REL, SKILL_STL_REL = boss.SKILL_STB_REL, boss.SKILL_STL_REL
EFFECT_STB_REL = r"3DDATA\STB\FILE_EFFECT.STB"
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
SLASH_CM = 3            # KATANA_SLASH_<n>.ZMS opens to n x 3 cm, along its own +X
SLASH_HEIGHT = 110      # cm over the floor


def fx(name):
    return FX_FIRST + FX.index(name)


SND_CUT, SND_CUT_HIT = 38, 43       # Sword_002, Sword_H01 (stock, until his own are made)

DAMAGE_SRC, ANIM_SRC = boss.BURST, 3585
SK_AFTER = 29           # what he does when the move ends: 2 = back to the fight he was in
NO_CAST = {SK.SK_RANGE: "", SK.SK_CAST_MOTION: "", SK.SK_CAST_SPEED: "", SK.SK_ACT_MOTION: "", SK.SK_ACT_SPEED: "",
           56: "", 57: "", 58: "", 62: "", 63: "", 64: "", SK.SK_TELEGRAPH: ""}
NO_EFFECT = {SK.SK_STATUS: "", SK.SK_SUCCESS: "", SK.SK_DURATION: "", 21: "", 22: "", 23: "", 24: "", 25: "", 26: "",
             74: "", 75: "", 76: ""}


def damage(power):
    """A damage row: what a player caught by the floor takes, sees (cuts through the body)
    and hears."""
    return {**NO_CAST, SK.SK_TYPE: 17, SK.SK_SCOPE: 400, SK.SK_POWER: power,
            74: fx("RIFT_1"), 75: 999, 76: SND_CUT_HIT}


def anim(slot, speed, e44=None, e64=None, e74=None, e84=None):
    """An animation row: cast on `slot` at `speed` %, released on slot + 1, with the effects
    its clips' events 44 / 64 / 74 / 84 fire on him (FILE_EFFECT rows; cols 56 / 59 / 62 / 65).
    On him means at his size: 2.2 times the kit's."""
    cells = {**NO_EFFECT, SK.SK_CAST_MOTION: slot, SK.SK_CAST_SPEED: speed, SK.SK_ACT_MOTION: slot + 1,
             SK.SK_ACT_SPEED: 100, SK_AFTER: 2}
    for col, effect in ((56, e44), (59, e64), (62, e74), (65, e84)):
        cells[col], cells[col + 1], cells[col + 2] = (effect or ""), (999 if effect else ""), ""
    return cells


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


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def clip_rel(name):
    return CLIP_DIR + "\\" + name + ".ZMO"


# ---------------------------------------------------------------------- the clips
def retargeted(ours, stem, cache={}):
    """The kit's clip `stem` on Ferdinando's skeleton (bytes), built once per run."""
    if stem not in cache:
        if not os.path.isfile(RETARGET):
            raise SystemExit(f"the retargeter is not at {RETARGET}")
        os.makedirs(BUILD, exist_ok=True)
        out = os.path.join(BUILD, stem + ".ZMO")
        src = mon.find_ci(SLOP, SRC_CLIPS + "\\" + stem + "_M1.ZMO")
        if not os.path.isfile(src):
            raise SystemExit(f"the kit has no {stem}_M1.ZMO under {SLOP}")
        cmd = [sys.executable, "-I", RETARGET, "--src-skel", mon.find_ci(SLOP, SRC_SKEL), "--src", src,
               "--dst-skel", mon.find_ci(ours, DST_SKEL), "--fill", mon.find_ci(ours, FILL), "--in-place",
               "--out", out, "--quiet"]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if r.returncode or not os.path.isfile(out):
            raise SystemExit("the retargeter failed on %s: %s" % (stem, (r.stderr or r.stdout).strip()[-400:]))
        cache[stem] = open(out, "rb").read()
    return cache[stem]


def with_event_table(blob):
    """The clip, refused if it came back without an event slot per frame."""
    _, frames, _ = rfzmo.read_header(blob)
    base, ev = rfzmo.events(blob)
    if base is None or len(ev or []) < frames:
        raise SystemExit("a retargeted clip has no event table to write the frames' events in")
    return blob


def build_clips(ours):
    out = {}
    for name, (stem, cut, events) in CLIPS.items():
        blob = with_event_table(retargeted(ours, stem))
        if cut:
            blob = rfzmo.sliced(blob, cut[0], cut[1])
        out[clip_rel(name)] = boss.cleared(blob, events)
    return out


def chr_entry(chr_):
    e = copy.deepcopy(chr_.chars[FERD])
    anims = dict(e["anims"])
    for slot, name in SLOTS.items():
        anims[slot] = boss.motion_index(chr_, clip_rel(name))
    e["anims"] = sorted(anims.items())
    return e


# ---------------------------------------------------------------------- the effects, in blood
BLOOD_GREEN, BLOOD_BLUE = 0.17, 0.20     # white becomes (255, 43, 56): crimson, not pink


def bloody(r, g, b):
    """A colour as blood: its brightest channel becomes the red, the others are crushed
    (white keeps a little, so a core still reads as hot)."""
    hi, lo = max(r, g, b), min(r, g, b)
    return hi, lo * BLOOD_GREEN, lo * BLOOD_BLUE + hi * 0.02


def blood_name(path):
    """KATANA_X.ext -> BLOOD_X.ext for the kit's own files, others unchanged."""
    d, f = os.path.split(path.replace("/", "\\"))
    return (d + "\\" if d else "") + ("BLOOD_" + f[7:] if f.upper().startswith("KATANA_") else f)


def blood_texture(path):
    """A kit texture in blood, every level of its chain (the kit ships them uncompressed)."""
    import numpy as np
    from PIL import Image
    img = dds.read_bgra(path).convert("RGBA")
    a = np.asarray(img, dtype=np.float32)
    hi, lo = a[..., :3].max(axis=2), a[..., :3].min(axis=2)
    out = np.stack([hi, lo * BLOOD_GREEN, lo * BLOOD_BLUE + hi * 0.02, a[..., 3]], axis=2)
    return dds.bgra_bytes(Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA"))


def kit_file(rel):
    return open(mon.find_ci(SLOP, rel), "rb").read()


def is_kit(path):
    return os.path.basename(path.replace("\\", "/")).upper().startswith("KATANA_")


def recoloured(c, files):
    """A kit effect (rosefmt.eft's shape) with its particle files and textures in blood;
    everything it needs lands in `files` ({data-relative path: bytes})."""
    parts = []
    for q in c["particles"]:
        q = dict(q)
        src = q["ptl"]
        if q["use_ani"] and q["ani_name"] not in ("", "NULL"):
            files[q["ani_name"]] = kit_file(q["ani_name"])
        if is_kit(src):
            seqs, tail = ptl.parse(kit_file(src))
            for sq in seqs:
                for ev in sq.events:
                    if ev.type == ptl.EVENT_COLOR:
                        v = list(ev.values)
                        v[0:3] = bloody(*v[0:3])
                        v[4:7] = bloody(*v[4:7])
                        ev.values = v
                if sq.loops <= 0:
                    sq.loops = 2            # a held emitter: here it ends by itself
                tex = sq.texture.decode("latin-1")
                if is_kit(tex):
                    files[blood_name(tex)] = blood_texture(mon.find_ci(SLOP, tex))
                    sq.texture = blood_name(tex).encode("latin-1")
            files[blood_name(src)] = ptl.build(seqs, tail)
            q["ptl"] = blood_name(src)
        parts.append(q)
    meshes = []
    for m in c["meshes"]:
        m = dict(m)
        for key in ("zms", "zmo"):
            if m[key] and m[key] != "NULL":
                files[m[key]] = kit_file(m[key])
        if m["use_ani"] and m["ani_name"] not in ("", "NULL"):
            files[m["ani_name"]] = kit_file(m["ani_name"])
            if m["ani_loops"] <= 0:
                m["ani_loops"] = 3          # held: the ring of swords turns 2.4 s, the cast's length
        if m["repeat"] <= 0:
            m["repeat"] = 2
        if is_kit(m["texture"]):
            files[blood_name(m["texture"])] = blood_texture(mon.find_ci(SLOP, m["texture"]))
            m["texture"] = blood_name(m["texture"])
        meshes.append(m)
    return dict(c, particles=parts, meshes=meshes)


def built_effects(kit):
    """The three effects built from the kit's slashes: {name: effect}. An effect's +X is
    the way its strip or cone points; a slash mesh lies along its own +X and a mesh
    entry's rotation is (twist about itself, turn about the vertical, tilt up), degrees."""
    gust, wake = kit["GALE_GUST"], kit["PHANTOM_WAKE"]
    slash, dust = gust["meshes"][0], [q for q in gust["particles"] if "GLINT" not in q["ptl"].upper()][0]
    glint = [q for q in gust["particles"] if "GLINT" in q["ptl"].upper()][0]

    def cut(length_cm, x=0.0, y=0.0, z=SLASH_HEIGHT, twist=0.0, turn=0.0, tilt=0.0, delay=0):
        n = length_cm // SLASH_CM
        stem = rf"3DDATA\EFFECT\EFFECTMESH\KATANA_SLASH_{n}"
        return dict(slash, zms=stem + ".ZMS", zmo=stem + ".ZMO", pos=(float(x), float(y), float(z)),
                    rot=(float(twist), float(turn), float(tilt), 0.0), delay=delay, repeat=1)

    def at(q, x, y=0.0, z=None, delay=0):
        return dict(q, pos=(float(x), float(y), q["pos"][2] if z is None else float(z)), delay=delay)

    line = dict(gust, name="BLOOD_LINE",
                meshes=[cut(1980), cut(1560, x=-150, z=150, twist=40, tilt=3, delay=60),
                        cut(1560, x=150, z=80, twist=-30, tilt=-3, delay=110)],
                particles=[at(dust, x, delay=25 * k) for k, x in enumerate((-700, -350, 0, 350, 700))]
                + [at(glint, x, delay=40 + 30 * k) for k, x in enumerate((-500, 0, 500))])
    dash = dict(gust, name="BLOOD_DASH",
                meshes=[cut(1440), cut(1200, z=140, twist=45, tilt=4, delay=50)],
                particles=[at(wake["particles"][0], x, z=100, delay=20 * k) for k, x in enumerate((-500, 0, 500))]
                + [at(dust, x, delay=30) for x in (-400, 400)])
    import math
    fan_cuts, fan_dust = [], []
    for k, a in enumerate((-40, -20, 0, 20, 40)):
        r = math.radians(a)
        fan_cuts.append(cut(1200, x=800 * math.cos(r), y=800 * math.sin(r), twist=20.0 * (k - 2), turn=a, tilt=6,
                            delay=40 * k))
    for a in (-30, 0, 30):
        r = math.radians(a)
        fan_dust += [at(dust, d * math.cos(r), d * math.sin(r), delay=60) for d in (500, 1100)]
    fan = dict(gust, name="BLOOD_FAN", meshes=fan_cuts, particles=fan_dust)
    return {"LINE": line, "DASH": dash, "FAN": fan}


def effect_files(ours):
    """{data-relative path: bytes} of the blood kit: effects, particle files, textures, and
    the kit's meshes and mesh animations as they are."""
    files, kit = {}, {}
    for name in KIT_FX:
        kit[name] = recoloured(eft.read(kit_file(rf"3DDATA\EFFECT\KATANA_{name}.EFT")), files)
        files[rf"3DDATA\EFFECT\BLOOD_{name}.EFT"] = eft.build(dict(kit[name], name="BLOOD_" + name))
    for name, e in built_effects(kit).items():
        for m in e["meshes"]:
            for key in ("zms", "zmo"):
                files[m[key]] = kit_file(m[key])
        files[rf"3DDATA\EFFECT\BLOOD_{name}.EFT"] = eft.build(e)
    for rel, blob in files.items():
        if rel.upper().endswith(".ZMS") and rfzms.parse(blob).pool != 1:
            raise SystemExit(f"{rel} is not a pool-1 mesh: the engine asserts on it in an effect")
    return files


# ---------------------------------------------------------------------- apply
def write(ours, rel, blob, dry):
    path = mon.find_ci(ours, rel)
    if os.path.isfile(path) and open(path, "rb").read() == blob:
        return False
    if not dry:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(blob)
    return True


def apply(ours, dry):
    changed = sum(write(ours, rel, blob, dry) for rel, blob in build_clips(ours).items())
    print(f"    {'the clips':26s} {len(CLIPS)} on his skeleton, {changed} written")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    want = chr_entry(chr_)
    if chr_.chars[FERD] != want:
        chr_.chars[FERD] = want
        chr_.save(dry, keep_backup=False)
        print(f"    {'LIST_NPC.CHR':26s} entry {FERD}: slots {sorted(SLOTS)}")
    files = effect_files(ours)
    changed = sum(write(ours, rel, blob, dry) for rel, blob in files.items())
    for stale in ("RIFT_CUT_2", "RIFT_CUT_3", "SUMMONED_SWORD", "SPECTRAL_HIT", "RIFT_CUT_FINISHER", "CHARGE_FINISHER"):
        path = mon.find_ci(ours, rf"3DDATA\EFFECT\BLOOD_{stale}.EFT")
        if os.path.isfile(path) and not dry:
            os.remove(path)             # a first cut of this script wrote them
    print(f"    {'the blood kit':26s} {len(files)} files, {changed} written")
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    dirty = False
    if fe.rows < FX_FIRST + len(FX):
        fe.grow_to(FX_FIRST + len(FX))
    for k, name in enumerate(FX):
        row = FX_FIRST + k
        cells = (("BLOOD_" + name).encode(), rf"3DDATA\EFFECT\BLOOD_{name}.EFT".encode("latin-1"))
        if (fe.get(row, 0), fe.get(row, 1)) != cells:
            if fe.get(row, 0).strip() or fe.get(row, 1).strip():
                raise SystemExit(f"FILE_EFFECT row {row} is {fe.get(row, 0)!r}: not free")
            fe.set(row, 0, cells[0])
            fe.set(row, 1, cells[1])
            dirty = True
    if dirty:
        fe.save(dry, keep_backup=False)
        print(f"    {'FILE_EFFECT.STB':26s} rows {FX_FIRST}-{FX_FIRST + len(FX) - 1}")
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    dirty = False
    for row, src, name, cells, _desc in SKILLS:
        if sk.rows <= row:
            if sk.rows != row:
                raise SystemExit(f"LIST_SKILL has {sk.rows} rows: {name} ({row}) would not be the next")
            sk.grow_to(row + 1)
        cur = sk.get(row, SK.SK_NAME).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"LIST_SKILL row {row} is {cur!r}: not free")
        want = boss.skill_row(sk, row, src, name, cells)
        if sk.d[row] != want:
            sk.d[row] = want
            dirty = True
            print(f"    {'LIST_SKILL.STB':26s} row {row} {name}")
    if dirty:
        sk.save(dry, keep_backup=False)
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    added = 0
    for row, _src, name, _cells, desc in SKILLS:
        if not skl.has(boss.skill_strid(row)):
            skl.append(boss.skill_strid(row), row, name, desc)
            added += 1
    if added:
        skl.save(dry, keep_backup=False)
        print(f"    {'LIST_SKILL_S.STL':26s} +{added} keys")


def verify(ours, check):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    before = len(chr_.motions)
    check(chr_.chars[FERD] == chr_entry(chr_) and len(chr_.motions) == before, f"LIST_NPC.CHR {FERD} and its sword clips")
    check(all(os.path.isfile(mon.find_ci(ours, clip_rel(n))) for n in CLIPS), f"{len(CLIPS)} FERD_* clips")
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    check(all(fe.get(FX_FIRST + k, 0) == ("BLOOD_" + n).encode() and
              os.path.isfile(mon.find_ci(ours, rf"3DDATA\EFFECT\BLOOD_{n}.EFT")) for k, n in enumerate(FX)),
          f"FILE_EFFECT {FX_FIRST}-{FX_FIRST + len(FX) - 1} and the blood kit's effects")
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    for row, src, name, cells, _desc in SKILLS:
        check(row < sk.rows and sk.d[row] == boss.skill_row(sk, row, src, name, cells), f"LIST_SKILL {row} {name}")


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
        from PIL import Image
        exe = os.path.join(ROOT, "bin", "release", "gm-item-browser.exe")
        out = os.path.join(BUILD, "sheets")
        os.makedirs(out, exist_ok=True)
        clips = build_clips(ours)
        for slot, name in sorted(SLOTS.items()):
            n = rfzmo.read_header(clips[clip_rel(name)])[1]
            frames = []
            for k in range(6):
                png = os.path.join(out, f"{name}_{k}.png")
                subprocess.run([exe, "--preview-monster", ours, png, str(FERD), "--motion", str(slot),
                                "--time", "%.3f" % ((k + 0.5) / 6 * n / 30.0)] + sys.argv[sys.argv.index("--sheet") + 1:],
                               capture_output=True, timeout=120)
                if os.path.isfile(png):
                    frames.append(Image.open(png).convert("RGB").resize((300, 300)))
                    os.remove(png)
            if frames:
                sheet = Image.new("RGB", (300 * len(frames), 300))
                for k, im in enumerate(frames):
                    sheet.paste(im, (300 * k, 0))
                sheet.save(os.path.join(out, f"{slot:02d}_{name}.png"))
                print(os.path.join(out, f"{slot:02d}_{name}.png"))
        return 0
    for name, (stem, cut, events) in CLIPS.items():
        print("%-14s %-14s frames %-9s events %s" % (name, stem, cut or "all", events))
    for row, _src, name, cells, _d in SKILLS:
        print(row, name, "power", cells.get(SK.SK_POWER, "-"), "cast slot", cells.get(SK.SK_CAST_MOTION, "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
