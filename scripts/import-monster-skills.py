"""Import a monster's skill kit from a reference dump, with its effect chain.

The 667 Oro import brought the monsters and kept RoseZA's AI files for them, but
never the *skills* those AI files cast: `OR_THORNIE.AIP` (Fearsome Terrasaurus
King, LIST_NPC 2265) casts 3603 "Charge" and 3604 "Fireball", both blank rows in
our LIST_SKILL. The result was a boss that played a casting animation for nothing
(and, until the client learned to present a pre-empted swing, ate the damage of
the swing it interrupted -- see `audit-ai-skill-refs.py`). This script is the
other half of that audit: instead of stripping the cast, bring the skill in.

What a monster skill needs (traced from io_skill.h and the client consumers)
---------------------------------------------------------------------------
  LIST_SKILL row      cols 0-86 (ours has 87 columns; RoseZA has 114, the rest is
                      newer-client data we do not read)
  FILE_EFFECT rows    cols 56/59/62/65 casting effects, 74 hit effect, 77/80 hit
                      dummy effects -> `g_pEffectLIST->Add_EffectWithIDX`
  FILE_SOUND rows     cols 58/61/64/67 casting, 73 bullet fire, 76 hit, 79/82
                      hit dummy -> `g_pSoundLIST->IDX_PlaySound3D`
  LIST_EFFECT row     col 71 SKILL_BULLET_NO -> `EFFECT_BULLET_NORMAL(I)` is
                      LIST_EFFECT col 11 (a FILE_EFFECT row), cols 9/10 the hit
                      effects, 13-15 move type / bullet type / speed, 16/17 sounds
  the .eft files and, transitively, their .PTL / texture / mesh files
                      (`import-oro.py: effect_chain`)
  the monster's CHR   the cast motion comes from the AI action's nMotion, not from
                      the skill row (CObjMOB::GetANI_Casting == m_cSkillMotionIDX,
                      the action motion is that +1) -- `import-oro-667.py` step 3h
                      already proved those slots animate on this model.

Every index above resolves to the same row in our tables as in RoseZA's, because
our FILE_EFFECT / FILE_SOUND / LIST_EFFECT are the same lineage: the survey for
this boss found identical paths at 95, 71, 1035 (effects), 67, 91, 93, 148, 1121
(sounds), and *blank* rows at exactly the indices the new skills need (FILE_EFFECT
1881-1884, LIST_EFFECT 476). So rows are written **in place at the source index**,
and the script refuses if a needed row is occupied by something else -- that is
the signal that a future dump's namespace has drifted and the row needs re-pointing
(the Artisan recipe, `import-artisan-skill.py`).

Damage is deliberately NOT copied
---------------------------------
RoseZA's SKILL_POWER (Fireball 3500, Charge 2000) is sized for RoseZA's stat
scale, the same way the monsters' HP/ATK were before `rebalance-oro-667.py`.
Through `CCal::Get_SkillDAMAGE` (monster branch) against a synthetic level-240
Knight (balance-sim.py `make("Knight", 240)`: 3918 HP, DEF 2004, RES 1060) they
would deal ~7250 and ~4350 -- an instant kill next to the boss's ~330 normal swing.
Two things fix the scale:

  * Our own monster attack skills (every AIACT24 cast in our .aip files, 39
    rows) run SKILL_POWER 25-100, median 50; the highest damage-dealing one is
    Tornado at 501.
  * For a 2900-ATK boss the *weapon* formula (SKILL_DAMAGE_TYPE 1) is dominated by
    the `(power + atk*0.2) * (atk+60)` term: even power 0 lands ~1400 on that
    Knight (36% of HP), and 667's Charge is authored as type 1. So both skills use
    the magic formula (type 2), which is linear in power.

    The first cut (450 / 350) was sized on balance-sim's synthetic Knight and came
    out 2x too hot on the real tester (6102 HP, evidently far less DEF/RES than
    the synthetic block): Fireball 2865-2923, Charge 2174-2290, i.e. 6.4 damage
    per power point on both. The powers are therefore calibrated on the measured
    ratio, to about a quarter (Fireball) and a fifth (Charge) of that tester's HP:

        Fireball  power 230  ->  ~1500 measured-scale   (vs its 470-620 swing)
        Charge    power 190  ->  ~1200 measured-scale

    A well-geared level-240 character takes roughly half of that. `--power`
    overrides per skill for tuning; `scripts/balance-sim.py`'s
    `magic_skill_damage` re-derives the synthetic side.

The status columns (11/12) are cleared: 667 attaches LIST_STATUS 150/120 there and
our status table has 62 rows. Everything else in the row is the source's. A skill
whose point *is* a status gets it back through `set` from OUR LIST_STATUS -- see
the Devourer below.

Grand Master Devourer (LIST_NPC 2226, the Oro odd04/05 boss; RoseZA's
OR_GMdevourer1.aip), third use, 2026-09-14
-------------------------------------------------------------------------------
Four casts on cur-target from its attack-move pattern: 3609 dispel, 3610 slow,
3611 stun+damage AOE, 3613 ranged bolt. Three things had to be re-based:

  * **RoseZA authors the status in column 88, which our 87-column table never
    reads.** Its rows carry 37 (Cancel Buffs) / 15 (Slow Run) / 32 (Stun) there
    and only a legacy 38 in column 11 on 3609; 667 moved the same ids to column
    90 (and its own 117 for the dispel). Copied verbatim the kit would be a stun
    that does not stun. `set` writes the same ids into our column 11 -- rows 15,
    32 and 37 are identical here and in RoseZA (the server's
    `Skill_ApplyIngSTATUS` reads SKILL_STATE_STB1/2, cols 11/12). The slow also
    needs a magnitude: `Get_SkillAdjustVALUE` takes ability (col 21) x rate
    (col 23), and every slow we ship is authored as AT_SPEED (23) at 30-70% --
    40% for 30 s here, the retail duration.
  * **3613 collides**: ours is Karkia Stun (the Jrose port, type 17); RoseZA's
    is a type-6 projectile (bullet LIST_EFFECT 162, casting fx 1358, hit fx 94,
    all present here at the same index). Appended at **7013** and the AI
    re-pointed: `audit-ai-skill-refs.py --remap or_gmdevourer1.aip:3613=7013`.
  * **Power**: 3611 (400) and 3613 (300) are weapon-formula (type 1) rows, which
    for a 2700-ATK boss floor around 1400 regardless of power (see the boss
    notes above). Both go to the magic formula; the Devourer's ATK/INT are ~93%
    of Terrasaurus King's, so ~6.0 damage per power point on the tester:
    Stun Blast 150 -> ~900 plus a 3 s stun, Range Attack 200 -> ~1200. 3609 and
    3610 deal no damage (types 12/8).

The rows import cleanly but the boss still could not *present* them: its model
has two wind-up/release clip pairs at CHR slots 6/7 and 8/9 (charge, skill01,
charge, skill02) and RoseZA's AI casts on nMotion 7 and 9 -- so our client
(cast = nMotion, release = nMotion+1) released on the event-less charge clip
and on a slot that does not exist, and the self-buffs on 2 released on the hit
clip. Fixed in the AI, not the CHR:
    audit-ai-skill-refs.py --remotion or_gmdevourer1.aip:7=6 \
                           --remotion or_gmdevourer1.aip:9=8 \
                           --remotion or_gmdevourer1.aip:2=6
Dispel is the retail design (a boss that strips your buffs); it cancels every
buff, where 667 rolls 3-8. Tune with `set` if it plays too mean.

The first in-game test (2026-09-14) never rolled the four: they sit on the
"attack move" pattern behind 25% (dispel, and only while you carry a buff),
8% (slow, and only when someone other than its target hit it), 8% and 5% (the
two damage casts, with an enemy in reach) -- and that pattern is evaluated only
while the boss is chasing, which a melee fighter standing on it rarely makes it
do. What the tester saw instead was the boss winding up, playing a spell sound
and applying nothing: its when-damaged self-casts **3596 / 3597**, the same
RoseZA rows as 3609 / 3610 imported nameless by the old Oro import with the
column-88 status lost. They are re-imported here over the identical rows
(`overwrite`), which also fixes the Eldeon/Karkia casters sharing them
(ed_s_kera01, ed_s_woman02, ks_2685). Survey note: ~25 more RoseZA-lineage rows
cast by shipped AI carry a column-88 status our column 11 lacks or disagrees
with -- several stuns (3527, 3551, 3560, 3562, 3572, 3582, 3595 -> 32) on
Eldeon and Karkia monsters. Left for their own session.

Eldeon's rows are ruff's, not RoseZA's (2026-09-15)
--------------------------------------------------
That survey compared against the wrong source. Our Eldeon skill rows are
byte-identical (cols 1-86) to the ruff dump's, an older authoring than RoseZA's,
and ruff's LIST_STATUS is our numbering -- so where our column 11 "disagrees"
with RoseZA's column 88 the row is usually doing what ruff meant, just something
different from RoseZA (the ants buff DEF here and RES there; the Melts and
prisoners debuff DEF here and ATK there; Thorn Hound and the Gargoyle give
themselves +300% run speed, which is odd but is what ruff shipped). Policy: keep
what works even where it differs, fix what would bug, fix what makes no sense.
That leaves eight rows, patched in place with `patch` (only the name, the
status and the ability columns change; the row's damage type, power and
effects stay ruff's, which the Eldeon balance passes already tuned):

  3588  Sikuku Jailer / Warden / Guard self-buff whose status is 30 -- the
        caster MUTES ITSELF for 30 s. RoseZA: ATK +30%.          -> 11=18, 21=18, 23=30
  3593  Sikuku Cannibals: type 8 self-buff with no status at all, a wind-up
        for nothing. RoseZA: ASPD +30%.                          -> 11=16, 21=24, 23=30
  3598  Jailer / Warden / Guard / Infiltrator: type 8, enemy filter, 30 m,
        no status -- an area cast that applies nothing. RoseZA: Sleep, 20 s
        at success 200. Source numbers kept; tune col 13/14 if it plays too
        mean.                                                    -> 11=31
  3551, 3582, 3595  "damage + stun" rows (the '+' survives in the name) with no
        status: Gargoyle, Moss/Neg Golem, Executor Kera.         -> 11=32
        (3527, the same shape on the unspawned Yigore/Shadow Ghosts, is left as
        plain damage by decision, 2026-09-15: nothing to test it on.)
  3572  the same shape, shared by the Ikaness Engineer AND the Murilos (Karkia's
        spiders: fast, numerous -- a stun there is a nuisance, not a mechanic,
        2026-09-15). The Engineer gets a stun-carrying copy of OUR row at
        **7014** (`patch` + `dest`) and ed_icanes6.aip is re-pointed:
            audit-ai-skill-refs.py --remap ed_icanes6.aip:3572=7014
        Murilo / Murilo Alpha keep 3572 as it is.

Left alone on purpose: 3571 / 3589 deal damage without RoseZA's poison (they
work), 3554 / 3555 already map RoseZA's stronger burns to our Flame Heat, and
2961 / 3006 cancel every status where RoseZA cancels buffs only.

The last stripped casts: Inguz and the Penguins (2026-09-15)
------------------------------------------------------------
Unspawned rows, so testable only by /mon: Moon Sister Inguz 654 (INGUZ.AIP,
3044), Gangster Pang Little Jack / Jack 1456-1457 (PENGUN.AIP, 2979), Penguin
Artillery 1458 (PENART.AIP, 2980). All three rows exist in RoseZA (and Jrose)
with their effect chains at our indices, and every caster's release slot 9
carries a presenting frame (Inguz and the Pangs 25/35, the Artillery 24/34).
3044 is a 2 s area stun already authored in column 11 by RoseZA (32) -- kept
via `set` -- but with SKILL_SCOPE 0, a radius of nothing: the first test cast it
eight times and the server sent no damage packet at all. `set` gives it 8 m.
Its 2 s duration is also raised to 4 s: a cast's status is presented at the
caster's release frame, which trails the server's application by the park delay
(1-3 s measured here), so a 2 s stun was over on the server before its icon
appeared and the player walked through it. Rule of thumb: a status on a cast
skill needs to outlast that delay, or it is decoration. Powers re-based to the magic formula by the casters' ATK, on the
~6.4-per-point-at-2900-ATK measurement scaled linearly: Inguz (925 ATK,
~2 per point) 120 -> ~250; the Penguins (~600 ATK, ~1.3 per point) 120 / 100
-> ~160 / ~130. Field-mob spikes, not nukes.

SHIBUYA's Deaders (2026-09-29)
------------------------------
The Jrose SECONDWALL monsters brought in by `import-shibuya.py` stage 4, which
also rewrites their AI (casts re-pointed, the casts we cannot run stripped). The
zone is levels 60-80 with two level-100 bosses, so every power is re-based to the
magic formula by the caster's own normal swing, measured in `balance-sim.py`
against level 65-85 players of all six profiles (the synthetic players misjudge
absolute damage -- the Oro kit was 2x off -- but a skill-to-swing ratio shares
the bias). Damage per power point: police (lv 73, ATK 330) ~1.7, Slave Puppet
(lv 85) ~2.0, both bosses (lv 100, ATK ~600) ~3.0.

  * Field casts ~2.5x a swing: Sniping 60, Twin Shot 40 (two hits), Poison
    Bullet 30 + Poison II; Puppet Bolt 45 (a copy of the Mage bolt, since up to
    five puppets cast it).
  * Boss AoEs ~2x a swing, the one nuke ~3x: Astral Nova 115, Rot Flame 110,
    Silencing Wave 60 + silence, Toxic Rain 50 + Poison II, Rot Inferno 170.
  * Jrose radii are 20-50 m; ours are 8-10 m, like our other monster AoEs.
  * Durations were authored for level 250: buffs 300 s -> 30 s, the hex 20 s,
    poison and burn 60 s -> 5-10 s on our lowest tiers (Poison II, Burning),
    since Jrose's statuses 103/106 tick for 500. Stuns and silences get 4-6 s
    so they outlast the 1-3 s the client takes to present a cast.
  * Collisions go to the tail: 869 -> 7015, 2072 -> 7016, 2111 -> 7017, 3687 ->
    7018 (ours: Summon Mastery, Sell Trick, Weapon Research, GM Healing). 3780
    is our own Karkia Stun, but at Karkia's power it would take ~70% of a level-75
    player's HP, so Deader Rex's 1% cast of it is stripped instead. 2969 is
    already here, identical to Jrose's.
  * Not imported: 3691 (Jrose's "leave N HP" formula 7 has no equivalent here),
    3656 (the puppets heal their master with cast target 3, which our server
    does not implement), 6141 (a damage skill Jrose casts on the caster itself).
    Stage 4 strips those casts from the AI.

Usage
-----
    python scripts/import-monster-skills.py --selftest
    python scripts/import-monster-skills.py --dry-run --skills 871
    python scripts/import-monster-skills.py --skills 871           # write
    python scripts/import-monster-skills.py --verify --skills 871
    python scripts/import-monster-skills.py --restore --skills 871 # from build/monster-skills/

`--skills` defaults to every SKILLS entry; rows already carrying the expected name
are reported as "already imported" and skipped, so a full run is idempotent. Each
skill set writes its own manifest (`manifest-<ids>.json`; the first boss run kept
the legacy `manifest.json`).

Then put the AI casts back:  `audit-ai-skill-refs.py --restore` followed by
`audit-ai-skill-refs.py` again -- with the rows present only the *other* files'
dangling casts are stripped, and `--verify` on both audits must pass. Servers
cache the STBs at startup: restart the gameserver, and ship the client the new
files (loose or baked).

`data/` is gitignored, so this docstring is the only committed record of the change.
"""

import argparse
import base64
import importlib.util
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
DATA = os.path.join(REPO, "data")
SOURCES = {
    "RoseZA": r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data",
    "Jrose": r"C:\Users\Thomas\Desktop\Testclients\Jrose",
}

STB = os.path.join("3DDATA", "STB")
BACKUP_DIR = os.path.join(REPO, "build", "monster-skills")
MANIFEST = "manifest.json"

# skill id -> which dump the row (and its effect chain) comes from, and what we
# change from that row. Everything else is copied. `clear` names extra columns
# to blank -- a row lifted from a *player* skill carries learn-tree metadata
# (level/points/tab, required skills, the STL name key, which would alias one of
# OUR STL keys) that a monster-cast row must not keep.
SKILLS = {
    3603: dict(name="Charge", source="RoseZA", dmgtype=2, power=190),
    3604: dict(name="Fireball", source="RoseZA", dmgtype=2, power=230),
    # Nigaki (LIST_NPC 2547, Karkia Flower Garden, kh_2676.aip x4). Karkia is a
    # Jrose import, so 871 is Jrose's row: the Mage's Voltage Jolt (type 6
    # projectile, casting fx 1635, bullet LIST_EFFECT 264 -> _lighting_firing_01,
    # hit fx 1634). Every index resolved to the identical row here and the whole
    # 33-file chain was already present -- only the skill row was missing.
    # Jrose's power 700 is on its player-skill scale with damage type 5 (the
    # default normal-attack branch); re-based like the boss's: magic formula,
    # and Nigaki's ATK 1200 hits about half as hard per power point as the
    # 2900-ATK boss (~3.2 vs 6.4 on the tester), so 150 -> ~500, a field-mob
    # spike rather than a boss nuke.
    871: dict(name="Voltage Jolt", source="Jrose", dmgtype=2, power=150,
              clear=(2, 3, 4, 39, 40, 41, 42, 43, 44, 86)),
    # Nigaki's other cast, missed by import-karkia.py stage 3i: Jrose 923 is rank
    # 3 of the Cleric's Heal -- type 11 (instant ability change on the target),
    # friend filter, +370 HP. Jrose keeps the ability in its newer-schema columns
    # 89/90 (type 16 = HP, value 370), so `set` writes it into our 21/22. Our 923
    # is the player's Healing (type 10, self), which is what the Nigakis were
    # spam-casting on each other -- hence the spell-sound loop and monsters that
    # never fought back. The id collides, so the row goes to a fresh row at the
    # tail (`dest`) and kh_2676.aip is re-pointed:
    #     audit-ai-skill-refs.py --remap kh_2676.aip:923=7012
    # 370 HP is nothing against a 13k-HP Nigaki, but it is what Jrose ships and a
    # support cast that does little is the honest port; tune with `set`.
    923: dict(name="Heal Ally", source="Jrose", dest=7012, set={21: 16, 22: 370},
              clear=(2, 3, 4, 16, 17, 39, 40, 45, 46, 86)),
    # Grand Master Devourer 2226 (or_gmdevourer1.aip, RoseZA). RoseZA keeps the
    # status in its column 88 (37 / 15 / 32), which we never read; `set` puts the
    # same LIST_STATUS ids into our column 11. The slow's magnitude is AT_SPEED
    # (21) x rate (23), the house 40%. 3613 is occupied by Karkia Stun -> 7013.
    # Powers re-based to the magic formula like the boss's above (~6.0/pt here).
    3609: dict(name="Dispel Buffs", source="RoseZA", set={11: 37}),
    3610: dict(name="Area Slow", source="RoseZA", set={11: 15, 21: 23, 23: 40}),
    3611: dict(name="Stun Blast", source="RoseZA", dmgtype=2, power=150, set={11: 32}),
    3613: dict(name="Range Attack", source="RoseZA", dest=7013, dmgtype=2, power=200),
    # The boss's when-damaged self-casts: the same rows as 3609/3610, already here
    # nameless and status-less from the old Oro import. `overwrite` lets the
    # importer replace a row that matches the source cell-for-cell.
    3596: dict(name="Dispel Buffs", source="RoseZA", set={11: 37}, overwrite=True),
    3597: dict(name="Area Slow", source="RoseZA", set={11: 15, 21: 23, 23: 40}, overwrite=True),
    # Eldeon rows (ruff lineage) patched in place -- see the docstring. `patch`
    # reads no source cell: our row keeps everything but the name and `set`.
    # EZ01, the Sikuku prison:
    3588: dict(name="Jailer Fury", source="RoseZA", patch=True, set={11: 18, 21: 18, 23: 30}),
    3593: dict(name="Cannibal Frenzy", source="RoseZA", patch=True, set={11: 16, 21: 24, 23: 30}),
    3598: dict(name="Prison Slumber", source="RoseZA", patch=True, set={11: 31}),
    3595: dict(name="Kera Stun Blast", source="RoseZA", patch=True, set={11: 32}),
    # EJ03 (the Murilos keep the stun-less 3572; the Engineer's AI is re-pointed):
    3572: dict(name="Engineer Stun", source="RoseZA", patch=True, dest=7014, set={11: 32}),
    3582: dict(name="Golem Stun Blast", source="RoseZA", patch=True, set={11: 32}),
    # EJ02:
    3551: dict(name="Gargoyle Stun", source="RoseZA", patch=True, set={11: 32}),
    # The last stripped casts (unspawned casters; see the docstring):
    # RoseZA authors 3044 with SKILL_SCOPE 0 -- a type-7 area skill with no
    # radius, so Skill_DamageToAROUND finds nobody and nothing happens (eight
    # casts, zero damage packets, 2026-09-15). 8 m, between our other monster
    # AOEs (3042 at 5.5 m, 3013 at 14 m).
    # Duration 2 s -> 4 s: the client presents a cast's status at the release
    # frame, 1-3 s after the server applied it (measured on this row), so a 2 s
    # stun expired server-side before its icon even showed and the player could
    # walk "while stunned". Our other monster stuns run 3-5 s.
    3044: dict(name="Inguz Stun Wave", source="RoseZA", dmgtype=2, power=120, set={11: 32, 8: 800, 14: 4}),
    2979: dict(name="Pang Jump Attack", source="RoseZA", dmgtype=2, power=120),
    2980: dict(name="Artillery Jump Attack", source="RoseZA", dmgtype=2, power=100),
    # SHIBUYA's Deaders (import-shibuya.py stage 4; see "SHIBUYA" in the docstring).
    # Rot Tracker 4069 (sw_sby02.aip) and Deader Rex 4068 (sw109_boss02.aip) share
    # the Astrum rows. Jrose keeps status and abilities in columns 88-99; `set`
    # moves them into our 11/12 and 21-26 (LIST_STATUS 8-37 is the same table in
    # both dumps). Durations and radii were authored for level 250 and are cut.
    3722: dict(name="Astral Guard", source="Jrose", set={11: 20, 21: 19, 23: 30, 14: 30}),
    3723: dict(name="Astral Might", source="Jrose", set={11: 18, 21: 18, 23: 30, 14: 30}),
    3724: dict(name="Astral Hex", source="Jrose",
               set={11: 19, 12: 21, 21: 18, 23: 30, 24: 19, 26: 30, 14: 20, 8: 1000}),
    3726: dict(name="Astral Nova", source="Jrose", dmgtype=2, power=115, set={8: 1000}),
    3744: dict(name="Stunning Strike", source="Jrose", set={11: 32, 14: 4}, clear=(21, 23)),
    3760: dict(name="Silencing Wave", source="Jrose", dmgtype=2, power=60, set={11: 30, 14: 6, 8: 1000}),
    3782: dict(name="Rot Flame", source="Jrose", dmgtype=2, power=110, set={11: 58, 14: 5, 8: 800}),
    3783: dict(name="Rot Inferno", source="Jrose", dmgtype=2, power=170, set={11: 58, 14: 8, 8: 1000}),
    3687: dict(name="Toxic Rain", source="Jrose", dest=7018, dmgtype=2, power=50,
               set={11: 8, 14: 10, 8: 800}),
    # Police zombies 1947/1948 (zombie_police_sby.aip): Jrose *player* gun skills,
    # so the learn-tree columns and the STL key are cleared as for 871.
    2067: dict(name="Sniping", source="Jrose", dmgtype=2, power=60,
               clear=(2, 3, 4, 16, 17, 27, 35, 39, 40, 41, 42, 43, 44, 45, 46, 86)),
    # Its dummy effect (77) is FILE_EFFECT 171, empty in Jrose too: dropped.
    2072: dict(name="Twin Shot", source="Jrose", dest=7016, dmgtype=2, power=40,
               clear=(2, 3, 4, 16, 17, 27, 35, 39, 40, 41, 42, 43, 44, 45, 46, 77, 78, 79, 86)),
    2466: dict(name="Poison Bullet", source="Jrose", dmgtype=2, power=30, set={11: 8, 14: 8},
               clear=(2, 3, 4, 16, 17, 27, 35, 39, 40, 41, 42, 43, 44, 45, 46, 86)),
    2111: dict(name="Master Weapon", source="Jrose", dest=7017, set={11: 18, 21: 18, 23: 30, 14: 30},
               clear=(2, 3, 4, 16, 17, 27, 35, 39, 40, 41, 42, 43, 44, 45, 46, 86)),
    # Slave Puppet 1833 (sw109_mannequin.aip), Deader Rex's summons. 869 is a rank
    # of the Jrose Mage bolt we already ship as 871; a full-power copy from five
    # puppets would take ~28% of a player's HP per bolt, so they get their own row.
    869: dict(name="Puppet Bolt", source="Jrose", dest=7015, dmgtype=2, power=45,
              clear=(2, 3, 4, 16, 17, 27, 35, 39, 40, 41, 42, 43, 44, 45, 46, 86)),
    2990: dict(name="Puppet Dispel", source="Jrose", set={11: 37}),
}

# LIST_SKILL columns (io_skill.h)
C_NAME, C_TYPE, C_POWER, C_STATUS1, C_STATUS2, C_DMGTYPE = 0, 5, 9, 11, 12, 15
C_BULLET = 71
EFFECT_COLS = (56, 59, 62, 65, 74, 77, 80)     # FILE_EFFECT rows
SOUND_COLS = (58, 61, 64, 67, 73, 76, 79, 82)  # FILE_SOUND rows
# LIST_EFFECT (bullet row) columns (stb.h)
BULLET_EFFECT_COLS = (9, 10, 11, 12)           # FILE_EFFECT rows
BULLET_SOUND_COLS = (16, 17)                   # FILE_SOUND rows


def load(name, fname):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    m = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [fname]
    try:
        spec.loader.exec_module(m)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return m


oro = load("import_oro", "import-oro.py")   # Stb (set/to_bytes), effect_chain, rel_path


def ival(stb, r, c):
    v = stb.get(r, c).strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def blank_row(stb, r):
    return r < stb.rows and not any(x.strip() for x in stb.d[r])


class Plan:
    """Everything the import will write, resolved before anything is touched."""

    def __init__(self, ours, ids):
        self.ours = ours
        self.o = {n: oro.Stb(os.path.join(ours, STB, n)) for n in
                  ("LIST_SKILL.STB", "FILE_EFFECT.STB", "FILE_SOUND.STB", "LIST_EFFECT.STB")}
        self._src_tables = {}    # dump name -> {table: Stb}
        self.skill_rows = {}     # id -> cells
        self.effect_rows = {}    # FILE_EFFECT idx -> cells
        self.sound_rows = {}     # FILE_SOUND idx -> cells (blank rows filled in place)
        self.bullet_rows = {}    # LIST_EFFECT idx -> cells
        self.efts = set()        # (dump root, data-relative .eft path)
        self.skipped = []        # ids whose row is already ours
        self.problems = []
        for sid in ids:
            spec = SKILLS[sid]
            self.src = SOURCES[spec["source"]]
            self.s = self._tables(spec["source"])
            self._skill(sid, spec)

    def _tables(self, dump):
        if dump not in self._src_tables:
            self._src_tables[dump] = {n: oro.Stb(os.path.join(SOURCES[dump], STB, n)) for n in self.o}
        return self._src_tables[dump]

    # -- resolution ------------------------------------------------------
    def _next_append(self):
        """The row a `dest` append may use next: the table's end, or one past the
        last row this plan already appends (several collisions in one run)."""
        rows = self.o["LIST_SKILL.STB"].rows
        planned = [d for d in self.skill_rows if d >= rows]
        return max(planned) + 1 if planned else rows

    def _need_effect(self, idx, why):
        if idx <= 0:
            return
        o, s = self.o["FILE_EFFECT.STB"], self.s["FILE_EFFECT.STB"]
        if idx >= s.rows or not s.get(idx, 1).strip():
            self.problems.append("%s: source FILE_EFFECT %d is empty" % (why, idx))
            return
        spath = s.get(idx, 1)
        self.efts.add((self.src, spath.decode("latin-1")))
        if idx < o.rows and o.get(idx, 1).strip():
            if o.get(idx, 1).strip().lower() != spath.strip().lower():
                self.problems.append("%s: FILE_EFFECT %d is %r here but %r in source"
                                     % (why, idx, o.get(idx, 1), spath))
            return
        if idx >= o.rows:
            self.problems.append("%s: FILE_EFFECT %d is past our table (%d rows)" % (why, idx, o.rows))
            return
        self.effect_rows[idx] = [s.get(idx, c) for c in range(o.cols)]

    def _need_sound(self, idx, why):
        if idx <= 0:
            return
        o, s = self.o["FILE_SOUND.STB"], self.s["FILE_SOUND.STB"]
        ours = o.get(idx, 0).strip() if idx < o.rows else b""
        theirs = s.get(idx, 0).strip() if idx < s.rows else b""
        if ours.lower() == theirs.lower() and ours:
            return
        # A row blank here is filled in place, like FILE_EFFECT. The .wav itself
        # is not in data/: sound ships loose in the deployed game's Sound folder.
        if theirs and not ours and idx < o.rows:
            self.sound_rows[idx] = [s.get(idx, c) for c in range(o.cols)]
            return
        self.problems.append("%s: FILE_SOUND %d differs (%r vs %r)" % (why, idx, ours, theirs))

    def _need_bullet(self, idx, why):
        if idx <= 0:
            return
        o, s = self.o["LIST_EFFECT.STB"], self.s["LIST_EFFECT.STB"]
        if idx >= s.rows:
            self.problems.append("%s: source LIST_EFFECT %d missing" % (why, idx))
            return
        for c in BULLET_EFFECT_COLS:
            self._need_effect(ival(s, idx, c), "%s bullet %d col %d" % (why, idx, c))
        for c in BULLET_SOUND_COLS:
            self._need_sound(ival(s, idx, c), "%s bullet %d col %d" % (why, idx, c))
        if idx < o.rows and not blank_row(o, idx):
            # Column 0 is a designer name the game never reads (EFFECT_NAME is
            # NULL) and it is stored in each dump's own encoding; compare the data.
            same = all(o.get(idx, c).strip() == s.get(idx, c).strip() for c in range(1, min(o.cols, s.cols)))
            if not same:
                self.problems.append("%s: LIST_EFFECT %d is occupied here and differs" % (why, idx))
            return
        if idx >= o.rows:
            self.problems.append("%s: LIST_EFFECT %d is past our table (%d rows)" % (why, idx, o.rows))
            return
        self.bullet_rows[idx] = [s.get(idx, c) for c in range(o.cols)]

    def _skill(self, sid, spec):
        o, s = self.o["LIST_SKILL.STB"], self.s["LIST_SKILL.STB"]
        dest = spec.get("dest", sid)
        why = "skill %d" % sid
        if sid >= s.rows or not s.get(sid, C_TYPE).strip():
            self.problems.append("%s: not in source" % why)
            return
        if dest > self._next_append():
            self.problems.append("%s: dest %d is past our LIST_SKILL end (next free row %d; only appending at the end is supported)" % (why, dest, self._next_append()))
            return
        if spec.get("patch"):
            # Our own row, corrected in place (or copied to `dest` when the row is
            # shared and only one caster should change): nothing is read from the
            # source.
            if sid >= o.rows or blank_row(o, sid):
                self.problems.append("%s: patch source row %d is blank here" % (why, sid))
                return
            if dest > self._next_append():
                self.problems.append("%s: dest %d is past our LIST_SKILL end (next free row %d)" % (why, dest, self._next_append()))
                return
            if dest < o.rows and not blank_row(o, dest) \
                    and o.get(dest, C_NAME) == spec["name"].encode("latin-1") \
                    and all(ival(o, dest, c) == v for c, v in spec.get("set", {}).items()):
                self.skipped.append(sid)
                return
            if dest != sid and dest < o.rows and not blank_row(o, dest):
                self.problems.append("%s: our row %d is occupied (%r)" % (why, dest, o.get(dest, 0)))
                return
            cells = [o.get(sid, c) for c in range(o.cols)]
            cells[C_NAME] = spec["name"].encode("latin-1")
            for c in spec.get("clear", ()):
                cells[c] = b""
            for c, v in spec.get("set", {}).items():
                cells[c] = str(v).encode()
            self.skill_rows[dest] = cells
            return
        if dest < o.rows and not blank_row(o, dest):
            if o.get(dest, C_NAME) == spec["name"].encode("latin-1"):
                self.skipped.append(sid)       # already imported on an earlier run
                return
            # A row an earlier import copied verbatim (no name, status lost): the
            # same source cells everywhere the spec does not rewrite.
            skip_cols = {C_NAME, C_STATUS1, C_STATUS2, *spec.get("set", {}), *spec.get("clear", ())}
            verbatim = all(o.get(dest, c).strip() == s.get(sid, c).strip()
                           for c in range(o.cols) if c not in skip_cols)
            if not (spec.get("overwrite") and verbatim):
                self.problems.append("%s: our row %d is occupied (%r)%s"
                                     % (why, dest, o.get(dest, 0),
                                        "" if verbatim else " and differs from the source"))
                return
        cells = [s.get(sid, c) for c in range(o.cols)]
        cells[C_NAME] = spec["name"].encode("latin-1")
        if "power" in spec:
            cells[C_POWER] = str(spec["power"]).encode()
        if "dmgtype" in spec:
            cells[C_DMGTYPE] = str(spec["dmgtype"]).encode()
        cells[C_STATUS1] = b""
        cells[C_STATUS2] = b""
        for c in spec.get("clear", ()):
            cells[c] = b""
        for c, v in spec.get("set", {}).items():
            cells[c] = str(v).encode()
        self.skill_rows[dest] = cells
        # Resolved from the row as written, so a `clear`ed column pulls nothing in.
        cell = lambda c: int(cells[c]) if cells[c].strip().lstrip(b"-").isdigit() else 0
        for c in EFFECT_COLS:
            self._need_effect(cell(c), "%s col %d" % (why, c))
        for c in SOUND_COLS:
            self._need_sound(cell(c), "%s col %d" % (why, c))
        self._need_bullet(cell(C_BULLET), why)

    # -- files ------------------------------------------------------------
    def files(self):
        """(src root, rel, present_here) for every asset the effect chains need."""
        out = []
        for src in sorted({r for r, _ in self.efts}):
            efts = sorted(p for r, p in self.efts if r == src)
            for rel in sorted(oro.effect_chain(efts, src), key=str.lower):
                if not os.path.isfile(oro.rel_path(src, rel)):
                    self.problems.append("asset missing from source: %s" % rel)
                    continue
                out.append((src, rel, os.path.isfile(oro.rel_path(self.ours, rel))))
        return out

    def report(self, files):
        for dest, cells in sorted(self.skill_rows.items()):
            print("   LIST_SKILL %4d  %-10s type %s dmgtype %s power %s bullet %s%s"
                  % (dest, cells[C_NAME].decode(), cells[C_TYPE].decode(), cells[C_DMGTYPE].decode() or "-",
                     cells[C_POWER].decode() or "-", cells[C_BULLET].decode() or "-",
                     "  (appended)" if dest >= self.o["LIST_SKILL.STB"].rows else ""))
        for idx, cells in sorted(self.effect_rows.items()):
            print("   FILE_EFFECT %4d  %s" % (idx, cells[1].decode("latin-1")))
        for idx, cells in sorted(self.sound_rows.items()):
            print("   FILE_SOUND  %4d  %s   (the .wav must be in the deployed Sound folder)"
                  % (idx, cells[0].decode("latin-1")))
        for idx, cells in sorted(self.bullet_rows.items()):
            print("   LIST_EFFECT %4d  bullet fx %s hit %s/%s speed %s" %
                  (idx, cells[11].decode(), cells[9].decode(), cells[10].decode(), cells[15].decode()))
        for sid in self.skipped:
            print("   LIST_SKILL %4d  already imported, skipped" % sid)
        new = [r for _s, r, have in files if not have]
        print("   %d asset file(s) in the effect chain, %d to copy:" % (len(files), len(new)))
        for r in new:
            print("      %s" % r)


def manifest_path(ids):
    """One manifest per skill set. The first run (3603+3604) wrote the legacy name."""
    legacy = os.path.join(BACKUP_DIR, MANIFEST)
    if sorted(ids) == [3603, 3604] and os.path.isfile(legacy):
        return legacy
    return os.path.join(BACKUP_DIR, "manifest-%s.json" % "-".join(str(i) for i in sorted(ids)))


def write_manifest(ids, man):
    os.makedirs(BACKUP_DIR, exist_ok=True)
    json.dump(man, open(manifest_path(ids), "w"), indent=1)


def do_restore(root, ids):
    mpath = manifest_path(ids)
    if not os.path.isfile(mpath):
        print("nothing to restore (%s not found)" % mpath)
        return 0
    man = json.load(open(mpath))
    for rel, b64 in sorted(man["stb"].items()):
        open(os.path.join(root, rel), "wb").write(base64.b64decode(b64))
        print("   restored %s" % rel)
    for rel in man["copied"]:
        p = oro.rel_path(root, rel)
        if os.path.isfile(p):
            os.remove(p)
            print("   removed  %s" % rel)
    os.remove(mpath)
    return 0


def verify(root, ids):
    ok = True
    o = {n: oro.Stb(os.path.join(root, STB, n)) for n in ("LIST_SKILL.STB", "FILE_EFFECT.STB", "LIST_EFFECT.STB")}
    for src_id in ids:
        spec = SKILLS[src_id]
        sid = spec.get("dest", src_id)
        sk = o["LIST_SKILL.STB"]
        good = (sk.get(sid, C_NAME) == spec["name"].encode("latin-1")
                and ("power" not in spec or ival(sk, sid, C_POWER) == spec["power"])
                and ("dmgtype" not in spec or ival(sk, sid, C_DMGTYPE) == spec["dmgtype"])
                and all(ival(sk, sid, c) == v for c, v in spec.get("set", {}).items())
                and ival(sk, sid, C_TYPE) > 0)
        print("   LIST_SKILL %d %-12s %s" % (sid, spec["name"], "OK" if good else "MISSING/DIFFERS"))
        ok &= good
        for c in EFFECT_COLS:
            i = ival(sk, sid, c)
            if i and not o["FILE_EFFECT.STB"].get(i, 1).strip():
                print("      FILE_EFFECT %d blank" % i); ok = False
            if i:
                rel = o["FILE_EFFECT.STB"].get(i, 1).decode("latin-1")
                if not os.path.isfile(oro.rel_path(root, rel)):
                    print("      missing file %s" % rel); ok = False
        b = ival(sk, sid, C_BULLET)
        if b and blank_row(o["LIST_EFFECT.STB"], b):
            print("      LIST_EFFECT %d blank" % b); ok = False
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=DATA, help="our data dir (default: <repo>/data)")
    ap.add_argument("--skills", default=None, metavar="ID[,ID..]",
                    help="which SKILLS entries to act on (default: all)")
    ap.add_argument("--power", action="append", default=[], metavar="ID=POWER",
                    help="override SKILL_POWER for one skill, e.g. 3604=400")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="prove the STB writer round-trips")
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    for ov in a.power:
        sid, pw = ov.split("=")
        SKILLS[int(sid)]["power"] = int(pw)
    ids = sorted(int(x) for x in a.skills.split(",")) if a.skills else sorted(SKILLS)
    for sid in ids:
        if sid not in SKILLS:
            print("unknown skill %d -- add it to SKILLS first" % sid)
            return 1

    if a.restore:
        return do_restore(root, ids)
    if a.verify:
        print("verify:")
        return 0 if verify(root, ids) else 1

    print("self-test (the STB writer must reproduce every table byte-identically):")
    for n in ("LIST_SKILL.STB", "FILE_EFFECT.STB", "FILE_SOUND.STB", "LIST_EFFECT.STB"):
        p = os.path.join(root, STB, n)
        same = oro.Stb(p).to_bytes() == open(p, "rb").read()
        print("   %-18s %s" % (n, "OK" if same else "FAIL"))
        if not same:
            return 1
    if a.selftest:
        return 0

    plan = Plan(root, ids)
    files = plan.files()
    print("\nplan:")
    plan.report(files)
    if plan.problems:
        print("\nABORT, unresolved:")
        for p in plan.problems:
            print("   " + p)
        return 1
    if a.dry_run:
        print("\ndry run: nothing written")
        return 0

    if not plan.skill_rows:
        print("\nnothing to write")
        return 0
    if os.path.isfile(manifest_path(ids)):
        print("\nABORT: %s exists -- --restore first, or delete it to re-run on top" % manifest_path(ids))
        return 1
    man = {"stb": {}, "copied": []}
    for n in ("LIST_SKILL.STB", "FILE_EFFECT.STB", "FILE_SOUND.STB", "LIST_EFFECT.STB"):
        rel = os.path.join(STB, n).replace("\\", "/")
        man["stb"][rel] = base64.b64encode(open(os.path.join(root, STB, n), "rb").read()).decode("ascii")
    for dest, cells in plan.skill_rows.items():
        plan.o["LIST_SKILL.STB"].grow_to(dest + 1)
        for c, v in enumerate(cells):
            plan.o["LIST_SKILL.STB"].set(dest, c, v)
    for idx, cells in plan.effect_rows.items():
        for c, v in enumerate(cells):
            plan.o["FILE_EFFECT.STB"].set(idx, c, v)
    for idx, cells in plan.sound_rows.items():
        for c, v in enumerate(cells):
            plan.o["FILE_SOUND.STB"].set(idx, c, v)
    for idx, cells in plan.bullet_rows.items():
        for c, v in enumerate(cells):
            plan.o["LIST_EFFECT.STB"].set(idx, c, v)
    for n in ("LIST_SKILL.STB", "FILE_EFFECT.STB", "FILE_SOUND.STB", "LIST_EFFECT.STB"):
        open(os.path.join(root, STB, n), "wb").write(plan.o[n].to_bytes())
        print("   wrote %s" % n)
    for src, rel, have in files:
        if have:
            continue
        d = oro.rel_path(root, rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copyfile(oro.rel_path(src, rel), d)
        man["copied"].append(rel)
    print("   copied %d file(s)" % len(man["copied"]))
    write_manifest(ids, man)
    print("\nverify:")
    return 0 if verify(root, ids) else 1


if __name__ == "__main__":
    sys.exit(main())
