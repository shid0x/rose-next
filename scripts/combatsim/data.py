"""Game data as the game server reads it: STB tables, names, motion clips, server.toml.

Every column number here is a *game* column, the one the C++ `get_int32(row, col)`
reads; `rosefmt.Stb.i(r, c)` uses the same numbering. Each accessor names the
macro it mirrors so a reader can grep the server for it.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.dirname(HERE)
ROOT = os.path.dirname(SCRIPTS)
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)

from rosefmt import Chr, Stb, Stl, zmo  # noqa: E402

try:
    import tomllib
except ImportError:  # Python < 3.11
    tomllib = None

# datatype.h t_AbilityINDEX
AT = dict(
    STR=10, DEX=11, INT=12, CON=13, CHARM=14, SENSE=15, HP=16, MP=17, ATK=18, DEF=19,
    HIT=20, RES=21, AVOID=22, SPEED=23, ATK_SPD=24, WEIGHT=25, CRITICAL=26,
    RECOVER_HP=27, RECOVER_MP=28, SAVE_MP=29, LEVEL=31, MAX_HP=38, MAX_MP=39, MONEY=40,
    PSV_ATK_POW_NO_WEAPON=41, PSV_ATK_POW_1HAND_WPN=42, PSV_ATK_POW_2HAND_WPN=43,
    PSV_ATK_POW_BOW=44, PSV_ATK_POW_GUN=45, PSV_ATK_POW_STAFF_WAND=46,
    PSV_ATK_POW_AUTO_BOW=47, PSV_ATK_POW_KATAR_PAIR=48, PSV_ATK_SPD_BOW=49,
    PSV_ATK_SPD_GUN=50, PSV_ATK_SPD_PAIR=51, PSV_MOV_SPD=52, PSV_DEF_POW=53,
    PSV_MAX_HP=54, PSV_MAX_MP=55, PSV_RECOVER_HP=56, PSV_RECOVER_MP=57, PSV_WEIGHT=58,
    PSV_SAVE_MP=61, AFTER_PASSIVE_SKILL=71, PSV_RES=98, PSV_HIT=99, PSV_CRITICAL=100,
    PSV_AVOID=101, PSV_SHIELD_DEF=102, PSV_IMMUNITY=103, AFTER_PASSIVE_SKILL_2ND=104,
    MAX=105,
)
AT_NAME = {v: k for k, v in AT.items()}
AT_NAME.update({16: "HP", 17: "MP"})

# datatype.h eING_TYPE
ING = dict(
    INC_HP=1, INC_MP=2, POISONED=3, INC_MAX_HP=4, INC_MAX_MP=5, INC_MOV_SPD=6,
    DEC_MOV_SPD=7, INC_ATK_SPD=8, DEC_ATK_SPD=9, INC_APOWER=10, DEC_APOWER=11,
    INC_DPOWER=12, DEC_DPOWER=13, INC_RES=14, DEC_RES=15, INC_HIT=16, DEC_HIT=17,
    INC_CRITICAL=18, DEC_CRITICAL=19, INC_AVOID=20, DEC_AVOID=21, DUMB=22, SLEEP=23,
    FAINTING=24, DISGUISE=25, TRANSPARENT=26, SHIELD_DAMAGE=27, DUMMY_DAMAGE=28,
    DEC_LIFE_TIME=29, TAUNT=34,
)
ING_NAME = {v: k for k, v in ING.items()}
# what a reader calls each state (reports)
ING_LABEL = {
    ING["INC_MAX_HP"]: "Max HP", ING["INC_MAX_MP"]: "Max MP", ING["INC_MOV_SPD"]: "Move",
    ING["DEC_MOV_SPD"]: "Move down", ING["INC_ATK_SPD"]: "ASPD", ING["DEC_ATK_SPD"]: "ASPD down",
    ING["INC_APOWER"]: "ATK", ING["DEC_APOWER"]: "ATK down", ING["INC_DPOWER"]: "DEF",
    ING["DEC_DPOWER"]: "DEF down", ING["INC_RES"]: "RES", ING["DEC_RES"]: "RES down",
    ING["INC_HIT"]: "HIT", ING["DEC_HIT"]: "HIT down", ING["INC_CRITICAL"]: "CRIT",
    ING["DEC_CRITICAL"]: "CRIT down", ING["INC_AVOID"]: "AVOID", ING["DEC_AVOID"]: "AVOID down",
    ING["DUMB"]: "Silenced", ING["SLEEP"]: "Asleep", ING["FAINTING"]: "Stunned",
    ING["POISONED"]: "Poison/burn", ING["SHIELD_DAMAGE"]: "Reflect",
    ING["DUMMY_DAMAGE"]: "Extra damage", ING["TAUNT"]: "Taunted",
}

# datatype.h item types -> table
ITEM_TABLES = {
    1: "LIST_FACEITEM", 2: "LIST_CAP", 3: "LIST_BODY", 4: "LIST_ARMS", 5: "LIST_FOOT",
    6: "LIST_BACK", 7: "LIST_JEWEL", 8: "LIST_WEAPON", 9: "LIST_SUBWPN",
    10: "LIST_USEITEM", 11: "LIST_JEMITEM", 12: "LIST_NATURAL", 14: "LIST_PAT",
}

# CItem::GetItemRareType: the item types whose names take an STR_ITEMPREFIX word
PREFIXED_TABLES = ("LIST_WEAPON", "LIST_BODY", "LIST_CAP", "LIST_ARMS", "LIST_FOOT")

# Build slot -> (item type, equip index). EQUIP_IDX_* from datatype.h:302-318.
SLOTS = {
    "face": (1, 1), "cap": (2, 2), "body": (3, 3), "back": (6, 4), "arms": (4, 5),
    "foot": (5, 6), "weapon": (8, 7), "subwpn": (9, 8), "necklace": (7, 9),
    "ring": (7, 10), "earring": (7, 11),
}

JOBS = {
    0: "Visitor", 111: "Soldier", 121: "Knight", 122: "Champion",
    211: "Muse", 221: "Mage", 222: "Cleric", 311: "Hawker", 321: "Raider",
    322: "Scout", 411: "Dealer", 421: "Bourgeois", 422: "Artisan",
}

# t_eSHOT (datatype.h:409-416) by WEAPON_TYPE (citem.cpp GetShotTYPE)
SHOT_ARROW, SHOT_BULLET, SHOT_THROW = 0, 1, 2
SHOT_BY_WEAPON = {231: SHOT_ARROW, 271: SHOT_ARROW, 232: SHOT_BULLET, 253: SHOT_BULLET,
                  233: SHOT_THROW}


class GameServerConfig:
    """server.toml [game]: flat bonuses classUSER::total_* adds for players only."""

    def __init__(self, path=None):
        self.path = path
        self.base_attack_power = self.base_attack_speed = self.base_hit = 0
        self.base_move_speed = 0
        self.stun_immunity_ms = 10000
        if path and os.path.exists(path):
            self._load(path)

    def _load(self, path):
        with open(path, "rb") as f:
            raw = f.read()
        if tomllib:
            g = tomllib.loads(raw.decode("utf-8")).get("game", {})
        else:
            g, inside = {}, False
            for line in raw.decode("utf-8").splitlines():
                line = line.split("#")[0].strip()
                if line.startswith("["):
                    inside = line == "[game]"
                elif inside and "=" in line:
                    k, v = (x.strip() for x in line.split("=", 1))
                    g[k] = int(v)
        self.base_attack_power = int(g.get("base_attack_power", 0))
        self.base_attack_speed = int(g.get("base_attack_speed", 0))
        self.base_hit = int(g.get("base_hit", 0))           # -> game.base_hit_rate
        self.base_move_speed = int(g.get("base_move_speed", 0))
        # a missing key keeps the server's default (server_config.h)
        self.stun_immunity_ms = int(g.get("stun_immunity_ms", 10000))


def default_server_toml():
    for p in ("bin/release/server.toml", "Exes/server.toml", "doc/server.toml.example"):
        full = os.path.join(ROOT, p)
        if os.path.exists(full):
            return full
    return None


class Motion:
    """tagMOTION as the server loads it (io_motion.cpp LoadZMO, server branch)."""
    ATTACK_EVENTS = {21, 22, 23, 24, 25, 26, 27, 28, 10, 20, 56, 66, 57, 67}

    def __init__(self, path, fps, frames, attack_frames):
        self.path = path
        self.fps = fps
        # m_wTotalFrame-- at the end of LoadZMO: the server times swings on frames-1
        self.total = frames - 1
        self.attack_frames = attack_frames

    def need_time(self, frames, ratio):
        """tagMOTION::Get_NeedTIME(wFrame, fRatio): (DWORD)((1000*wFrame)/(fRatio*m_wFPS))."""
        return int((1000 * frames) / (ratio * self.fps))

    def pass_frame(self, ms, ratio):
        return (ratio * self.fps * ms) / 1000.0

    def duration_ms(self, ratio):
        return self.need_time(self.total, ratio)

    @classmethod
    def load(cls, path):
        if not path or not os.path.exists(path):
            return None
        with open(path, "rb") as f:
            b = f.read()
        hdr = zmo.read_header(b)
        if not hdr:
            return None
        fps, frames, _ = hdr
        _, ev = zmo.events(b)
        if ev is None:
            atk = 0            # no EZMO/3ZMO trailer: m_wTatalAttackFrame stays 0
        else:
            atk = sum(1 for e in ev if e in cls.ATTACK_EVENTS) or 1
        return cls(path, fps, frames, atk)


def _key_names(stb, stl_path):
    """row -> in-game name, through the STB column that holds the STL keys."""
    names = {}
    if not os.path.exists(stl_path):
        return names
    try:
        stl = Stl(stl_path, encoding="utf-8")
    except Exception:
        return names
    lang = 1 if len(stl.langs) > 1 else 0
    index = {k: j for j, (k, _) in enumerate(stl.keys)}
    # the key column: most cells found among the STL's keys on a sample of rows
    # (Stb.key_column regex-scans every cell, ~1.4 s on LIST_SKILL)
    sample = [r for r in range(stb.rows) if stb.occupied(r)][:400]
    kc, best = None, 0
    for c in range(stb.cols):
        hits = sum(1 for r in sample if stb.get(r, c).strip() in index)
        if hits > best:
            kc, best = c, hits
    for r in range(stb.rows):
        key = stb.get(r, kc).strip() if kc is not None else b""
        j = index.get(key)
        if j is not None:
            names[r] = stl.langs[lang][j][0].decode("utf-8", "replace").strip()
    return names


class GameData:
    """Lazy access to every table the simulator needs."""

    def __init__(self, data_dir=None, server_toml=None):
        self.data_dir = data_dir or os.path.join(ROOT, "data")
        self.stb_dir = os.path.join(self.data_dir, "3DDATA", "STB")
        if not os.path.isdir(self.stb_dir):
            raise SystemExit(f"missing {self.stb_dir} -- run with data/ present")
        self.server = GameServerConfig(server_toml or default_server_toml())
        self._stb, self._names, self._motions = {}, {}, {}
        self._chr = None

    # ------------------------------------------------------------ tables
    def stb(self, name):
        if name not in self._stb:
            self._stb[name] = Stb(os.path.join(self.stb_dir, name + ".STB"))
        return self._stb[name]

    def names(self, name):
        if name not in self._names:
            self._names[name] = _key_names(self.stb(name),
                                           os.path.join(self.stb_dir, name + "_S.STL"))
        return self._names[name]

    def name(self, table, row):
        n = self.names(table).get(row)
        if not n:
            raw = self.stb(table).get(row, 0).strip()
            try:
                n = raw.decode("utf-8")          # STB cells mix codecs (Korean is UTF-8)
            except UnicodeDecodeError:
                n = raw.decode("latin-1")
        elif table == "LIST_SKILL" and self.stb(table).get(row, 86).startswith(b"LMobSkill"):
            # a monster skill: the player-facing name plus what it does and who casts it
            # (name-monster-skills-players.py / name-monster-skills.py)
            label = self.stb(table).get(row, 0).strip().decode("utf-8", "replace")
            if label and label != n:
                n = f"{n} [{label}]"
        n = n or f"#{row}"
        if table in PREFIXED_TABLES:
            pre = self.item_prefix(table, row)
            if pre:
                n = f"{pre} {n}"
        return n

    def item_prefix(self, table, row):
        """CStringManager::GetItemName: weapons, bodies, caps, gloves and boots get the
        STR_ITEMPREFIX word whose id sits in the column before the STL key (CItem::
        GetItemRareType, col_count - 2): Demon, Dark, Golden, Elven, Saint, ..."""
        s = self.stb(table)
        pid = s.i(row, s.cols - 2)
        if pid <= 0:
            return ""
        return self.prefixes().get(pid, "")

    def prefixes(self):
        if "_prefix" not in self._names:
            out = {}
            p = os.path.join(self.stb_dir, "STR_ITEMPREFIX.STL")
            if os.path.exists(p):
                stl = Stl(p, encoding="utf-8")
                lang = 1 if len(stl.langs) > 1 else 0
                for j, (k, _) in enumerate(stl.keys):
                    if k.isdigit():
                        out[int(k)] = stl.langs[lang][j][0].decode("utf-8", "replace").strip()
            self._names["_prefix"] = out
        return self._names["_prefix"]

    def item(self, itype, row):
        return self.stb(ITEM_TABLES[itype]), row

    # ------------------------------------------------------------ items (stb.h:99-139)
    def item_i(self, itype, row, col):
        return self.stb(ITEM_TABLES[itype]).i(row, col)

    def item_quality(self, itype, row):          # ITEM_QUALITY col 8
        return self.item_i(itype, row, 8)

    def item_durability(self, itype, row):       # ITEM_DURABITY col 29
        return self.item_i(itype, row, 29)

    def item_def(self, itype, row):              # ITEM_DEFENCE col 31
        return self.item_i(itype, row, 31)

    def item_res(self, itype, row):              # ITEM_RESISTENCE col 32
        return self.item_i(itype, row, 32)

    def item_type(self, itype, row):             # ITEM_TYPE col 4
        return self.item_i(itype, row, 4)

    def item_add(self, itype, row):
        """ITEM_NEED_UNION / ITEM_ADD_DATA_TYPE / ITEM_ADD_DATA_VALUE (cols 23+3C..25+3C)."""
        return [(self.item_i(itype, row, 23 + 3 * c), self.item_i(itype, row, 24 + 3 * c),
                 self.item_i(itype, row, 25 + 3 * c)) for c in range(2)]

    def item_req_level(self, itype, row):
        for c in range(2):                       # ITEM_NEED_DATA_TYPE/VALUE 19+2C/20+2C
            if self.item_i(itype, row, 19 + 2 * c) == AT["LEVEL"]:
                return self.item_i(itype, row, 20 + 2 * c)
        return 0

    def item_req_class(self, itype, row):        # ITEM_EQUIP_REQUIRE_CLASS col 16
        return self.item_i(itype, row, 16)

    # weapons (stb.h:169-182)
    def weapon_type(self, row):
        return self.stb("LIST_WEAPON").i(row, 4)

    def weapon_motion_type(self, row):
        return self.stb("LIST_WEAPON").i(row, 34)

    def weapon_atk(self, row):
        return self.stb("LIST_WEAPON").i(row, 35)

    def weapon_speed(self, row):
        return self.stb("LIST_WEAPON").i(row, 36)

    def weapon_is_magic(self, row):
        return self.stb("LIST_WEAPON").i(row, 37)

    def weapon_range(self, row):
        return self.stb("LIST_WEAPON").i(row, 33)

    # gems / options (stb.h:235-246)
    def gem_add(self, row):
        s = self.stb("LIST_JEMITEM")
        return [(s.i(row, 16 + 2 * c), s.i(row, 17 + 2 * c)) for c in range(2)]

    # refine (LIST_GRADE: ATK HIT DEF RES AVOID)
    def grade(self, g, col):
        return self.stb("LIST_GRADE").i(g, col)

    # ------------------------------------------------------------ skills (io_skill.h)
    def sk(self, row, col):
        return self.stb("LIST_SKILL").i(row, col)

    def skill(self, row):
        return Skill(self, row)

    # ------------------------------------------------------------ status (stb.h:561-590)
    def state_type(self, row):
        return self.stb("LIST_STATUS").i(row, 1)

    def state_can_dup(self, row):
        return self.stb("LIST_STATUS").i(row, 2)

    def state_harmful(self, row):                # STATE_PRIFITS_LOSSES
        return self.stb("LIST_STATUS").i(row, 3)

    def state_apply(self, row, t):               # STATE_APPLY_ING_STB / _VALUE
        s = self.stb("LIST_STATUS")
        return s.i(row, 5 + 2 * t), s.i(row, 6 + 2 * t)

    # ------------------------------------------------------------ NPCs (stb.h:253-317)
    def npc(self, row):
        return NpcRow(self, row)

    # ------------------------------------------------------------ motions
    def _path(self, p):
        p = p.decode("latin-1") if isinstance(p, bytes) else p
        return os.path.join(self.data_dir, p.replace("\\", "/"))

    def motion(self, path):
        if path not in self._motions:
            self._motions[path] = Motion.load(self._path(path)) if path else None
        return self._motions[path]

    def file_motion(self, weapon_motion_type, action, female=False):
        """FILE_MOTION(WEAPON, ACTION) = TYPE_MOTION[action][weapon] -> FILE_MOTION row.

        CMotionLIST::IDX_GetMOTION: a female clip (column 1) falls back to the male one.
        """
        if not action:
            return None
        idx = self.stb("TYPE_MOTION").i(action, weapon_motion_type)
        if not idx:
            return None
        fm = self.stb("FILE_MOTION")
        if female:
            m = self.motion(fm.s(idx, 1).strip())
            if m:
                return m
        return self.motion(fm.s(idx, 0).strip())

    def chr(self):
        if self._chr is None:
            self._chr = Chr(os.path.join(self.data_dir, "3DDATA", "NPC", "LIST_NPC.CHR"))
        return self._chr

    def npc_motion(self, npc_row, slot):
        """CCharDATA: anim slot -> LIST_NPC.CHR motion path; None when the slot is unset."""
        c = self.chr()
        if npc_row >= len(c.chars) or not c.chars[npc_row]:
            return None
        for s, idx in c.chars[npc_row]["anims"]:
            if s == slot and idx < len(c.motions):
                return self.motion(c.motions[idx].decode("latin-1"))
        return None


class Skill:
    """One LIST_SKILL row (one skill *level*); macros from io_skill.h."""

    def __init__(self, gd, row):
        self.gd, self.row = gd, row
        g = lambda c: gd.sk(row, c)  # noqa: E731
        self.name = gd.name("LIST_SKILL", row)
        self.first = g(1)                    # SKILL_1LEV_INDEX
        self.level = g(2)                    # SKILL_LEVEL
        self.type = g(5)                     # SKILL_TYPE
        self.distance = g(6)
        self.target_filter = g(7)
        self.scope = g(8)
        self.power = g(9)
        self.harm = g(10)
        self.states = [g(11), g(12)]         # SKILL_STATE_STB
        self.success = g(13)                 # SKILL_SUCCESS_RATIO
        self.duration = g(14)                # seconds
        self.damage_type = g(15)
        self.use = [(g(16), g(17)), (g(18), g(19))]   # SKILL_USE_PROPERTY / VALUE
        self.reload = g(20)                  # x 0.2 s
        self.ability = [(g(21 + 3 * t), g(22 + 3 * t), g(23 + 3 * t)) for t in range(2)]
        self.action_mode = g(29)
        self.need_weapon = [g(30 + t) for t in range(5)]
        self.crit_roll = g(49)               # SKILL_CRITICAL_ROLL (critical burst)
        self.crit_rate = g(50)               # SKILL_CRITICAL_RATE, % of the damage
        self.ani_casting = g(52)
        self.ani_casting_speed = g(53)
        self.ani_repeat = g(54)
        self.ani_repeat_cnt = g(55) if g(54) >= 1 else 0
        self.ani_action = g(68)
        self.ani_action_speed = g(69)

    @property
    def reload_ms(self):
        # io_skill.cpp: m_pReloadTIME = SKILL_RELOAD_TIME * 200 - 100 (DWORD)
        return max(0, self.reload * 200 - 100)

    def mp_cost(self, rate_use_mp):
        """Skill_ToUseAbilityVALUE for AT_MP: (int)(value * GetCur_RateUseMP())."""
        return sum(int(v * rate_use_mp) for p, v in self.use if p == AT["MP"])

    def hp_cost(self):
        return sum(v for p, v in self.use if p == AT["HP"])

    def action_speed(self):
        s = self.ani_action_speed / 100.0
        return s if s > 0 else 1.0

    def casting_speed(self):
        """SkillLIST::m_pCastingAniSPEED (io_skill.cpp:59-108), repeat folded in."""
        gd = self.gd
        if self.ani_repeat_cnt:
            tm = gd.stb("TYPE_MOTION")
            mtype = next((m for m in range(tm.cols) if tm.i(self.ani_repeat, m)), 0)
            rep = gd.file_motion(mtype, self.ani_repeat)
            cast = gd.file_motion(mtype, self.ani_casting)
            if rep and cast:
                fspeed = self.ani_casting_speed / 100.0
                tot = rep.need_time(rep.total * self.ani_repeat_cnt, fspeed)
                tot += cast.need_time(cast.total, fspeed)
                s = (1000.0 * cast.total) / (tot * cast.fps) if tot else 0
                return s if s > 0 else 1.0
        s = self.ani_casting_speed / 100.0
        return s if s > 0 else 1.0

    def is_damage(self):
        return self.type in (3, 4, 5, 6, 7, 17, 19) and self.damage_type is not None

    def is_passive(self):
        return self.type == 15

    def restores(self):
        """"hp" / "mp" for a heal or mana skill a player casts on itself -- type 10 (self) or
        11 (a friend, so itself too) with no state, only an instant AT_HP / AT_MP gain
        (Healing, Cure, Mana Blood) -- else None."""
        if self.type not in (10, 11) or any(self.states):
            return None
        for ab, flat, rate in self.ability:
            if flat or rate:
                return {16: "hp", 17: "mp"}.get(ab)
        return None


class NpcRow:
    def __init__(self, gd, row):
        s = gd.stb("LIST_NPC")
        self.row = row
        self.name = gd.name("LIST_NPC", row)
        self.level = max(1, s.i(row, 7))     # lib_gsmain.cpp: level < 1 -> 1
        self.hp_col = s.i(row, 8)
        self.atk = s.i(row, 9)
        self.hit = s.i(row, 10)
        self.df = s.i(row, 11)
        self.res = s.i(row, 12)
        self.avoid = s.i(row, 13)
        aspd = s.i(row, 14)
        self.aspd = aspd if aspd > 0 else 100   # lib_gsmain.cpp: <= 0 -> 100
        self.magic = s.i(row, 15)
        self.ai = s.i(row, 16)
        self.exp = s.i(row, 17)
        self.range = s.i(row, 26)
        self.scale = s.i(row, 4)


def find_rows(gd, table, query, limit=50):
    """Rows whose in-game name matches `query` (case-insensitive substring or regex)."""
    names = gd.names(table)
    s = gd.stb(table)
    out = []
    try:
        rx = re.compile(query, re.I)
    except re.error:
        rx = re.compile(re.escape(query), re.I)
    for r in range(1, s.rows):
        n = gd.name(table, r) if (names.get(r) or s.s(r, 0).strip()) else ""
        if n and rx.search(n):
            out.append((r, n))
            if len(out) >= limit:
                break
    return out
