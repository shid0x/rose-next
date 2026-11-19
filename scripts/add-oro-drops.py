"""Oro's loot tables: wire the zone rows, then fill every species table.

--- 2026-11-19: reshaped (roadmap 1.2) -- this section supersedes the rates below

Same pass as add-karkia-drops.py's (its shapes, its docstring's first section): per
field kill materials ~36-49%, consumables 6%, gear 3.0% (Karkia pays 4.2%: the harder
place keeps the better loot), jewellery 1.5% (Platinum and the named q50-54 pieces),
gems 0.8% ([5]/[6]), money 15%; bosses ~90% gear; the six zone rows are map tables
(tier materials and the two Oro generics, consumables, no gear). The pools follow the
re-levelled bands (relevel-200-band.py): the Golden Ring pays the lv210/215 lines
(its 1447/1450 oddments left), the Wasteland lv220 + lv225 (Cursed Vermillion, which
had no source) and the Egyptian set, the Fossil Sanctuary lv225 + lv230 and the
Egyptian set; kings take the tier's top line. Every row is re-planned in place with
`--rewrite`. The sections below are the 2026-09 history.

Oro has dropped nothing since it was imported, and it is worse than "empty":

  * every Oro monster points `LIST_NPC` col 18 at a table in 773-870, and all
    98 of those rows are blank (RoseZA never authored them either);
  * the 19 species the 667 import added carry col 20 = 100, which sends every
    roll to that blank table, and the rest carry 80, which sends a fifth of
    their rolls to **the row numbered after the zone** -- and rows 71-85 are
    live tables belonging to somebody else. 74-79 are the Eldeon Ikaness
    (lv191-205), 80-85 Orpe/Eudy/Follen Ant/Melt Dizzle, 71 and 73 the Junon
    town NPCs, and 83 and 85 are still titled "MVP Zone (1P-VS-Kings)" and
    "TVT Zone (Party-VS-Party)" from before those zone ids were reused;
  * col 19 (money) is 0 everywhere, so not even zuly.

This is the same shape as Karkia's problem and it gets the same fix, in one
script: the rewire of rewire-karkia-drops.py (legacy tables squatting on an Oro
zone number move to a free row and their users are repointed) and the content
pass of add-karkia-drops.py (whose `build_row`/`encode` and item pools are
imported, so the two continents share one encoding and one calibration).

--- what fills the tables

**One table per species, not one per roster.** Karkia shares a table across a
zone's roster; Oro keeps the per-species tables it already has (col 18 is left
alone), because the RoseZA materials import-oro-materials.py brings in are
named for the family that drops them -- Asper Fang off an asper, Snapper Beak
off a snapper, Devourer Plate off a devourer -- and a shared table would pay
those out from the wrong animal. The table is generated, so forty rows cost the
same as four.

A field table is: two family materials, four tier materials (metals and
leather climbing with the zone), the two XL waters, then a shield bucket and a
weapon bucket -- 10 slots. The Fossil Sanctuary adds two more buckets for the
lv240 armour (body and cap), 12 slots. Karkia's field tables fill 12, so at
equal level Oro pays out a little less often than Karkia, on purpose: Karkia
is meant to be the harder place with the better loot.

Weapons come from what add-karkia-shops.py and add-karkia-drops.py left
unused: the partial Jrose lv210 set (1420-1425, "duplicates Oro's tier" is
exactly why it fits the Golden Ring) with the lv200/205 oddments 1447 and 1450,
then the shared lv220 and lv235 pools in the Wasteland and Sanctuary. Each
species takes a five-wide window rotated through its tier's pool, so the pool
is spread across the roster instead of five weapons everywhere.

**The lv240 armour finally has a source.** The 2026-09-08 armour pass made the
cap tier deliberately drop-only, and nothing dropped it. It is Oro's signature
the way the mythical weapons are Karkia's: the Sanctuary's field tables reach
the body and cap through buckets (about 1 in 20 a kill), and its four kings
carry the gauntlets and boots in eight common slots (about 36% a kill, the
same shape and rate as a Karkia boss's mythical slots, and deliberately not
above it -- BOSS_ARMOUR_SLOTS is the knob if it should be lower still).
The Ring's kings pay the lv210 weapons, the Wasteland's the lv235 pool.

--- rates, at level parity (see --simulate)

Field col 20 is set to 80 on every Oro species (the 19 new ones drop from 100)
and money to 15 -- add-karkia-drops.py's calibration, so the two continents
feel the same in hand. Bosses stay at money 0 so a zuly roll can never eat an
armour roll.

Zone rows 71/78/79/81/83/85 get a generic table for their tier (tier materials
plus Spiritual Stone of Oro and a rune piece, same buckets) so the 20% zone
fallback is not a dead roll. Zones 72/73/80 spawn nothing and are left alone.

Idempotent. `--dry-run` / `--verify` / `--restore` / `--simulate`. Both STBs
are restored **cell by cell** from the sidecar in build/, never from a file
copy: LIST_NPC is written by a dozen scripts and ITEM_DROP by add-karkia-drops,
so a whole-file backup goes stale the moment either runs. Backups never go
beside the data (pack.rs would bake a .bak into the .vfs).

After running: restart the servers (they cache STBs) and re-bake the client VFS
(the Monster Inspector reads these tables client-side).
"""
import argparse
import collections
import glob
import importlib.util
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB = os.path.join(ROOT, "data", "3DDATA", "STB")
DROP_STB = os.path.join(STB, "ITEM_DROP.STB")
NPC_STB = os.path.join(STB, "LIST_NPC.STB")
MAPS = os.path.join(ROOT, "data", "3DDATA", "MAPS", "ORO")
SIDECAR = os.path.join(ROOT, "build", "oro-drops.json")

COL_DROP_TYPE, COL_DROP_MONEY, COL_DROP_ITEM = 18, 19, 20
T_CAP, T_BODY, T_ARMS, T_FOOT, T_WEAPON, T_SUBWPN, T_USE, T_NATURAL = 2, 3, 4, 5, 8, 9, 10, 12

ORO_ID_RANGE = range(2100, 2400)
# map folder -> zone id, only the zones where an Oro monster can die
ZONE_OF_FOLDER = {"TOWN": 71, "ODD04": 78, "ODD05": 79, "ODRP01": 81,
                  "ODGR01": 83, "ODFS01": 85}
TIER_OF_ZONE = {71: "WASTE", 78: "WASTE", 79: "WASTE", 81: "WASTE",
                83: "RING", 85: "FOSSIL"}
LEGACY_BAND = 949          # rewire-karkia-drops.py used 940-948

BOSSES = {2207, 2254, 2281,           # Golden Ring kings
          2226, 2247, 2296,           # Wasteland / Ruins Path
          2221, 2259, 2265, 2276}     # Fossil Sanctuary

DROP_RATE = 80             # col 20, add-karkia-drops.py's calibration
REDIRECT_BASE = 25         # Rose::Drop::kRedirectBaseColumn (26 until 2026-11-19), for --simulate
MONEY_FIELD, MONEY_BOSS = 15, 0

# ---------------------------------------------------------------- materials
# family keyword in the monster name -> the two RoseZA monster parts
# (import-oro-materials.py; looked up by name so a row move there is followed)
FAMILY_MATS = {
    "Asper":       ["Asper Fang", "Asper Tongue"],
    "Scorpio":     ["Dry Scale", "Broken Rune Piece"],
    "Scarab":      ["Broken Rune Piece", "Huge Horn"],
    "Scavenger":   ["Nymph Essence", "Broken Rune Piece"],
    "Ikaness":     ["Ikaness Jewels", "Heavygear Scrap"],
    "Devourer":    ["Devourer Plate", "Devourer Horn"],
    "Snapper":     ["Snapper Beak", "Snapper Tail"],
    "Mastyx":      ["Dry Scale", "Dragon Scale"],       # also Armastyx
    "Terrasaurus": ["Dry Scale", "Huge Horn"],
}
# Zone rows. "Spiritual Stone of Oro" (row 290) left on 2026-11-19: its col-0 name says Oro
# but players see its STL text, "[PvP] Tokens ... from the Junon PvP tournament", and Oro
# paid that on ~10% of kills. Look items up by what players see before using them.
GENERIC_MATS = ["Broken Rune Piece", "Dry Scale"]

# Tier materials (LIST_NATURAL rows we already ship). Four per tier until 2026-11-19, all
# metal, leather and cloth: Oro paid 24 distinct materials against 63-114 in every band
# below it (economy-audit.py). Since then each tier draws eight vanilla materials across
# seven classes -- metal, rare metal, wood, leather, cloth, essence, powder -- at the top
# of each class (q62-70), stepping up a notch per tier; Karkia keeps the very top
# (Hermes, Lisent Ra / U, Mercury) as its own.
TIER_MATS = {
    # Chromium, Tiar, Red Sandalwood, Hard Leather, Plain Weave, Otherworldly Metal, Hime, Lisent (Na)
    "RING": [8, 9, 38, 48, 58, 18, 67, 86],
    # Damascus, Otherworldly Wood, Transparent Leather, Twill, Orihalcon, Philosopher Stone,
    # Low Enthiric, Lisent (Cr)
    "WASTE": [10, 39, 49, 59, 16, 19, 68, 87],
    # Adamantium, Sephiroth Wood, Patterned Leather, Lace, Orihalcon, Enthiric, Lisent (Au), Damascus
    "FOSSIL": [17, 40, 50, 60, 16, 69, 88, 10],
}

# ---------------------------------------------------------------- equipment
# Jrose lv200-210. Was 1420-1425 + 1450 + 1447 until 2026-10-20: weapon-ladder-pass.py
# made Photon Cannon (1424) the lv230 Arcidian line's launcher, and the other five
# (1420-1423, 1425) wore the lv230 Arcidian line's models -- no weapon may share a
# higher tier's look -- and were erased (erase-retired-weapons.py).
# (The Golden Ring's 1447/1450 oddments left the pool on 2026-11-19: the Ring pays the
# lv210/215 lines now, add-karkia-drops.WPN_210 / WPN_215.)
# The Egyptian set: lv240 when this was written, lv225 since 2026-10-20
# (armour-endgame-sets.py) -- the Wasteland's and the Fossil Sanctuary's armour.
EGYPT_BY = {T_CAP: [989, 990, 991, 992],     # Royal Guardian / Sun Prophet /
            T_BODY: [916, 917, 918, 919],    # Jackal Bandit / Serpent Trader
            T_ARMS: [892, 893, 894, 895],
            T_FOOT: [890, 891, 892, 893]}
EGYPT = [(t, EGYPT_BY[t][c]) for c in range(4) for t in (T_BODY, T_CAP, T_ARMS, T_FOOT)]


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name + ".py"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def gi(stb, row, col):
    v = stb.get(row, col).strip()
    try:
        return int(v)
    except ValueError:
        return 0


def rot(pool, i, n=5):
    """Five consecutive entries of pool, starting i steps in, wrapping."""
    return [pool[(i + k) % len(pool)] for k in range(min(n, len(pool)))]


class Plan:
    def __init__(self, oro, kd, npc, drop, mats):
        self.oro, self.kd, self.npc, self.drop = oro, kd, npc, drop
        self.mat = mats                              # name -> LIST_NATURAL row
        self.species, self.zones_of = self.rosters()
        self.tier = {}
        for i, zones in self.zones_of.items():
            best = max(zones.items(), key=lambda kv: kv[1])[0]
            self.tier[i] = TIER_OF_ZONE[best]
        self.family = {i: self.family_of(i) for i in self.species}

    # -- rosters ----------------------------------------------------------
    def rosters(self):
        zones_of = collections.defaultdict(collections.Counter)
        seen = set()
        for p in glob.glob(os.path.join(MAPS, "*", "*.[iI][fF][oO]")):
            if p.lower() in seen:
                continue
            seen.add(p.lower())
            folder = os.path.basename(os.path.dirname(p)).upper()
            if folder not in ZONE_OF_FOLDER:
                continue
            buf, bounds = self.oro.read_ifo(p)
            objs, _ = self.oro.read_lump(buf, bounds, self.oro.LUMP_REGEN)
            for o in objs or ():
                for i in self.oro.regen_mob_ids(o["extra"]):
                    if i in ORO_ID_RANGE and self.npc.get(i, 0).strip():
                        zones_of[i][ZONE_OF_FOLDER[folder]] += 1
        return sorted(zones_of), zones_of

    def name(self, i):
        return self.npc.get(i, 0).decode("latin-1")

    def family_of(self, i):
        n = self.name(i).lower()
        for key in FAMILY_MATS:
            if key.lower() in n:          # "Armastyx" is a Mastyx
                return key
        raise SystemExit(f"{i} {n!r}: no material family matches its name -- add one to FAMILY_MATS")

    def m(self, name):
        if name not in self.mat:
            raise SystemExit(f"material {name!r} is not in LIST_NATURAL -- run import-oro-materials.py first")
        return (T_NATURAL, self.mat[name])

    # -- table shapes (2026-11-19, roadmap 1.2: add-karkia-drops.py's shapes) ----
    # Per tier (the zone a species mostly spawns in), after relevel-200-band.py:
    #   RING   lv205-222  weapons lv210 + lv215, off-hands 210/215, no armour
    #   WASTE  lv217-226  weapons lv220 + lv225, off-hands 220/225, Egyptian (lv225)
    #   FOSSIL lv224-230  weapons lv225 + lv230, off-hands 225/230, Egyptian (lv225)
    # Bosses (~90% gear) take the tier's top line on top and the line below under it:
    #   RING kings lv220, WASTE bosses lv225 (+ Egyptian body/cap buckets),
    #   FOSSIL kings lv230 (+ Egyptian gauntlets/boots buckets).
    def ids(self, mats):
        return [r for _t, r in mats]

    def field(self, tier, fam_mats, i):
        kd = self.kd
        mats = self.ids(fam_mats) + TIER_MATS[tier]
        if tier == "RING":
            weapons = kd.W(kd.win(kd.WPN_210, 3 * i, 3) + kd.win(kd.WPN_215, 2 * i, 2))
            offarm = kd.S(kd.win(kd.OFF_210 + kd.OFF_215, i, 5))
        elif tier == "WASTE":
            weapons = kd.W(kd.win(kd.WPN_220_ALL, 3 * i, 3) + kd.win(kd.WPN_225, 2 * i, 2))
            offarm = kd.S([kd.OFF_220[i % len(kd.OFF_220)], kd.OFF_225[0]]) + kd.win(EGYPT, 3 * i, 3)
        else:
            weapons = kd.W(kd.win(kd.WPN_225, 2 * i, 2) + kd.win(kd.WPN_230, 3 * i, 3))
            offarm = kd.S([kd.OFF_225[0], kd.OFF_230[i % len(kd.OFF_230)]]) + kd.win(EGYPT, 3 * i, 3)
        return kd.field_table(mats, kd.USE_FIELD, weapons, offarm,
                              kd.J(kd.win(kd.JEWEL_ORO, 5 * i, 5)),
                              kd.G(kd.win(kd.GEM_ORO, 4 * i, 4)), gear="oro")

    def boss(self, tier, fam_mats, i):
        kd = self.kd
        mats = self.ids(fam_mats) + TIER_MATS[tier][-2:]
        if tier == "RING":
            top = kd.W(kd.win(kd.WPN_220_ALL, 6 * i, 6))
            armour = []
            lower = kd.W(kd.win(kd.WPN_215, 5 * i, 10)) + kd.S(kd.OFF_215 + kd.OFF_220[:4])
        elif tier == "WASTE":
            top = kd.W(kd.win(kd.WPN_225, 6 * i, 6))
            armour = [[(T_BODY, b) for b in EGYPT_BY[T_BODY]], [(T_CAP, c) for c in EGYPT_BY[T_CAP]]]
            lower = kd.W(kd.win(kd.WPN_220_ALL, 5 * i, 10)) + kd.S(kd.OFF_225 + kd.OFF_230)
        else:
            top = kd.W(kd.win(kd.WPN_230, 6 * i, 6))
            armour = [[(T_ARMS, a) for a in EGYPT_BY[T_ARMS]], [(T_FOOT, f) for f in EGYPT_BY[T_FOOT]]]
            lower = kd.W(kd.win(kd.WPN_225, 5 * i, 10)) + kd.S(kd.OFF_230 + kd.OFF_235)
        return kd.boss_table(top, armour, lower, mats, kd.USE_BOSS[0],
                             kd.J(kd.win(kd.JEWEL_ORO, 5 * i, 5)),
                             kd.G(kd.win(kd.GEM_ORO, 4 * i, 4)))

    # -- the plan -----------------------------------------------------------
    def tables(self):
        """{row: (label, cells)} for every species table and zone row."""
        out = {}
        for k, i in enumerate(self.species):
            t = gi(self.npc, i, COL_DROP_TYPE)
            if t <= 0:
                raise SystemExit(f"{i} {self.name(i)!r} has no drop table (col 18)")
            if t in out:
                raise SystemExit(f"{i} {self.name(i)!r} shares table {t} with another species")
            tier, fam = self.tier[i], self.family[i]
            fam_mats = [self.m(n) for n in FAMILY_MATS[fam]]
            common, groups = (self.boss if i in BOSSES else self.field)(tier, fam_mats, k)
            kind = "BOSS" if i in BOSSES else "field"
            out[t] = (f"{kind} {self.name(i)[:28]} lv{gi(self.npc, i, 7)} {tier}",
                      self.kd.build_row(common, groups))
        for z, tier in TIER_OF_ZONE.items():
            # a real map table since 2026-11-19: tier materials + the two Oro generics
            mats = TIER_MATS[tier] + self.ids([self.m(n) for n in GENERIC_MATS])
            common, groups = self.kd.map_table(mats, self.kd.USE_FIELD)
            out[z] = (f"zone {z} map ({tier})", self.kd.build_row(common, groups))
        return out

    def legacy(self, live_rows):
        """Zone rows that hold somebody else's table -> (users, new row)."""
        users = collections.defaultdict(list)
        for r in range(1, self.npc.rows):
            if not self.npc.get(r, 0).strip() or r in ORO_ID_RANGE:
                continue
            t = gi(self.npc, r, COL_DROP_TYPE)
            if t in TIER_OF_ZONE:
                users[t].append(r)
        moves = {}
        nxt = LEGACY_BAND
        for z in sorted(set(users) | {z for z in TIER_OF_ZONE if z in live_rows}):
            while nxt in live_rows or nxt in moves.values():
                nxt += 1
            moves[z] = nxt
            nxt += 1
        return dict(users), moves


def row_live(drop, row):
    return any(gi(drop, row, c) > 0 for c in range(1, drop.cols))


def live_rows(npc, drop):
    live = {r for r in range(drop.rows) if row_live(drop, r)}
    live |= {gi(npc, r, COL_DROP_TYPE) for r in range(1, npc.rows)
             if npc.get(r, 0).strip()} - {0}
    return live


def materials(oro):
    nat = oro.Stb(os.path.join(STB, "LIST_NATURAL.STB"))
    return {nat.get(r, 0).decode("latin-1").strip(): r
            for r in range(nat.rows) if nat.get(r, 0).strip()}


def row_cells(drop, row):
    return [drop.get(row, c).decode("latin-1") for c in range(drop.cols)]


# ---------------------------------------------------------------- simulate
def simulate(cells, money, fallback, n=300000, seed=7):
    """Get_DropITEM at level parity, WorldDROP 100, no charm/drop-rate bonus.

    `fallback` is the zone row's cells: a fifth of every roll lands there, and
    for a boss that row is field-shaped, which is what keeps a king's armour
    rate at Karkia's boss figure rather than a fifth above it.
    """
    rnd = random.Random(seed)
    counts = collections.Counter()
    for _ in range(n):
        drop_var = int((100 + DROP_RATE - (1 + rnd.randrange(100)) - 16 * 3.5 - 10) * 0.38)
        if drop_var <= 0:
            counts["nothing"] += 1
            continue
        if 1 + rnd.randrange(100) <= money:
            counts["money"] += 1
            continue
        tbl = cells if DROP_RATE - (1 + rnd.randrange(100)) >= 0 else fallback
        idx = rnd.randrange(30) if drop_var > 30 else rnd.randrange(drop_var)
        v = tbl.get(idx, 0)
        if 0 < v <= 4:
            idx = REDIRECT_BASE + v * 5 + rnd.randrange(5)
            v = tbl.get(idx, 0)
        if v <= 0:
            counts["nothing"] += 1
            continue
        t = v // 100000 if v >= 100000 else v // 1000
        counts[{T_NATURAL: "material", T_USE: "use item", T_SUBWPN: "shield",
                T_WEAPON: "weapon", 7: "jewel", 11: "gem"}.get(t, "armour")] += 1
    return {k: c / n for k, c in counts.items()}


# ---------------------------------------------------------------- apply
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--simulate", action="store_true",
                    help="print drop rates per table shape (no writes)")
    ap.add_argument("--rewrite", action="store_true",
                    help="re-plan in place: rewrite only the ITEM_DROP rows this script "
                         "already owns, keeping the sidecar's originals")
    args = ap.parse_args()

    import rosedata as oro  # the shared data layer (was import-oro.py)
    kd = load("add-karkia-drops")
    npc = oro.Stb(NPC_STB)
    drop = oro.Stb(DROP_STB)

    saved = None
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, cells in saved["drop_rows"].items():
            for c, v in enumerate(cells):
                drop.set(int(row), c, v.encode("latin-1"))
        for row, cols in saved["npc_cells"].items():
            for col, v in cols.items():
                npc.set(int(row), int(col), v.encode("latin-1"))
        with open(DROP_STB, "wb") as fh:
            fh.write(drop.to_bytes())
        with open(NPC_STB, "wb") as fh:
            fh.write(npc.to_bytes())
        os.remove(SIDECAR)
        print(f"restored {len(saved['drop_rows'])} ITEM_DROP rows and "
              f"{sum(len(c) for c in saved['npc_cells'].values())} LIST_NPC cells; sidecar removed")
        return 0

    plan = Plan(oro, kd, npc, drop, materials(oro))
    tables = plan.tables()

    if args.simulate:
        shown = set()
        for row, (label, cells) in sorted(tables.items()):
            key = (label.split()[0], label.split()[-1])
            if key in shown:
                continue
            shown.add(key)
            money = MONEY_BOSS if label.startswith("BOSS") else MONEY_FIELD
            zone = {"RING": 83, "WASTE": 78, "FOSSIL": 85}[label.split()[-1].strip("()")]
            r = simulate(cells, money, tables[zone][1])
            print(f"  {label:<44} " + "  ".join(
                f"{k} {r.get(k, 0) * 100:5.1f}%" for k in
                ("material", "use item", "shield", "weapon", "armour", "money", "nothing")))
        return 0

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- not applied")
        bad = []
        for row, (label, cells) in tables.items():
            for s in range(kd.LAST_SLOT + 1):
                if gi(drop, row, 1 + s) != cells.get(s, 0):
                    bad.append(f"row {row} slot {s}: {gi(drop, row, 1 + s)} != {cells.get(s, 0)}")
                    break
        # drops-below-oro.py (2026-11-19) owns some cells of the relocated Marsh tables
        # (a uniques bucket): compare through its manifest's recorded originals
        below = os.path.join(ROOT, "build", "drops-below-oro", "manifest.json")
        if os.path.isfile(below):
            with open(below, encoding="utf-8") as fh:
                for key, (was, now) in json.load(fh)["drop"].items():
                    r, c = (int(x) for x in key.split(","))
                    if drop.get(r, c).strip() == now.encode("latin-1"):
                        drop.set(r, c, was.encode("latin-1"))
        for z, new in saved["moved"].items():
            if row_cells(drop, int(new)) != saved["drop_rows"][z]:
                bad.append(f"relocated table {z}->{new} no longer holds its content")
            for r in saved["users"].get(z, []):
                if gi(npc, r, COL_DROP_TYPE) != int(new):
                    bad.append(f"npc {r} not repointed to {new}")
        for i in plan.species:
            want = MONEY_BOSS if i in BOSSES else MONEY_FIELD
            if gi(npc, i, COL_DROP_MONEY) != want or gi(npc, i, COL_DROP_ITEM) != DROP_RATE:
                bad.append(f"npc {i}: money/rate {gi(npc, i, COL_DROP_MONEY)}/{gi(npc, i, COL_DROP_ITEM)}")
        print(f"{len(tables)} rows planned ({len(plan.species)} species, {len(TIER_OF_ZONE)} zone rows); "
              f"{len(bad)} problem(s)" + ("\n  " + "\n  ".join(bad[:10]) if bad else ""))
        return 1 if bad else 0

    if args.rewrite:
        # A pool edit only changes table contents, so rewrite the rows in place: a
        # --restore + apply would also redo the legacy relocations and every row.
        if not saved:
            sys.exit("no sidecar -- apply first")
        changed = 0
        for row, (label, cells) in sorted(tables.items()):
            if str(row) not in saved["drop_rows"]:
                sys.exit(f"row {row} ({label}) is not one this script wrote -- "
                         "--restore and re-apply instead")
            if all(gi(drop, row, 1 + s) == cells.get(s, 0) for s in range(kd.LAST_SLOT + 1)):
                continue
            for s in range(kd.LAST_SLOT + 1):
                v = cells.get(s, 0)
                drop.set(row, 1 + s, str(v).encode("latin-1") if v else b"")
            changed += 1
            print(f"   row {row:<4} {label}")
        if changed and not args.dry_run:
            with open(DROP_STB, "wb") as fh:
                fh.write(drop.to_bytes())
        print(f"{changed} row(s) {'would be ' if args.dry_run else ''}rewritten")
        return 0

    if saved:
        print("already applied -- --restore first to re-plan (or --rewrite after a pool edit)")
        return 0

    live = live_rows(npc, drop)
    users, moves = plan.legacy(live)

    record = {"drop_rows": {}, "npc_cells": collections.defaultdict(dict),
              "moved": {str(z): n for z, n in moves.items()},
              "users": {str(z): u for z, u in users.items()}}

    def remember_row(row):
        record["drop_rows"].setdefault(str(row), row_cells(drop, row))

    def remember_npc(r, col):
        record["npc_cells"][str(r)].setdefault(str(col), npc.get(r, col).decode("latin-1"))

    print("1. legacy tables on an Oro zone number, relocated")
    for z, new in sorted(moves.items()):
        remember_row(z)
        remember_row(new)
        title = drop.get(z, 0).decode("latin-1", "replace")[:30]
        print(f"   row {z:<3} -> {new:<4} {title!r:34} {len(users.get(z, []))} user(s)")
        for c, v in enumerate([drop.get(z, c) for c in range(drop.cols)]):
            drop.set(new, c, v)
        for c in range(drop.cols):
            drop.set(z, c, b"")
        for r in users.get(z, []):
            remember_npc(r, COL_DROP_TYPE)
            npc.set(r, COL_DROP_TYPE, str(new).encode("latin-1"))

    print("2. species and zone tables")
    for row in sorted(tables):
        label, cells = tables[row]
        if row not in moves and row_live(drop, row):
            raise SystemExit(f"ITEM_DROP row {row} ({label}) already has content -- refusing to overwrite")
        remember_row(row)
        for s in range(kd.LAST_SLOT + 1):
            v = cells.get(s, 0)
            drop.set(row, 1 + s, str(v).encode("latin-1") if v else b"")
        common = sum(1 for s in cells if s < kd.COMMON_SLOTS)
        rare = sum(1 for s in cells if s >= kd.COMMON_SLOTS)
        print(f"   row {row:<4} {label:<48} {common:>2} common, {rare:>2} rare")

    print("3. money / rate columns")
    for i in plan.species:
        for col, want in ((COL_DROP_MONEY, MONEY_BOSS if i in BOSSES else MONEY_FIELD),
                          (COL_DROP_ITEM, DROP_RATE)):
            if gi(npc, i, col) != want:
                remember_npc(i, col)
                npc.set(i, col, str(want).encode("latin-1"))
    print(f"   {len(plan.species)} species: field money {MONEY_FIELD}%, boss {MONEY_BOSS}%, rate {DROP_RATE}")

    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    os.makedirs(os.path.dirname(SIDECAR), exist_ok=True)
    with open(DROP_STB, "wb") as fh:
        fh.write(drop.to_bytes())
    with open(NPC_STB, "wb") as fh:
        fh.write(npc.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(record, fh, indent=1)
    print(f"\nwritten; sidecar {os.path.relpath(SIDECAR, ROOT)}. Run --verify, then restart "
          "the servers and re-bake the VFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
