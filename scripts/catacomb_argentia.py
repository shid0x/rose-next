#!/usr/bin/env python3
"""Argentia, the Mad Giant: the data of the first floor fight (2026-11-12).

The body is `catacomb_pool.py`'s (LIST_NPC 2558); the fight is the controller's
(src/sho_gameserver/src/catacombs.cpp, FightArgentia / ArgentiaMechanic / ProcFloor:
**the constants there and here must agree**). Applied by `import-catacombs.py`.

Argentia never walks. It stands in the middle of the far room, and what it does is
drawn on the floor by the controller (GSV_ZONE_WARN) and hits whoever still stands there
when the time is up -- not a monster's cast, so none of these rows is ever cast for its
effect:

  * **Four damage rows** (LIST_SKILL 7207-7210): Shattered Floor (a tile of the room
    erupting), Grasp of the Mad God (an arm coming down, a cone), Howl of Madness (the
    ring) and Seed of Madness (the circle under a player). The controller computes each
    hit with the server's own skill formula on one of them
    (CCal::Get_SkillDAMAGE(boss, player, row)) and sends it as that skill, so the level
    gate, magic resistance and the damage meter's names all work as for a cast. Copies of
    Grave Burst's row (magic damage, no status) with the cast columns cleared.
    Powers: sized like the Necromancer's two (catacomb_boss.SIZING: about 400 is a third
    of a light build's HP at Argentia's attack) and to be tuned by playing.
  * **Two animation rows** (7211, 7212): self "skills" with no effect at all, only the
    clips and their casting effects, ordered so the giant moves while the floor is
    drawn. The howl is its four-second channel (cast slot 8: casting2, whose own events
    fire casting effects 0-2 two seconds in); the slam is slot 6 / 7, whose arms land 55
    frames into the release, 2.53 s after the cast starts -- the cones are drawn for
    exactly that long. Copies of a monster self-buff row with the status cleared.
  * **The tile eruption** (3DDATA\\EFFECT\\KCAT_TILE_ERUPT.eft, FILE_EFFECT 2902): the
    rock cluster of Earthsplitter (EARTHSPLIT_ROCKS.eft, SlopStudio's toolkit) nine times
    over a 15 m tile, rippling out from its middle, with dust on five of them. The client
    plays it on the middle of each tile as it fires.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import eft  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_monsters as mon  # noqa: E402

SKILL_STB_REL, SKILL_STL_REL = boss.SKILL_STB_REL, boss.SKILL_STL_REL
EFFECT_STB_REL = r"3DDATA\STB\FILE_EFFECT.STB"
TRACKED = [EFFECT_STB_REL]

SK = boss
DAMAGE_SRC = boss.BURST             # 7205 Grave Burst: type 17, magic, no status
ANIM_SRC = 3585                     # a monster's self buff: type 8, cast at itself
FX_CAST, FX_SHOCKWAVE = boss.FX_CAST, boss.FX_SHOCKWAVE
SND_CAST, SND_SKILL_ATTACK = boss.SND_CAST, boss.SND_SKILL_ATTACK

# what a damage row does not keep from Grave Burst: it is never cast
NO_CAST = {SK.SK_RANGE: "", SK.SK_CAST_MOTION: "", SK.SK_CAST_SPEED: "", SK.SK_ACT_MOTION: "", SK.SK_ACT_SPEED: "",
           56: "", 57: "", 58: "", 62: "", 63: "", 64: "", 74: "", 75: "", 76: "", SK.SK_TELEGRAPH: ""}
# what an animation row does not keep from the self buff: no status, no ability
NO_EFFECT = {SK.SK_STATUS: "", SK.SK_SUCCESS: "", SK.SK_DURATION: "", 21: "", 22: "", 23: "", 24: "", 25: "", 26: "",
             74: "", 75: "", 76: ""}


FX_STRIKE_HIT, FX_DARK_BURST = 370, boss.FX_DARK_BURST
SND_HEAVY, SND_HEAVY_ATT, SND_MAGIC_HIT, SND_HIT = 1551, 147, 93, boss.SND_HIT


def damage(power, scope, hit_effect, hit_sound):
    """A damage row: never cast, but its hit effect and hit sound (cols 74-76) are what a
    player sees and hears when the floor catches him (CObjCHAR::Hit_WithSKILL sends the
    hit as this skill's result; the client shows the sound only with an effect)."""
    return {**NO_CAST, SK.SK_TYPE: 17, SK.SK_SCOPE: scope, SK.SK_POWER: power,
            74: hit_effect, 75: 999, 76: hit_sound}


SKILLS = (  # row, source row, name, cells, description
    (7207, DAMAGE_SRC, "Shattered Floor", damage(420, 750, FX_STRIKE_HIT, SND_HEAVY),
     "The floor of the crypt breaks open under the Mad Giant's will. Leave the marked tiles."),
    (7208, DAMAGE_SRC, "Grasp of the Mad God", damage(480, 2800, FX_STRIKE_HIT, SND_HEAVY_ATT),
     "Three of Argentia's arms come down at once. Stand between them."),
    (7209, DAMAGE_SRC, "Howl of Madness", damage(380, 1400, FX_DARK_BURST, SND_MAGIC_HIT),
     "A howl that breaks first what is near, then what is far."),
    (7210, DAMAGE_SRC, "Seed of Madness", damage(300, 450, FX_STRIKE_HIT, SND_HIT),
     "Something takes root where you stand. Do not be there when it opens."),
    (7211, ANIM_SRC, "Argentia howls", {**NO_EFFECT, **{
        SK.SK_CAST_MOTION: 8, SK.SK_CAST_SPEED: 100, SK.SK_ACT_MOTION: 9, SK.SK_ACT_SPEED: 100,
        56: FX_CAST, 57: 999, 58: SND_CAST,                 # casting effect 0: event 44, two seconds in
        59: "", 60: "", 61: "",
        62: FX_SHOCKWAVE, 63: 999, 64: SND_SKILL_ATTACK}},  # casting effect 2: event 74
     "The Mad Giant's howl."),
    (7212, ANIM_SRC, "Argentia strikes", {**NO_EFFECT, **{
        SK.SK_CAST_MOTION: 6, SK.SK_CAST_SPEED: 100, SK.SK_ACT_MOTION: 7, SK.SK_ACT_SPEED: 100,
        56: "", 57: "", 58: "", 59: "", 60: "", 61: "",
        62: FX_SHOCKWAVE, 63: 999, 64: SND_SKILL_ATTACK}},  # the arms land: event 74 of the release
     "The Mad Giant's arms come down."),
)

# ---- the tile eruption
TILE_EFT = r"3DDATA\EFFECT\KCAT_TILE_ERUPT.eft"
TILE_FX_ROW = 2902                  # blank in FILE_EFFECT
TILE_FX_NAME = "KCAT_TILE_ERUPT"
ROCKS_EFT = r"3DDATA\EFFECT\EARTHSPLIT_ROCKS.eft"
TILE_SPACING_CM = 450.0             # 3 x 3 clusters over the 15 m tile
TILE_RIPPLE_MS = 90                 # per ring out from the middle
# deterministic "scatter": the same file on every run
JITTER = ((0, 0), (60, -40), (-70, 30), (40, 80), (-30, -90), (90, 20), (-80, -60), (20, 100), (-50, 70))


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def tile_effect(ours):
    c = eft.read(open(mon.find_ci(ours, ROCKS_EFT), "rb").read())
    parts, meshes = [], []
    k = 0
    for gy in (-1, 0, 1):
        for gx in (-1, 0, 1):
            jx, jy = JITTER[k]
            cx, cy = gx * TILE_SPACING_CM + jx, gy * TILE_SPACING_CM + jy
            turn = 180.0 * (k % 2)
            delay = TILE_RIPPLE_MS * max(abs(gx), abs(gy)) + 15 * (k % 3)
            a = math.radians(turn)
            ca, sa = math.cos(a), math.sin(a)

            def place(e):
                e = dict(e)
                x, y, z = e["pos"]
                e["pos"] = (cx + ca * x - sa * y, cy + sa * x + ca * y, z)
                r = list(e["rot"])
                r[1] = (r[1] + turn) % 360.0
                e["rot"] = tuple(r)
                e["delay"] = e["delay"] + delay
                return e
            meshes += [place(m) for m in c["meshes"]]
            if gx * gy != 0 or (gx == 0 and gy == 0):       # dust on the middle and the corners
                parts += [place(p) for p in c["particles"]]
            k += 1
    return eft.build(dict(c, name=TILE_FX_NAME, particles=parts, meshes=meshes))


def apply(ours, dry):
    # the skills
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
            print(f"    {'LIST_SKILL.STB':26s} row {row} {name} (from {src})")
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
    # the effect
    blob = tile_effect(ours)
    path = mon.find_ci(ours, TILE_EFT)
    changed = not (os.path.isfile(path) and open(path, "rb").read() == blob)
    if changed and not dry:
        with open(path, "wb") as fh:
            fh.write(blob)
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    cells = (TILE_FX_NAME.encode(), TILE_EFT.encode("latin-1"))
    if (fe.get(TILE_FX_ROW, 0), fe.get(TILE_FX_ROW, 1)) != cells:
        if fe.get(TILE_FX_ROW, 0).strip() or fe.get(TILE_FX_ROW, 1).strip():
            raise SystemExit(f"FILE_EFFECT row {TILE_FX_ROW} is {fe.get(TILE_FX_ROW, 0)!r}: not free")
        fe.set(TILE_FX_ROW, 0, cells[0])
        fe.set(TILE_FX_ROW, 1, cells[1])
        fe.save(dry, keep_backup=False)
        print(f"    {'FILE_EFFECT.STB':26s} row {TILE_FX_ROW} = {TILE_FX_NAME}")
    print(f"    {os.path.basename(TILE_EFT):26s} {'written' if changed else 'in place'} ({len(blob)} bytes)")
    if not dirty and not added and not changed:
        print(f"    {'Argentia':26s} already in place")


def verify(ours, check):
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    for row, src, name, cells, _desc in SKILLS:
        check(row < sk.rows and sk.d[row] == boss.skill_row(sk, row, src, name, cells), f"LIST_SKILL {row} {name}")
        check(skl.has(boss.skill_strid(row)), f"LIST_SKILL_S {boss.skill_strid(row)}")
    path = mon.find_ci(ours, TILE_EFT)
    check(os.path.isfile(path) and open(path, "rb").read() == tile_effect(ours)
          and fe.get(TILE_FX_ROW, 1) == TILE_EFT.encode("latin-1"), f"FILE_EFFECT {TILE_FX_ROW} {TILE_FX_NAME}")


def remove_files(ours):
    path = mon.find_ci(ours, TILE_EFT)
    if os.path.isfile(path):
        os.remove(path)
        print(f"    removed {os.path.basename(TILE_EFT)}")


if __name__ == "__main__":
    for row, src, name, cells, _desc in SKILLS:
        print("%d %-22s from %d, power %s" % (row, name, src, cells.get(SK.SK_POWER, "-")))
