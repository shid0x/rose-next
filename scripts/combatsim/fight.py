"""One fight, tick by tick, the way a zone thread runs it.

Timing is a literal port of `CObjCHAR::Proc` (cobjchar.cpp:2054): every tick each
actor advances its current motion; when a motion completes inside the tick, the
next action starts at once and the leftover milliseconds carry into it. A normal
swing applies its damage when the attack motion *starts* (`Start_ATTACK` ->
`Attack_START`), multiplied by the clip's attack-frame count. A skill pays its MP
when the cast motion starts and lands at frame 0 of the action motion
(`CObjAI::Do_SKILL`).

The monster runs its AI file (ai.py) the way CObjMOB does: the DAMAGED pattern on
every player hit or miss (and every harmful status), the STOP pattern on its timer
while it stands idle. A skill it orders is cast when its current motion ends (cast
clip = LIST_NPC.CHR slot nMotion, action clip = slot nMotion+1); afterwards it
attacks again, restores its previous command or stops, by SKILL_ACTION_MODE.

The player's summons (skill type 14) are the same machine on the player's side (Mob
with an owner): their own AI file, stats scaled by CObjSUMMON::SetCallerOBJ, out when
the fight starts and called again when they die. The monster's AI can turn on them.
"""
import random

from . import formulas as F
from .ai import CREATED, DAMAGED, HARMLESS_ACTS, STOP, Brain
from .data import AT, ING

TICK_MS = 100                 # gs_threadzone.cpp: _FRAME_PER_SECOND 10
RECOVERY_MS = 2000            # GameStaticConfig::RECOVERY_RATE_TIME
# Any two skills: the server refuses a cast within SKILL_DELAY_TIME (gs_user.cpp, 500 ms)
# of the last, and the client will not even send one within 1000 ms of the last request
# (CSkillCommandDelay::m_dwCastingTimeDelay, skillcommanddelay.cpp) -- the client wins.
SKILL_DELAY_MS = max(500, 1000)
POTION_DELAY_MS = 500         # client useitemdelay.h; instant restores add col 26 (server)
# heal / mana skills in the rotation are cast only when the player needs them
HEAL_BELOW = 0.60             # HP share under which a heal (Healing, Cure) goes out
MANA_BELOW = 0.40             # MP share under which a mana skill (Mana Blood) goes out,
MANA_HP_ABOVE = 0.60          # ... and only while HP is above this share
# grinding: after a kill the player sits (CMD_SIT) when it is low, until it is topped up
REST_BELOW = (0.50, 0.30)     # HP, MP share under which it rests before the next pull
REST_UNTIL = (0.90, 0.80)
RECOVERY_RATE_SITTING = 100   # GameStaticConfig::RECOVERY_RATE_SITTING
SELF_TYPES = (8, 10, 12)      # SKILL_TYPE_08/10/12: stat/status change on the caster

# Which ING slot a buff or debuff on AT_x lands in; (raise, lower)
ING_FOR_AT = {
    AT["ATK"]: ("INC_APOWER", "DEC_APOWER"), AT["DEF"]: ("INC_DPOWER", "DEC_DPOWER"),
    AT["RES"]: ("INC_RES", "DEC_RES"), AT["HIT"]: ("INC_HIT", "DEC_HIT"),
    AT["CRITICAL"]: ("INC_CRITICAL", "DEC_CRITICAL"), AT["AVOID"]: ("INC_AVOID", "DEC_AVOID"),
    AT["ATK_SPD"]: ("INC_ATK_SPD", "DEC_ATK_SPD"), AT["SPEED"]: ("INC_MOV_SPD", "DEC_MOV_SPD"),
    AT["MAX_HP"]: ("INC_MAX_HP", None), AT["MAX_MP"]: ("INC_MAX_MP", None),
}


class Status:
    """StatusEffects: one slot per ING type, value + remaining 1-second ticks.

    `on_change` is called whenever a slot is set or cleared, so the owner can
    recompute its totals once instead of on every read.
    """

    def __init__(self, on_change=None):
        self.val = {}         # ing -> value
        self.ticks = {}       # ing -> remaining ticks (None = permanent)
        self.stb = {}         # ing -> LIST_STATUS row
        self.skill = {}       # ing -> skill row that applied it
        self.acc = 0
        self.on_change = on_change

    def get(self, name):
        return self.val.get(ING[name], 0)

    def is_set(self, ing):
        return ing in self.val

    def can_apply(self, gd, stb_row, value):
        """IsEnableApplay: a set slot accepts only a duplicable state of >= value."""
        ing = gd.state_type(stb_row)
        if ing in self.val:
            if not gd.state_can_dup(stb_row):
                return False
            if self.val[ing] > value:
                return False
        return True

    def apply(self, gd, stb_row, duration, value, skill_row):
        ing = gd.state_type(stb_row)
        self.val[ing] = value
        self.ticks[ing] = duration
        self.stb[ing] = stb_row
        self.skill[ing] = skill_row
        if self.on_change:
            self.on_change()
        return ing

    def clear(self, ing):
        if ing not in self.val:
            return
        for d in (self.val, self.ticks, self.stb, self.skill):
            d.pop(ing, None)
        if self.on_change:
            self.on_change()

    def disabled(self):
        """StatusEffects::IsIgnoreSTATUS: sleep or faint blocks every command."""
        return _SLEEP in self.val or _FAINT in self.val

    def proc(self, ms, on_tick=None):
        """StatusEffects::Proc: once per accumulated second, `if (m_nTICKs[i]-- <= 0) clear`;
        `on_tick(ing)` runs for every state with ticks left (poison deals its damage then:
        `if (m_nTICKs[ING_POISONED]-- > 0) Give_STATUS_DAMAGE`)."""
        self.acc += ms
        if self.acc < 1000:
            return False
        changed = False
        while self.acc >= 1000:
            self.acc -= 1000
            for ing in list(self.ticks):
                t = self.ticks[ing]
                if t is None:
                    continue
                if t <= 0:
                    for d in (self.val, self.ticks, self.stb, self.skill):
                        d.pop(ing, None)
                    changed = True
                else:
                    if on_tick:
                        on_tick(ing)
                    self.ticks[ing] = t - 1
        if changed and self.on_change:
            self.on_change()
        return changed


_SLEEP, _FAINT, _DUMMY = ING["SLEEP"], ING["FAINTING"], ING["DUMMY_DAMAGE"]
_DUMB, _TAUNT = ING["DUMB"], ING["TAUNT"]
_POISON = ING["POISONED"]
_I = {k: ING[k] for k in ("INC_APOWER", "DEC_APOWER", "INC_HIT", "DEC_HIT", "INC_ATK_SPD",
                          "DEC_ATK_SPD", "INC_DPOWER", "DEC_DPOWER", "INC_RES", "DEC_RES",
                          "INC_AVOID", "DEC_AVOID", "INC_CRITICAL", "DEC_CRITICAL",
                          "INC_MAX_HP", "INC_MAX_MP")}


class Combatant:
    """The getters calculation.cpp calls, over a base stat block + live status.

    The totals are plain attributes, recomputed by `refresh()` whenever the status
    changes (or a base field is edited): the formulas read them thousands of times
    per fight, statuses change a few times.
    """

    def __init__(self, name, level, attack_power, hit_rate, defence, resist, avoid,
                 critical, attack_speed, max_hp, max_mp, int_, sense, magic, is_user,
                 base_atk=0, base_hit=0, base_aspd=0):
        self.name, self.level = name, level
        self.attack_power, self.hit_rate = attack_power, hit_rate
        self.defence, self.resist, self.avoid, self.critical = defence, resist, avoid, critical
        self.attack_speed = attack_speed
        self.ori_max_hp, self.ori_max_mp = max_hp, max_mp
        self.int_, self.sense, self.magic, self.is_user = int_, sense, magic, is_user
        self.get_int, self.get_sense = int_, sense
        self.base_atk, self.base_hit, self.base_aspd = base_atk, base_hit, base_aspd
        self.dummy_skill_power = 0
        self.charm = 0
        self.katar_crit = False   # a Hawker / Raider with a katar: skills can crit
        self.shield_def = 0       # an avatar's shield's own Defense (item + refine)
        self.base_value = None    # Get_BaseAbilityValue override (the player's pre-buff stat)
        self.st = Status(self.refresh)
        self.refresh()
        self.hp, self.mp = max_hp, max_mp

    def refresh(self):
        v = self.st.val.get
        I = _I  # noqa: E741
        # CObjCHAR::total_* (+ classUSER's server.toml bases). Unsigned in C++: a debuff
        # bigger than the stat wraps to a huge number; clamped here.
        self.total_atk = max(0, self.attack_power + v(I["INC_APOWER"], 0)
                             - v(I["DEC_APOWER"], 0) + self.base_atk)
        self.total_hit = max(0, self.hit_rate + v(I["INC_HIT"], 0) - v(I["DEC_HIT"], 0)
                             + self.base_hit)
        self.total_aspd = max(1, self.attack_speed + v(I["INC_ATK_SPD"], 0)
                              - v(I["DEC_ATK_SPD"], 0) + self.base_aspd)
        # CObjCHAR::Get_* floor at 10
        self.get_def = max(10, self.defence + v(I["INC_DPOWER"], 0) - v(I["DEC_DPOWER"], 0))
        self.get_res = max(10, self.resist + v(I["INC_RES"], 0) - v(I["DEC_RES"], 0))
        self.get_avoid = max(10, self.avoid + v(I["INC_AVOID"], 0) - v(I["DEC_AVOID"], 0))
        self.get_crit = max(10, self.critical + v(I["INC_CRITICAL"], 0)
                            - v(I["DEC_CRITICAL"], 0))
        self.max_hp = self.ori_max_hp + v(I["INC_MAX_HP"], 0)
        self.max_mp = self.ori_max_mp + v(I["INC_MAX_MP"], 0)
        self.dummy_power = self.dummy_skill_power if _DUMMY in self.st.val else 0

    def base(self, at):
        """Get_BaseAbilityValue: CObjAVT's pre-buff stat for the player; a monster has no
        override, so CObjCHAR's default reads its live (already buffed) value."""
        if self.base_value and at not in (AT["HP"], AT["MP"]):
            return self.base_value(at)
        return self.current_ability(at)    # CObjAVT's default: Get_AbilityValue (live HP)

    def current_ability(self, at):
        """CObjMOB::Get_AbilityValue: a monster's percentage base is its live value."""
        return {AT["HP"]: self.hp, AT["MP"]: self.mp, AT["ATK"]: self.total_atk, AT["DEF"]: self.get_def, AT["HIT"]: self.total_hit,
                AT["RES"]: self.get_res, AT["AVOID"]: self.get_avoid,
                AT["ATK_SPD"]: self.total_aspd, AT["CRITICAL"]: self.get_crit,
                AT["MAX_HP"]: self.max_hp}.get(at, 0)


class Actor:
    """Motion clock for one combatant (CObjCHAR::Proc + ProcMotionFrame)."""

    def __init__(self, unit):
        self.u = unit
        self.motion = None
        self.ratio = 1.0
        self.cur = 0
        self.acc = 0.0

    def set_motion(self, motion, ratio):
        self.motion, self.ratio = motion, ratio
        self.cur, self.acc = 0, 0.0

    def advance(self, ms, on_complete):
        m = self.motion
        if m is None:
            on_complete()
            return
        remain = m.total - self.cur
        need = max(1, m.need_time(remain, self.ratio))
        while ms >= need:
            self.cur += remain
            on_complete()                       # RealProc: next action starts now
            ms -= need
            self.cur, self.acc = 0, 0.0
            m = self.motion
            if m is None:
                return
            remain = m.total
            need = max(1, m.need_time(remain, self.ratio))
        self.acc += m.pass_frame(ms, self.ratio)
        r = int(self.acc)
        if r:
            self.acc -= r
            self.cur += r


class Log:
    def __init__(self):
        self.p_swings = self.p_hits = self.p_crits = self.p_dmg = 0
        self.m_swings = self.m_hits = self.m_crits = self.m_dmg = 0
        self.p_swing_t = []       # (first, last) swing start ms, as the trace measures it
        self.m_swing_t = []
        self.skills = {}          # row -> dict(casts, landed, dmg, states_landed)
        self.mp_spent = 0
        self.potions = {"hp": 0, "mp": 0}
        self.regen_hp = self.regen_mp = self.potion_hp = self.potion_mp = 0
        self.debuff_ms = {}       # ing -> ms active on the monster
        self.oom_ms = None
        self.monster_disabled_ms = 0
        self.p_samples, self.m_samples = [], []   # landed hit damages, for percentiles
        self.skill_samples = {}                   # skill row -> landed damages
        self.end_ms = 0
        self.result = None        # "win" | "loss" | "timeout"
        # monster AI
        self.m_skills = {}        # row -> dict(orders, casts, landed, dmg, states, state_tries)
        self.m_skill_samples = {}
        self.m_skill_dmg = 0
        self.ai_damaged = self.ai_stop = 0      # pattern evaluations
        self.ai_events = {}       # (pattern, event index) -> times it fired
        self.ai_unsim = {}        # action number (or "summon skill") -> times it fired
        self.m_idle_ms = 0        # monster standing in CMD_STOP (not fighting)
        self.monster_buff_ms = {}  # beneficial ing on the monster -> ms
        self.player_state_ms = {}  # monster-applied ing on the player -> ms
        self.player_disabled_ms = 0
        self.respawns = 0         # survival runs: fresh monsters
        self.kills = 0            # monsters killed in a survival / grinding run
        self.rest_ms = 0          # grinding: time spent sitting between pulls
        self.suicides = 0
        # the player's summons
        self.s_skills = {}        # summon AI skill row -> dict(orders, casts, landed, ...)
        self.s_skill_samples = {}
        self.s_unsim = {}         # summon AI actions not simulated
        self.s_swings = self.s_hits = self.s_dmg = 0   # s_dmg: swings + skills
        self.s_samples = []
        self.s_calls = 0          # summons called again during the fight
        self.s_deaths = 0
        self.ms_dmg = 0           # the monster's damage to summons
        self.m_dot_dmg = 0        # poison ticks on the player
        self.ms_swings = 0        # the monster's swings at summons
        self.s_swing_t = []       # (first, last) summon swing start ms
        self.m_on_summon_ms = 0   # time the monster spends on a summon

    def skill(self, row):
        return self.skills.setdefault(row, dict(casts=0, landed=0, dmg=0, states=0,
                                                state_tries=0, dot=0, drain=0, heal=0))

    def m_skill(self, row):
        return self.m_skills.setdefault(row, dict(orders=0, casts=0, landed=0, dmg=0,
                                                  states=0, state_tries=0))

    def s_skill(self, row):
        return self.s_skills.setdefault(row, dict(orders=0, casts=0, landed=0, dmg=0,
                                                  states=0, state_tries=0))


MOB_ANI_ATTACK = 2            # datatype.h, monster attack slot in LIST_NPC.CHR
FOLLOW_DISTANCE = 350         # CObjSUMMON::Proc: where a summon rests from its owner (cm)


class Mob:
    """An NPC under its AI file: the monster the player fights, or one of the player's
    summons (`owner` set). Both run the same CObjAI command machine -- attack / stop /
    skill orders, cast and action clips, SKILL_ACTION_MODE -- and the same AI patterns;
    a summon's "owner" conditions and actions (AICOND 21/22/18, AIACT 31) are live.

    This object is also the view ai.Brain reads: `m` (itself), `m_target`, `distance`,
    `t`, `rng`, `candidates()`, `status_flags()`, `ai_action()` and the owner hooks.
    """

    def __init__(self, fight, unit, row, ai, owner=None, skill=None):
        self.f, self.u, self.row, self.ai = fight, unit, row, ai
        self.owner, self.skill = owner, skill
        self.enemy = owner is None
        self.a = Actor(unit)
        self.brain = Brain(ai, self) if ai else None
        self.rng = fight.rng
        lg = fight.log
        self.skill_log = lg.m_skill if self.enemy else lg.s_skill
        self.unsim = lg.ai_unsim if self.enemy else lg.s_unsim
        self.reset(fight.p if self.enemy else None)

    def reset(self, target):
        self.cmd = "attack" if target is not None else "stop"   # m_wCommand
        self.target = target
        self.before = self.cmd    # m_wBeforeCMD
        self.todo = None          # (Skill, nMotion) ordered, not started
        self.active = None        # (Skill, nMotion, self_cast) from cast start to release
        self.phase = "swing" if target is not None else "idle"
        self.clip = None          # last clip set (m_pCurMOTION)
        self.stop_check = -10 ** 9
        self.created_only = False
        self.taunter = None       # m_iTargetOBJ[ING_TAUNT]: who a landed Taunt ties it to

    # ---- the view ai.Brain reads
    @property
    def m(self):
        return self.u

    @property
    def m_target(self):
        return self.target

    @property
    def distance(self):
        return self.f.distance

    @property
    def t(self):
        return self.f.t

    @property
    def has_owner(self):
        return self.owner is not None

    def owner_has_target(self):
        """AICOND_22: the owner's Get_TARGET() -- the player fights the monster throughout."""
        return self.owner is not None and self.owner.hp > 0 and self.f.m.hp > 0

    def owner_distance(self):
        return FOLLOW_DISTANCE

    def candidates(self):
        """AI_FindFirstOBJ in this fight: the characters hostile to this one, alive, in
        the order the sector walk is assumed to meet them (the player first)."""
        return self.f.foes(self)

    def status_flags(self, who, kind):
        return self.f.status_flags(who, kind)

    def motion(self, slot):
        return self.f.sim.npc_motion(self.row, slot)

    # ---- command machine (CObjAI)
    def alive_target(self):
        t = self.target
        return t if t is not None and t.hp > 0 else None

    def swing(self):
        """Start_ATTACK on the current target: damage at frame 0 of the attack clip."""
        mot = self.motion(MOB_ANI_ATTACK)
        tgt = self.target
        self.phase = "swing"
        self.clip = mot
        self.a.set_motion(mot, self.u.total_aspd / 100.0)
        if mot is None or not mot.attack_frames or tgt is None or tgt.hp <= 0:
            return
        self.f.swing_started(self, tgt)
        dmg, crit = F.normal_attack(self.rng, self.u, tgt, mot.attack_frames)
        self.f.swing_landed(self, tgt, dmg, crit)

    def next(self):
        """RealProc when the motion ends, or on an idle tick."""
        if self.phase == "cast":
            self.skill_action()
            return
        if self.cmd == "attack":
            if self.alive_target() is not None:
                self.swing()
                return
            self.stop()
            return
        if self.cmd in ("skill_obj", "skill_self") and self.todo:
            self.skill_start()
            return
        # CMD_STOP: ProcCMD_STOP -> SetCMD_STOP (target cleared), then Do_StopAI
        self.cmd, self.target, self.phase = "stop", None, "idle"
        self.a.motion = None

    def on_motion_end(self):
        if self.f.p.hp <= 0 or self.f.m.hp <= 0 or self.u.hp <= 0:
            self.a.motion = None
            return
        self.next()

    def idle_tick(self):
        if self.cmd == "stop":
            self.stop_ai()
        if self.cmd != "stop":
            self.next()

    def tick(self, ms):
        lg = self.f.log
        disabled = self.u.st.disabled()
        if disabled:
            if self.enemy:
                lg.monster_disabled_ms += ms
        elif self.a.motion is None:
            self.idle_tick()        # no AI file: it simply attacks again
        else:
            self.a.advance(ms, self.on_motion_end)
        if self.enemy:
            if self.cmd == "stop" and self.phase == "idle" and not self.u.st.disabled():
                lg.m_idle_ms += ms
            elif self.target is not None and self.target is not self.f.p:
                lg.m_on_summon_ms += ms

    def stop(self):
        """CObjAI::SetCMD_STOP: refused while a skill is active (__BLOCK_WHEN_SKILL)."""
        if self.active:
            return False
        self.cmd, self.target, self.phase = "stop", None, "idle"
        self.todo = None
        self.a.motion = None                # Set_MOTION(GetANI_Stop())
        return True

    def disable(self):
        """UpdateIngSTATUS for sleep/faint -> Del_ActiveSKILL + CObjAI::SetCMD_STOP."""
        self.active = self.todo = None
        if self.brain:
            self.cmd, self.target = "stop", None
        self.phase = "idle"
        self.a.motion = None

    # ---- AI hooks
    def ai_run(self, pattern, dest=None, damage=0):
        i = self.brain.run(pattern, dest, damage)
        if i is not None and self.enemy:
            k = (pattern, i)
            self.f.log.ai_events[k] = self.f.log.ai_events.get(k, 0) + 1

    def damaged(self, dest, dmg):
        """CObjMOB::Do_DamagedAI -> CAI_FILE::AI_WhenDAMAGED (cai_file.cpp:687)."""
        if self.brain is None or self.u.hp <= 0 or self.u.st.disabled():
            return
        if self.alive_target() is not None and self.ai.damaged_rate < F.R(self.rng, 100):
            return
        if self.enemy:
            self.f.log.ai_damaged += 1
        self.ai_run(DAMAGED, dest, dmg)

    def stop_ai(self):
        """CObjMOB::Do_StopAI: the STOP pattern, at most once per iSecond."""
        if self.brain is None or self.u.st.disabled():
            return
        if self.f.t - self.stop_check < self.ai.stop_ms:
            return
        self.stop_check = self.f.t
        if self.enemy:
            self.f.log.ai_stop += 1
        self.ai_run(STOP)

    def ai_action(self, act, prm):
        n = act.n
        if self.created_only and n != 35:
            return
        if n == 35:
            self.brain.set_var(act.f["var"], act.f["op"], act.f["value"])
        elif n == 24:
            self.order_skill(act.f, prm)
        elif n in (6, 12, 13, 15, 19) or (n == 31 and self.owner is not None):
            if n == 6:
                foes = self.candidates()
                who = foes[0] if foes and self.distance <= act.f["dist"] else None
            elif n == 31:                   # F_AIACT31: the owner's target, if hostile
                who = self.f.m
            else:
                who = {12: prm.near, 19: prm.near, 13: prm.find, 15: prm.dest}[n]
            if who is not None and who.hp > 0:
                self.order_attack(who)
        elif n == 0:
            self.stop()
        elif n == 23:                       # Add_DAMAGE(Get_HP() + 1)
            self.u.hp = 0
            if self.enemy:
                self.f.log.suicides += 1
        elif n not in HARMLESS_ACTS:
            self.unsim[n] = self.unsim.get(n, 0) + 1

    def taunted_away_from(self, who):
        """StatusEffects::IsTauntSTATUS: taunted by someone still alive, and `who` is not
        them -- the attack order is refused. A dead taunter frees it."""
        return (self.u.st.is_set(_TAUNT) and self.taunter is not None
                and self.taunter.hp > 0 and who is not self.taunter)

    def order_attack(self, who, forced=False):
        """SetCMD_RUNnATTACK -> CObjAI::SetCMD_ATTACK (replaces an order not yet cast);
        refused while a Taunt ties the monster to someone else (Is_TauntSTATUS)."""
        if self.active:
            return
        if not forced and self.taunted_away_from(who):
            return
        if self.cmd == "attack" and self.target is who:
            return
        self.cmd, self.target, self.todo = "attack", who, None

    def order_skill(self, f, prm):
        """F_AIACT24 -> CObjMOB::SetCMD_Skill2OBJ / SetCMD_Skill2SELF."""
        if self.u.st.disabled() or self.u.st.is_set(_DUMB):
            return
        sk = self.f.sim.m_skill(f["skill"])
        if sk is None:                      # F_AIACT24 refuses a blank LIST_SKILL row
            return
        if f["target"] == 2:
            if self.active:
                return
            self.before, self.cmd = self.cmd, "skill_self"
        else:
            who = prm.find if f["target"] == 0 else self.alive_target()
            if who is None or who.hp <= 0 or self.active:
                return
            if (self.todo and self.todo[0].row == sk.row and self.cmd == "skill_obj"
                    and self.target is who):
                return                      # the same order again
            self.before, self.cmd, self.target = self.cmd, "skill_obj", who
        self.todo = (sk, f["motion"])
        self.skill_log(sk.row)["orders"] += 1

    def skill_start(self):
        """Do_SKILL, first half: the casting clip (slot nMotion) if nMotion != 0."""
        sk, motion = self.todo
        self.todo = None
        selfc = self.cmd == "skill_self"
        if not selfc and self.alive_target() is None:
            self.cmd = "stop"
            self.next()
            return
        self.active = (sk, motion, selfc)
        self.skill_log(sk.row)["casts"] += 1
        if motion:
            clip = self.motion(motion)
            self.phase = "cast"
            if clip is not None:
                self.clip = clip
                self.a.set_motion(clip, sk.casting_speed())
                return
            # Set_MOTION fails on an unmapped slot (or nMotion -1): no cast clip plays
        self.skill_action()

    def skill_action(self):
        """Do_SKILL, second half: the action clip (slot nMotion+1) and Skill_START at
        its frame 0, then SKILL_ACTION_MODE decides the next command."""
        sk, motion, selfc = self.active
        clip = self.motion(motion + 1)
        if clip is not None:
            self.clip = clip                # else the current clip plays on (Set_MOTION fails)
        self.phase = "action"
        self.a.set_motion(self.clip, sk.action_speed())
        f, me = self.f, self.u
        tgt = None if selfc else self.alive_target()
        rec = self.skill_log(sk.row)
        t, ok = sk.type, True
        if t in (3, 4, 5, 6, 19):
            if tgt is not None and f.pass_filter(sk, tgt, me):
                hit = self.clip.attack_frames if self.clip else 0
                dmg = self.skill_hit(sk, rec, hit, tgt)
                if t == 19:
                    if dmg:                 # the drain lands on the caster, at its own INT
                        f.apply_states(sk, tgt, me, rec, speller_int=me.get_int)
                else:
                    f.apply_states(sk, me, tgt, rec)
        elif t in (7, 17):                  # around the caster (m_PosGOTO = m_PosCUR)
            caught = f.play["caught"]
            if (f.sim.cast_distance(sk, selfc) <= sk.scope
                    or (self.enemy and caught and f.rng.random() < caught)):
                for c in self.candidates():
                    if f.pass_filter(sk, c, me):
                        self.skill_hit(sk, rec, 1, c)
                        if f.pass_filter(sk, c, me):
                            f.apply_states(sk, me, c, rec)
        elif t in (8, 12):
            if f.pass_filter(sk, me, me):
                f.apply_states(sk, me, me, rec)
        elif t in (9, 11, 13):
            if tgt is not None and f.pass_filter(sk, tgt, me):
                f.apply_states(sk, me, tgt, rec)
        elif t == 10:
            if not selfc:
                ok = False                  # Skill_START: type 10 at a target fails
            elif f.pass_filter(sk, me, me):
                f.apply_states(sk, me, me, rec)
        elif t == 14:
            self.unsim["summon skill"] = self.unsim.get("summon skill", 0) + 1
        elif t != 20:
            ok = False
        self.active = None                  # Del_ActiveSKILL
        if not ok:
            self.cmd = self.before if selfc else "stop"
        elif sk.action_mode == 1:           # SA_ATTACK
            self.cmd = "attack"
        elif sk.action_mode == 2:           # SA_RESTORE
            b = self.before
            self.cmd = "stop" if b in ("skill_obj", "skill_self") else b
        else:                               # SA_STOP (0 or blank)
            self.cmd = "stop"

    def skill_hit(self, sk, rec, hit_cnt, tgt):
        dmg = F.skill_damage(self.rng, self.u, tgt, sk, hit_cnt)
        tgt.st.clear(_SLEEP)
        if dmg:
            rec["landed"] += 1
            rec["dmg"] += dmg
            lg = self.f.log
            (lg.m_skill_samples if self.enemy else lg.s_skill_samples) \
                .setdefault(sk.row, []).append(dmg)
            if not self.enemy:
                lg.s_dmg += dmg
            elif tgt is self.f.p:
                lg.m_skill_dmg += dmg
            else:
                lg.ms_dmg += dmg
            tgt.hp -= dmg
        self.f.hit_reaction(self, tgt, dmg)
        return dmg


class Fight:
    def __init__(self, sim, rng=None, immortal_monster=False, max_seconds=600, gap_ms=0):
        self.sim, self.gd = sim, sim.gd
        self.rng = rng or random.Random()
        self.p = sim.make_player()
        # survival runs: a monster that would die is replaced by a fresh one (_respawn),
        # so its HP-driven AI phases repeat the way they do fight after fight
        self.immortal = immortal_monster
        # grinding: the next monster arrives `gap_ms` after a kill (the walk to it); HP,
        # MP, cooldowns, states and summons carry over, and the player eats meanwhile
        self.gap_ms = gap_ms
        self.next_spawn = None
        self.resting = False
        rate = sim.stats.rate_use_mp
        self.cost = {sk.row: sk.mp_cost(rate) for sk in sim.rotation}
        self.min_cost = min(self.cost.values()) if self.cost else None
        self.cost.update({sk.row: sk.mp_cost(rate) for sk in sim.summons})
        self.hp_trace = []        # (ms, hp) every 2 s, for survival extrapolation
        self.max_ms = max_seconds * 1000
        self.log = Log()
        self.pa = Actor(self.p)
        self.t = 0
        self.p_state = "idle"     # idle | attack | cast | action
        self.p_pending = None     # skill being cast
        self.p_summon_slot = None  # which summon a summon cast refills
        self.last_cast = {}       # skill row -> accept time
        self.last_any_cast = -10 ** 9
        self.p_stun_immune_until = -10 ** 9  # no monster stun lands on the player before
        self.p_stun_ends = None   # a player's stun ends on its duration (Proc_IngSTATUS)
        # sim.play: a typical player reacts late and refreshes buffs/debuffs late
        self.play = sim.play
        self.react_ms = 0         # this cast's extra reaction delay (drawn per cast)
        self.cast_once = set()    # rows cast this fight: their refresh may run late
        self.off_since = {}       # buff/debuff row -> (ms it was seen down, lag drawn)
        self.spacing = sim.build.skill_spacing
        self.swings_since_skill = self.spacing  # the opener may be a skill
        self.regen_acc = 0
        self.last_potion = {"hp": -10 ** 9, "mp": -10 ** 9}
        self.potion_run = {}      # "hp"/"mp" -> dict(total, acc_value, tot_time, acc_time, stb)
        self.ai = sim.ai
        self.distance = sim.distance
        self.summons = [None] * len(sim.summons)   # one slot per summon the build keeps up
        self.enemy = Mob(self, sim.make_monster(), sim.npc_row, sim.ai)

    @property
    def m(self):
        return self.enemy.u

    @property
    def ma(self):
        return self.enemy.a

    # ------------------------------------------------------------ who is who
    def live_summons(self):
        return [s for s in self.summons if s is not None and s.u.hp > 0]

    def foes(self, mob):
        if mob.enemy:
            return [u for u in [self.p] + [s.u for s in self.live_summons()] if u.hp > 0]
        return [self.m] if self.m.hp > 0 else []

    def mob_of(self, unit):
        if unit is self.enemy.u:
            return self.enemy
        for s in self.summons:
            if s is not None and s.u is unit:
                return s
        return None

    def allied(self, a, b):
        """Is_ALLIED: the player and its summons on one side, the monster on the other."""
        return (a is self.m) == (b is self.m)

    def pass_filter(self, sk, target, caster):
        """Skill_IsPassFilter (cobjchar.cpp:1318) for an NPC caster: no party, no clan
        (SKILL_CLASS_FILTER, LIST_SKILL col 7); summons are OBJ_MOB, the player is a user."""
        if target.hp <= 0:
            return False
        own = target is caster
        ally = self.allied(target, caster)
        user = target.is_user
        return {0: own, 1: own, 2: False, 3: ally, 4: not user, 5: not ally,
                6: user and not ally, 7: user, 8: True, 10: not ally and not user
                }.get(sk.target_filter, False)

    # ------------------------------------------------------------ blows between NPCs
    def swing_started(self, mob, tgt):
        lg = self.log
        if not mob.enemy:
            lg.s_swings += 1
            lg.s_swing_t[1:] = [self.t]
            if not lg.s_swing_t[:1]:
                lg.s_swing_t = [self.t, self.t]
        elif tgt is not self.p:
            lg.ms_swings += 1
        elif tgt is self.p:
            lg.m_swings += 1
            lg.m_swing_t[1:] = [self.t]
            if not lg.m_swing_t[:1]:
                lg.m_swing_t = [self.t, self.t]

    def swing_landed(self, mob, tgt, dmg, crit):
        lg = self.log
        tgt.st.clear(_SLEEP)                # Apply_DAMAGE wakes the target, hit or miss
        if dmg:
            if not mob.enemy:
                lg.s_hits += 1
                lg.s_dmg += dmg
                lg.s_samples.append(dmg)
            elif tgt is self.p:
                lg.m_hits += 1
                lg.m_crits += crit
                lg.m_dmg += dmg
                lg.m_samples.append(dmg)
            else:
                lg.ms_dmg += dmg
            tgt.hp -= dmg
        self.hit_reaction(mob, tgt, dmg)

    def hit_reaction(self, mob, tgt, dmg):
        """Apply_DAMAGE -> the target's Do_DamagedAI with the attacker, hit or miss."""
        other = self.mob_of(tgt)
        if other is not None and other is not mob:
            other.damaged(mob.u, dmg)

    # ------------------------------------------------------------ player decisions
    def _skill_ready(self, sk):
        if self.p.st.is_set(_DUMB):
            return False
        if self.t - self.last_any_cast < SKILL_DELAY_MS + self.react_ms:
            return False
        if self.t - self.last_cast.get(sk.row, -10 ** 9) < sk.reload_ms:
            return False
        if self.p.mp < self.cost[sk.row]:
            return False
        if sk.hp_cost() and self.p.hp <= sk.hp_cost():
            return False
        kind = sk.restores()
        if kind == "hp":
            return self.p.hp < HEAL_BELOW * self.p.max_hp
        if kind == "mp":
            return (self.p.mp < MANA_BELOW * self.p.max_mp
                    and self.p.hp - sk.hp_cost() > MANA_HP_ABOVE * self.p.max_hp)
        if not sk.is_damage():
            # a pure (de)buff is recast only when one of its states has dropped off
            who = self.p if sk.type in SELF_TYPES else self.m
            states = [s for s in sk.states if s]
            if states and all(who.st.is_set(self.gd.state_type(s)) for s in states):
                self.off_since.pop(sk.row, None)
                return False
            lo, hi = self.play["refresh_lag_ms"]
            if hi and sk.row in self.cast_once:
                # a typical player notices a dropped buff/debuff some seconds late
                if sk.row not in self.off_since:
                    self.off_since[sk.row] = (self.t, self.rng.uniform(lo, hi))
                since, lag = self.off_since[sk.row]
                if self.t - since < lag:
                    return False
        return True

    def _player_next(self):
        """What the player does when its current motion ends (or at t=0)."""
        if self.p_state == "cast":
            self._skill_action(self.p_pending)
            return
        # Build.skill_spacing: this many normal swings between two skills (if it can swing)
        if self.swings_since_skill < self.spacing and self._can_swing():
            self._player_swing()
            return
        # a summon that died is called again first
        for i, sk in enumerate(self.sim.summons):
            if self.summons[i] is None and self._skill_ready(sk):
                self.p_summon_slot = i
                self._skill_start(sk)
                return
        for sk in self.sim.rotation:
            if self._skill_ready(sk):
                self._skill_start(sk)
                return
        self._player_swing()

    def _can_swing(self):
        mot = self.sim.p_attack_motion
        return (mot is not None and mot.attack_frames
                and (self.sim.stats.shot is None or self.sim.build.ammo))

    def _player_swing(self):
        mot = self.sim.p_attack_motion
        self.p_state = "attack"
        self.pa.set_motion(mot, self.p.total_aspd / 100.0)
        if not self._can_swing():
            return
        self.swings_since_skill += 1
        lg = self.log
        lg.p_swings += 1
        lg.p_swing_t[1:] = [self.t]
        if not lg.p_swing_t[:1]:
            lg.p_swing_t = [self.t, self.t]
        dmg, crit = F.normal_attack(self.rng, self.p, self.m, mot.attack_frames)
        self.m.st.clear(ING["SLEEP"])       # Apply_DAMAGE: an attack wakes a sleeper
        if dmg:
            lg.p_hits += 1
            lg.p_crits += crit
            lg.p_dmg += dmg
            lg.p_samples.append(dmg)
            self.m.hp -= dmg
        self.enemy.damaged(self.p, dmg)

    def _skill_start(self, sk):
        """Do_SKILL first half: pay, then cast motion (or straight to the action)."""
        self.last_cast[sk.row] = self.t
        self.last_any_cast = self.t
        self.swings_since_skill = 0
        self.cast_once.add(sk.row)
        self.off_since.pop(sk.row, None)
        lo, hi = self.play["reaction_ms"]
        self.react_ms = self.rng.uniform(lo, hi) if hi else 0
        cost = self.cost[sk.row]
        self.p.mp -= cost
        self.p.hp -= sk.hp_cost()
        self.log.mp_spent += cost
        self.log.skill(sk.row)["casts"] += 1
        cast = self.sim.p_cast_motion.get(sk.row)
        if cast is not None:
            self.p_state, self.p_pending = "cast", sk
            self.pa.set_motion(cast, sk.casting_speed())
        else:
            self._skill_action(sk)

    def _skill_action(self, sk):
        """Do_SKILL second half: action motion starts, Skill_START applies at once."""
        act = self.sim.p_action_motion.get(sk.row)
        self.p_state, self.p_pending = "action", None
        self.pa.set_motion(act, sk.action_speed())
        rec = self.log.skill(sk.row)
        if sk.type == 14:                   # SKILL_TYPE_14: the summon appears
            self._summon(sk, self.p_summon_slot)
            self.p_summon_slot = None
            return
        if sk.is_damage():
            hit_cnt = 1 if sk.type in (7, 17) else (act.attack_frames if act else 1)
            dmg = F.skill_damage(self.rng, self.p, self.m, sk, hit_cnt)
            self.m.st.clear(ING["SLEEP"])
            if dmg:
                rec["landed"] += 1
                rec["dmg"] += dmg
                self.log.skill_samples.setdefault(sk.row, []).append(dmg)
                self.m.hp -= dmg
            self.enemy.damaged(self.p, dmg)
            if sk.type == 19:
                # Skill_START_19: a drain that lands heals the caster -- its
                # Skill_ApplyIngSTATUS on the caster, the target standing in as speller,
                # at the caster's own INT
                if dmg:
                    hp0 = self.p.hp
                    self.apply_states(sk, self.m, self.p, rec, speller_int=self.p.get_int)
                    rec["drain"] += self.p.hp - hp0
                return
        # Skill_ChangeIngSTATUS runs after the damage for 3/4/5/6, on its own for 9/13;
        # 8/10/12 are self-cast, and a heal (11) is cast on the player itself
        target = self.p if sk.type in SELF_TYPES or sk.restores() else self.m
        hp0, mp0 = self.p.hp, self.p.mp
        self.apply_states(sk, self.p, target, rec)
        if sk.restores():
            rec["heal"] += (self.p.hp - hp0) + (self.p.mp - mp0)

    def _summon(self, sk, slot):
        """Skill_START type 14: a CObjSUMMON of LIST_SKILL col 28, stats scaled by
        SetCallerOBJ; its CREATED pattern runs (only AI variables matter here)."""
        if slot is None:
            return
        mob = Mob(self, self.sim.make_summon(sk), self.sim.summon_row(sk),
                  self.sim.summon_ai(sk), owner=self.p, skill=sk)
        self.summons[slot] = mob
        self.log.s_calls += 1
        if mob.brain:
            mob.created_only = True
            mob.ai_run(CREATED)
            mob.created_only = False

    def apply_states(self, sk, caster, target, rec, speller_int=None):
        """Skill_ApplyIngSTATUS for each state slot of the skill (cobjchar.cpp:1411)."""
        if target.hp <= 0:
            return
        gd = self.gd
        mob = self.mob_of(target)
        if sk.harm and mob is not None and not self.allied(caster, target):
            mob.damaged(caster, 0)          # SKILL_HARM: Do_DamagedAI(pSpeller, 0)
        cint = caster.get_int if speller_int is None else speller_int
        for n, stb_row in enumerate(sk.states):
            ab, flat, rate = sk.ability[n]
            if not stb_row:
                # no state: an instant HP/MP change (AT_HP / AT_MP), no success roll
                if ab in (AT["HP"], AT["MP"]):
                    v = F.skill_adjust_value(target.base(ab), rate, flat, cint)
                    if ab == AT["HP"]:
                        target.hp = min(target.max_hp, target.hp + v)
                    else:
                        target.mp = min(target.max_mp, target.mp + v)
                    rec["states"] += 1
                    rec["state_tries"] += 1
                continue
            rec["state_tries"] += 1
            # Skill_ApplyIngSTATUS: a monster's stun does not land on a player within
            # server.toml stun_immunity_ms of the last one ending
            stun_guard = (target is self.p and caster is not self.p
                          and gd.state_type(stb_row) == _FAINT and gd.server.stun_immunity_ms)
            if stun_guard and self.t < self.p_stun_immune_until:
                continue
            if sk.success:
                if gd.state_harmful(stb_row):
                    if not F.harmful_state_lands(self.rng, sk.success, caster.level,
                                                 caster.get_int, target):
                        continue
                elif sk.success < target.level - caster.level + 1 + F.R(self.rng, 100):
                    continue
            value = F.skill_adjust_value(target.base(ab), rate, flat, cint)
            if not target.st.can_apply(gd, stb_row, value):
                continue
            ing = target.st.apply(gd, stb_row, sk.duration, value, sk.row)
            if ing == _FAINT and target is self.p:
                self.p_stun_ends = self.t + sk.duration * 1000
                if stun_guard:
                    self.p_stun_immune_until = self.p_stun_ends + gd.server.stun_immunity_ms
            if ing == _TAUNT and mob is not None:
                # Skill_ApplyIngSTATUS: a landed Taunt turns the target on its caster
                mob.taunter = caster
                mob.order_attack(caster, forced=True)
            if ing == ING["DUMMY_DAMAGE"]:
                target.dummy_skill_power = sk.power
                target.refresh()
            if ing in (_SLEEP, _FAINT):
                # UpdateIngSTATUS -> Del_ActiveSKILL + CObjAI::SetCMD_STOP
                if mob is not None:
                    mob.disable()
                else:
                    self.pa.motion = None
                    self.p_state, self.p_pending = "idle", None
            rec["states"] += 1

    def _on_player_motion_end(self):
        if self.p.hp <= 0 or self.p.st.disabled():
            self.pa.motion = None
            return
        if self.m.hp <= 0:
            # no monster (between grinding pulls): only a heal / mana skill if needed
            self.pa.motion = None
            if self.p_state == "cast":
                self._skill_action(self.p_pending)
                return
            self.p_state = "idle"
            for sk in self.sim.rotation:
                if sk.restores() and self._skill_ready(sk):
                    self._skill_start(sk)
                    return
            return
        self._player_next()

    def status_flags(self, who, kind):
        """Get_MagicSTATUS & FLAG_ING_BAD (0) / FLAG_ING_GOOD (1) / any (2), by each
        state's STATE_PRIFITS_LOSSES."""
        for stb in who.st.stb.values():
            if kind == 2:
                return True
            if bool(self.gd.state_harmful(stb)) == (kind == 0):
                return True
        return False

    def _poison(self, unit):
        """The per-second hook for `unit`: poison deals its LIST_STATUS value
        (STATE_APPLY_ABILITY_VALUE) through Give_STATUS_DAMAGE, which never kills (it
        leaves 1 HP), wakes nobody and runs no AI."""
        def tick(ing):
            if ing != _POISON or unit.hp <= 1:
                return
            dmg = min(self.gd.state_apply(unit.st.stb[ing], 0)[1], unit.hp - 1)
            if dmg <= 0:
                return
            unit.hp -= dmg
            lg, row = self.log, unit.st.skill.get(ing)
            if unit is self.p:
                lg.m_dot_dmg += dmg
            elif unit is self.m and row in self.cost:
                lg.skill(row)["dot"] += dmg
            elif unit is self.m:
                lg.s_dmg += dmg
            else:
                lg.ms_dmg += dmg
        return tick

    def _respawn(self):
        """Survival runs: the monster that would have died is replaced by a fresh one."""
        self.enemy = Mob(self, self.sim.make_monster(), self.sim.npc_row, self.ai)
        self.log.respawns += 1
        self.enemy.swing()
        for s in self.live_summons():       # their target is gone; the STOP pattern finds
            s.reset(None)                   # the new one

    def _spawn_summons(self):
        """The build's summons are out when the fight starts (called before the pull)."""
        for i, sk in enumerate(self.sim.summons):
            self._summon(sk, i)
        self.log.s_calls = 0

    def _drop_dead_summons(self):
        for i, s in enumerate(self.summons):
            if s is not None and s.u.hp <= 0:
                self.summons[i] = None
                self.log.s_deaths += 1

    # ------------------------------------------------------------ upkeep
    def _regen(self, ms):
        """CObjAVT::Check_PerFRAME, not sitting: every 2 s, (Recover + (CON+40)/6.f)/6.f."""
        self.regen_acc += ms
        while self.regen_acc >= RECOVERY_MS:
            self.regen_acc -= RECOVERY_MS
            if self.p.hp <= 0:
                return
            if self.immortal:
                self.hp_trace.append((self.t, self.p.hp))
            con = self.sim.stats.cur["CON"]
            st = self.sim.stats
            if self.resting:            # CObjAI::Get_RecoverHP / MP(RECOVERY_RATE_SITTING)
                hp = st.recover_hp + (con + 30) // 8 * (RECOVERY_RATE_SITTING + 3) // 10
                mp = st.recover_mp + (con + 20) // 10 * RECOVERY_RATE_SITTING // 7
            else:
                hp = int((st.recover_hp + (con + 40) / 6.0) / 6.0)
                mp = int((st.recover_mp + (con + 40) / 6.0) / 6.0)
            if self.p.st.is_set(_POISON):
                hp = 0                  # poisoned: no HP regen, standing or sitting
            gain = min(hp, self.p.max_hp - self.p.hp)
            self.p.hp += gain
            self.log.regen_hp += gain
            gain = min(mp, self.p.max_mp - self.p.mp)
            self.p.mp += gain
            self.log.regen_mp += gain

    def _potions(self, ms):
        pot = self.sim.build.potions
        if not pot["enabled"]:
            return
        gd = self.gd
        # running over-time potions (StatusEffects::Proc_IngPOTION)
        for kind in ("hp", "mp"):
            run = self.potion_run.get(kind)
            if not run:
                continue
            run["acc_time"] += ms
            cur = int(run["acc_time"] * run["total"] / run["tot_time"])
            diff = (run["total"] - run["acc"]) if cur >= run["total"] else cur - run["acc"]
            run["acc"] += diff
            if kind == "hp":
                self.p.hp = min(self.p.max_hp, self.p.hp + diff)
                self.log.potion_hp += diff
                full = self.p.hp >= self.p.max_hp
            else:
                self.p.mp = min(self.p.max_mp, self.p.mp + diff)
                self.log.potion_mp += diff
                full = self.p.mp >= self.p.max_mp
            if full or run["acc"] >= run["total"]:
                del self.potion_run[kind]
        # drink when below the threshold
        for kind, cur, mx in (("hp", self.p.hp, self.p.max_hp), ("mp", self.p.mp, self.p.max_mp)):
            row = pot[kind]
            if not row or self.p.hp <= 0 or cur * 100 >= mx * pot[kind + "_below"]:
                continue
            ui = gd.stb("LIST_USEITEM")
            status = ui.i(row, 24)
            total = ui.i(row, 20)
            restore_ms = 0
            if not status and ui.i(row, 19) in (AT["HP"], AT["MP"]):
                # an instant restore (the waters, UseItem_RestoreValue / _CooldownMS):
                # col 20 + col 21 % of the maximum, col 26 ms between two of a kind
                total += mx * ui.i(row, 21) // 100
                restore_ms = max(0, ui.i(row, 26))
            delay = max(POTION_DELAY_MS, restore_ms, int(pot.get("cooldown_s", 0) * 1000))
            if self.t - self.last_potion[kind] < delay or kind in self.potion_run:
                continue
            self.last_potion[kind] = self.t
            self.log.potions[kind] += 1
            if status:
                # heal over time (UseItem_PotionDurationMS): col 20 over col 21 seconds,
                # or the status row's rate per second when col 21 is blank
                per_sec = next((v for _, v in (gd.state_apply(status, n) for n in range(2)) if v), 0)
                seconds = ui.i(row, 21)
                if seconds > 0 or per_sec:
                    tot_time = seconds * 1000 if seconds > 0 else 1000 * total // per_sec
                    self.potion_run[kind] = dict(total=total, acc=0, acc_time=0,
                                                 tot_time=max(1, tot_time))
            else:
                if kind == "hp":
                    gain = min(total, self.p.max_hp - self.p.hp)
                    self.p.hp += gain
                    self.log.potion_hp += gain
                else:
                    gain = min(total, self.p.max_mp - self.p.mp)
                    self.p.mp += gain
                    self.log.potion_mp += gain

    def _extrapolate(self):
        """Still alive at the cap of a survival run: project the death time
        from the HP lost over the second half of the run (the steady state, once the
        opening MP pool and any potions have played out). None = never dies."""
        tr = self.hp_trace
        if len(tr) < 8:
            return None
        h = len(tr) // 2
        q = h + (len(tr) - h) // 2
        a, b = tr[h:q], tr[q:]
        mean_a = sum(x for _, x in a) / len(a)
        mean_b = sum(x for _, x in b) / len(b)
        dt = (sum(t for t, _ in b) / len(b) - sum(t for t, _ in a) / len(a)) / 1000.0
        loss = (mean_a - mean_b) / dt if dt > 0 else 0
        if loss <= 0.05:          # under 0.05 HP/s net: regen keeps up
            return None
        return self.t / 1000.0 + max(0, self.p.hp) / loss

    # ------------------------------------------------------------ run
    def run(self):
        lg = self.log
        e = self.enemy
        if e.brain:
            # the monster spawned before the pull: only its CREATED-pattern AI variables
            # are still in effect when the fight starts
            e.created_only = True
            e.ai_run(CREATED)
            e.created_only = False
        self._spawn_summons()
        self._player_next()           # both open on the first tick in range
        e.swing()
        while True:
            if self.m.hp <= 0 and self.immortal and self.p.hp > 0:
                if self.next_spawn is None and not self.resting:
                    lg.kills += 1
                    p = self.p
                    if self.gap_ms and (p.hp < REST_BELOW[0] * p.max_hp
                                        or p.mp < REST_BELOW[1] * p.max_mp):
                        self.resting = True
                    else:
                        self.next_spawn = self.t + self.gap_ms
                if self.resting:
                    p = self.p
                    lg.rest_ms += TICK_MS
                    if (p.hp >= REST_UNTIL[0] * p.max_hp and p.mp >= REST_UNTIL[1] * p.max_mp):
                        self.resting = False
                        self.next_spawn = self.t + self.gap_ms
                if self.next_spawn is not None and self.t >= self.next_spawn:
                    self.next_spawn = None
                    self._respawn()
                    if self.gap_ms and not self.p.st.disabled() and self.p_state != "cast":
                        self._player_next()     # it walked up: attack the new one
            if self.m.hp <= 0 and not (self.immortal and self.p.hp > 0):
                lg.result = "win"
                break
            if self.p.hp <= 0:
                lg.result = "loss"
                break
            if self.t >= self.max_ms:
                lg.result = "timeout"
                break
            self.t += TICK_MS
            if self.p_stun_ends is not None:
                # CObjCHAR::Proc_IngSTATUS: a player's stun ends when its duration is up,
                # not on the status tick after
                if not self.p.st.is_set(_FAINT):
                    self.p_stun_ends = None
                elif self.t >= self.p_stun_ends:
                    self.p_stun_ends = None
                    self.p.st.clear(_FAINT)
            if self.p.st.ticks:
                for ing, tk in self.p.st.ticks.items():
                    if tk is not None:
                        lg.player_state_ms[ing] = lg.player_state_ms.get(ing, 0) + TICK_MS
                self.p.st.proc(TICK_MS, self._poison(self.p))
            if self.m.st.val:
                for ing, stb in self.m.st.stb.items():
                    d = lg.debuff_ms if self.gd.state_harmful(stb) else lg.monster_buff_ms
                    d[ing] = d.get(ing, 0) + TICK_MS
                self.m.st.proc(TICK_MS, self._poison(self.m))
            for s in self.live_summons():
                if s.u.st.ticks:
                    s.u.st.proc(TICK_MS, self._poison(s.u))
            if self.p.st.disabled():
                lg.player_disabled_ms += TICK_MS
            else:
                self.pa.advance(TICK_MS, self._on_player_motion_end)
            if self.m.hp > 0:
                self.enemy.tick(TICK_MS)
            for s in self.live_summons():
                s.tick(TICK_MS)
            self._drop_dead_summons()
            if self.p.hp > 0:
                self._regen(TICK_MS)
                self._potions(TICK_MS)
            if lg.oom_ms is None and self.min_cost is not None and self.p.mp < self.min_cost:
                lg.oom_ms = self.t
        lg.end_ms = self.t
        lg.surv_est = self._extrapolate() if self.immortal and lg.result == "timeout" else None
        lg.hp_left, lg.mp_left = max(0, self.p.hp), max(0, self.p.mp)
        lg.max_hp, lg.max_mp = self.p.max_hp, self.p.max_mp
        lg.monster_hp_left = max(0, self.m.hp)
        return lg
