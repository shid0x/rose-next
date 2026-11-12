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
  * **Weapons.** Ferdinando holds our own Murasamemaru (1462): Jrose's row is that mesh
    with another texture, and the red set is what the crypt's bosses drop. Gladiomurus'
    greatsword (Jrose LIST_WEAPON 1196, a new mesh) and the Assassin's black Death
    Expeler (4033, our osw41 mesh with a new texture) come in through import-item.py
    --art-only at rows 29 and 30, every stat cloned from our Death Expeler (21).
  * **No stat, no AI, no skill of Jrose's.** Rows are a copy of the Crypt Necromancer's
    (2556: voice, death effect, drop table 913, skull) with each body's own numbers in
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

TEMPLATE = boss.BOSS                # 2556, the Crypt Necromancer's row
WEAPON_TEMPLATE = 21                # Death Expeler: a one-handed sword's columns
WEAPON_ICON = 1                     # a monster's weapon: never seen in a bag

FERDINANDO, ARGENTIA, GLADIOMURUS, ASSASSIN = 2557, 2558, 2559, 2560
# our weapon row -> (Jrose row, name, description)
WEAPONS = {
    29: (1196, "Gladiomurus' Greatblade", "The blade of the demon knight Gladiomurus."),
    30: (4033, "Black Death Expeler", "A Death Expeler gone black, carried by the Greater Assassin."),
}
# our row -> the body. `cells` are what differs from the Crypt Necromancer's row.
BODIES = {
    FERDINANDO: dict(src=2049, name="Ferdinando, the Eternal", cells={
        C_WALK: 300, C_RUN: 700, C_SIZE: 220, C_RWEAPON: 1462, C_HEIGHT: 400,
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


def apply(ours, dry, src=JROSE):
    if not os.path.isdir(src):
        raise SystemExit(f"the Jrose dump is not at {src}")
    weapons(ours, src, dry)
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
