"""Give the 200+ materials real classes and a tier of their own (roadmap 1.2 follow-up, 2026-11-19).

`import-oro-materials.py` (Oro's nine monster parts) and `import-karkia-materials.py`
(Karkia's twenty-five) took every stat column from our Chromium (`--art-only
--template-row 8`), so all thirty-four were **class 421 (metal ore), quality 62**: a Scroll
of Might, a Snapper Beak and a Ruber Core all counted as ore. Both columns do something:

  * **col 4, the class.** A recipe's first ingredient is a class, not an item
    (`PRODUCT_RAW_MATERIAL`, checked against `ITEM_TYPE` in
    `classUSER::Recv_cli_CREATE_ITEM_REQ`), and breaking an item up pays the class's
    vanilla row `(class - 421) * 10 + tier` -- so a scroll would craft and break up as ore.
    421 metal, 422 rare metal, 423 stone, 424 wood, 425 leather, 426 cloth, 427 essence,
    428 powder, 429 other (where retail keeps its monster parts: Dragon Scale, Huge Horn).
  * **col 8, the quality.** The material's quality is the "progress" side of every craft
    roll (`nMAT_QUAL` in the same function), so better materials craft better. Every class
    tops out at 68-70 below level 200 (Sephiroth Wood, Patterned Leather, Lace, Mercury;
    the Lisents run to 78 but (Ra) and (U) dropped nowhere); the 200+ materials get 72-80,
    Oro's lower than Karkia's, so the endgame's materials are the best inputs in the game.

The classes, by what each thing is (descriptions and names): Oro's beast parts are "other"
like retail's, Nymph Essence is an essence, the Broken Rune Piece a stone; Karkia's scrolls,
tomes, talisman, sigil, starlight and lantern are essences, its cores, Stella Libra and magic
stones and crystals are stones, Black Iron Gear is metal, Tamahagane a rare metal, the Sol
Niger Horn a monster part. Price and every other column are untouched.

  * **col 10, the field model** (LIST_FieldITEM.ZSC, how it lies on the ground), also
    Chromium's ore lump on all thirty-four (the user: the scrolls "have one that's usable,
    same as Blood Charm, Spark Charm"). By what each thing is, from models retail already
    uses: 151 the charm / scroll (Ice, Spark and Blood Charm and ~100 scrolls) for the
    scrolls, tomes, talisman and sigil; 197-200 the green / blue / red / white crystals for
    the cores, Stella Libra, Starlight, the lantern and the magic stones, by colour; 189 the
    stone (Pebble .. Meteorite) for the rune piece and Graphistone; 211 retail's monster
    parts (feathers, shells, skins) for the beast parts; 144 the bottle for Nymph Essence;
    186, the ore, kept for Black Iron Gear and Tamahagane.

`--dry-run` / `--verify` / `--restore`; rows are checked by name before any write; cell-level
manifest in build/material-classes-200/. Client and server both read LIST_NATURAL: restart
the servers and re-bake.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from rosefmt.stb import Stb  # noqa: E402

NAT_STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NATURAL.STB")
MANIFEST = os.path.join(REPO, "build", "material-classes-200", "manifest.json")
COL_NAME, COL_CLASS, COL_QUALITY, COL_FIELD = 0, 4, 8, 10
SCROLL, ORE, STONE_M, PART, BOTTLE = 151, 186, 189, 211, 144
GREEN, BLUE, RED, WHITE = 197, 198, 199, 200
METAL, RARE_METAL, STONE, ESSENCE, OTHER = 421, 422, 423, 427, 429

# row: (name in col 0, class, quality, field model)
PLAN = {
    # --- Oro (import-oro-materials.py): the Golden Ring 72, the Wasteland 74, the Sanctuary 76
    260: ("Dry Scale", OTHER, 72, PART),
    268: ("Asper Fang", OTHER, 72, PART),
    269: ("Asper Tongue", OTHER, 72, PART),
    287: ("Broken Rune Piece", STONE, 72, STONE_M),
    275: ("Devourer Plate", OTHER, 74, PART),
    276: ("Devourer Horn", OTHER, 74, PART),
    277: ("Nymph Essence", ESSENCE, 74, BOTTLE),
    285: ("Snapper Beak", OTHER, 76, PART),
    286: ("Snapper Tail", OTHER, 76, PART),
    # --- Karkia (import-karkia-materials.py): Cemetery 72-74, Spire / crypts 74-78, bosses 80
    742: ("Black Iron Gear", METAL, 74, ORE),
    743: ("Scroll of Might", ESSENCE, 72, SCROLL),
    744: ("Scroll of Reason", ESSENCE, 72, SCROLL),
    751: ("Talisman of Enchantment", ESSENCE, 74, SCROLL),
    745: ("Scroll of the Water Mirror", ESSENCE, 74, SCROLL),
    746: ("Scroll of Gale", ESSENCE, 74, SCROLL),
    760: ("Magic Stone Crystal", STONE, 74, BLUE),
    747: ("Tome of Might", ESSENCE, 76, SCROLL),
    748: ("Tome of Reason", ESSENCE, 76, SCROLL),
    752: ("Arcane Sigil of Enchantment", ESSENCE, 76, SCROLL),
    763: ("Tamahagane", RARE_METAL, 76, ORE),
    758: ("Sol Niger Horn", OTHER, 76, PART),
    753: ("Ruber Core", STONE, 76, RED),
    754: ("Caerula Core", STONE, 76, BLUE),
    755: ("Viride Core", STONE, 76, GREEN),
    756: ("Flavus Core", STONE, 76, WHITE),
    740: ("Graphistone", STONE, 76, STONE_M),
    749: ("Tome of the Water Mirror", ESSENCE, 78, SCROLL),
    750: ("Tome of Gale", ESSENCE, 78, SCROLL),
    741: ("Starlight", ESSENCE, 78, WHITE),
    757: ("Stella Libra", STONE, 78, WHITE),
    764: ("Stardust Lantern", ESSENCE, 78, WHITE),
    759: ("Phil Tempest's Magic Stone", STONE, 80, BLUE),
    761: ("Sacred Demon Crystal", STONE, 80, RED),
    762: ("Fafnir's Magic Stone", STONE, 80, RED),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    stb = Stb(NAT_STB)
    man = json.load(open(MANIFEST, encoding="utf-8")) if os.path.isfile(MANIFEST) else {"cells": {}}

    if args.restore:
        if not man["cells"]:
            print("nothing to restore")
            return 0
        skipped = 0
        for key, (old, new) in man["cells"].items():
            r, c = (int(x) for x in key.split(","))
            if stb.get(r, c).strip() != new.encode("latin-1"):
                skipped += 1
                continue
            stb.set(r, c, old.encode("latin-1"))
        open(NAT_STB, "wb").write(stb.to_bytes())
        os.remove(MANIFEST)
        print(f"restored ({skipped} cell(s) edited since were left alone)")
        return 0

    pending = {}
    for r, (name, cls, q, field) in sorted(PLAN.items()):
        got = stb.get(r, COL_NAME).decode("latin-1").strip()
        if not got.startswith(name[:14]):
            print(f"ABORT: LIST_NATURAL {r} is {got!r}, expected {name!r}")
            return 1
        for c, v in ((COL_CLASS, cls), (COL_QUALITY, q), (COL_FIELD, field)):
            if stb.i(r, c) != v:
                pending[(r, c)] = v
        print(f"   {r:4} {name[:28]:28} class {stb.i(r, COL_CLASS)} -> {cls}, quality {stb.i(r, COL_QUALITY)} -> {q}, field model {stb.i(r, COL_FIELD)} -> {field}")
    print(f"{len(PLAN)} materials; {len(pending)} cell(s) differ")
    if args.verify:
        print("VERIFY FAILED" if pending else "ALL CHECKS PASSED")
        return 1 if pending else 0
    if not pending:
        print("nothing to do.")
        return 0
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    for (r, c), v in pending.items():
        key = f"{r},{c}"
        man["cells"].setdefault(key, [stb.get(r, c).decode("latin-1").strip(), ""])[1] = str(v)
        stb.set(r, c, str(v).encode("latin-1"))
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    json.dump(man, open(MANIFEST, "w", encoding="utf-8"), indent=1)
    open(NAT_STB, "wb").write(stb.to_bytes())
    print(f"written (undo: --restore, manifest {os.path.relpath(MANIFEST, REPO)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
