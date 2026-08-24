# Skill Import Investigation

**Status:** investigation only — nothing imported, no code or data changed.
**Date:** 2026-08-24.
**Question:** can we borrow skills from the Evolution-era clients (v667 / RoseZA) as new
high-level content, taking only the name, icon, animation and effects, with all stats
and descriptions authored by us?

**Answer:** yes. The blocker is never the schema — columns 0–86 align across every dump we
have. The real work is the *dependency chain* behind an animation or effect, and how much
of it we already own. Two skills were traced end to end; one turned out to need nothing at
all, the other needs 6 files totalling 5.3 KB.

Reference dump used: `C:\Users\Thomas\Desktop\Testclients\667\extracted data`.
Ours: `data/` (gitignored — see the deploy workflow note at the end).

---

## 1. Verdict up front

| | Geon Archangel Crumpler | Hailstorm |
|---|---|---|
| v667 row | 655 (5 levels, 655–659) | 915 (5 levels, 915–919) |
| Class | Champion (62) | Muse (42) |
| Weapon | 221 two-hand sword | 241 staff/wand |
| Skill type | 3 (damage) | 17 (self + AoE damage) |
| Animation | **already ours** — generic "Heavy" set | **already ours** — generic "extent" set |
| Effects | **already ours** — `_heavy_*` | **2 new** + 4 new `.ptl` |
| Icon | new art | new art |
| Files to copy | **0** | **6 (5.3 KB)** |
| Verdict | not worth it — visually identical to our Power Burst | **clean import** |

---

## 2. How the pieces resolve

Worth writing down, because half the investigation was just finding which table an index
points at. All column numbers are *game* columns (the reader skips the root column, so an
STB editor shows them one higher — see the `reference_stb_column_offset` memory).

Macros live in `src/common/io_skill.h` (mirrored in `src/client/common/` and
`src/sho_gameserver/src/common/`).

### Animation

```
SKILL_ANI_ACTION_TYPE (col 68)   ->  row index into TYPE_MOTION.STB
SKILL_ANI_CASTING     (col 52)   ->  row index into TYPE_MOTION.STB
SKILL_ANI_CASTING_REPEAT (col 54)->  row index into TYPE_MOTION.STB
```

`TYPE_MOTION.STB` is a matrix: **row = action type, column = weapon motion type**.

```c
// src/common/include/rose/io/stb.h:320
#define FILE_MOTION(WEAPON, ACTION) g_TblAniTYPE.get_int32(ACTION, WEAPON)
```

The cell value is a row index into `FILE_MOTION.STB`, whose column 0 is the `.ZMO` path.
The weapon column comes from `WEAPON_MOTION_TYPE(I) = g_TblWEAPON.get_int32(I, 34)`, keyed
by weapon *item number*. `SKILL_NEED_WEAPON` (col 30) is an item *type*, so the mapping had
to be derived empirically from `LIST_WEAPON.STB` (item type in col 4, motion type in col 34):

| weapon item type | TYPE_MOTION column |
|---|---|
| 211 one-hand sword | 1 |
| 212 one-hand blunt | 0 |
| 221 two-hand sword | 5 |
| 222 spear | 6 |
| 223 two-hand axe | 8 |
| 231 bow | 10 |
| 232 gun | 12 |
| 233 launcher | 9 |
| 241 staff/wand | 6 |
| 242 wand | 14 |
| 251 katar | 16 |
| 252 dual | 7 |
| 253 dual gun | 12 |
| 271 crossbow | 11 |

Scanning *every* column of a TYPE_MOTION row over-reports badly (it flags "Shield Reflect"
as needing a bow animation). Always restrict to the skill's own weapon column.

### Effects

```
SKILL_CASTING_EFFECT(T)  cols 56 / 59 / 62 / 65   (4 slots, stride 3)
SKILL_BULLET_NO          col 71
SKILL_HIT_EFFECT         col 74
SKILL_HIT_DUMMY_EFFECT   cols 77 / 80
SKILL_AREA_HIT_EFFECT    col 83
```

These are row indices into **`FILE_EFFECT.STB`**, *not* `LIST_EFFECT.STB` — a trap, since
`LIST_EFFECT.STB` is far smaller and the indices look out of range against it. Confirmed via
`CEffectLIST::Add_EffectWithIDX` (`src/client/io_effect.cpp:912`), which bounds-checks
against `m_nFileEffectCNT`, populated from `FILE_EFFECT.STB`.

The paired `*_POINT` columns are dummy-bone link indices.
`INVALID_DUMMY_POINT_NUM = 999` (`src/client/cobjchar.h:34`) means "link to the model root".

### Everything else

- **Sounds** — cols 58/61/64/67, 73, 76, 84: row index into `FILE_SOUND.STB`.
- **Icon** — col 51: global sprite index in `3DDATA/CONTROL/RES/SKILLICON.TSI`.
- **Name/description** — col 86 holds an STL key (`LSkill0651`) into `LIST_SKILL_S.STL`.
  **One key per skill, shared by every level row** (rows 651–660 all carry `LSkill0651`).
  The server instead reads `SKILL_NAME` from STB col 0, so a new skill needs both.

### Schema alignment

| dump | rows | usable cols | STL key col |
|---|---|---|---|
| ours | 7002 | 87 | **86** |
| v667 | 8521 | 125 | 124 |
| Evo 137 / RoseZA | 8002 / 6107 | 114 | — |

Columns 0–86 are identical in meaning everywhere; newer schemas only append. Porting a row
is therefore "truncate to the first 87 columns, then re-point every ID column".

Note our col 86 is repurposed for the STL key, where v667 keeps `SKILL_ATTRIBUTE` at col 87.
So `SKILL_ATTRIBUTE(I)` returns 0 for **every** skill in our data (`atoi("LSkill0651")`).
This is harmless: its only consumers sit inside `#if defined(_GBC)` in
`src/client/interface/slotcontainer/cskillslot.cpp`, and `_GBC` is not defined in any
vcxproj or props file. Pre-existing, unrelated to importing.

---

## 3. Case study A — Geon Archangel Crumpler (negative result)

The skill that prompted the question. Its "animation" is not a bespoke asset.

`ANI_ACTION_TYPE 94`, `ANI_CASTING 129`, `ANI_CASTING_REPEAT 149` — and
**`TYPE_MOTION.STB` rows 94, 129 and 149 are byte-identical between v667 and our data**,
pointing at the same `FILE_MOTION` rows 461–478:

```
461 empty_Heavycast_m1.ZMO      470 twoswd_Heavycast_m1.ZMO
462 empty_Heavyrotate_m1.ZMO    471 twoswd_Heavyrotate_m1.ZMO
463 empty_Heavyattack_m1.ZMO    472 twoswd_Heavyattack_m1.ZMO
464 onehand_Heavycast_m1.ZMO    473 twospear_Heavycast_m1.ZMO
...                             ...
469 onetool_Heavyattack_m1.ZMO  478 kartar_Heavyattack_m1.ZMO
```

All 21 `.ZMO` files are present in `data/3DDATA/MOTION/AVATAR/`. Its effects
(`_heavy_casting_01`, `_heavy_rotate_01`, `_heavy_skill_01`, `_heavy_hit_01`) sit at the
*same* `FILE_EFFECT` indices in both dumps, and both sounds resolve identically.

Our **Power Burst** (row 1511) is the same recipe:

| col | Crumpler | Power Burst |
|---|---|---|
| 52 casting anim | 129 | 129 |
| 53 casting speed | 100 | 50 |
| 54 casting repeat | 149 | 149 |
| 68 action anim | 94 | 94 |
| 69 action speed | **200** | 100 |
| 70 hit count | **2** | 1 |
| 56/59/62/65 cast fx | 1041/1042/1043/1043 | identical |
| 74 hit fx | 1040 | 1040 |
| 76 hit sound | 1551 | 67 |
| 51 icon | **56** | 215 |
| 30 weapon | 221 | 251 |
| 35 class | **62 Champion** | 65 Raider |

Only the icon is real new art (checked against all 512 of our sprites; closest match scored
mean-abs-diff 55/255 — nothing near identical). Everything else is timing and gating.

Our skills already using action type 94: Heavy Attack (301), Power Attack (1501),
Power Burst (1511).

**Conclusion:** importable in an afternoon, but it would look like a faster Power Burst with
a new icon. Not new content. Rejected as a candidate.

One useful by-product: `AVAILBLE_CLASS_SET 62` is **"Champion" in both dumps identically**
(`LIST_CLASS.STB` row 62), so class gating ports for free between v667 and us. Verified for
rows 41–68; the job names line up exactly.

---

## 4. Case study B — Hailstorm (positive result)

v667 row 915, 5 levels (915–919). Muse, staff, `SKILL_TYPE 17` (self + damage to scope),
`SCOPE 1000`, icon 274.

### Already ours — no work

| Dependency | Status |
|---|---|
| `TYPE_MOTION` rows 154 / 117 / 137 | **byte-identical rows** |
| `extent_skill01_m1.zmo` (624), `extent_casting01_m1.zmo` (607), `extent_rotate01_m1.zmo` (617) | present, same indices |
| cast fx `_self_casting_01.eft` (1754), `_self_rotate_01.eft` (1763) | present, **same index** |
| `_SCOPE_02.ZMS` + `_SCOPE_02.ZMO` (glyph mesh + spin) | **byte-identical** |
| `STON_PATH_01.ZMO` (hailstone flight path) | **byte-identical** |
| textures `_ice_02`, `_smoke_04`, `_dust_04`, `_shockwave_02`, `_WORD_01` | same art, mean-abs-diff **0.0–1.0** |
| sounds `skill_attack01.wav` (131), `point_luna01.wav` (1574) | identical index |
| sound `casting05.wav` | ours **1004**, v667 **1005** — one-number re-point |

The shared textures are the same art re-encoded: v667 ships 16-bit uncompressed, ours are
32-bit with mipmaps. The effect will render correctly against our copies.

### To copy — 6 files, 5,423 bytes

```
3DDATA/EFFECT/HAILSTORM_01.EFT              3169 b   20 emitters
3DDATA/EFFECT/_GROUND_BLUE_GLYPH_01.EFT      272 b   1 mesh (ground rune)
3DDATA/EFFECT/PARTICLES/hailstorm_hail_01.ptl        868 b
3DDATA/EFFECT/PARTICLES/hailstorm_hail_blast_01.ptl  356 b
3DDATA/EFFECT/PARTICLES/hailstorm_hail_blowup_01.ptl 420 b
3DDATA/EFFECT/PARTICLES/hailstorm_hail_dust_01.ptl   338 b
```

`HAILSTORM_01.EFT` is 5 hailstones × 4 particle layers (hail / blast / blowup / dust), each
hailstone following `STON_PATH_01.ZMO`.

### Table edits

- **`FILE_EFFECT.STB` rows 1641 and 1642 are empty in our table** — the two effects drop
  into the *exact indices v667 uses*, so the skill's effect columns need no re-pointing.
- Icon 274 is new art (closest match in our atlas mad=65) → `scripts/add-skill-icon.py`
  → index **512** (our extension sheet `skill04.dds` has 5 of 169 cells used).
- Skill rows: free block **6210–7000**, or nearer the Muse cluster.
- STL key + STB col 0 name mirror.
- Server: `SKILL_TYPE_17` is fully handled
  (`src/sho_gameserver/src/cobjchar.cpp:1701`, `gs_user.cpp:5836`). **No server code.**

---

## 5. Format compatibility — verified structurally

**Both `.eft` and `.ptl` are unversioned.** No magic, no version field; the loaders read a
count and then fields sequentially. An Evolution-era schema change would desync *silently*
and read garbage rather than fail cleanly. So compatibility had to be tested, not assumed.

Method: mirror the C++ loaders in a parser and check every file consumes to **exact EOF**.

| corpus | result |
|---|---|
| the 4 new v667 `.ptl` | **4/4 exact EOF** |
| our own `.ptl` (control) | 679/682 exact EOF |
| the 2 new v667 `.eft` | **2/2 exact EOF** |
| our own `.eft` (control) | 559/563 exact EOF |

The control offenders (`_1YEAR`, `_HAPPYNEWYEAR`, `_MERRYCHRISTMAS`, `CHAINED`,
`ELECTSPHERE`, `RAINBOW01`) are pre-existing quirks in *our* data — several appear in both
lists — not parser bugs.

The new files use only particle event types SIZE / ALPHA / COLOR / VEL / TEXTURE, all inside
our factory's range. **This check matters:** `EventFactory`
(`src/engine/src/zz_particle_event_sequence.cpp:182`) returns NULL for an unknown type and
the following `assert(pEvent)` compiles out in release, so an unrecognised event type would
be a null-deref at `pEvent->Load(fs)`, not a warning.

### `.ptl` layout — `zz_particle_emitter::load` + `zz_particle_event_sequence::Load`

```
u32 sequenceCount
per sequence:
  u32 len + name
  f32 lifetime.min, lifetime.max
  f32 emitRate.min, emitRate.max
  u32 loops
  f32 spawnDir   [6]      (min.xyz, max.xyz)
  f32 emitRadius [6]
  f32 gravity    [6]
  u32 len + textureFilename
  u32 numParticles, alignType, updateCoord, texSizeW, texSizeH,
      implementType, destBlend, srcBlend, blendOp
  u32 eventCount
  per event:
    u32 eventType
    f32 timeRange.min, timeRange.max
    u8  fadeFlag
    f32 payload[N]        (N per table below)
```

Event type → payload float count (`zz_particle_event.h:65`):

| id | type | floats | id | type | floats |
|---|---|---|---|---|---|
| 1 | SIZE | 4 | 8 | VELOCITYX | 2 |
| 2 | EVENTTIMER | 2 | 9 | VELOCITYY | 2 |
| 3 | REDCOLOR | 2 | 10 | VELOCITYZ | 2 |
| 4 | GREENCOLOR | 2 | 11 | VELOCITY | 6 |
| 5 | BLUECOLOR | 2 | 12 | TEXTURE | 2 |
| 6 | ALPHA | 2 | 13 | ROTATION | 2 |
| 7 | COLOR | 8 | | | |

### `.eft` layout — `CEffectDATA::Load` (`src/client/io_effect.cpp:166`)

```
i32 len + effectName
i32 useSoundFlag
i32 len + soundFile
i32 soundLoopCount
i32 particleCount
per particle:
  i32 len + effectName ; i32 len + uniqueName ; i32 stbIndex
  i32 len + particleFilePath
  i32 useAniFile ; i32 len + aniName ; i32 aniLoopCount ; i32 stbIndex
  f32 pos[3] ; f32 rot[4]        (rot is yaw/pitch/roll, not a quaternion)
  i32 delay ; i32 isLink
i32 meshAniCount
per mesh:
  i32 len + effectName ; i32 len + uniqueName ; i32 stbIndex
  i32 len + meshPath ; i32 len + meshAniPath ; i32 len + texturePath
  i32 alpha, twoSide, alphaTest, zTest, zWrite
  i32 blendSrc, blendDest, blendOp
  i32 useAniFile ; i32 len + aniName ; i32 aniLoopCount ; i32 stbIndex
  f32 pos[3] ; f32 rot[4]
  i32 delay ; i32 repeatCount ; i32 isLink
```

The validators were written in the session scratchpad and are **not committed**. The layouts
above are enough to rebuild them in a few minutes; worth promoting to `scripts/` if we do
more than one import.

---

## 6. Traps

1. **`LIST_STATUS.STB` size mismatch.** v667 has 277 rows, **ours has 62**. Hailstorm's
   `STATE_STB1 = 103` does not exist here. `STBDATA::value()` *is* bounds-checked (returns
   `""` → `get_int32` → 0), so it degrades to "no state" rather than corrupting memory — but
   it is silently wrong. Any imported `STATE_STB1`/`STATE_STB2` must be re-pointed into our
   0–61 range or zeroed. Related: the `reference_stb_columns_as_array_indices` memory, where
   raw STB values used as array indices caused genuine client-side OOB writes.
2. **Skill IDs do not correspond across dumps.** v667 skill 400 is "Sword Force"; ours is
   "Divine Lightening". Every `NEED_SKILL_INDEX` (cols 39/41/43) must be re-pointed.
3. **Row collisions.** v667's Crumpler at 655–659 lands inside our Blood Attack (651–660).
   Always place into a verified-free block.
4. **`SKILL_ANI_HIT_COUNT` (col 70) is dead.** Defined in all three copies of `io_skill.h`,
   **never called anywhere in the codebase**. Multi-hit visuals will not reproduce without
   client work.
5. **Unversioned asset formats** — see section 5. Validate structurally before shipping.
6. **Double-backslash paths.** The new `.ptl` files store textures as
   `3DData\\effect\\particles\\texture\\_ice_02.dds`. That is `zz_slash_converter` territory —
   per the root `CLAUDE.md`, never pass that class through `ZZ_LOG` varargs without `.get()`.
7. **Sound index drift.** Identical filename, different row (`casting05.wav` 1005 vs 1004).
   Re-point by *path*, never by index.
8. **`FILE_EFFECT.STB`, not `LIST_EFFECT.STB`** — see section 2.
9. **Skill TSI uses the `x..x+39` rect convention**; `ITEM1.TSI` uses `x..x+40`.
   `scripts/add-skill-icon.py` already matches the skill convention.
10. **Clean up `.bak` files before a bake.** `src/pipeline/src/pack.rs` filters only *hidden*
    entries — no extension filter — so any backup left next to the data gets baked into the
    `.vfs`. See also the 2 GB `.vfs` offset limit.
11. **Servers cache STBs at startup.** Restart after any data edit; restart the client too.
12. Skill-tree nodes are **ours only** — hand-authored in
    `data/3DDATA/CONTROL/xml/skilltree_{soldier,muse,howker,dealer}.xml`. Champion lives in
    `skilltree_soldier.xml`. Neither newer client ships tree XML.

---

## 7. Candidate shortlist for the next attempt

Base-level rows whose own weapon column pulls a motion, or any effect, we do not have.
`(PvP)`/`(PvM)` duplicates and event/emote skills omitted.

| row | name | class | weapon | type | brings |
|---|---|---|---|---|---|
| 545 | Impact Wave | Knight | 211 | 17 | `impact_wave.eft` |
| 565 | Shield Reflect | Knight | 261 | 8 | `_reflect_01.eft` |
| 620 | Rain of Arrows | Knight | 271 | 7 | **motion** `bow_arrowshower_m1.zmo` |
| 710 | Dance of Flames | **Champion** | 222 | 17 | `_blaze_01.eft` |
| **915** | **Hailstorm** | **Muse** | **241** | **17** | **`_ground_blue_glyph_01.eft` + `hailstorm_01.eft`** |
| 1190 | Frost Ring | Mage | 241 | 17 | `_floor_frost_01.eft` |
| 1250 | Blaze | Mage | 241 | 17 | `_blaze_01.eft` |
| 1265 | Elemental Wave | Mage | 241 | 17 | `elemental_wave_01.eft` + `hit_02.eft` |
| 1870 | Arrow Shower | Scout | 231 | 7 | **motion** `bow_arrowshower_m1.zmo` |
| 2460 | Zulie Stun | Bourgeois | 232 | 3 | **motion** `h_dart02_m1.zmo` |
| 2515 | Triple Shot | Bourgeois | 233 | 3 | `_slow_snow_01.eft` |
| 2535 | Dual Layer | Bourgeois | 233 | 8 | `_reflect_01.eft` + `_reflect_02.eft` |

Overall v667 holds **159 `.eft`** and **22 `.zmo`** files absent from our data, so the pool is
larger than the skills that reference them.

`Dance of Flames` (710) is the standout if we want another **Champion** skill — same class
slot the original question was aiming at.

---

## 8. What was not verified

Everything above is static analysis: table tracing, byte comparison, and structural parsing.
**Nothing was run in-game.** Open questions for the actual attempt:

- Do the emitter scale and timing read correctly through our `ZZ_SCALE_IN`?
- Does the ground glyph sit at the right height on our terrain?
- Does `SKILL_TYPE 17`'s scope behave the same at our monster densities?
- Does the imported icon read correctly at 40×40 after the atlas round-trip?

## 9. Deploy reminder

`data/` is gitignored. Per the established workflow, edits are made in `data/`, then the user
bakes the VFS and deploys — **the script is the only committed record of a data change**, so
any import must land as an idempotent script in `scripts/` with `--dry-run` / `--verify` /
`--restore`, following `scripts/import-item.py` and `scripts/add-skill-icon.py`.
Ship every `rose*.vfs` alongside `data.idx`, not just `rose.vfs`.
