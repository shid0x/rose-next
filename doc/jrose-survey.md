# Jrose Data Survey

**Status:** survey only — nothing imported, no data or code changed.
**Date:** 2026-08-27.
**Dump:** `C:\Users\Thomas\Desktop\Testclients\Jrose` (loose `3Ddata\`, 79,709 files).
**Browser:** https://claude.ai/code/artifact/454d3391-12da-47f0-8f7c-80f664be4043 —
searchable tables for every zone, monster, item, skill and ability below. This doc is the
part you need while *writing* an import; the artifact is for *looking things up*.

Reader used, and the one to reuse: `scripts/rose-data-reader.py`. It handles both STL
dialects, both encodings, and sniffs the STL key column, which is exactly where a
naive parser silently produces wrong data rather than an error.

---

## 1. Verdict up front

Jrose is the **latest reference dump we have by several years**, and the only one that is
still recognisably the same game as ours structurally. Schema-wise it is the *easiest*
import target of any dump on the disk: it only ever appends columns.

| | |
|---|---|
| What it is | Late, actively-developed Japanese branch. Monster models are date-stamped through **2024-09**; `TRose.exe` is **2024-12** |
| Format | `STB1`, shared columns at **identical indices** to ours |
| Text | **Shift-JIS (cp932)**, legacy `N_NUM`/`I_NUM` STL dialect |
| Content | 2,441 monsters, 31,174 items, 140 zones, 3,087 skills |
| Art | Present in the loose tree (38,834 DDS, 17,235 ZMS, 91 zones with terrain) |
| Archives | `.VFS` + `data.idx` are **encrypted** (`IDX2`) — no key, and not needed |

The thing actually worth taking is **not** the content. It is the **stat vocabulary**
(§5) — Jrose has a modern affix system layered onto ROSE that we have no equivalent of,
and it is data-driven.

---

## 2. Three traps that produce wrong data instead of errors

These are the reason `scripts/rose-data-reader.py` exists. Each one fails *quietly*.

### 2.1 The STL dialect differs

We write `NRST01`/`ITST01`/`QEST01`: a language table, then per-block per-entry offset
tables. Jrose writes the older `N_NUM`/`I_NUM`/`Q_NUM`, which have **neither** — entries
follow the key table directly.

Point a modern-only parser at a legacy file and it reads the first varint of the string
data as a language count and asks for an 8 GB buffer. That one is loud. The reverse is
the quiet one, and see §2.3.

### 2.2 Jrose puts the STL key in the *last* column

Ours is column 45 on `LIST_WEAPON`. Jrose uses `cols - 1` (52 on `LIST_WEAPON`, 51 on
`LIST_BODY`, 29 on `LIST_USEITEM`). Hardcoding 45 against a Jrose table reads a
*different, valid* column and yields a plausible wrong string.

`Stb.key_column()` sniffs it (`[A-Za-z]{3,6}\d{3,5}`, >70% of occupied rows) and gets
45 for ours and 52 for theirs. Use it rather than a constant.

### 2.3 Our own STLs are UTF-8 with five language blocks

This one cost a wrong first draft of the survey. **Our** name tables are not latin-1 and
block 0 is not English:

| block | language |
|---|---|
| 0 | Korean |
| **1** | **English** |
| **2** | **Japanese** |
| 3 | Chinese (simplified) |
| 4 | Chinese (traditional) |

Reading ours as latin-1 block 0 produces Korean mojibake (`ê¼¬ë§ ì ¤ë¦¬ë¹`) that reads
as a corrupt file. Always `Stl(path, "utf-8")` and pick the block explicitly.

---

## 3. Matching a Jrose row to one of ours

Block 2 above is the useful lever, but **only for two tables**.

| Table | Best identity | Result |
|---|---|---|
| `LIST_NPC` | **Japanese name** vs our block 2 | 1,424 exact; +652 more by model file |
| `LIST_SKILL` | **Japanese name** vs our block 2 | 293 of 3,087 — so ~2,800 are new to us |
| Item tables | model path (col 1) | **weak, see below** |

Name matching beats model filename because filenames reorder within a family — Jrose row 2
is `JELLYBEAN2.MON` where ours is `JELLYBEAN1.MON`, and both are real Jelly Beans. Row
index is worse still: rows align ~85% by model file, but levels differ on 842 of the 1,833
that do align.

**Item name matching does not work at all.** Our item STLs were Anglicised: `LIST_WEAPON`
block 2 is *empty*, `LIST_BODY` and `LIST_USEITEM` block 2 are *copies of the English*.
Only `LIST_NPC` and `LIST_SKILL` kept genuine Japanese.

So items fall back to the model path, which answers a **weaker question**. It is
many-to-one — 1,856 Jrose weapon rows land on the 425 model paths we hold, because Jrose
ships many stat variants per model. It tells you *"the art already exists on our side"*
(7,295 of 31,174 item rows), which is the useful number for import cost. It never tells
you *"we already have this item"*.

> Carried over from `project_oro_import_survey`, and it still applies: **item IDs are not
> portable across dumps.** Copying a shop tab or a quest reward verbatim hands out the
> wrong item with no error.

---

## 4. What is in it

Occupied rows, Jrose against ours.

| Table | Jrose | Ours | × |
|---|---|---|---|
| `LIST_USEITEM` | 6,070 | 648 | 9.4 |
| `LIST_WEAPON` | 4,568 | 724 | 6.3 |
| `LIST_CAP` | 4,443 | 404 | 11.0 |
| `LIST_BODY` | 4,253 | 417 | 10.2 |
| `LIST_FOOT` | 3,586 | 387 | 9.3 |
| `LIST_ARMS` | 3,204 | 384 | 8.3 |
| `LIST_BACK` | 2,864 | 144 | 19.9 |
| `LIST_JEMITEM` | 1,745 | 356 | 4.9 |
| `LIST_JEWEL` | 1,339 | 203 | 6.6 |
| `LIST_PAT` | 1,168 | 230 | 5.1 |
| `LIST_SUBWPN` | 1,020 | 79 | 12.9 |
| `LIST_NPC` | 3,558 | 1,864 | 1.9 |
| `LIST_ZONE` | 140 | 57 | 2.5 |
| `LIST_SKILL` | 3,087 | 4,554 | **0.7** |

`LIST_SKILL` is the one table where we carry more rows. The difference is *width*: Jrose
runs 115 columns to our 87, so its rows describe more per skill rather than more skills.

**Zones we have no trace of:** Karkia (8 areas + an 11-map catacomb), Satellite, Skaaj,
Tenku, Ramesses (a 5×5 dungeon), Junon's Arena / Fortress / School / Ulverick, MyRoom
housing, ECL in Eldeon. Roughly 40 zone IDs all point at the single Ramesses map file —
that is an instanced dungeon, not 40 maps.

**Monster levels run to 275** against our 240 cap, with a genuine spread across every band
(290 rows at 200–219, 179 at 220–239, 191 at 240–259). Their curve is not ours: treat it
as a separate design, not a correction to `doc/balance-analysis.md`.

---

## 5. The find that matters: `STR_ABILITY.STL`

Jrose defines **159 ability ids reaching 501**; ours stops at 136. The gap is a modern
affix system, entirely data-driven, sitting on the same `(ability, value)` pair mechanism
our buffs and item bonuses already use.

- **Per-weapon-class attack and speed** (104–159): 1H sword, blunt, 2H sword, spear, axe,
  bow, gun, launcher, dual gun, staff, wand, katar, dual-wield, crossbow — each with its
  own ATK and ASPD id, plus combined ids (e.g. 139 "1H sword + blunt attack").
- **Damage and resistance by monster family** (178–194): vs Dragon, Giant, Undead, Insect,
  Plant, and "large Dedder". Attack *and* resist, separately.
- **Global multipliers**: 184 damage dealt, 185 damage taken, and separately 174/175 for
  the PvP-only versions.
- **Utility**: 195 skill cooldown reduction, 196/197 HP/MP leech, 190 attack range,
  191 skill AoE size, 192 bonus drop, 133 EXP gain, 134 drop count.
- **Rates as distinct stats** (99–103): hit rate, crit, evade rate, shield block, immunity.

`PVP_REVISION.STB` confirms the weapon-class enum those ids key off, and it matches ours:
211 1H sword, 212 blunt, 221 2H sword, 222 spear, 223 axe, 231 bow, 232 gun, 233 launcher,
241 staff, 242 wand, 251 katar, 252 dual, 271 crossbow, 261 shield.

Worth reading against `doc/balance-analysis.md`: our endgame problem is fight *length*
and the damage formula being scale-invariant. Family-specific and percentage multipliers
are exactly the lever that problem wants, and this is a working reference implementation
of the data layout for them.

---

## 6. Systems with no table on our side

All decode cleanly; column headings are in the artifact's Systems tab.

| Table | Rows | What it is |
|---|---|---|
| `LIST_BREAK` | 7,543 | Item dismantling: source item → up to 20 (item, qty, chance) outputs |
| `SERIES` | 1,174 | Item set bonuses: up to 12 item IDs + (ability, value) pairs |
| `LIST_ACHIEVEMENT` | 588 | Titles from kill counts, granting permanent stats (`38:100 23:10`) |
| `MYROOM` | 255 | Housing furniture (model + placement image) |
| `LIST_BLACKSMITH` | 208 | Upgrade recipes: base equip + materials + success% → upgraded equip |
| `LIST_KODUTITRADERATE` | 130 | Point-shop: timed buffs ("drop rate 2.0× 3 min") |
| `LIST_GRADE_CHANGE` | 59 | One item → three cosmetic variants |
| `LIST_EPIC` | 27 | Named affixes (Dragon Slayer = ability 178/179 at 25) + item whitelist |
| `LIST_ATTRIBUTE` | 11 | 8×8 elemental effectiveness matrix (100 neutral, 150 strong, 50 weak) |
| `PVP_REVISION` | 6 | Per-weapon-class PvP damage adjustment |

Also present and absent from ours: `ITEM_DROPNEW` (a 64-column drop table alongside the
legacy 51-column one), `KT_SWAP_OPT`, `PET_ACCE`/`PET_CLOTH`, `LIST_NPCFACE`,
`LIST_BALLOON`, `LIST_WEAPONEFFECT`, `APPRAISAL_GRADE`.

---

## 7. Things that look like problems and are not

- **`.MON` files do not exist in either dump.** `LIST_NPC` column 1 is a vestigial editor
  field, exactly like the model-file text column in `LIST_WEAPON` that nothing reads.
  Monster visuals come from `NPC/PART_NPC.ZSC` (374 KB here against our 214 KB) plus
  `LIST_NPC.CHR`. A first pass at this survey wrongly concluded 531 monsters were missing
  art; they are not.
- **The encrypted `.VFS` archives do not matter.** The loose `3Ddata\` tree is a full
  extraction of what we need: 91 zones with `.HIM`/`.TIL`/`.IFO`/`.LIT`, 142 ZSC, 547 QSD,
  463 CON.
- **Mojibake column headings are Korean, not corruption.** Some editor headings were saved
  in another codepage and decode under cp932 to U+FFFD and private-use noise. They carry no
  information. `rd.scrub()` drops them — worth doing, since some consumers reject those
  characters outright.
- **A required level in the 1200s is a disabled row**, not a real requirement. Requirement
  stat id 31 is level; a value of 1259/1264 is the standard trick for taking an item out of
  circulation without deleting it.

---

## 8. If we do import from this

Order of increasing risk:

1. **The affix vocabulary (§5).** Data-only on the client side; the work is server support
   for the new ability ids in `Get_SkillAdjustVALUE` / `Get_BaseAbilityValue`. No art, no
   IDs to remap, and it targets a problem we already have documented.
2. **Individual items using art we already hold.** 7,295 rows qualify. Use
   `scripts/import-item.py`; the STB row is new, the model index is not.
3. **Individual monsters.** Match by Japanese name to confirm what a row *is*, then treat
   stats as Jrose's curve and re-derive ours. Their AI ids and drop ids will not resolve.
4. **A zone.** Same shape as the Oro import — see `project_oro_import` and
   `scripts/import-oro.py`. Model index tables, AI rows and drop tables all need remapping,
   and the `.CON`/`.QSD` are Shift-JIS.

Whatever the target, **never bulk-copy a table**. Every dump we have is behind ours in
`CONTROL`, `PAT` and `QUESTDATA`, and item IDs are not portable.
