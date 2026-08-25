"""Move buffs and passives from LIST_SKILL.STB's flat column onto its percentage column.

Why
---
Every buff and every passive in our data carries its effect in the FLAT column
(`SKILL_INCREASE_ABILITY_VALUE`, game col 22/25); the percentage column
(`SKILL_CHANGE_ABILITY_RATE`, col 23/26) is zero almost everywhere. Those flat
numbers were sized for a level-100 cap, so their real worth decays as the
character grows -- Power Support's +70 ATK is 19% of a level-100 character and
7% of a level-240 one; Two-Hand Mastery's +110 is 29% then 11%. Since Rose Next
removed cleric buffs and compensated with starting stats tuned for that same
level-100 cap, the decay lands squarely on the drift this is meant to fix.

The engine already reads the percentage column on both sides:
  * passives  -- `CUserDATA::InitPassiveSkill` / `Skill_LEARN` call
                 `AddPassiveSkillRate`, and every `Cal_*` applies it. The client
                 and server copies of `InitPassiveSkill` are byte-identical.
  * buffs     -- `CCal::Get_SkillAdjustVALUE` sums a rate term and a flat term.

So this is a data change. See `doc/balance-analysis.md` for the measurements.

Two column semantics that differ, and bite in opposite directions
-----------------------------------------------------------------
  * For PASSIVES the loader is `if (RATE) {...} else {...}` -- either/or. A
    non-zero rate makes the flat value dead data.
  * For BUFFS `Get_SkillAdjustVALUE` ADDS both terms. Leaving the flat value in
    place would apply the old bonus AND the new one.
Both cases are handled by zeroing the flat column whenever a rate is written, so
the two behave the same way and the data says what it means.

Prerequisite (already fixed in src/, do not run this against an older server)
----------------------------------------------------------------------------
`Get_SkillAdjustVALUE` used to take the percentage off the target's *current*
stat, which already includes the running buff, while
`StatusEffects::IsEnableApplay` rejects a recast only when it is weaker. A
percentage buff therefore compounded on every recast and settled at
`rate/(1-rate)`: a declared +30% delivered +43%, +50% delivered +100%, and
anything >= 100% grew until the `(short)` cast overflowed. It now reads
`Get_BaseAbilityValue`. Passives never had the bug (their rate is applied to a
base that each `Cal_*` recomputes from scratch), which is why they could have
been converted without the fix.

How the numbers are chosen
--------------------------
Each percentage reproduces what the flat value was worth at LEVEL 100 -- the cap
Rose Next was originally balanced for -- so the conversion is deliberately
neutral at that level, slightly weaker below it (the flat values were
over-generous for a low-level character) and progressively stronger above it.
It is a self-correcting curve rather than a flat buff.

Anchor stats come from `scripts/balance-sim.py`'s player model, so they
re-derive if gear or formulas change. Lower ranks keep their existing shape:
`rate_i = anchor_rate * flat_i / flat_max`.

SELF_BOOST multiplies the percentage for effects a player can put on themselves
without a support class in the party (all passives, plus single-target self
buffs with no area of effect). Party-facing buffs -- the scope>0 auras and the
type-9 buffs cast on someone else -- stay at the anchor so stacking a full party
does not run away. 1.25 was picked by simulation, not taste: at 1.25 a solo
character's level-60 kill time stays close to what the flat kit gives today
while level-220 fights get about 2.2-2.4x shorter. 1.5 and above start
trivialising the early game.

Deliberately NOT converted, and why
-----------------------------------
  * AT_HP / AT_MP effects (Healing, Cure, Mana Blood, the type 10/11 instants).
    `Get_SkillAdjustVALUE` resolves AT_HP to *current* HP, so a percentage heal
    would heal a fraction of what you have left -- worthless exactly when you
    need it. Converting heals needs an AT_MAX_HP-based redesign, not a column
    move.
  * AT_HIT. Accuracy is a cliff, not a slope (`iSuc < 20` drops every swing to a
    flat ~7% chance), and the builds pinned at 7% are the low-CON ones with the
    least HIT -- so a percentage would hand them *less* than the flat value they
    get now. Hit Support's +80 is worth 61% to a level-100 Raider; a median-
    anchored 45% would be a nerf to the build that needs it most. Leave HIT flat
    until the cliff itself is fixed.
  * AT_SPEED (movement). Not a combat-throughput lever, and speed has its own
    feel and caps.
  * The base stats STR..SEN. `Skill_LEARN`'s stat-passive branch has a
    `return 0x03;` *inside* the ability-slot loop, so slot 1 of such a passive is
    already ignored; do not build on it.
  * Harmful states (`LIST_STATUS.STB` PRIFITS_LOSSES != 0) -- Fire Ring,
    Freezing, Mild and the event debuffs. This is a player-power pass.
  * Rows with no rank progression (event / GM / NPC skills, ranks 0).
  * Anything that already carries a rate.

Note `m_Battle.m_nMaxHP` and friends are `short`, so the ceiling on any HP
percentage is 32767; the current numbers land near 5,700 at level 240.

After running: restart the servers (STBs are cached at startup) and re-bake the
client VFS. Existing characters need no migration -- `InitPassiveSkill` rebuilds
passive state from the table on load.

Usage:
    python scripts/convert-buffs-to-percent.py --dry-run
    python scripts/convert-buffs-to-percent.py
    python scripts/convert-buffs-to-percent.py --verify
    python scripts/convert-buffs-to-percent.py --restore
"""
import argparse
import importlib.util
import io
import json
import os
import shutil
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKILL_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.STB")
STATUS_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_STATUS.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_SKILL.pct-buffs.json")

ANCHOR_LEVEL = 100
SELF_BOOST = 1.25

# LIST_SKILL game columns (src/common/io_skill.h)
C_BASE, C_RANK, C_TYPE, C_SCOPE = 1, 2, 5, 8
C_STATE_STB = 11
C_ABIL = lambda t: 21 + t * 3
C_FLAT = lambda t: 22 + t * 3
C_RATE = lambda t: 23 + t * 3
ABILITY_SLOTS = 2

SKILL_TYPE_SELF_DUR, SKILL_TYPE_TARGET_DUR, SKILL_TYPE_PASSIVE = 8, 9, 15

# t_AbilityINDEX (src/common/shared/datatype.h)
AT_ATK, AT_DEF, AT_RES, AT_AVOID, AT_CRITICAL = 18, 19, 21, 22, 26
AT_ASPD, AT_MAX_HP, AT_MAX_MP = 24, 38, 39
AT_PSV_ATK_1H, AT_PSV_ATK_2H, AT_PSV_ATK_BOW = 42, 43, 44
AT_PSV_ATK_GUN, AT_PSV_ATK_STAFF, AT_PSV_ATK_XBOW, AT_PSV_ATK_KATAR = 45, 46, 47, 48
AT_PSV_ASPD_BOW, AT_PSV_ASPD_GUN, AT_PSV_ASPD_PAIR = 49, 50, 51
AT_PSV_DEF_POW, AT_PSV_MAX_HP, AT_PSV_MAX_MP = 53, 54, 55
AT_PSV_RES, AT_PSV_CRITICAL, AT_PSV_AVOID, AT_PSV_SHIELD_DEF = 98, 100, 101, 102

# ability -> (stat to anchor against, archetypes to anchor on; None = all six)
CONVERTIBLE = {
    AT_ATK:              ("ATK", None),
    AT_DEF:              ("DEF", None),
    AT_RES:              ("RES", None),
    AT_AVOID:            ("AVOID", None),
    AT_CRITICAL:         ("CRIT", None),
    AT_ASPD:             ("ASPD", None),
    AT_MAX_HP:           ("MAXHP", None),
    AT_MAX_MP:           ("MAXMP", None),
    AT_PSV_ATK_1H:       ("ATK", ("Knight",)),
    AT_PSV_ATK_2H:       ("ATK", ("Champion",)),
    AT_PSV_ATK_BOW:      ("ATK", ("Scout",)),
    AT_PSV_ATK_GUN:      ("ATK", ("Bourgeois",)),
    AT_PSV_ATK_STAFF:    ("ATK", ("Mage",)),
    AT_PSV_ATK_XBOW:     ("ATK", ("Bourgeois",)),
    AT_PSV_ATK_KATAR:    ("ATK", ("Raider",)),
    AT_PSV_ASPD_BOW:     ("ASPD", ("Scout",)),
    AT_PSV_ASPD_GUN:     ("ASPD", ("Bourgeois",)),
    AT_PSV_ASPD_PAIR:    ("ASPD", ("Raider",)),
    AT_PSV_DEF_POW:      ("DEF", ("Knight", "Champion")),
    AT_PSV_SHIELD_DEF:   ("DEF", ("Knight",)),
    AT_PSV_MAX_HP:       ("MAXHP", ("Knight", "Champion")),
    AT_PSV_MAX_MP:       ("MAXMP", ("Mage",)),
    AT_PSV_RES:          ("RES", None),
    AT_PSV_CRITICAL:     ("CRIT", None),
    AT_PSV_AVOID:        ("AVOID", None),
}


# ------------------------------------------------------------------ STB I/O
class Stb:
    def __init__(self, path):
        raw = open(path, "rb").read()
        if raw[:4] != b"STB1":
            raise SystemExit(f"{path}: not an STB1 file")
        self.path = path
        self.data_offset, rr, rc = struct.unpack_from("<III", raw, 4)
        self.rows, self.cols = rr - 1, rc - 1
        self.header = raw[16:self.data_offset]
        f = io.BytesIO(raw)
        f.seek(self.data_offset)

        def cell():
            n, = struct.unpack("<H", f.read(2))
            return f.read(n)

        self.d = [[cell() for _ in range(self.cols)] for _ in range(self.rows)]
        self.tail = f.read()

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

    def set_i(self, r, c, v):
        self.d[r][c] = str(int(v)).encode("latin-1")

    def write(self, path):
        out = io.BytesIO()
        out.write(b"STB1")
        out.write(struct.pack("<III", 16 + len(self.header), self.rows + 1, self.cols + 1))
        out.write(self.header)
        for row in self.d:
            for cell in row:
                out.write(struct.pack("<H", len(cell)))
                out.write(cell)
        out.write(self.tail)
        with open(path, "wb") as fh:
            fh.write(out.getvalue())


def load_sim():
    spec = importlib.util.spec_from_file_location("balance_sim",
                                                  os.path.join(HERE, "balance-sim.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ------------------------------------------------------------------ selection
def families(skill):
    fam = {}
    for r in range(1, skill.rows):
        if not skill.get(r, 0).strip():
            continue
        fam.setdefault(skill.i(r, C_BASE) or r, []).append(r)
    return fam


def plan(skill, status, sim):
    """Return [(row, slot, ability, old_flat, new_rate, label)] plus skipped reasons."""
    P = {n: sim.make(n, ANCHOR_LEVEL) for n in sim.PROFILES}

    def stat_of(pl, key):
        return dict(ATK=pl.atk, DEF=pl.df, RES=pl.res, AVOID=pl.avoid, CRIT=pl.crit,
                    ASPD=pl.aspd, MAXHP=pl.maxhp, MAXMP=pl.maxmp)[key]

    def anchor_pct(flat, key, profs):
        vals = [flat / stat_of(P[c], key) * 100 for c in (profs or P)]
        return statistics.median(vals)

    actions, skipped = [], []
    for base, rows in sorted(families(skill).items()):
        typ = skill.i(rows[0], C_TYPE)
        if typ not in (SKILL_TYPE_SELF_DUR, SKILL_TYPE_TARGET_DUR, SKILL_TYPE_PASSIVE):
            continue
        top = max(rows, key=lambda r: skill.i(r, C_RANK))
        name = skill.s(top, 0)
        if skill.i(top, C_RANK) < 1:
            continue                                   # NPC / unranked rows
        # Every player-facing buff and passive has a rank ladder (5-20 steps).
        # A single-rank family is a GM scroll or an event buff -- e.g. the +500
        # ATK/DEF/RES "invincibility" rows, which anchor out at 157-190% and are
        # meant to be absurd flat numbers, not a scaling percentage.
        if len({skill.i(r, C_RANK) for r in rows}) < 2:
            skipped.append((name, "single-rank event/GM skill"))
            continue

        if typ != SKILL_TYPE_PASSIVE:
            st = skill.i(top, C_STATE_STB)
            if st and status.i(st, 3) != 0:
                skipped.append((name, "harmful state"))
                continue

        # Self-reachable: every passive, plus a self buff with no area of effect.
        # A scope>0 aura is a party buff by design even though the caster benefits.
        self_reachable = (typ == SKILL_TYPE_PASSIVE
                          or (typ == SKILL_TYPE_SELF_DUR and skill.i(top, C_SCOPE) == 0))
        boost = SELF_BOOST if self_reachable else 1.0

        for slot in range(ABILITY_SLOTS):
            abil = skill.i(top, C_ABIL(slot))
            if not abil:
                continue
            if any(skill.i(r, C_RATE(slot)) for r in rows):
                skipped.append((f"{name} slot{slot}", "already uses a rate"))
                continue
            if abil not in CONVERTIBLE:
                skipped.append((f"{name} slot{slot}", f"ability {abil} not converted"))
                continue
            flat_max = skill.i(top, C_FLAT(slot))
            if flat_max <= 0:
                continue
            key, profs = CONVERTIBLE[abil]
            pct_max = anchor_pct(flat_max, key, profs) * boost
            if pct_max > 100:
                # A rate at or above 100% doubles the stat off one cast and is
                # far outside anything the flat data expressed; treat it as a
                # scoping mistake rather than writing it.
                skipped.append((f"{name} slot{slot}",
                                f"anchor {pct_max:.0f}% exceeds the 100% sanity cap"))
                continue
            for r in rows:
                flat = skill.i(r, C_FLAT(slot))
                if flat <= 0:
                    continue
                rate = max(1, int(round(pct_max * flat / flat_max)))
                actions.append((r, slot, abil, flat, rate,
                                f"{name} rank{skill.i(r, C_RANK)} {key}"
                                f"{' [self]' if self_reachable else ''}"))
    return actions, skipped


# ------------------------------------------------------------------ commands
def apply(actions, skill, dry):
    for r, slot, _abil, _flat, rate, _lbl in actions:
        if dry:
            continue
        skill.set_i(r, C_RATE(slot), rate)
        skill.set_i(r, C_FLAT(slot), 0)   # flat is either ignored (passives) or added (buffs)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="list every rank, not just each family's top rank")
    a = ap.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass

    for p in (SKILL_STB, STATUS_STB):
        if not os.path.exists(p):
            raise SystemExit(f"missing {p}")

    skill = Stb(SKILL_STB)

    if a.restore:
        if not os.path.exists(SIDECAR):
            raise SystemExit(f"no sidecar at {SIDECAR}; nothing to restore")
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)
        for rec in saved["actions"]:
            skill.set_i(rec["row"], C_FLAT(rec["slot"]), rec["flat"])
            skill.set_i(rec["row"], C_RATE(rec["slot"]), rec["rate_before"])
        skill.write(SKILL_STB)
        os.remove(SIDECAR)
        print(f"restored {len(saved['actions'])} cells from the sidecar")
        return

    status = Stb(STATUS_STB)
    sim = load_sim()
    actions, skipped = plan(skill, status, sim)

    if a.verify:
        if not os.path.exists(SIDECAR):
            raise SystemExit(f"no sidecar at {SIDECAR}; conversion has not been applied")
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)
        bad = 0
        for rec in saved["actions"]:
            got_rate = skill.i(rec["row"], C_RATE(rec["slot"]))
            got_flat = skill.i(rec["row"], C_FLAT(rec["slot"]))
            if got_rate != rec["rate"] or got_flat != 0:
                bad += 1
                print(f"  MISMATCH row {rec['row']} slot {rec['slot']}: "
                      f"rate {got_rate} (want {rec['rate']}), flat {got_flat} (want 0)")
        print(f"verify: {len(saved['actions']) - bad}/{len(saved['actions'])} cells correct")
        sys.exit(1 if bad else 0)

    if os.path.exists(SIDECAR):
        print("sidecar already present -- conversion has been applied. "
              "Use --verify, or --restore then re-run.")
        sys.exit(1)

    if not a.quiet:
        print(f"anchor level {ANCHOR_LEVEL}, self-reachable boost x{SELF_BOOST}\n")
        print(f"{'row':>6}{'slot':>5}{'flat':>7}{'->rate':>8}  effect")
        top_rows = {r for r, *_ in actions}
        if not a.all:
            # one line per family/slot: the top rank, which is what needs review
            best = {}
            for rec in actions:
                r, slot = rec[0], rec[1]
                key = (skill.i(r, C_BASE) or r, slot)
                if key not in best or skill.i(r, C_RANK) > skill.i(best[key][0], C_RANK):
                    best[key] = rec
            shown = [best[k] for k in sorted(best)]
        else:
            shown = actions
        for r, slot, _abil, flat, rate, lbl in shown:
            print(f"{r:>6}{slot:>5}{flat:>7}{rate:>7}%  {lbl}")
        if not a.all:
            print(f"  (top rank of each family shown; --all lists all "
                  f"{len(actions)} cells)")
        if skipped:
            print("\nskipped:")
            seen = set()
            for what, why in skipped:
                if (what, why) in seen:
                    continue
                seen.add((what, why))
                print(f"  {what:<40} {why}")
    print(f"\n{len(actions)} cells across "
          f"{len({r for r, *_ in actions})} skill rows")

    if a.dry_run:
        print("dry run -- nothing written")
        return

    # Keep the backup OUT of data/: src/pipeline/src/pack.rs walks the data tree
    # with no extension filter, so a stray .bak gets baked into the .vfs.
    backup_dir = os.path.join(ROOT, "build", "pct-buffs-backup")
    os.makedirs(backup_dir, exist_ok=True)
    shutil.copy2(SKILL_STB, os.path.join(backup_dir, "LIST_SKILL.STB.bak"))
    payload = dict(anchor_level=ANCHOR_LEVEL, self_boost=SELF_BOOST, actions=[
        dict(row=r, slot=slot, ability=abil, flat=flat, rate=rate,
             rate_before=0, label=lbl)
        for r, slot, abil, flat, rate, lbl in actions])
    apply(actions, skill, dry=False)
    skill.write(SKILL_STB)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1)

    check = Stb(SKILL_STB)
    bad = sum(1 for r, slot, _a, _f, rate, _l in actions
              if check.i(r, C_RATE(slot)) != rate or check.i(r, C_FLAT(slot)) != 0)
    if bad:
        raise SystemExit(f"write-back verification FAILED for {bad} cells; "
                         f"restore from {SKILL_STB}.bak")
    print(f"written and verified. sidecar: {SIDECAR}")
    print(f"pre-change copy kept at {backup_dir} (outside data/, so it is never baked)")


if __name__ == "__main__":
    main()
