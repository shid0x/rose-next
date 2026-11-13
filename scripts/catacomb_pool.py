#!/usr/bin/env python3
"""The Karkia catacombs' boss pool: four bodies from the Jrose dump (2026-11-12).

The user's picks from 96 previews of Jrose's level 180+ monsters (rendered with
`gm-item-browser --preview-monster`, sheets in build/catacombs/boss-candidates): the
far room's boss is meant to be what draws players into the crypt, and both crypts
share one pool. Applied by `import-catacombs.py`; this file is the plan.

    python scripts/catacomb_pool.py                # the plan

| ours | Jrose | name                       | the body                                        |
|------|-------|----------------------------|-------------------------------------------------|
| 2557 | 2049  | Ferdinando, the Eternal    | a top-hat vampire lord with a red katana        |
| 2558 | 2044  | Argentia, the Mad Giant    | a black and red many-armed giant; it never walks |
| 2559 | 2487  | Gladiomurus                | a red, antlered demon knight with a greatsword  |
| 2560 | 2035  | Ikaness Greater Assassin   | dark armour, a black trident-sword, a halo      |

What comes across, and what does not
------------------------------------
  * **The model** through rosedata.import_characters: the LIST_NPC.CHR entry with every
    index remapped, PART_NPC models, and the files we lack. Ferdinando (the Brinto rig)
    and the Assassin (the Ikaness soldier's rig) ride skeletons and clips we already
    ship and bring only textures and a head; Argentia and Gladiomurus are whole new
    rigs with their ten and nine clips.
  * **Weapons.** Ferdinando holds a **red katana of his own** (row 26, 2026-11-13; the
    user: "red katana for Ferdinando, how do you suggest to do it? browsing client to
    find one?"). No dump has a better one than what we hold: Jrose's Ferdinando carries
    its Muramasa (LIST_WEAPON 1098), **the same mesh as our Murasamemaru (1462)** with a
    pale blade, red runes and a red grip, and ours is that mesh in black stone with teal
    cracks. So the red one is ours recoloured -- the teal glow turned blood red, the
    colour of his effects (`blood_texture`) -- on a monster-only row, and the players'
    Murasamemaru (teal, his drop) is untouched. He held 1462 itself until then. Gladiomurus'
    greatsword (Jrose LIST_WEAPON 1196, a new mesh) and the Assassin's black Death
    Expeler (4033, our osw41 mesh with a new texture) come in through import-item.py
    --art-only at rows 29 and 30, every stat cloned from our Death Expeler (21).
  * **No stat, no AI, no skill of Jrose's.** Rows are a copy of the Crypt Necromancer's
    (2556: voice, death effect, skull) with each body's own numbers in
    BODIES; all are level 240 and start from the Necromancer's sizing
    (catacomb_boss.SIZING), to be tuned fight by fight. They share its AI file (find a
    target, answer a hit, leash): **the controller runs a crypt boss's fight**, and
    each of these gets its own script there.

After it: add-dds-mipmaps.py over the new texture folders and fix-zms-bone-slots.py
(import-catacombs.py runs both), then bake and restart.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_monsters as mon  # noqa: E402

JROSE = r"C:\Users\Thomas\Desktop\Testclients\Jrose"
NPC_STB_REL, NPC_STL_REL, NPC_CHR_REL = mon.NPC_STB_REL, mon.NPC_STL_REL, mon.NPC_CHR_REL
WEAPON_STB_REL = r"3DDATA\STB\LIST_WEAPON.STB"
WEAPON_ZSC_REL = r"3DDATA\WEAPON\LIST_WEAPON.ZSC"
WEAPON_STL_REL = r"3DDATA\STB\LIST_WEAPON_S.STL"
PART_ZSC_REL = r"3DDATA\NPC\PART_NPC.ZSC"
TRACKED = [WEAPON_STB_REL, WEAPON_ZSC_REL, WEAPON_STL_REL, PART_ZSC_REL]

C_NAME, C_MODEL, C_WALK, C_RUN, C_SIZE, C_RWEAPON, C_LWEAPON, C_LEVEL, C_HP, C_ATK, C_HIT, \
    C_DEF, C_RES, C_AVOID, C_ASPD, C_MAGIC, C_AI = range(17)
C_TARGET, C_RANGE, C_GLOW, C_DEADEVENT, C_HEIGHT = 25, 26, 39, 41, 42
C_HANDHIT = 33

# Ferdinando's katana: object KATANA_LOOK's mesh with our texture recoloured, row
# KATANA_COLUMNS' cells (the Black Death Expeler: a monster's one-handed sword).
KATANA, KATANA_LOOK, KATANA_COLUMNS = 26, 1462, 30
KATANA_NAME, KATANA_KEY = "Ferdinando's Katana", "LWEA26"
KATANA_DESC = "A cursed blade split by veins of blood, carried by Ferdinando, the Eternal."
KATANA_SRC_TEX = r"3DDATA\WEAPON\WEAPON\dsw\muramasa\youtoumurasamemaru.dds"
KATANA_TEX = r"3Ddata\WEAPON\WEAPON\dsw\muramasa\muramasa_blood.dds"
C_DROP = 18                         # the boss's own drop table (add-karkia-drops.py CRYPT_BOSSES)

TEMPLATE = boss.BOSS                # 2556, the Crypt Necromancer's row
WEAPON_TEMPLATE = 21                # Death Expeler: a one-handed sword's columns
WEAPON_ICON = 1                     # a monster's weapon: never seen in a bag

FERDINANDO, ARGENTIA, GLADIOMURUS, ASSASSIN = 2557, 2558, 2559, 2560
# Each boss pays one class line of the lv240 gear (2026-11-13): the Necromancer keeps
# 913. add-karkia-drops.py writes the tables and names the same rows (NPC_TABLE_FIX).
DROP_TABLE = {FERDINANDO: 914, ARGENTIA: 915, GLADIOMURUS: 916, ASSASSIN: 917}
# our weapon row -> (Jrose row, name, description)
WEAPONS = {
    29: (1196, "Gladiomurus' Greatblade", "The blade of the demon knight Gladiomurus."),
    30: (4033, "Black Death Expeler", "A Death Expeler gone black, carried by the Greater Assassin."),
}
# our row -> the body. `cells` are what differs from the Crypt Necromancer's row.
BODIES = {
    FERDINANDO: dict(src=2049, name="Ferdinando, the Eternal", cells={
        C_WALK: 300, C_RUN: 700, C_SIZE: 220, C_RWEAPON: KATANA, C_HEIGHT: 400,
        C_HP: 3000, C_ATK: 2700, C_HIT: 1300, C_DEF: 900, C_RES: 950, C_AVOID: 500, C_ASPD: 115,
        C_MAGIC: "", C_RANGE: 250}),
    # Jrose's walk and run are 0: it stands where it is raised and the fight is about the floor
    ARGENTIA: dict(src=2044, name="Argentia, the Mad Giant", cells={
        C_WALK: 0, C_RUN: 0, C_SIZE: 600, C_RWEAPON: "", C_HEIGHT: 900,
        C_HP: 4000, C_ATK: 2600, C_HIT: 1250, C_DEF: 1050, C_RES: 900, C_AVOID: 200, C_ASPD: 85,
        C_MAGIC: "", C_RANGE: 300, C_HANDHIT: 403}),
    # its swing lands twice (the clip has two attack frames): the attack is sized for that
    GLADIOMURUS: dict(src=2487, name="Gladiomurus", cells={
        C_WALK: 340, C_RUN: 650, C_SIZE: 330, C_RWEAPON: 29, C_HEIGHT: 600,
        C_HP: 3300, C_ATK: 1700, C_HIT: 1300, C_DEF: 1000, C_RES: 850, C_AVOID: 400, C_ASPD: 100,
        C_MAGIC: "", C_RANGE: 300, C_GLOW: "110110110"}),
    ASSASSIN: dict(src=2035, name="Ikaness Greater Assassin", cells={
        C_WALK: 400, C_RUN: 900, C_SIZE: 380, C_RWEAPON: 30, C_HEIGHT: 650,
        C_HP: 2600, C_ATK: 2400, C_HIT: 1400, C_DEF: 850, C_RES: 900, C_AVOID: 650, C_ASPD: 130,
        C_MAGIC: "", C_RANGE: 250}),
}
# the folders that received new textures (add-dds-mipmaps.py --subdir)
TEXTURE_DIRS = (r"3DDATA\NPC\animal\Brinto1", r"3DDATA\NPC\npc\elc_boss01", r"3DDATA\NPC\npc\elc_boss02",
                r"3DDATA\NPC\npc\Gob_girl", r"3DDATA\NPC\etc\Icanes_G")


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def b(v):
    return str(v).encode("latin-1")


def strid(row):
    return "LCATMOB%d" % row


def body_row(n, s_npc, row):
    body = BODIES[row]
    out = list(n.d[TEMPLATE])
    out[C_NAME] = body["name"].encode("latin-1")
    out[C_MODEL] = s_npc.get(body["src"], C_MODEL)          # /mon refuses a row with no model text
    for c, v in body["cells"].items():
        out[c] = b(v)
    out[C_DROP] = b(DROP_TABLE[row])
    for c in (C_TARGET, C_DEADEVENT, *oro.NPC_SELL_TAB_COLS):
        out[c] = b""
    out[oro.NPC_STRID_COL] = strid(row).encode()
    out[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    return out


def weapons(ours, src, dry):
    w = oro.Stb(P(ours, WEAPON_STB_REL))
    for row, (srow, name, desc) in sorted(WEAPONS.items()):
        cur = w.get(row, 0).decode("latin-1").strip()
        if cur == name:
            print(f"    {'LIST_WEAPON.STB':26s} row {row} {name} already in place")
            continue
        if cur:
            raise SystemExit(f"LIST_WEAPON row {row} is {cur!r}: not free")
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"), "--type", "weapon",
               "--source", src, "--source-row", str(srow), "--art-only",
               "--template-row", str(WEAPON_TEMPLATE), "--name", name, "--desc", desc,
               "--icon", str(WEAPON_ICON), "--target-row", str(row)]
        if dry:
            cmd.append("--dry-run")
        print(f"    {'import-item.py':26s} weapon {srow} -> {row} {name}")
        res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        for line in (res.stdout + res.stderr).strip().splitlines()[-6:]:
            print("        " + line)
        if res.returncode:
            raise SystemExit("import-item.py failed")


# ------------------------------------------------------------- Ferdinando's red katana
def blood_texture(ours):
    """Our Murasamemaru texture with its teal glow turned blood red, as an uncompressed
    BGRA DDS with a full mip chain (each channel resized by itself: an RGBA resize
    premultiplies and darkens the levels)."""
    import colorsys
    from PIL import Image
    from rosefmt import dds as rfdds
    img = Image.open(mon.find_ci(ours, KATANA_SRC_TEX)).convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, bl, a = px[x, y]
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, bl / 255)
            if 0.40 <= h <= 0.62 and s > 0.18:                # the glow and its halo; the steel is grey
                r2, g2, b2 = colorsys.hsv_to_rgb((0.995 - (h - 0.5) * 0.15) % 1.0, min(1.0, s * 1.15), v)
                px[x, y] = (int(r2 * 255), int(g2 * 255), int(b2 * 255), a)
    levels, w, h = [], img.width, img.height
    while True:
        lvl = img if (w, h) == img.size else Image.merge(
            "RGBA", [c.resize((w, h), Image.BOX) for c in img.split()])
        levels.append(rfdds.bgra_bytes(lvl)[len(rfdds.build_header(rfdds.bgra_header(w, h))):])
        if w == 1 and h == 1:
            break
        w, h = max(1, w // 2), max(1, h // 2)
    return rfdds.build_header(rfdds.bgra_header(img.width, img.height, len(levels))) + b"".join(levels)


def blood_katana(ours, dry):
    """LIST_WEAPON row KATANA: the Murasamemaru's object with the red texture, the
    Black Death Expeler's columns (a monster's weapon: never in a bag)."""
    from rosefmt import zsc as rfzsc
    tex = blood_texture(ours)
    tex_path = os.path.join(os.path.dirname(mon.find_ci(ours, KATANA_SRC_TEX)), os.path.basename(KATANA_TEX))
    if not os.path.isfile(tex_path) or open(tex_path, "rb").read() != tex:
        if not dry:
            with open(tex_path, "wb") as fh:
                fh.write(tex)
        print(f"    {'muramasa_blood.dds':26s} written ({len(tex)} bytes)")
    z = rfzsc.Zsc(P(ours, WEAPON_ZSC_REL))
    names = [p.lower() for p, _f in z.materials]
    if KATANA_TEX.lower().encode() not in names:
        cyl, parts, dummies, bb = z.objects[KATANA_LOOK]
        if z.objects[KATANA][1]:
            raise SystemExit(f"LIST_WEAPON.ZSC object {KATANA} is not empty")
        (mesh, mat, props), = parts
        src_path, flags = z.materials[mat]
        if os.path.basename(src_path.decode("latin-1").replace("\\", "/")).lower() != os.path.basename(KATANA_SRC_TEX).lower():
            raise SystemExit(f"object {KATANA_LOOK}'s texture is {src_path!r}: not the Murasamemaru's")
        z.objects[KATANA] = (cyl, [(mesh, len(z.materials), props)], dummies, bb)
        blob = z.rebuild_with(extra_mats=[(KATANA_TEX.encode(), flags)])
        if not dry:
            with open(P(ours, WEAPON_ZSC_REL), "wb") as fh:
                fh.write(blob)
        print(f"    {'LIST_WEAPON.ZSC':26s} object {KATANA} = object {KATANA_LOOK} with {os.path.basename(KATANA_TEX)}")
    w = oro.Stb(P(ours, WEAPON_STB_REL))
    cur = w.get(KATANA, 0).decode("latin-1").strip()
    if cur != KATANA_NAME:
        if cur or any(c.strip() for c in w.d[KATANA]):
            raise SystemExit(f"LIST_WEAPON row {KATANA} is {cur!r}: not free")
        row = list(w.d[KATANA_COLUMNS])
        row[0] = KATANA_NAME.encode("latin-1")
        row[-1] = KATANA_KEY.encode()
        w.d[KATANA] = row
        w.save(dry, keep_backup=False)
        stl = oro.Stl(P(ours, WEAPON_STL_REL))
        if not stl.has(KATANA_KEY):
            stl.append(KATANA_KEY, KATANA, KATANA_NAME, KATANA_DESC)
            stl.save(dry, keep_backup=False)
        print(f"    {'LIST_WEAPON.STB':26s} row {KATANA} {KATANA_NAME}")


def verify_katana(ours, check):
    from rosefmt import zsc as rfzsc
    z = rfzsc.Zsc(P(ours, WEAPON_ZSC_REL))
    parts = z.objects[KATANA][1]
    ok = len(parts) == 1 and z.materials[parts[0][1]][0].lower() == KATANA_TEX.lower().encode() \
        and parts[0][0] == z.objects[KATANA_LOOK][1][0][0]
    check(ok, f"LIST_WEAPON.ZSC object {KATANA} is the Murasamemaru's mesh with the red texture")
    tex = mon.find_ci(ours, KATANA_TEX)
    check(os.path.isfile(tex) and open(tex, "rb").read() == blood_texture(ours), "the red texture is the recolour of ours")


def apply(ours, dry, src=JROSE):
    if not os.path.isdir(src):
        raise SystemExit(f"the Jrose dump is not at {src}")
    weapons(ours, src, dry)
    blood_katana(ours, dry)
    s_npc = oro.Stb(P(src, NPC_STB_REL), "cp932")
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    dirty = stl_dirty = False
    todo = []
    for row, body in sorted(BODIES.items()):
        cur = n.get(row, C_NAME).decode("latin-1").strip()
        if cur and cur != body["name"]:
            raise SystemExit(f"LIST_NPC row {row} is {cur!r}: not free")
        if not cur and row < len(chr_.chars) and chr_.chars[row] is not None:
            raise SystemExit(f"LIST_NPC row {row} is blank but has a CHR entry: not free")
        want = body_row(n, s_npc, row)
        if n.d[row] != want:
            n.d[row] = want
            dirty = True
            print(f"    {'LIST_NPC.STB':26s} row {row} {body['name']} (Jrose {body['src']})")
        if not stl.has(strid(row)):
            stl.append(strid(row), row, body["name"])
            stl_dirty = True
        if row >= len(chr_.chars) or chr_.chars[row] is None:
            todo.append(row)
    if dirty:
        n.save(dry, keep_backup=False)
    if stl_dirty:
        stl.save(dry, keep_backup=False)
        print(f"    {'LIST_NPC_S.STL':26s} keys added")
    if todo:
        oro.import_characters(todo, ours, src, dry, "crypt boss", src_of={r: BODIES[r]["src"] for r in todo})
    elif not dirty:
        print(f"    {'the four bodies':26s} already in place")
    return bool(todo)


def finish(ours):
    """What a fresh model import needs before a bake: mip chains on the new textures, and
    no stray bone slot in the new skinned meshes."""
    for sub in TEXTURE_DIRS:
        r = subprocess.run([sys.executable, os.path.join(HERE, "add-dds-mipmaps.py"), "--subdir", sub],
                           cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]
        print(f"    {'add-dds-mipmaps.py':26s} {sub}: {tail[0][:90]}")
    r = subprocess.run([sys.executable, os.path.join(HERE, "fix-zms-bone-slots.py")], cwd=ROOT,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]
    print(f"    {'fix-zms-bone-slots.py':26s} {tail[0][:100]}")


def verify(ours, check, src=JROSE):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    w = oro.Stb(P(ours, WEAPON_STB_REL))
    s_npc = oro.Stb(P(src, NPC_STB_REL), "cp932") if os.path.isdir(src) else None
    for row, (_srow, name, _desc) in sorted(WEAPONS.items()):
        check(w.get(row, 0).decode("latin-1").strip() == name, f"LIST_WEAPON {row} {name}")
    check(w.get(KATANA, 0).decode("latin-1").strip() == KATANA_NAME, f"LIST_WEAPON {KATANA} {KATANA_NAME}")
    verify_katana(ours, check)
    for row, body in sorted(BODIES.items()):
        if s_npc is not None:
            check(n.d[row] == body_row(n, s_npc, row), f"LIST_NPC {row} {body['name']}")
        check(stl.has(strid(row)), f"LIST_NPC_S {strid(row)}")
        e = chr_.chars[row] if row < len(chr_.chars) else None
        ok = e is not None and all(os.path.isfile(mon.find_ci(ours, chr_.motions[i].decode("latin-1")))
                                   for _s, i in e["anims"] if i < len(chr_.motions))
        check(ok, f"LIST_NPC.CHR {row} and its clips")


def main():
    for row, body in sorted(BODIES.items()):
        c = body["cells"]
        print("%d %-26s Jrose %d  HP %7d  ATK %4d  DEF %4d  RES %4d  AVOID %3d  size %3d  run %3d  weapon %s" % (
            row, body["name"], body["src"], 240 * c[C_HP], c[C_ATK], c[C_DEF], c[C_RES], c[C_AVOID],
            c[C_SIZE], c[C_RUN], c[C_RWEAPON] or "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
