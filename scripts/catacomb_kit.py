#!/usr/bin/env python3
"""What the crypt bosses' fights share on the data side (2026-11-13): SlopRose's katana
kit in another colour, player clips on a monster's skeleton, and the rows of a fight.

Imported by catacomb_ferdinando.py (the kit in blood) and catacomb_gladiomurus.py (in
gold); their docstrings are the record of each fight. Nothing is written from here.

  * **The kit** is SlopRose's iai katana set (the SlopStudio toolkit's work, in
    C:\\Users\\Thomas\\Desktop\\Rose\\sloprose\\overrides; ours to use): KATANA_*.EFT with
    their particle files, textures, slash meshes and mesh animations. `Kit(prefix,
    colour)` gives it another colour: every colour key of the kit's particle files and
    every pixel of its own textures goes through `colour(hi, lo)` (the brightest and the
    darkest channel of the original, so a tint keeps its light and its white cores), and
    the files are named <PREFIX>_* in place of KATANA_*. Meshes and their animations are
    copied as they are, under the kit's names.
  * **Held effects.** The kit's own client removes some effects itself (a sword that
    hovers, a ring that turns): a mesh with `repeat` 0, a particle sequence with `loops`
    0. Ours would never end, so `recoloured` gives each a count.
  * **Built effects** are made from the kit's slash meshes: KATANA_SLASH_<n>.ZMS opens
    to n x 3 cm along its own +X, and a mesh entry's rotation is (twist about itself,
    turn about the vertical, tilt up) in degrees. The floor layer turns an effect's +X
    down its strip or cone (client groundtelegraph.cpp, Erupt).
  * **Clips** are the player's, moved onto a monster's skeleton by EVO-REFORGED's
    retargeter (Rose_Engine/TOOLS/AnimRetarget/retarget.py; their code, used with their
    authorisation), run as a subprocess because its own `rosefmt.py` would shadow ours.
    It maps bones by name: a skeleton with the avatar's `b1_*` bones needs nothing, a
    3ds Max biped under its default names ("Bip01 L Thigh") needs a map file.
  * **Rows.** A fight has damage rows (never cast: the controller hits with them, and
    their cols 74-76 are what a player caught sees and hears) and animation rows (self
    "skills" that are only two clips and the effects their events 44 / 64 / 74 / 84 fire
    on the boss; cast speeds in multiples of 10 %, since the engine plays a clip at a
    whole number of frames a second).
"""
import copy
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import dds, eft, ptl  # noqa: E402
from rosefmt import zmo as rfzmo  # noqa: E402
from rosefmt import zms as rfzms  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_monsters as mon  # noqa: E402

SLOP = r"C:\Users\Thomas\Desktop\Rose\sloprose\overrides"
RETARGET = r"C:\Users\Thomas\Desktop\Rose\EVO-REFORGED\Rose_Engine\TOOLS\AnimRetarget\retarget.py"
SRC_SKEL = r"3DDATA\AVATAR\MALE.ZMD"                    # the kit's own (two more dummies)
SRC_CLIPS = r"3DDATA\MOTION\AVATAR"
EFFECT_STB_REL = r"3DDATA\STB\FILE_EFFECT.STB"
SKILL_STB_REL, SKILL_STL_REL = boss.SKILL_STB_REL, boss.SKILL_STL_REL
MESH_DIR = "3DDATA\\EFFECT\\EFFECTMESH\\"
SLASH_CM = 3            # KATANA_SLASH_<n>.ZMS opens to n x 3 cm, along its own +X
SLASH_HEIGHT = 110      # cm over the floor
SK = boss
SK_AFTER = 29           # what the boss does when the move ends: 2 = back to the fight it was in
DAMAGE_SRC, ANIM_SRC = boss.BURST, 3585
NO_CAST = {SK.SK_RANGE: "", SK.SK_CAST_MOTION: "", SK.SK_CAST_SPEED: "", SK.SK_ACT_MOTION: "", SK.SK_ACT_SPEED: "",
           56: "", 57: "", 58: "", 62: "", 63: "", 64: "", SK.SK_TELEGRAPH: ""}
NO_EFFECT = {SK.SK_STATUS: "", SK.SK_SUCCESS: "", SK.SK_DURATION: "", 21: "", 22: "", 23: "", 24: "", 25: "", 26: "",
             74: "", 75: "", 76: ""}


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def kit_file(rel):
    return open(mon.find_ci(SLOP, rel), "rb").read()


def is_kit(path):
    return os.path.basename(path.replace("\\", "/")).upper().startswith("KATANA_")


# ---------------------------------------------------------------------- rows
def damage(power, hit_effect, hit_sound):
    """A damage row: what a player caught by the floor takes, sees and hears."""
    return {**NO_CAST, SK.SK_TYPE: 17, SK.SK_SCOPE: 400, SK.SK_POWER: power, 74: hit_effect, 75: 999, 76: hit_sound}


def anim(slot, speed, e44=None, e64=None, e74=None, e84=None):
    """An animation row: cast on `slot` at `speed` %, released on slot + 1, with the effects
    its clips' events 44 / 64 / 74 / 84 fire on the boss (FILE_EFFECT rows; cols 56 / 59 /
    62 / 65). On the boss means at its size."""
    cells = {**NO_EFFECT, SK.SK_CAST_MOTION: slot, SK.SK_CAST_SPEED: speed, SK.SK_ACT_MOTION: slot + 1,
             SK.SK_ACT_SPEED: 100, SK_AFTER: 2}
    for col, effect in ((56, e44), (59, e64), (62, e74), (65, e84)):
        cells[col], cells[col + 1], cells[col + 2] = (effect or ""), (999 if effect else ""), ""
    return cells


def write(ours, rel, blob, dry):
    path = mon.find_ci(ours, rel)
    if os.path.isfile(path) and open(path, "rb").read() == blob:
        return False
    if not dry:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(blob)
    return True


def apply_effect_rows(ours, first, names, prefix, dry):
    """FILE_EFFECT rows first.. = <PREFIX>_<name> -> 3DDATA\\EFFECT\\<PREFIX>_<name>.EFT."""
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    dirty = False
    if fe.rows < first + len(names):
        fe.grow_to(first + len(names))
    for k, name in enumerate(names):
        row = first + k
        cells = ((prefix + "_" + name).encode(), rf"3DDATA\EFFECT\{prefix}_{name}.EFT".encode("latin-1"))
        if (fe.get(row, 0), fe.get(row, 1)) != cells:
            if fe.get(row, 0).strip() or fe.get(row, 1).strip():
                raise SystemExit(f"FILE_EFFECT row {row} is {fe.get(row, 0)!r}: not free")
            fe.set(row, 0, cells[0])
            fe.set(row, 1, cells[1])
            dirty = True
    if dirty:
        fe.save(dry, keep_backup=False)
        print(f"    {'FILE_EFFECT.STB':26s} rows {first}-{first + len(names) - 1}")


def verify_effect_rows(ours, first, names, prefix, check):
    fe = oro.Stb(P(ours, EFFECT_STB_REL))
    check(all(fe.get(first + k, 0) == (prefix + "_" + n).encode() and
              os.path.isfile(mon.find_ci(ours, rf"3DDATA\EFFECT\{prefix}_{n}.EFT")) for k, n in enumerate(names)),
          f"FILE_EFFECT {first}-{first + len(names) - 1} and the {prefix.lower()} kit's effects")


def apply_skill_rows(ours, skills, dry):
    """(row, source row, name, cells, description) rows of LIST_SKILL, appended in order."""
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    dirty = False
    for row, src, name, cells, _desc in skills:
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
    for row, _src, name, _cells, desc in skills:
        if not skl.has(boss.skill_strid(row)):
            skl.append(boss.skill_strid(row), row, name, desc)
            added += 1
    if added:
        skl.save(dry, keep_backup=False)
        print(f"    {'LIST_SKILL_S.STL':26s} +{added} keys")


def verify_skill_rows(ours, skills, check):
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    for row, src, name, cells, _desc in skills:
        check(row < sk.rows and sk.d[row] == boss.skill_row(sk, row, src, name, cells), f"LIST_SKILL {row} {name}")


# ---------------------------------------------------------------------- clips
def retargeted(stem, dst_skel, fill, build_dir, map_file=None, cache={}):
    """The kit's (or SlopRose's) player clip `stem` on a monster's skeleton (bytes), in
    place (no walking off), built once per run. `dst_skel` and `fill` (the monster's own
    clip for the bones the player does not have) are files."""
    key = (stem, dst_skel, map_file)
    if key not in cache:
        if not os.path.isfile(RETARGET):
            raise SystemExit(f"the retargeter is not at {RETARGET}")
        os.makedirs(build_dir, exist_ok=True)
        out = os.path.join(build_dir, stem + ".ZMO")
        src = mon.find_ci(SLOP, SRC_CLIPS + "\\" + stem + "_M1.ZMO")
        if not os.path.isfile(src):
            raise SystemExit(f"the kit has no {stem}_M1.ZMO under {SLOP}")
        cmd = [sys.executable, "-I", RETARGET, "--src-skel", mon.find_ci(SLOP, SRC_SKEL), "--src", src,
               "--dst-skel", dst_skel, "--fill", fill, "--in-place", "--out", out, "--quiet"]
        if map_file:
            cmd += ["--map", map_file]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if r.returncode or not os.path.isfile(out):
            raise SystemExit("the retargeter failed on %s: %s" % (stem, (r.stderr or r.stdout).strip()[-400:]))
        cache[key] = open(out, "rb").read()
    return cache[key]


def cut_clip(blob, cut, events):
    """Frames [cut[0], cut[1]) of a clip (all of it for None) with only `events`
    ({frame: event}) on them: a source's own events mean something to its own client."""
    blob = rfzmo.with_trailer(blob)     # a monster's own casting clip may ship without one
    if cut:
        blob = rfzmo.sliced(blob, cut[0], cut[1])
    return boss.cleared(blob, events)


def chr_entry(chr_, npc, slot_clips):
    """The monster's CHR entry with `slot_clips` ({slot: data-relative clip}) on its slots."""
    e = copy.deepcopy(chr_.chars[npc])
    anims = dict(e["anims"])
    for slot, rel in slot_clips.items():
        anims[slot] = boss.motion_index(chr_, rel)
    e["anims"] = sorted(anims.items())
    return e


def sheets(ours, npc, slot_clips, clips, out_dir, extra=(), cols=6):
    """A contact sheet per slot of `npc` as the GM item browser draws it (`clips`: the
    bytes by data-relative path, for their lengths)."""
    from PIL import Image
    exe = os.path.join(ROOT, "bin", "release", "gm-item-browser.exe")
    os.makedirs(out_dir, exist_ok=True)
    for slot, rel in sorted(slot_clips.items()):
        n = rfzmo.read_header(clips[rel])[1]
        stem = os.path.splitext(os.path.basename(rel.replace("\\", "/")))[0]
        frames = []
        for k in range(cols):
            png = os.path.join(out_dir, f"{stem}_{k}.png")
            subprocess.run([exe, "--preview-monster", ours, png, str(npc), "--motion", str(slot),
                            "--time", "%.3f" % ((k + 0.5) / cols * n / 30.0)] + list(extra),
                           capture_output=True, timeout=120)
            if os.path.isfile(png):
                frames.append(Image.open(png).convert("RGB").resize((300, 300)))
                os.remove(png)
        if frames:
            sheet = Image.new("RGB", (300 * len(frames), 300))
            for k, im in enumerate(frames):
                sheet.paste(im, (300 * k, 0))
            sheet.save(os.path.join(out_dir, f"{slot:02d}_{stem}.png"))
            print(os.path.join(out_dir, f"{slot:02d}_{stem}.png"))


# ---------------------------------------------------------------------- the kit in a colour
class Kit:
    """SlopRose's katana kit as <PREFIX>_*, every colour through `colour(hi, lo)` -> (r, g,
    b): hi and lo are the brightest and darkest channel of the original, numbers or numpy
    arrays alike. `held` = (particle loops, mesh turn loops, mesh repeats) given to what the
    kit's own client would have removed."""

    def __init__(self, prefix, colour, held=(2, 3, 2)):
        self.prefix, self.colour, self.held = prefix, colour, held

    def name(self, path):
        """KATANA_X.ext -> <PREFIX>_X.ext for the kit's own files, others unchanged."""
        d, f = os.path.split(path.replace("/", "\\"))
        return (d + "\\" if d else "") + (self.prefix + "_" + f[7:] if f.upper().startswith("KATANA_") else f)

    def tint(self, r, g, b):
        return self.colour(max(r, g, b), min(r, g, b))

    def texture(self, path):
        """A kit texture in the colour (the kit ships them uncompressed; the top level only)."""
        import numpy as np
        from PIL import Image
        a = np.asarray(dds.read_bgra(path).convert("RGBA"), dtype=np.float32)
        r, g, b = self.colour(a[..., :3].max(axis=2), a[..., :3].min(axis=2))
        out = np.stack([r, g, b, a[..., 3]], axis=2)
        return dds.bgra_bytes(Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA"))

    def recoloured(self, c, files):
        """A kit effect (rosefmt.eft's shape) with its particle files and textures in the
        colour; everything it needs lands in `files` ({data-relative path: bytes})."""
        loops, turns, repeats = self.held
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
                            v[0:3] = self.tint(*v[0:3])
                            v[4:7] = self.tint(*v[4:7])
                            ev.values = v
                    if sq.loops <= 0:
                        sq.loops = loops        # a held emitter: here it ends by itself
                    tex = sq.texture.decode("latin-1")
                    if is_kit(tex):
                        files[self.name(tex)] = self.texture(mon.find_ci(SLOP, tex))
                        sq.texture = self.name(tex).encode("latin-1")
                files[self.name(src)] = ptl.build(seqs, tail)
                q["ptl"] = self.name(src)
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
                    m["ani_loops"] = turns
            if m["repeat"] <= 0:
                m["repeat"] = repeats
            if is_kit(m["texture"]):
                files[self.name(m["texture"])] = self.texture(mon.find_ci(SLOP, m["texture"]))
                m["texture"] = self.name(m["texture"])
            meshes.append(m)
        return dict(c, particles=parts, meshes=meshes)

    def effect(self, name, files):
        """The kit's KATANA_<name>.EFT in the colour, written into `files`; the effect back."""
        e = self.recoloured(eft.read(kit_file(rf"3DDATA\EFFECT\KATANA_{name}.EFT")), files)
        files[rf"3DDATA\EFFECT\{self.prefix}_{name}.EFT"] = eft.build(dict(e, name=self.prefix + "_" + name))
        return e

    def built(self, name, effect, files):
        """An effect built here, with the kit meshes it names, written into `files`."""
        for m in effect["meshes"]:
            for key in ("zms", "zmo"):
                if m[key] not in files and m[key] and m[key] != "NULL":
                    files[m[key]] = kit_file(m[key])
        files[rf"3DDATA\EFFECT\{self.prefix}_{name}.EFT"] = eft.build(dict(effect, name=self.prefix + "_" + name))


def cut(slash, length_cm, x=0.0, y=0.0, z=SLASH_HEIGHT, twist=0.0, turn=0.0, tilt=0.0, delay=0):
    """A slash `length_cm` long as a mesh entry, from a kit effect's own slash entry."""
    stem = MESH_DIR + "KATANA_SLASH_%d" % (length_cm // SLASH_CM)
    return dict(slash, zms=stem + ".ZMS", zmo=stem + ".ZMO", pos=(float(x), float(y), float(z)),
                rot=(float(twist), float(turn), float(tilt), 0.0), delay=delay, repeat=1)


def at(q, x, y=0.0, z=None, delay=0):
    """An entry somewhere else, a while later."""
    return dict(q, pos=(float(x), float(y), q["pos"][2] if z is None else float(z)), delay=delay)


def gust_parts(gust):
    """(slash mesh entry, dust particle entry, glint particle entry) of the kit's GALE_GUST."""
    dust = [q for q in gust["particles"] if "GLINT" not in q["ptl"].upper()][0]
    glint = [q for q in gust["particles"] if "GLINT" in q["ptl"].upper()][0]
    return gust["meshes"][0], dust, glint


def line_effect(gust):
    """A 20 m cut and two shorter ones along +X, for a strip."""
    slash, dust, glint = gust_parts(gust)
    return dict(gust,
                meshes=[cut(slash, 1980), cut(slash, 1560, x=-150, z=150, twist=40, tilt=3, delay=60),
                        cut(slash, 1560, x=150, z=80, twist=-30, tilt=-3, delay=110)],
                particles=[at(dust, x, delay=25 * k) for k, x in enumerate((-700, -350, 0, 350, 700))]
                + [at(glint, x, delay=40 + 30 * k) for k, x in enumerate((-500, 0, 500))])


def fan_effect(gust, angles=(-40, -20, 0, 20, 40), reach=800, length=1200, dust_angles=(-30, 0, 30)):
    """Cuts fanned from the origin about +X, for a cone."""
    slash, dust, _ = gust_parts(gust)
    mid = (len(angles) - 1) / 2.0
    cuts, puffs = [], []
    for k, a in enumerate(angles):
        r = math.radians(a)
        cuts.append(cut(slash, length, x=reach * math.cos(r), y=reach * math.sin(r), twist=20.0 * (k - mid), turn=a,
                        tilt=6, delay=40 * k))
    for a in dust_angles:
        r = math.radians(a)
        puffs += [at(dust, d * math.cos(r), d * math.sin(r), delay=60) for d in (reach - 300, reach + 300)]
    return dict(gust, meshes=cuts, particles=puffs)


def check_meshes(files):
    for rel, blob in files.items():
        if rel.upper().endswith(".ZMS") and rfzms.parse(blob).pool != 1:
            raise SystemExit(f"{rel} is not a pool-1 mesh: the engine asserts on it in an effect")
