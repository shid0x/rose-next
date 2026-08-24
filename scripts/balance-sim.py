"""Replay the server's combat math against the real STB data, to see where balance drifts.

Why this exists
---------------
`data/` is gitignored, so nothing in the repo records what the numbers actually
look like. This harness reads `data/3DDATA/STB/` directly and re-implements the
server formulas in Python, so any balance claim can be re-derived instead of
remembered. It changes nothing; it only reports.

What it ports (all from the real source, PVM branches only):
  * `src/common/calculation.cpp`      Get_SuccessRATE / Get_BasicDAMAGE /
                                      Get_MagicDAMAGE / Get_SkillDAMAGE
  * `src/sho_gameserver/src/common/cuserdata.cpp`  Cal_MaxHP / Cal_MaxMP /
                                      Cal_DEFENCE / Cal_RESIST / Cal_AvoidRATE /
                                      Cal_CRITICAL
  * `src/sho_gameserver/src/cobjavt.cpp`           Cal_ATTACK / Cal_HIT
  * `src/sho_gameserver/src/cobjnpc.cpp`           monster HP = level * NPC_HP

Player model
------------
A synthetic character per archetype: stat points from the real grant formula
(`level*0.8 + 10` per level, cost `floor(stat*0.2)` per point, MAX_STAT 300),
weapon = best ATK inside the newest tier the level unlocks, armour = best
DEF+RES obtainable at that level. No refine grades, no gems, no buffs -- a
clean, comparable baseline, deliberately a little pessimistic. Cross-check: a
level-216 Raider measured in game via `/stats` had ATK 1201; this model gives
987 at 220 without grades or masteries, so it lands about 20% low, which is the
expected gap.

Monster model
-------------
The *median* monster within +/-4 levels, after dropping the top HP quintile so
bosses and quest NPCs (which sit at millions of HP) do not distort the median.

Usage
-----
    python scripts/balance-sim.py                # every section
    python scripts/balance-sim.py --section root # one section
    python scripts/balance-sim.py --list

Sections:
    stats     synthetic player stat blocks per archetype
    mobs      median field-monster stats by level
    curve     cost of one kill, by class and level
    root      which term runs away with level (the actual root cause)
    cliff     the iSuc<20 accuracy cliff
    decay     what flat buffs and passives are worth as the level rises
    levers    candidate fixes, measured on swings-per-kill
    rotation  seconds and MP per kill with auto-attacks + one skill on cooldown
"""
import argparse
import io
import os
import random
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")


# ------------------------------------------------------------------ STB reader
class Stb:
    """STB1: "STB1" | u32 data_offset | u32 raw_rows | u32 raw_cols | header | cells.

    Game indices drop the header row and the root column, so d[r][c] is get_int32(r, c).
    """

    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] != b"STB1":
            raise SystemExit(f"{path}: not an STB1 file")
        self.data_offset, rr, rc = struct.unpack_from("<III", raw, 4)
        self.rows, self.cols = rr - 1, rc - 1
        f = io.BytesIO(raw)
        f.seek(self.data_offset)

        def cell():
            n, = struct.unpack("<H", f.read(2))
            return f.read(n)

        self.d = [[cell() for _ in range(self.cols)] for _ in range(self.rows)]

    def get(self, r, c):
        return self.d[r][c] if r < self.rows and c < self.cols else b""

    def i(self, r, c):
        v = self.get(r, c).strip()
        try:
            return int(v)
        except ValueError:
            return 0

    def s(self, r, c):
        return self.get(r, c).decode("latin-1", "replace").strip()


_TBL = {}


def tbl(name):
    if name not in _TBL:
        p = os.path.join(STB_DIR, name)
        if not os.path.exists(p):
            raise SystemExit(f"missing {p} -- run from the repo with data/ present")
        _TBL[name] = Stb(p)
    return _TBL[name]


# ------------------------------------------------------------------ combat math
# Faithful ports of src/common/calculation.cpp, server build, PVM branches.
LEVEL_GATE = 1.05  # kLevelGateScale in Get_SuccessRATE
MAX_DAMAGE = 9999  # GameStaticConfig::MAX_DAMAGE


def R(n):
    return 1 + random.randrange(n)


def success_rate(alv, ahit, dlv, davoid):
    s = (alv + 10) - dlv * LEVEL_GATE + R(50)
    if s <= 0:
        return 0
    return int(s * (ahit * 1.1 - davoid * 0.93 + R(60) + 5 + alv * 0.2) / 80.0)


def cri_success(alv, acrit):
    return int((R(100) * 3 + alv + 30) * 16 / (acrit + 70))


def basic_damage(atk, ddef, davoid, alv, acrit, suc, level_term=0.0):
    """Get_BasicDAMAGE, monster branch. level_term models a proposed change."""
    if cri_success(alv, acrit) < 20:
        d = int(atk * (suc * 0.05 + 29) * (atk - ddef + 230 + level_term)
                / ((ddef + davoid * 0.3 + 5) * 100))
        d = max(d, 10)
    else:
        d = int(atk * (suc * 0.03 + 26) * (atk - ddef + 250 + level_term)
                / ((ddef + davoid * 0.4 + 5) * 145))
        d = max(d, 5)
    return min(d, MAX_DAMAGE)


def magic_damage(atk, ddef, dres, davoid, alv, acrit, suc):
    if cri_success(alv, acrit) < 20:
        d = int(atk * (suc * 0.05 + 33) * (atk - ddef * 0.8 + 310)
                / ((dres + davoid * 0.3 + 5) * 200))
        d = max(d, 10)
    else:
        d = int(atk * (suc * 0.03 + 30) * (atk - ddef * 0.8 + 280)
                / ((dres + davoid * 0.3 + 5) * 280))
        d = max(d, 5)
    return min(d, MAX_DAMAGE)


def normal_swing(atk, alv, ahit, acrit, magic, dlv, ddef, dres, davoid, level_term=0.0):
    """Get_DAMAGE. Returns 0 for a miss.

    Note the cliff: when iSuc < 20 the swing survives only if
    `1+RANDOM(100) + (alv-dlv)*0.6 >= 94`, i.e. a flat ~7% at equal levels,
    regardless of how far below 20 iSuc actually is.
    """
    suc = success_rate(alv, ahit, dlv, davoid)
    if suc < 20 and int(R(100) + (alv - dlv) * 0.6) < 94:
        return 0
    if magic:
        return magic_damage(atk, ddef, dres, davoid, alv, acrit, suc)
    return basic_damage(atk, ddef, davoid, alv, acrit, suc, level_term)


def weapon_skill_damage(power, atk, alv, ahit, asen, dlv, ddef, dres, davoid,
                        sen_coeff=0.7):
    """Get_SkillDAMAGE case 1 (weapon skill), monster branch."""
    suc = int(((alv + 20) - dlv + R(60)) * (ahit - davoid * 0.6 + R(70) + 10) / 110.0)
    if suc < 10:
        return 0
    if suc < 20:
        d = int((power * 0.4) * (atk + 50) * (R(30) + asen * 1.2 + 340)
                / (ddef + dres + 20) / (250 + dlv - alv) + 20)
    else:
        d = int(((power + atk * 0.2) * (atk + 60) * (R(30) + asen * sen_coeff + 370))
                * 0.01 * (120 - dlv + alv)
                / (ddef + dres * 0.8 + davoid * 0.4 + 20) / 270 + 20)
    return min(max(d, 5), MAX_DAMAGE)


def magic_skill_damage(power, atk, alv, ahit, aint, asen, dlv, ddef, dres, davoid,
                       sen_coeff=0.7):
    """Get_SkillDAMAGE case 2 (magic skill), monster branch."""
    suc = int(((alv + 30) - dlv + R(50)) * (ahit - davoid * 0.56 + R(70) + 10) / 110.0)
    if suc < 8:
        return 0
    if suc < 20:
        d = int((power * (atk * 0.8 + aint + 80) * (R(30) + asen * 1.3 + 280) * 0.2)
                / (ddef * 0.3 + dres + 30) / (250 + dlv - alv) + 20)
    else:
        d = int((power * (atk * 0.8 + aint * 1.2 + 100)
                 * (R(30) + asen * sen_coeff + 350) * 0.01)
                * (150 - dlv + alv)
                / (ddef * 0.3 + dres + davoid * 0.3 + 60) / 350.0 + 20)
    return min(max(d, 5), MAX_DAMAGE)


# ------------------------------------------------------------------ monsters
NPC_COL = dict(level=7, hp=8, atk=9, hit=10, df=11, res=12, avoid=13, aspd=14, exp=17)


class Mob:
    __slots__ = ("row", "name", "lv", "hp", "atk", "hit", "df", "res", "avoid", "aspd")

    def __init__(self, s, r):
        self.row, self.name = r, s.s(r, 0)
        self.lv = s.i(r, NPC_COL["level"])
        self.hp = self.lv * s.i(r, NPC_COL["hp"])   # cobjnpc.cpp: m_iOriMaxHP
        self.atk = s.i(r, NPC_COL["atk"])
        self.hit = s.i(r, NPC_COL["hit"])
        self.df = s.i(r, NPC_COL["df"])
        self.res = s.i(r, NPC_COL["res"])
        self.avoid = s.i(r, NPC_COL["avoid"])
        self.aspd = s.i(r, NPC_COL["aspd"]) or 100


_MOBS = None


def mobs():
    global _MOBS
    if _MOBS is None:
        s = tbl("LIST_NPC.STB")
        _MOBS = [m for m in (Mob(s, r) for r in range(s.rows) if s.get(r, 0).strip())
                 if m.lv > 0 and m.hp > 0]
    return _MOBS


def field_mobs(lv, band=4):
    """Ordinary grindable monsters: drop the top HP quintile (bosses, quest NPCs)."""
    c = [m for m in mobs() if abs(m.lv - lv) <= band and m.atk > 0]
    if len(c) < 5:
        return c
    cut = sorted(m.hp for m in c)[int(len(c) * 0.80)]
    return [m for m in c if m.hp <= cut] or c


class MedianMob:
    def __init__(self, lv):
        c = field_mobs(lv)
        self.lv, self.n = lv, len(c)

        def med(f):
            return int(statistics.median([f(m) for m in c])) if c else 0

        self.hp = med(lambda m: m.hp)
        self.atk = med(lambda m: m.atk)
        self.hit = med(lambda m: m.hit)
        self.df = med(lambda m: m.df)
        self.res = med(lambda m: m.res)
        self.avoid = med(lambda m: m.avoid)
        self.aspd = med(lambda m: m.aspd) or 100
        self.crit = int(lv * 0.6)   # cobjnpc.cpp: m_nCritical


# ------------------------------------------------------------------ gear
AT_LEVEL = 31
# Event / GM gear has req-0 rows with endgame stats and would wreck the model.
JUNK = ("GM ", "GameMaster", "Game Master", "Christmas", "Wreath", "Test")


def _req_level(s, r):
    for c in range(2):
        if s.i(r, 19 + c * 2) == AT_LEVEL:
            return s.i(r, 20 + c * 2)
    return 0


def _junk(name):
    return any(p in name for p in JUNK)


_WPN, _ARM = {}, {}


def best_weapon(lv, wtypes):
    """Best ATK inside the newest tier the level unlocks -- players upgrade weapons,
    and picking purely by ATK selects low-quality outliers that wreck HIT."""
    k = (lv, wtypes)
    if k in _WPN:
        return _WPN[k]
    s = tbl("LIST_WEAPON.STB")
    c = []
    for r in range(1, s.rows):
        if not s.get(r, 0).strip() or s.i(r, 4) not in wtypes:
            continue
        rl = _req_level(s, r)
        if rl > lv or _junk(s.s(r, 0)) or s.i(r, 35) <= 0:
            continue
        c.append(dict(name=s.s(r, 0), atk=s.i(r, 35), spd=s.i(r, 36), q=s.i(r, 8),
                      dur=s.i(r, 29), wtype=s.i(r, 4), magic=s.i(r, 37), rl=rl))
    if not c:
        _WPN[k] = None
        return None
    top = max(x["rl"] for x in c)
    tier = [x for x in c if x["rl"] >= top - 10]
    _WPN[k] = max(tier, key=lambda x: x["atk"] * 2 + x["q"])
    return _WPN[k]


def best_armor(name, lv):
    """Best DEF+RES obtainable at this level -- players wear the best piece they own."""
    k = (name, lv)
    if k in _ARM:
        return _ARM[k]
    s = tbl(name)
    best = None
    for r in range(1, s.rows):
        if not s.get(r, 0).strip() or _req_level(s, r) > lv or _junk(s.s(r, 0)):
            continue
        d, res = s.i(r, 31), s.i(r, 32)
        if d <= 0 and res <= 0:
            continue
        x = dict(name=s.s(r, 0), df=d, res=res, dur=s.i(r, 29))
        if best is None or x["df"] + x["res"] > best["df"] + best["res"]:
            best = x
    _ARM[k] = best
    return best


ARMOR_SLOTS = ["LIST_CAP.STB", "LIST_BODY.STB", "LIST_ARMS.STB",
               "LIST_FOOT.STB", "LIST_BACK.STB"]


# ------------------------------------------------------------------ player
def total_bp(L):
    """gs_user.cpp Add_EXP: AddCur_BonusPOINT(level*0.8 + 10) on each level-up."""
    return sum(int(l * 0.8) + 10 for l in range(2, L + 1))


BASE_STATS = dict(STR=15, DEX=15, INT=15, CON=15, CHA=10, SEN=10)  # INIT_AVATAR.STB
MAX_STAT = 300  # GameStaticConfig::MAX_STAT


def allocate(L, weights):
    """Spend bonus points proportionally. Cost of +1 is floor(stat*0.2) (cuserdata.h)."""
    st = dict(BASE_STATS)
    bp = total_bp(L)
    order = [k for k, w in weights.items() if w > 0]
    tot = sum(weights[k] for k in order)
    while bp > 0:
        gained = {k: st[k] - BASE_STATS[k] for k in order}
        g = sum(gained.values()) or 1
        pick, worst = None, None
        for k in order:
            if st[k] >= MAX_STAT:
                continue
            deficit = weights[k] / tot - gained[k] / g
            if worst is None or deficit > worst:
                pick, worst = k, deficit
        if pick is None:
            break
        cost = int(st[pick] * 0.2)
        if cost > bp:
            break
        st[pick] += 1
        bp -= cost
    return st


HP_TAB = {111: (7, 12), 121: (-3, 14), 122: (2, 13), 211: (11, 10), 221: (11, 10),
          222: (5, 11), 311: (10, 11), 321: (2, 13), 322: (11, 11), 411: (12, 10),
          421: (13, 10), 422: (6, 11)}
MP_TAB = {111: (3, 4.0), 121: (0, 4.5), 122: (-6, 5.0), 211: (0, 6.0), 221: (-7, 7.0),
          222: (-4, 6.5), 311: (4, 4.0), 321: (4, 4.0), 322: (0, 4.5), 411: (3, 4.0),
          421: (3, 4.0), 422: (0, 4.5)}


class Player:
    def __init__(self, L, job, weights, wtypes, shield=False, buffs=None):
        b = buffs or {}
        self.L, self.job, self.st = L, job, allocate(L, weights)
        self.w = best_weapon(L, wtypes)
        gear = [best_armor(t, L) for t in ARMOR_SLOTS]
        if shield:
            gear.append(best_armor("LIST_SUBWPN.STB", L))
        gear = [x for x in gear if x]

        STR, DEX, INT, CON, SEN = (self.st[k] for k in ("STR", "DEX", "INT", "CON", "SEN"))
        A, M1 = HP_TAB[job]
        Am, M1m = MP_TAB[job]
        hp = (L + A) * M1 + STR * 2
        self.maxhp = hp + b.get("MAXHP", 0) + int(hp * b.get("MAXHP_R", 0) / 100)
        self.maxmp = int((L + Am) * M1m + INT * 4)

        tdef = sum(x["df"] for x in gear)
        tres = sum(x["res"] for x in gear)
        tdur = sum(x["dur"] for x in gear)
        df = int(tdef + (STR + 5) * 0.35 + (L + 15) * 0.7)
        self.df = df + b.get("DEF", 0) + int(df * b.get("DEF_R", 0) / 100)
        res = int(tres + (INT + 5) * 0.6 + (L + 15) * 0.8)
        self.res = res + b.get("RES", 0) + int(res * b.get("RES_R", 0) / 100)
        self.avoid = int((DEX * 1.9 + L * 0.3 + 10) * 0.4) + int(tdur * 0.3) + b.get("AVOID", 0)
        self.crit = int(SEN + (CON + 20) * 0.2) + b.get("CRIT", 0)

        wa = self.w["atk"] if self.w else 0
        wt = self.w["wtype"] if self.w else 0
        self.magic = bool(self.w and self.w["magic"])
        if wt in (211, 212, 221, 222, 223):
            ap = STR * 0.75 + L * 0.2 + wa * (STR * 0.05 + 29) / 30.0
        elif wt == 241:
            ap = STR * 0.4 + INT * 0.4 + L * 0.2 + wa * (INT * 0.05 + 29) / 30.0
        elif wt == 242:
            ap = INT * 0.6 + L * 0.2 + wa * (SEN * 0.1 + 26) / 27.0
        elif wt == 252:
            ap = STR * 0.63 + DEX * 0.45 + L * 0.2 + wa * (DEX * 0.05 + 25) / 26.0
        elif wt in (251, 253):
            ap = STR * 0.42 + DEX * 0.55 + L * 0.2 + wa * (DEX * 0.05 + 20) / 21.0
        elif wt in (231, 232, 233):
            ap = DEX * 0.62 + STR * 0.2 + L * 0.2 + (wa + 8) * (DEX * 0.04 + SEN * 0.03 + 29) / 30.0
        elif wt in (271, 272):
            ap = DEX * 0.4 + CON * 0.5 + L * 0.2 + (wa + 8) * (CON * 0.03 + SEN * 0.05 + 29) / 30.0
        else:
            ap = STR * 0.5 + DEX * 0.3 + L * 0.2
        ap = int(ap)
        self.atk = ap + b.get("ATK", 0) + int(ap * b.get("ATK_R", 0) / 100)

        wq = self.w["q"] if self.w else 0
        wd = self.w["dur"] if self.w else 0
        self.hit = int((CON + 10) * 0.8) + int(wq * 0.6 + wd * 0.8) + b.get("HIT", 0)
        self.aspd = int(1500.0 / (self.w["spd"] if self.w and self.w["spd"] else 15)) \
            + b.get("ASPD", 0)
        self.INT, self.SEN = INT, SEN


PROFILES = {
    "Knight":    dict(job=121, w=dict(STR=5, CON=3, DEX=1, SEN=1), wt=(211, 212), shield=True),
    "Champion":  dict(job=122, w=dict(STR=5, CON=3, SEN=2), wt=(221, 222, 223)),
    "Raider":    dict(job=321, w=dict(STR=3, DEX=4, SEN=3), wt=(251, 252, 253)),
    "Scout":     dict(job=322, w=dict(DEX=5, SEN=3, CON=2), wt=(231, 232, 233)),
    "Mage":      dict(job=221, w=dict(INT=5, SEN=3, CON=2), wt=(241, 242)),
    "Bourgeois": dict(job=421, w=dict(CON=4, DEX=3, SEN=3), wt=(271, 272)),
}


def make(prof, L, buffs=None):
    p = PROFILES[prof]
    return Player(L, p["job"], p["w"], p["wt"], p.get("shield", False), buffs)


# ------------------------------------------------------------------ measurement
TRIALS = 6000


def measure(pl, mob, level_term=0.0):
    """(land rate, damage per landed swing, damage per swing attempt)."""
    hits = dmg = 0
    for _ in range(TRIALS):
        d = normal_swing(pl.atk, pl.L, pl.hit, pl.crit, pl.magic,
                         mob.lv, mob.df, mob.res, mob.avoid, level_term)
        if d:
            hits += 1
            dmg += d
    return hits / TRIALS, (dmg / hits if hits else 0), dmg / TRIALS


def measure_incoming(pl, mob, atk_scale=1.0):
    hits = dmg = 0
    a = int(mob.atk * atk_scale)
    for _ in range(TRIALS):
        d = normal_swing(a, mob.lv, mob.hit, int(mob.lv * 0.6), False,
                         pl.L, pl.df, pl.res, pl.avoid)
        if d:
            hits += 1
            dmg += d
    return hits / TRIALS, (dmg / hits if hits else 0), dmg / TRIALS


def measure_skill(pl, mob, power, sen_coeff=0.7, power_mult=1.0):
    tot = 0
    for _ in range(TRIALS):
        tot += weapon_skill_damage(power * power_mult, pl.atk, pl.L, pl.hit, pl.SEN,
                                   mob.lv, mob.df, mob.res, mob.avoid, sen_coeff)
    return tot / TRIALS


LEVELS = [20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220]
PROFS = ("Knight", "Champion", "Raider", "Scout", "Mage", "Bourgeois")


# ------------------------------------------------------------------ sections
def sec_stats():
    print("Synthetic player stat blocks (no refine grades, gems, passives or buffs).\n")
    for prof in PROFS:
        print(f"--- {prof}")
        for L in (30, 60, 100, 140, 180, 220, 240):
            p = make(prof, L)
            w = p.w["name"][:22] if p.w else "-"
            print(f"  L{L:>3} HP{p.maxhp:>6} MP{p.maxmp:>6} ATK{p.atk:>5} DEF{p.df:>5} "
                  f"RES{p.res:>5} HIT{p.hit:>4} AVO{p.avoid:>4} CRT{p.crit:>4} "
                  f"ASPD{p.aspd:>4}  {w}")
        print()


def sec_mobs():
    print("Median field monster by level (top HP quintile dropped: bosses/quest NPCs).\n")
    print(f"{'lv':>4}{'n':>5}{'HP':>9}{'ATK':>7}{'HIT':>6}{'DEF':>6}{'RES':>6}"
          f"{'AVOID':>7}{'ASPD':>6}{'HPcol':>7}")
    for L in range(20, 246, 10):
        m = MedianMob(L)
        if not m.n:
            continue
        print(f"{L:>4}{m.n:>5}{m.hp:>9}{m.atk:>7}{m.hit:>6}{m.df:>6}{m.res:>6}"
              f"{m.avoid:>7}{m.aspd:>6}{m.hp // max(L, 1):>7}")


def sec_curve():
    print("COST of one kill = (swings I need to kill it) / (hits it needs to kill me).")
    print("1.00 means a solo 1v1 exactly trades your whole HP bar for one monster.\n")
    print(f"{'lv':>5}" + "".join(f"{p:>11}" for p in PROFS))
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        line = f"{L:>5}"
        for prof in PROFS:
            pl = make(prof, L)
            _, _, out = measure(pl, mob)
            _, _, inc = measure_incoming(pl, mob)
            sw = mob.hp / out if out else float("inf")
            mh = pl.maxhp / inc if inc else float("inf")
            line += f"{sw / mh:>11.2f}"
        print(line)

    print("\nHow much of every swing lands:")
    print(f"{'lv':>5}" + "".join(f"{p:>11}" for p in PROFS) + f"{'mobAVOID':>10}")
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        line = f"{L:>5}"
        for prof in PROFS:
            hr, _, _ = measure(make(prof, L), mob)
            line += f"{hr * 100:>10.0f}%"
        print(line + f"{mob.avoid:>10}")


def sec_root():
    L0, L1 = 20, 220
    m0, m1 = MedianMob(L0), MedianMob(L1)
    p0, p1 = make("Champion", L0), make("Champion", L1)
    _, od0, avg0 = measure(p0, m0)
    _, od1, avg1 = measure(p1, m1)
    print(f"Growth from level {L0} to level {L1} (Champion vs median field monster)\n")

    def g(label, a, b):
        print(f"  {label:<34}{a:>10.0f} ->{b:>10.0f}   x{(b / a if a else 0):>6.1f}")

    g("monster HP", m0.hp, m1.hp)
    g("monster DEF", m0.df, m1.df)
    g("monster ATK", m0.atk, m1.atk)
    g("player ATK", p0.atk, p1.atk)
    g("player DEF", p0.df, p1.df)
    g("player max HP", p0.maxhp, p1.maxhp)
    g("player damage per landed swing", od0, od1)
    g("=> swings per kill", m0.hp / avg0, m1.hp / avg1)

    print("\nWhy per-swing damage is flat: Get_BasicDAMAGE is nearly scale-invariant.")
    print("  dmg = ATK*(suc*0.03+26)*(ATK - DEF + 250) / ((DEF + AVOID*0.4 + 5)*145)")
    print("Scale ATK, DEF and AVOID together and the ratios cancel; only the +250")
    print("constant survives, so inflating both sides of the arms race does nothing.\n")
    print(f"{'scale':>7}{'ATK':>7}{'DEF':>7}{'AVOID':>7}{'damage':>9}")
    for k in (1, 2, 4, 8, 16):
        a, d, v = 80 * k, 55 * k, 34 * k
        dmg = a * (60 * 0.03 + 26) * (a - d + 250) / ((d + v * 0.4 + 5) * 145)
        print(f"{k:>6}x{a:>7}{d:>7}{v:>7}{dmg:>9.0f}")

    print("\nSwings and skill casts needed per kill (Champion, Champion Hit power 330):")
    print(f"{'lv':>5}{'mobHP':>8}{'auto dmg':>10}{'swings':>9}{'skill dmg':>11}{'casts':>8}")
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        _, _, out = measure(pl, mob)
        sk = measure_skill(pl, mob, 330)
        print(f"{L:>5}{mob.hp:>8}{out:>10.0f}{mob.hp / out:>9.1f}{sk:>11.0f}"
              f"{mob.hp / sk:>8.1f}")


def sec_cliff():
    print("Get_SuccessRATE returns iSuc. When iSuc < 20 the swing survives only if")
    print("`1+RANDOM(100) + (alv-dlv)*0.6 >= 94` -- a flat ~7% at equal levels, no")
    print("matter how far below 20 iSuc is. Accuracy is a cliff, not a slope.\n")
    print("Sampled at level 160 against a monster with AVOID 260:")
    print(f"{'player HIT':>11}{'mean iSuc':>11}{'lands':>9}")
    for hit in range(80, 461, 40):
        lands, sucs = 0, []
        for _ in range(20000):
            s = success_rate(160, hit, 160, 260)
            sucs.append(s)
            if s < 20 and int(R(100)) < 94:
                continue
            lands += 1
        print(f"{hit:>11}{statistics.mean(sucs):>11.1f}{lands / 200:>8.1f}%")


def sec_decay():
    print("Every buff and passive in LIST_SKILL.STB carries its effect in the FLAT")
    print("column; the RATE column is 0 everywhere. Flat numbers were sized for a")
    print("level-100 cap, so their real value decays as the character grows.\n")
    print("Get_SkillAdjustVALUE: value = target_stat*RATE/100 + FLAT*(casterINT+300)/315")
    print("so INT only ever multiplies the flat part, and caps out near 1.9x.\n")
    print(f"{'lv':>5}{'ChampATK':>10}{'PowerSup +70':>14}{'+70 @INT300':>13}"
          f"{'Berserk +100':>14}{'2H Mastery +110':>17}")
    for L in (40, 60, 100, 140, 180, 220, 240):
        p = make("Champion", L)
        print(f"{L:>5}{p.atk:>10}{70 / p.atk * 100:>13.1f}%"
              f"{70 * 600 / 315 / p.atk * 100:>12.1f}%{100 / p.atk * 100:>13.1f}%"
              f"{110 / p.atk * 100:>16.1f}%")
    print()
    print(f"{'lv':>5}{'ChampDEF':>10}{'BlessArmor +210':>17}{'ArmorMastery +100':>19}"
          f"{'  ChampHP':>10}{'HardenBody +800':>17}")
    for L in (40, 60, 100, 140, 180, 220, 240):
        p = make("Champion", L)
        print(f"{L:>5}{p.df:>10}{210 / p.df * 100:>16.1f}%{100 / p.df * 100:>18.1f}%"
              f"{p.maxhp:>10}{800 / p.maxhp * 100:>16.1f}%")


def _swings(pl, mob, level_term=0.0, hp_scale=1.0, aspd_bonus=0.0):
    _, _, out = measure(pl, mob, level_term)
    if out <= 0:
        return float("inf")
    return mob.hp * hp_scale / out / (1.0 + aspd_bonus)


def sec_levers():
    print("Candidate fixes, measured on the axis that actually drifted: swings per kill.")
    print("Multiplier > 1 means shorter fights. Watch the SHAPE, not just the size:")
    print("a good fix does little at level 40 and a lot at level 220.\n")
    cols = [("mob HP x0.6 @200+", "hp"), ("dmg const +4*lv", "lt4"),
            ("dmg const +8*lv", "lt8"), ("+20% ATK", "atk20"),
            ("+40% ATK", "atk40"), ("+30% atk speed", "aspd")]
    print(f"{'lv':>4}{'baseline':>10}" + "".join(f"{c[0]:>19}" for c in cols))
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        base = _swings(pl, mob)
        row = f"{L:>4}{base:>10.1f}"
        for _, kind in cols:
            if kind == "hp":
                v = _swings(pl, mob, hp_scale=0.6 if L >= 200 else 1.0)
            elif kind == "lt4":
                v = _swings(pl, mob, level_term=4.0 * L)
            elif kind == "lt8":
                v = _swings(pl, mob, level_term=8.0 * L)
            elif kind == "atk20":
                v = _swings(make("Champion", L, dict(ATK_R=20)), mob)
            elif kind == "atk40":
                v = _swings(make("Champion", L, dict(ATK_R=40)), mob)
            else:
                v = _swings(pl, mob, aspd_bonus=0.30)
            row += f"{base / v:>18.2f}x"
        print(row)

    print("\nRaising raw skill POWER is the one lever whose shape runs BACKWARDS:")
    print("`(POWER + ATK*0.2)` means a growing ATK dilutes POWER, so the same")
    print("multiplier buys less at high level than at low level.")
    print(f"{'lv':>5}" + "".join(f"{'pow x' + str(m):>11}" for m in (1.0, 1.5, 2.0, 3.0)))
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        base = measure_skill(pl, mob, 330)
        print(f"{L:>5}" + "".join(
            f"{measure_skill(pl, mob, 330, power_mult=m) / base:>10.2f}x"
            for m in (1.0, 1.5, 2.0, 3.0)))

    print("\nRaising the SEN coefficient in Get_SkillDAMAGE has the RIGHT shape --")
    print("small at low level, larger at high level -- but a modest ceiling, because")
    print("SEN sits next to a constant 370 that dilutes it.")
    print(f"{'lv':>5}{'SEN':>6}" + "".join(f"{'SENx' + str(c):>11}" for c in (0.7, 1.4, 2.1, 3.0)))
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        base = measure_skill(pl, mob, 330, sen_coeff=0.7)
        print(f"{L:>5}{pl.SEN:>6}" + "".join(
            f"{measure_skill(pl, mob, 330, sen_coeff=c) / base:>10.2f}x"
            for c in (0.7, 1.4, 2.1, 3.0)))

    print("\nWhy % ATK is so strong at endgame: (ATK - DEF + 250) is nearly cancelled,")
    print("so a small relative ATK gain is a large relative damage gain -- and the same")
    print("sensitivity makes endgame damage fragile to small monster-DEF changes.")
    print(f"{'lv':>5}{'pATK':>7}{'mDEF':>7}{'ATK-DEF+250':>14}{'as % of ATK':>14}")
    for L in LEVELS:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        n = pl.atk - mob.df + 250
        print(f"{L:>5}{pl.atk:>7}{mob.df:>7}{n:>14}{n / pl.atk * 100:>13.0f}%")


BASE_SWING_S = 1.4  # nominal 1H/2H swing animation at attack-speed 100


def sec_rotation():
    print("Seconds and MP per kill with a realistic rotation: auto-attack continuously,")
    print(f"fire one skill whenever it is off cooldown. Swing interval assumed to be")
    print(f"{BASE_SWING_S}s at attack-speed 100 (the client sets ani speed = aspd/100);")
    print("SKILL_RELOAD_TIME is column x 0.2 s (io_skill.cpp).\n")
    power, cd, mp_cost = 330, 78 * 0.2, 110   # Champion Hit rank 10
    print("Champion / Champion Hit: power 330, 15.6 s cooldown, 110 MP\n")
    print(f"{'lv':>5}{'mobHP':>9}{'auto':>7}{'skill':>8}{'swing s':>9}"
          f"{'SEC/KILL':>10}{'MP/kill':>9}{'MP pool':>9}{'bars':>7}")
    for L in LEVELS[1:]:
        mob = MedianMob(L)
        if not mob.n:
            continue
        pl = make("Champion", L)
        _, _, auto = measure(pl, mob)
        sk = measure_skill(pl, mob, power)
        interval = BASE_SWING_S / (pl.aspd / 100.0)
        hp, t, mp, nxt = mob.hp, 0.0, 0, 0.0
        while hp > 0 and t < 100000:
            if t >= nxt:
                hp -= sk
                mp += mp_cost
                nxt = t + cd
            hp -= auto
            t += interval
        print(f"{L:>5}{mob.hp:>9}{auto:>7.0f}{sk:>8.0f}{interval:>9.2f}"
              f"{t:>10.1f}{mp:>9}{pl.maxmp:>9}{mp / pl.maxmp:>7.2f}")
    print("\nStanding MP regen is (RecoverMP + (CON+40)/6)/6 per 2 s -- CON only, not")
    print("level and not max MP -- so at endgame the MP bill is paid by sitting.")


SECTIONS = {
    "stats": sec_stats, "mobs": sec_mobs, "curve": sec_curve, "root": sec_root,
    "cliff": sec_cliff, "decay": sec_decay, "levers": sec_levers,
    "rotation": sec_rotation,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--section", action="append", choices=sorted(SECTIONS),
                    help="run only this section (repeatable)")
    ap.add_argument("--list", action="store_true", help="list sections and exit")
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()
    if a.list:
        for k in SECTIONS:
            print(k)
        return
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass
    random.seed(a.seed)
    for name in (a.section or list(SECTIONS)):
        print("=" * 78)
        print(f"== {name}")
        print("=" * 78)
        SECTIONS[name]()
        print()


if __name__ == "__main__":
    main()
