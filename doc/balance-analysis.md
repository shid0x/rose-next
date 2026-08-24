# Endgame balance: where the drift actually comes from

Status: analysis only, nothing changed. Every number below is reproducible with
`python scripts/balance-sim.py` (see that file's docstring for the player/monster
model and its known biases). Written 2026-08-24.

## The question

Retail ROSE was designed around cleric buffs; Rose Next removed them and
compensated with extra starting stats, but did that tuning for a level-100 cap.
Our content now runs to 240+. Somewhere in the middle, fights get long and hard
in a way nobody designed. Where, and why?

## The short answer

The drift is real and roughly a factor of ten, but **it is almost entirely on the
fight-length axis, not the danger axis.** Your effective HP pool, measured in
"how many monster hits kill me", is basically flat from level 60 onward. What
explodes is how long a monster takes to die.

Champion, best gear available at each level, no buffs, no refines, vs the median
field monster of its own level:

| level | seconds per kill | MP per kill | MP bars per kill | monster hits to kill you |
|------:|-----------------:|------------:|-----------------:|-------------------------:|
|  40   |    9             | 110         | 0.48             | 104 |
|  60   |   18             | 220         | 0.67             |  43 |
| 100   |   34             | 330         | 0.62             |  42 |
| 140   |   40             | 330         | 0.45             |  44 |
| 180   |   64             | 550         | 0.59             |  38 |
| 200   |   80             | 660         | 0.64             |  20 |
| 220   |  109             | 770         | 0.68             | 127 |

Twelve times longer per kill, while survivability per hit stays put. That is
exactly the shape of "battles are longer and harder than intended": the *harder*
part is a second-order consequence of the *longer* part — a fight that takes ten
times as long lets ten times as many hits land, so the same per-hit survivability
feels far worse.

## Root cause: the damage formula is scale-invariant

`Get_BasicDAMAGE` (src/common/calculation.cpp, monster branch) is

```
dmg = ATK * (suc*0.03 + 26) * (ATK - DEF + 250) / ((DEF + AVOID*0.4 + 5) * 145)
```

Multiply attacker ATK and defender DEF/AVOID by the same factor and almost
everything cancels. Only the constant `+250` survives:

| scale | ATK | DEF | AVOID | damage |
|------:|----:|----:|------:|-------:|
| 1x    |   80 |  55 |  34 |  57 |
| 2x    |  160 | 110 |  68 |  65 |
| 4x    |  320 | 220 | 136 |  77 |
| 8x    |  640 | 440 | 272 | 100 |
| 16x   | 1280 | 880 | 544 | 145 |

Sixteen times the stats on both sides buys 2.5x the damage. So the entire gear /
monster-stat arms race across 220 levels is a treadmill. Measured on real data,
level 20 → 220:

| quantity | growth |
|---|---:|
| monster HP | **x29.8** |
| monster DEF | x13.8 |
| monster ATK | x19.3 |
| player ATK | x11.4 |
| player DEF | x6.1 |
| player max HP | x9.0 |
| **player damage per landed swing** | **x1.6** |
| ⇒ swings per kill | **x20.9** |

Monster HP is `level * NPC_HP` (`cobjnpc.cpp`), and the `NPC_HP` column itself
creeps from a median of ~26 in the 20-39 band to ~70 in the 220-239 band. So HP
gets the level multiplier *and* a column that nearly triples. Player damage gets
neither, because of the cancellation above.

**Nothing else in the system is close to this in magnitude.** Any fix that does
not either raise damage multiplicatively or lower monster HP is rearranging
deck chairs.

## Four secondary problems, all real

### 1. Every buff and passive in the data is flat, and flat numbers rot

`Get_SkillAdjustVALUE` supports both a percentage and a flat term:

```
value = target_stat * RATE/100  +  FLAT * (casterINT + 300) / 315
```

`LIST_SKILL.STB` uses the FLAT column for essentially every buff and every
passive; the RATE column is zero almost everywhere. Those flat numbers were sized
for a level-100 game. What the best buffs in the data are worth today:

| level | Champion ATK | Power Support +70 | +70 at INT 300 | Berserk +100 | 2H Mastery +110 |
|------:|-------------:|------------------:|---------------:|-------------:|----------------:|
|  40   |  150 | 46.7% | 88.9% | 66.7% | 73.3% |
| 100   |  378 | 18.5% | 35.3% | 26.5% | 29.1% |
| 180   |  762 |  9.2% | 17.5% | 13.1% | 14.4% |
| 240   |  959 |  7.3% | 13.9% | 10.4% | 11.5% |

Same story on defence: Blessing Armor +210 is 128% of a level-40 Champion's DEF
and 16% of a level-240 one's; Harden Body +800 HP is 112% then 21%.

Two consequences worth separating. First, this is a *large* share of the drift on
its own — stacking the whole retail buff kit still cuts the cost of a kill by
~37x at level 60 but only ~7.6x at level 200, so the kit itself decays by a
factor of five. Second, `INT` is the only stat that touches buff strength, and
only on the flat half, capping near 1.9x. A Soldier's self-buffs are weak by
construction because Soldiers do not build INT.

### 2. Accuracy is a cliff, not a slope

`Get_SuccessRATE` computes `iSuc`; when `iSuc < 20`, `Get_DAMAGE` gives the swing
a flat ~7% chance to land at equal levels, no matter how far below 20 it is:

| player HIT (lv160 vs AVOID 260) | mean iSuc | lands |
|--------------------------------:|----------:|------:|
|  120 | -14.0 |  6.9% |
|  160 |   0.5 |  7.0% |
|  **200** | **15.2** | **35.5%** |
|  240 |  30.4 | 70.1% |
|  320 |  60.5 | 87.6% |

Forty points of HIT moves you from 7% to 35%. That is a build falling off a
ledge, not a build being slightly under-geared. In the model a dual-wield Raider
(which dumps CON, the only stat feeding HIT) sits at 7% from level 140 onward,
while a gun Bourgeois (CON-based) stays above 79% forever. Scouts and Mages fall
from ~85% at level 60 to ~35% at level 200.

This is not the same problem as fight length, and no damage change fixes it. It
also means player HIT and monster AVOID are the most dangerous numbers in the
data to touch casually.

### 3. Skills already dominate, and MP is the real limiter

A max-rank weapon skill is worth about **10 auto-attacks**, and its accuracy gate
is much gentler (`lv + 20 - mlv` instead of the scaled `lv*1.05` one), so skills
land ~100% where auto-attacks land 74%. Skills are already the whole game.

But: skill cooldowns are 5-18 s (`SKILL_RELOAD_TIME` x 0.2 s), MP costs are fixed
per rank, max skill rank is 10 and is reached long before level 240, and MP
regen while standing is `(RecoverMP + (CON+40)/6)/6` per 2 s — driven by CON
alone, not by level and not by max MP. A level-220 Champion needs ~770 MP of
Champion Hit to kill one median monster out of a 1130 MP pool. The bill gets paid
by sitting between kills, which is downtime the "seconds per kill" table above
does not even count.

So "more/stronger skills" runs straight into an MP wall unless the MP economy
moves with it.

### 4. There is a separate, sharp cliff at level 200+

The COST curve is not smooth. It is roughly 1.2-1.5 from level 100 to 160, then
2.8 at 180, then spikes to ~6.3 at 200 before falling back. Monster ATK jumps
937 → 1228 → 1784 (level 210) → 1332 (level 220) — non-monotonic, which is the
signature of the Oro/Sikuku imports rather than a designed curve. The DEF/RES
passes already fixed part of this tier; ATK and HP have not had the same
treatment.

## Assessment of the four proposals

### 1. Make SEN/CHA amplify skill power and class buffs

**Right instinct, right shape, but small on its own — and CHA is not free.**

SEN is already in the skill formula: `(rand30 + SEN*0.7 + 370)`. Raising that
coefficient has the *correct* shape — it does more at high level than at low,
because SEN grows with your bonus-point budget:

| level | SEN | x0.7 (now) | x1.4 | x2.1 | x3.0 |
|------:|----:|-----------:|-----:|-----:|-----:|
|  60   |  51 | 1.00x | 1.08x | 1.16x | 1.27x |
| 140   | 103 | 1.00x | 1.15x | 1.31x | 1.50x |
| 220   | 211 | 1.00x | 1.27x | 1.54x | 1.89x |

Even a 4x coefficient only buys 1.9x at cap, because SEN sits next to a hardcoded
`370`. To make SEN a real multiplier you would have to shrink that constant, and
that nerfs every low-SEN build at the same time. Usable as a *contributing*
lever, not as the fix.

CHA is a different matter. Today CHA does exactly two things: it multiplies quest
and vendor reward value (`Get_RewardVALUE`) and it feeds drop rate
(`Get_DropITEM`). It has **zero** combat effect. That makes it clean to
repurpose — no formula conflicts — but note that for Dealers it is already the
money/loot stat, so putting their damage on CHA makes it mandatory rather than
optional. That may be exactly what you want (it gives Dealers a real identity),
but it is a design decision, not a free win.

Worth flagging: buff strength is keyed to the caster's **INT** in
`Get_SkillAdjustVALUE`, and that call site passes `pSpeller->Get_INT()` on both
server and client (the value is on the wire in `GSV_EFFECT_OF_SKILL.m_nINT`).
Changing which stat scales buffs is a coordinated client+server change, not a
data edit.

### 2. Improve passives / raise their ceilings

**Right target, wrong mechanism.** More ranks of a flat passive is still flat —
you would be adding, say, +110 more ATK to a character with 900. The fix that
matters is switching passives from the FLAT column to the RATE column, which the
engine already reads (`GetPassiveSkillRate` is applied in `Cal_MaxHP`,
`Cal_DEFENCE`, `Cal_RESIST`, `Cal_HIT`, `Cal_ATTACK`, `Cal_MaxWEIGHT`,
`Cal_RunSPEED`). That is a **data-only change**, self-scaling forever, and it is
the single highest leverage-per-risk item on this list.

Raising ceilings *after* converting to percentages is then a real knob.

### 3. Nerf monster attack power / improve class defences

**Aimed at the axis that did not drift.** Effective HP in monster hits is flat
across the whole curve; per-hit lethality is not what changed. Cutting monster
ATK 35% makes fights *safer* without making them shorter, so the honest outcome
is "long and boring" instead of "long and dangerous". It also devalues every
defensive stat and item at once.

The exception is the level-200+ tier, where monster ATK genuinely left the curve
(that non-monotonic 1228/1784/1332 sequence). A targeted pass there, in the
existing `rebalance-*.py` idempotent-with-sidecar style, is well justified. A
broad nerf is not.

Class defensive identity (Hawker dodge, Soldier HP/regen, Mage glass-cannon) is a
good design goal on its own merits, and is worth doing — just do not expect it to
address the pacing complaint.

### 4. Import new skills

**Good for variety, poor as a balance fix — and it is the one lever that runs
backwards.** Because weapon-skill damage is `(POWER + ATK*0.2) * ...`, a growing
ATK dilutes POWER, so the same power multiplier buys *less* at high level:

| level | pow x1.5 | pow x2.0 | pow x3.0 |
|------:|---------:|---------:|---------:|
|  20   | 1.47x | 1.93x | 2.87x |
| 120   | 1.37x | 1.75x | 2.50x |
| 220   | 1.32x | 1.64x | 2.27x |

Doubling skill power helps a level-20 character more than a level-220 one. New
skills are worth importing for content and class identity; they will not flatten
the curve.

## What I would actually do, in order

Measured on swings-per-kill (>1 means shorter fights), Champion:

| level | mob HP x0.6 at 200+ | +20% ATK | +40% ATK | +30% attack speed | dmg constant +4*lv |
|------:|--------------------:|---------:|---------:|------------------:|-------------------:|
|  40   | 1.00x | 1.33x | 1.71x | 1.29x | 1.59x |
| 120   | 1.00x | 1.51x | 2.14x | 1.30x | 2.31x |
| 220   | 1.67x | 1.78x | 2.78x | 1.30x | 3.42x |

1. **Convert buffs and passives from flat to percentage.** Data only, both
   columns already work, self-scaling, and it directly restores the thing the
   removal of cleric buffs took away. Start here; re-measure before doing
   anything else, because it moves more than it looks like it will (+20% ATK
   alone is 1.5-1.8x shorter fights, because `ATK - DEF + 250` is nearly
   cancelled at endgame and is therefore very sensitive).
2. **Flatten the monster HP column at high level**, the same way DEF/RES were
   already put back on trend. This attacks the x29.8 term directly and is the
   only lever that touches it. Idempotent script + sidecar, same pattern as
   `rebalance-endgame-curve.py`.
3. **Finish the level-200+ ATK pass** so the tier stops being non-monotonic.
4. **Fix the accuracy cliff** — either soften the `iSuc < 20` step into a ramp,
   or give the low-HIT builds a source of HIT that scales. This is a correctness
   problem more than a balance one: a 40-point stat swing should not move you
   from 7% to 35%.
5. **Then** class identity work: SEN/CHA coefficients, defensive niches, new
   skills. These are worth doing for the game, and by that point you will be
   tuning on top of a curve that is no longer running away.

Deliberately not recommended:
- A broad monster ATK nerf (wrong axis; makes fights boring instead of short).
- Adding a level term to the damage constant (`+250` → `+250 + k*level`). It has
  the strongest effect of anything measured — 3.4x at level 220 with k=4 — but it
  also gives 1.6x at level 40, where nothing is broken, and it silently rewrites
  every damage number in the game including PvP and monster-on-player. If the
  data-side fixes turn out to be insufficient, this is the escape hatch, and it
  should be gated on level (`max(0, level - 80) * k`) rather than applied flat.

## Model caveats

- No refine grades, gems, or passives are equipped in the baseline, so absolute
  ATK/DEF run about 20% low. A level-216 Raider measured in game via `/stats` had
  ATK 1201; this model gives 987 at level 220. Ratios and growth factors are the
  point, not absolute values.
- "Median field monster" comes from `LIST_NPC.STB` with the top HP quintile
  dropped. It does not know which monsters are actually spawned in which map, so
  a level band whose STB rows are mostly unspawned duplicates will be
  mis-weighted. Bands above 220 have very few rows and should be read as
  indicative only.
- Seconds-per-kill assumes a 1.4 s swing animation at attack-speed 100. The real
  interval comes from the `.ZMO` motion length; the *ratios* between levels are
  unaffected by that assumption, the absolute seconds are.
- Only the PVM branches are ported. PvP has its own formulas and its own damage
  caps (25-45% of the defender's max HP) and was not analysed.
