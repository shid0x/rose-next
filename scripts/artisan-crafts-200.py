"""The Artisan's 200+ crafts (roadmap 1.4, 2026-11-19).

Retail crafting stops around item level 130. Above it nothing could be crafted: the 140-199
tier never had a craft category, and at 200+ our imports had copied one (rank 14 on 96
weapons and 21 off-hands, category 57 on the armour) with empty recipes -- 57 is a category
no skill has, and the craft window lists an item whose recipe is empty, which then fails.

The user's rules: **the Dealer and the Bourgeois keep exactly what they craft today**
(ranks 1-5, which already belong to the whole Dealer line); **the Artisan gets the new
crafts** (craft ranks 6+ are Artisan-only, class set 68); weapons and armour at **tiers 225
and 230**, "some are dropped, some are craftable, some are both"; **Jewel Item Craft gets
something to make** (no jewel had a craft category, so the skill listed nothing); **a few
craft-only backs** as the Artisan's exclusives, "they can even be cosmetics"; **scope 200+**.

What is craftable is decided in add-karkia-drops.py (CRAFT_225 / CRAFT_230 / CRAFT_ARMOUR /
PLATINUM / DIAMOND / JEWEL_CRAFT_ONLY / CRAFT_BACKS), because every drop pool is built
there and the craft-only items leave them. This script writes the crafting side:

  item cols   12 craft category, 13 craft rank, 14 recipe row, 15 difficulty
  recipes     one new LIST_PRODUCT row per item (the 200+ items shared recipe rows:
              the armour sets all point at 1400-1415, weapons pairwise)

  item                       category            rank
  weapons 225 / 230          by weapon type      12/13
  armour  225 / 230          17 combat (class    8/9
                             sets 51, 53) or
                             18 magic (52, 54)
  jewels  Platinum/Diamond   31 Jewel Item Craft 6/9
  backs   219-226 / 234-240  16 Sub Weapon Craft 14/15

Difficulty is solved per item (solve_difficulty): ~75% success for a lv230 Artisan, CON
180, at the recipe's rank, with a quality-70 first material (~60% with q62, ~84% with q76).

The weapon category comes from what retail gives that weapon type (`make_of_type`).
Recipes follow retail's lv-130 shape (LIST_PRODUCT: slot 0 a material class and an amount,
slots 1-3 exact items): slot 0 the class (Otherworldly Metal for every weapon and for
combat armour; leather / cloth for magic armour by class line; Otherworldly Metal for
jewels, leather for backs). Retail asks plain Metal (421, the ore line: Rusted Iron q30 ..
Damascus q70) of guns and combat armour; the user found "Metal" read as early-game stuff
(2026-11-19), so ours ask Otherworldly Metal (422: Otherworldly Metal, Philosopher Stone,
Mercury, Tamahagane) at a little above retail's top amounts; then a top vanilla material; then
the tier's signature material where the tier drops -- Oro parts for 225 (the Wasteland and
the Fossil Sanctuary), Karkia Cemetery materials for 230 -- and a rarer third for the higher
tier; the exclusives each need Fafnir's Magic Stone (the Drakes). Every material is
<= 999 (a recipe cell is legacy-encoded) and drops at 200+ (economy-audit.py).

**Clean-up:** items left with a craft category and an empty recipe at 200+ (or a rank of
11+, or category 57) lose their category and rank, so the craft window stops listing
recipes that cannot be made. Existing recipes are never touched.

`--dry-run` / `--verify` / `--restore` (cell-level, build/artisan-crafts-200/). Client and
server read the item tables and LIST_PRODUCT: restart the servers and re-bake.
"""
import argparse
import collections
import functools
import importlib.util
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from rosefmt.stb import Stb  # noqa: E402


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [file]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


kd = _load("add_karkia_drops", "add-karkia-drops.py")
oro = _load("add_oro_drops", "add-oro-drops.py")

STB = os.path.join(REPO, "data", "3DDATA", "STB")
MANIFEST = os.path.join(REPO, "build", "artisan-crafts-200", "manifest.json")
FILES = {2: "LIST_CAP", 3: "LIST_BODY", 4: "LIST_ARMS", 5: "LIST_FOOT", 6: "LIST_BACK",
         7: "LIST_JEWEL", 8: "LIST_WEAPON", 9: "LIST_SUBWPN"}
T_CAP, T_BODY, T_ARMS, T_FOOT, T_BACK, T_JEWEL, T_WEAPON = 2, 3, 4, 5, 6, 7, 8
C_MAKE, C_RANK, C_PRODUCT, C_DIFF, C_TYPE, C_CLASSREQ = 12, 13, 14, 15, 4, 16
AT_LEVEL = 31
PRODUCT_BLOCK = 1700            # first row of our recipes (free, unreferenced; checked)

METAL, RARE, WOOD, LEATHER, CLOTH = 421, 422, 424, 425, 426
# vanilla top materials (LIST_NATURAL)
MERCURY, SEPHIROTH, PATTERNED, LACE, ENTHIRIC, HERMES, LISENT_AU = 20, 40, 50, 60, 69, 70, 88
# Oro parts (225) and Karkia materials (230, exclusives)
SNAPPER_BEAK, SNAPPER_TAIL, ASPER_FANG, DRY_SCALE = 285, 286, 268, 260
DEVOURER_PLATE, DEVOURER_HORN, NYMPH = 275, 276, 277
BLACK_IRON, SCROLL_MIGHT, SCROLL_REASON, TALISMAN, MAGIC_CRYSTAL = 742, 743, 744, 751, 760
TAMAHAGANE, STARLIGHT, STELLA, TOME_GALE, LANTERN, FAFNIR = 763, 741, 757, 750, 764, 762

# weapon recipes by craft category: (class, second, Oro signature, Karkia signature)
WEAPON_RECIPE = {11: (RARE, LACE, SNAPPER_TAIL, BLACK_IRON),        # swords, spears, axes
                 # blunt: Dry Scale, not Devourer Horn -- Eldeon's retail tables name row 276,
                 # so 83% of the Horn's drops came from lv170-200 monsters (checked 2026-11-19)
                 12: (RARE, SEPHIROTH, DRY_SCALE, BLACK_IRON),
                 13: (RARE, SEPHIROTH, ASPER_FANG, SCROLL_MIGHT),   # bows, crossbows
                 14: (RARE, PATTERNED, DEVOURER_PLATE, MAGIC_CRYSTAL),   # guns, launchers
                 15: (RARE, ENTHIRIC, NYMPH, SCROLL_REASON)}        # staffs, wands
TIER = {225: dict(rank=12, diff=58, amount=200, second=40, sig=25, third=(LISENT_AU, 10)),
        230: dict(rank=13, diff=64, amount=240, second=45, sig=30, third=(TAMAHAGANE, 10))}
ARMOUR_TIER = {225: dict(rank=8, diff=52, sig=SNAPPER_BEAK, third=None),
               230: dict(rank=9, diff=58, sig=TALISMAN, third=(MAGIC_CRYSTAL, 10))}
ARMOUR_AMOUNT = {T_CAP: 120, T_BODY: 180, T_ARMS: 90, T_FOOT: 100}
BACKS = {1079: dict(rank=14, diff=62, second=(BLACK_IRON, 25)),     # Antares Ward, lv219
         1080: dict(rank=14, diff=62, second=(SCROLL_MIGHT, 25)),   # Noble Sacred Cross, lv226
         1081: dict(rank=15, diff=70, second=(STARLIGHT, 25)),      # Michael's Half-Wing, lv234
         1082: dict(rank=15, diff=70, second=(TAMAHAGANE, 30))}     # Chieftain's Heirloom Blade, lv240


# --- difficulty: solved per item (2026-11-19) --------------------------------------
# A craft is a chain of rolls, one per recipe slot, each slot consumed as it goes
# (classUSER::Recv_cli_CREATE_ITEM_REQ). No step reads its own material's quality: every
# step reads the FIRST material's quality, the item's difficulty (col 15) and quality
# (col 8), the crafter's CON and craft rank, and a 1-100 roll. The first cut copied retail's
# top difficulties (52-70) onto items of quality 90-113 and failed most crafts (the user
# failed at "material two", Patterned Leather): 13-59% for weapons with q62-70 material,
# where retail's lv-130 recipes run ~70-85% for a lv-130 Artisan. So each item's difficulty
# is now the highest at which the reference crafter below succeeds TARGET_SUCCESS of the
# time; a weaker first material (q62) still does clearly worse, a better one (q76) better.
REF_CON, REF_MAT_QUALITY, TARGET_SUCCESS = 180, 70, 0.75
DIFF_RANGE = (10, 70)


def _share(a, b, c):
    """Share of the rolls r = 0..99 with a * (r + b) >= c."""
    if a <= 0:
        return 0.0
    k = max(0, math.ceil(c / a - b - 1e-9))
    return max(0, 100 - k) / 100


@functools.lru_cache(maxsize=None)
def p_success(dif, qual, nmat, con, rank, matq, world=100):
    """The CREATE_ITEM rolls (gs_user.cpp), exact: rolls 0 and 1 are enumerated, steps 2
    and 3 counted from their thresholds."""
    suc0 = (dif + 35) * (qual + 15) / 16
    suc1 = (dif + 15) * (qual + 140) / (nmat + 3) / 4
    suc2 = (dif + 90) * (qual + 30) / (nmat + 3) / 4
    suc3 = (dif + 40) * (qual + 60) / (nmat + 2) / 6
    total = 0.0
    for r0 in range(100):
        pro0 = matq * (r0 + 71) * (0.5 * con + dif / 2 + 530) * world / 800000
        if pro0 < suc0:
            continue
        p2 = (_share((pro0 / 6 + qual) * (0.3 * con + rank * 5 + matq * 2 + 600) / (nmat + 7) / 2000,
                     81, suc2) if nmat >= 3 else 1.0)
        if nmat < 2:
            total += 0.01
            continue
        for r1 in range(100):
            pro1 = ((matq + dif / 2) * (r1 + 96) * (0.5 * con + rank * 6 + matq * 2 + 770)
                    / (nmat + 7) / 1600)
            if pro1 < suc1:
                continue
            p3 = _share((pro0 + pro1 + 40) / 200, 51, suc3) if nmat >= 4 else 1.0
            total += 1e-4 * p2 * p3
    return total


@functools.lru_cache(maxsize=None)
def solve_difficulty(qual, nmat, rank):
    """Highest difficulty in DIFF_RANGE at which the reference crafter succeeds
    TARGET_SUCCESS of the time (success falls as difficulty rises)."""
    lo, hi = DIFF_RANGE
    if p_success(lo, qual, nmat, REF_CON, rank, REF_MAT_QUALITY) < TARGET_SUCCESS:
        return lo
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if p_success(mid, qual, nmat, REF_CON, rank, REF_MAT_QUALITY) >= TARGET_SUCCESS:
            lo = mid
        else:
            hi = mid - 1
    return lo


def code(item_no, t=12):
    return t * 1000 + item_no


class Tables:
    def __init__(self):
        self.t = {k: Stb(os.path.join(STB, f + ".STB")) for k, f in FILES.items()}
        self.prod = Stb(os.path.join(STB, "LIST_PRODUCT.STB"))

    def level(self, t, n):
        tb = self.t[t]
        for c in (19, 21):
            if tb.i(n, c) == AT_LEVEL:
                return tb.i(n, c + 1)
        return 0

    def recipe_filled(self, p):
        return 0 < p < self.prod.rows and any(self.prod.i(p, c) for c in range(1, 10))


def make_of_type(tb):
    """Weapon type (col 4) -> the craft category retail gives it (most common, ranks 1-10)."""
    seen = collections.defaultdict(collections.Counter)
    for n in range(1, tb.t[T_WEAPON].rows):
        w = tb.t[T_WEAPON]
        if w.occupied(n) and w.i(n, C_MAKE) in WEAPON_RECIPE and 0 < w.i(n, C_RANK) <= 10:
            seen[w.i(n, C_TYPE)][w.i(n, C_MAKE)] += 1
    return {k: c.most_common(1)[0][0] for k, c in seen.items()}


def plan(tb):
    """[(item type, row, make, rank, diff, recipe)] -- recipe: (class, amount, [(code, n)])."""
    out = []
    mot = make_of_type(tb)
    for tier, pool, split in ((225, kd.WPN_225, kd.CRAFT_225), (230, kd.WPN_230, kd.CRAFT_230)):
        T = TIER[tier]
        for i in split["craft"] + split["both"]:
            n = pool[i]
            make = mot[tb.t[T_WEAPON].i(n, C_TYPE)]
            cls, second, sig_oro, sig_karkia = WEAPON_RECIPE[make]
            sig = sig_oro if tier == 225 else sig_karkia
            items = [(code(second), T["second"]), (code(sig), T["sig"]),
                     (code(T["third"][0]), T["third"][1])]
            out.append((T_WEAPON, n, make, T["rank"], T["diff"], (cls, T["amount"], items)))
    sets = {225: [(t, r) for t, r in oro.EGYPT], 230: kd.ARM_OG}
    for tier, key in ((225, "egypt"), (230, "og")):
        A, pick = ARMOUR_TIER[tier], kd.CRAFT_ARMOUR[key]
        for t, n in sets[tier]:
            if t not in (pick["craft"], pick["both"]):
                continue
            creq = tb.t[t].i(n, C_CLASSREQ)
            make = 17 if creq in (51, 53) else 18
            cls = RARE if make == 17 else (LEATHER if creq == 52 else CLOTH)
            second = PATTERNED if cls != LEATHER else LACE
            items = [(code(second), 40), (code(A["sig"]), 20)]
            if A["third"]:
                items.append((code(A["third"][0]), A["third"][1]))
            out.append((t, n, make, A["rank"], A["diff"], (cls, ARMOUR_AMOUNT[t], items)))
    for jewels, rank, diff, amount, items in (
            (kd.PLATINUM, 6, 42, 80, [(code(MERCURY), 20), (code(NYMPH), 10)]),
            (kd.DIAMOND, 9, 52, 120, [(code(STARLIGHT), 20), (code(MAGIC_CRYSTAL), 15),
                                      (code(STELLA), 5)])):
        for n in jewels:
            out.append((T_JEWEL, n, 31, rank, diff, (RARE, amount, items)))
    assert set(BACKS) == set(kd.CRAFT_BACKS), "BACKS must name add-karkia-drops.CRAFT_BACKS"
    for n, b in sorted(BACKS.items()):
        items = [(code(LANTERN), 20), (code(b["second"][0]), b["second"][1]), (code(FAFNIR), 3)]
        out.append((T_BACK, n, 16, b["rank"], b["diff"], (LEATHER, 160, items)))
    return out


def stale(tb, planned):
    """Items whose craft listing cannot work: an empty recipe at lv200+, a rank 11+ or
    category 57 -- (type, row)."""
    out = []
    for t, s in tb.t.items():
        for n in range(1, s.rows):
            if not s.occupied(n) or (t, n) in planned or not s.i(n, C_MAKE):
                continue
            if tb.recipe_filled(s.i(n, C_PRODUCT)):
                continue
            if tb.level(t, n) >= 200 or s.i(n, C_RANK) >= 11 or s.i(n, C_MAKE) == 57:
                out.append((t, n))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    man = json.load(open(MANIFEST, encoding="utf-8")) if os.path.isfile(MANIFEST) else {"cells": {}}
    tb = Tables()

    def table(name):
        return tb.prod if name == "LIST_PRODUCT" else tb.t[next(k for k, f in FILES.items() if f == name)]

    if args.restore:
        if not man["cells"]:
            print("nothing to restore")
            return 0
        skipped = 0
        for key, (old, new) in man["cells"].items():
            f, r, c = key.split(",")
            s = table(f)
            if s.get(int(r), int(c)).strip() != new.encode("latin-1"):
                skipped += 1
                continue
            s.set(int(r), int(c), old.encode("latin-1"))
        for f in set(k.split(",")[0] for k in man["cells"]):
            s = table(f)
            open(os.path.join(STB, f + ".STB"), "wb").write(s.to_bytes())
        os.remove(MANIFEST)
        print(f"restored ({skipped} cell(s) edited since were left alone)")
        return 0

    # plan against the tables as they were before this pass
    for key, (old, new) in man["cells"].items():
        f, r, c = key.split(",")
        s = table(f)
        if s.get(int(r), int(c)).strip() == new.encode("latin-1"):
            s.set(int(r), int(c), old.encode("latin-1"))
    items = plan(tb)
    planned = {(t, n) for t, n, *_ in items}
    want = {}
    rows = list(range(PRODUCT_BLOCK, PRODUCT_BLOCK + len(items)))
    referenced = {s.i(n, C_PRODUCT) for s in tb.t.values() for n in range(1, s.rows)}
    for p in rows:
        if p >= tb.prod.rows or tb.recipe_filled(p) or (p in referenced and not any(
                tb.t[t].i(n, C_PRODUCT) == p for t, n in planned)):
            sys.exit(f"LIST_PRODUCT row {p} is not free")
    solved = {}
    for (t, n, make, rank, diff, (cls, amount, needs)), p in zip(items, rows):
        f = FILES[t]
        diff = solve_difficulty(tb.t[t].i(n, 8), 1 + len(needs), rank)
        solved[(t, n)] = diff
        for c, v in ((C_MAKE, make), (C_RANK, rank), (C_PRODUCT, p), (C_DIFF, diff)):
            want[(f, n, c)] = str(v)
        want[("LIST_PRODUCT", p, 0)] = f"200+ craft {f[5:]} {n}"
        want[("LIST_PRODUCT", p, 1)] = str(cls)
        want[("LIST_PRODUCT", p, 2)] = ""
        want[("LIST_PRODUCT", p, 3)] = str(amount)
        for k in range(3):
            it = needs[k] if k < len(needs) else None
            want[("LIST_PRODUCT", p, 4 + 2 * k)] = str(it[0]) if it else ""
            want[("LIST_PRODUCT", p, 5 + 2 * k)] = str(it[1]) if it else ""
    for t, n in stale(tb, planned):
        want[(FILES[t], n, C_MAKE)] = "0"
        want[(FILES[t], n, C_RANK)] = "0"

    # re-read the live tables for the comparison
    live = Tables()

    def ltable(name):
        return live.prod if name == "LIST_PRODUCT" else live.t[next(k for k, f in FILES.items() if f == name)]
    pending = {k: v for k, v in want.items()
               if ltable(k[0]).get(k[1], k[2]).strip().decode("latin-1") != v}
    kinds = collections.Counter(FILES[t] for t, *_ in items)
    print(f"{len(items)} craftable items: " + ", ".join(f"{k} {v}" for k, v in sorted(kinds.items())))
    print(f"{len(stale(tb, planned))} stale craft listings cleared; recipes in LIST_PRODUCT "
          f"{rows[0]}-{rows[-1]}")
    for t, n, make, rank, diff, (cls, amount, needs) in items:
        diff = solved[(t, n)]
        q, nm8 = tb.t[t].i(n, 8), 1 + len(needs)
        rates = "/".join(f"{100 * p_success(diff, q, nm8, REF_CON, rank, m):.0f}" for m in (62, 70, 76))
        nm = tb.t[t].get(n, 0).decode("latin-1").strip()[:26]
        print(f"   {FILES[t][5:]:6} {n:5} lv{tb.level(t, n):3} {nm:26} cat {make} rank {rank:2} "
              f"diff {diff} (q{q}; success {rates}% with q62/70/76): class {cls} x{amount}")
    print(f"{len(want)} cells planned; {len(pending)} differ")
    if args.verify:
        print("VERIFY FAILED" if pending else "ALL CHECKS PASSED")
        return 1 if pending else 0
    if not pending:
        print("nothing to do.")
        return 0
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    for (f, r, c), v in pending.items():
        s = ltable(f)
        key = f"{f},{r},{c}"
        man["cells"].setdefault(key, [s.get(r, c).strip().decode("latin-1"), ""])[1] = v
        s.set(r, c, v.encode("latin-1"))
    for f in set(k[0] for k in pending):
        open(os.path.join(STB, f + ".STB"), "wb").write(ltable(f).to_bytes())
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    json.dump(man, open(MANIFEST, "w", encoding="utf-8"), indent=1)
    print(f"written (undo: --restore, manifest {os.path.relpath(MANIFEST, REPO)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
