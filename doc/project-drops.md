# Drops — the endgame loot tier

> **Status, 2026-11-16.** Written on 2026-09-07 as the plan. The loot it plans **is
> authored**: `scripts/add-karkia-drops.py` (tables 900-917 and their zone rows,
> the catacombs' included; its docstring carries the measured rates) and
> `scripts/add-oro-drops.py`. Read "drops nothing" below as the state on that date;
> the sections on how the drop system works still hold.

Karkia stage 5. **The wiring is done; no loot is authored yet.** This is the plan for
the content pass, plus everything about the drop system that is not obvious from the
code. Companion to [doc/karkia-roadmap.md](karkia-roadmap.md).

---

## 1. Where this stands

`scripts/rewire-karkia-drops.py` (applied 2026-09-07, idempotent, `--restore`able)
fixed the plumbing. **Karkia currently drops nothing.** That is deliberate — it was
previously dropping *the wrong thing*, and correct-and-empty is a better base to
author onto than plausible-and-wrong.

What it corrected:

- Karkia's own drop tables were **831/832/835/851–854**, copied verbatim from Jrose.
  Three of those are live for our own monsters — 831 Desert Scavenger, 832 Agitated
  Desert Scavenger, and **851 Fearsome Terrasaurus King**, our top boss. Authoring
  Karkia loot into them would have handed it to Oro. They are now **900–906**,
  keeping Jrose's own grouping (one shared table for the Cemetery roster, one for
  Spire Village, one per boss).
- Karkia's **zone fallback** (see §2) resolved to `ITEM_DROP` rows 86, 87, 88, 131,
  133–136 and 144 — which were live tables for thirteen of our own low-level
  monsters (Melt Dut, Moss Ant, Moss Golem, the Pomics, the Rackies, Beetle,
  Assistant Chef Woopie). Rows 86 and 87 are literally named *"EVE Zone (Fishing
  System)"* and *"(Farming System)"*. Those tables moved to **940–948** with their
  users repointed, so the zone numbers are Karkia's now.
- Drop chance (`col 20`) went **20 → 80**, our own level-200+ median. At 20, ~80% of
  Karkia's rolls fell through to the zone table; that is what players were picking up.

| | Karkia table |
|---|---|
| Cemetery roster (20 monsters) | 900 |
| Revived Quarantine Officer | 901 |
| D-Seed | 902 |
| Hebarn Officer Pazugenti | 903 |
| Hebarn Officer Scylla Mira | 904 |
| Corroded Golem + Revived Veteran | 905 |
| Spire Village roster (10, incl. Drake α) | 906 |
| zone fallback, per zone | 86, 87, 88, 131, 133–136, 144 |

---

## 2. How the drop system actually works

From `CCal::Get_DropITEM` (`src/common/calculation.cpp:132`). Worth reading before
touching any of this — three of its properties are surprising.

```
drop_var = (WorldDROP + col20 - rand(1..100) - (levelgap + 16)*3.5 - 10 + dropRate) * 0.38
if drop_var <= 0                     -> no drop at all
if rand(1..100) <= col19             -> money instead of an item
if col20 - rand(1..100) >= 0         -> table = col 18   (the monster's own)
else                                 -> table = the ZONE NUMBER
cell = ITEM_DROP[table][1 + rand(0..min(drop_var, 30))]
```

- **A drop-table id and a zone id are the same namespace.** There is no separation.
  This is why Karkia's zone numbers were already somebody's loot table, and it will
  happen again to any content imported at a new zone number. Check both before
  allocating.
- **`col 20` is doing three jobs at once**: how often anything drops, how wide a
  slice of the table is reachable, and how often the zone fallback is used. It is not
  a clean "drop rate" knob.
- **A cell is `type * 1000 + number`**, so `item.m_cType = cell / 1000` and
  `m_nItemNo = cell % 1000`. **Item numbers above 999 cannot be dropped**, full stop.
  That is a tighter limit than the item *encoding*, which `tagBaseITEM` widened to an
  11-bit number (0–2047) — the drop table simply cannot address them.
- Cells valued **1–4 are redirect groups**, re-rolled at index `25 + value*5 +
  rand(5)`, i.e. column `26 + value*5 + k` (**since 2026-11-19**; the retail base was 26,
  one column late -- section 7). A table therefore has a common section (columns 1–30,
  "Item 1-20" + "Common 1-10") and four rarity buckets of five (columns 31–50, "Valuable
  1-20"). `ITEM_DROP.STB` has 51 columns.
- `Get_DropITEM` returns false outright when the player is **10+ levels above** the
  monster, so lowering a monster's level can silently kill its drops.

---

## 3. What we can and cannot drop

**Resolved 2026-09-08: everything is droppable now.** The table below was the state
before the drop encoding was widened; it is kept because the *reason* still matters
when reading old tables.

| set | ids | droppable before | now |
|---|---|---|---|
| Jrose weapons (`project_jrose_weapon_import`) | 1381–1453 | **0 of 73** — all above 999 | **73 of 73** |
| Jrose back items (`project_jrose_back_import`) | 957–1061 | 43 of 105 (957–999 only) | **105 of 105** |
| Jrose shields (`project_jrose_subwpn_import`) | 308–342 | 35 of 35 | 35 of 35 |

`src/common/include/rose/common/drop_item_code.h` gives drop cells the same wide form
shops already had — `type * 100000 + id` above 999 — and the three readers now share
it. The ranges cannot collide (legacy tops out at 31,999), and **all 10,421 non-zero
cells in `ITEM_DROP.STB` decode bit-for-bit as before**; there were no wide cells and
the 1,039 sentinels are all legitimate 1–4 redirect groups.

The trap worth remembering: `CLIB_GameSRV::CheckSTB_DropITEM` runs at server start-up
and **zeroes** any cell whose type it cannot make sense of. A decoder ignorant of the
wide form would not merely skip a wide cell — it would delete it at load, so the table
would look fine on disk and be empty in memory. That is why the sanitiser is one of
the three sites, alongside `CCal::Get_DropITEM` and the Monster Inspector's list.

Ceiling is now the wire format, not the packing: `tagBaseITEM` is 5 bits of type and
**11** of item number, so ids up to 2047 drop.

The imported Jrose weapon sets **cannot be loot** without changing the cell encoding.
Options, in increasing order of cost: sell them from a Karkia NPC in stage 6 (free,
and stage 6 is coming anyway); make them quest rewards; or widen the drop encoding,
which touches shared client/server code and every existing table's meaning. **The
shop is the recommendation** — it also gives Karkia's NPCs a reason to exist.

**Resolved by stage 6d, and better than expected.** `Rose::Store::encode_store_item`
already has a **wide form** — `type * 100000 + no` for ids above 999 — supported by
the client, the server *and* the shop editor. So the shop route needed no encoding
work at all. Drops have since been given the same form (§3), so the wall is gone on
both sides.

`scripts/add-karkia-shops.py` sells two of the tiers and **reserves three for
drops**, chosen so Karkia never duplicates Oro (whose merchant Huzam sells 210 and
230 from tabs 5 and 6):

| tier | ids | where |
|---|---|---|
| lv215 "Mirere" (13, all weapon types) | 1381, 1384, 1387, 1390, 1393, 1396, 1399, 1402, 1405, 1408, 1411, 1414, 1417 | **shop** (Gelt) |
| the Rose set (13, all weapon types; lv225, **lv100 since 2026-10-20**) | 1382, 1385, 1388, 1391, 1394, 1397, 1400, 1403, 1406, 1409, 1412, 1415, 1418 | **shop** (Gelt; since 2026-10-20 Luna: Pavrick, Sergei, Pabel -- `weapon-ladder-pass.py`) |
| lv220 (11) | 1426–1436 | **reserved for drops** |
| lv235 (13, **668's dinosaur set since 2026-10-25**; the Jotunn line 1437–1446 was erased, `replace-235-weapons.py`) | 908, 915, 926, 932, 939, 947, 950, 959, 966, 974, 982, 990, 999 | **reserved for drops** |
| lv240 (13, the mythical names) | 1383, 1386, 1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410, 1413, 1416, 1419 | **reserved for drops** |

Left out of both: the partial lv210 set (1420–1425; 1424 Photon Cannon is the lv230 Arcidian line's
launcher since 2026-10-20, the other five wore its models and were erased, `erase-retired-weapons.py`) duplicates Oro's tier, and the
low oddments (1447–1453, levels 150–205) sit far below Karkia's 215–240 band.

The lv240 set — Bahamut, Phoenix, Griffon, Unicorn, Albion, Quetzalcoatl, Aerie,
Catoblepas, Oberon, Mermaid, Spriggan, Giranda, Hellhound — is the crown tier and
belongs on the bosses (§4d), and **as of 2026-09-08 it can drop** — that was the last
thing blocked on the encoding. `add-karkia-shops.py --verify` proves none of the 34
reserved weapons has leaked into any shop tab.

---

## 4. The content plan

Three layers, in the order they should be authored.

### 4a. Materials as the backbone

The filler that stops a kill feeling empty, and it feeds crafting. Cheap, uses items
we already have, and it is what our own tables are mostly made of — our best live
endgame tables (940–942, ex-Moss Golem/Ant, level 200–210) run roughly **15–20
material cells to 4–8 use-item cells**, which is the ratio to copy.

Candidates already in `LIST_NATURAL`, at the tier Karkia sits in:

| | |
|---|---|
| metals | `12:8` Chromium, `12:9` Tiar, `12:10` Damascus, `12:16` Orihalcon, `12:17` Adamantium |
| woods | `12:37` Cinnamon, `12:38` Red Sandalwood, `12:39` Otherworldly, `12:40` Sephiroth |
| leathers | `12:47` Thick, `12:48` Hard, `12:49` Transparent, `12:50` Patterned |
| cloth | `12:59` Twill |

Karkia is a dead world of ruins and undead, so the flavour argument is for **metals,
leather and bone over wood** — Spire Village and the Cemetery should not read like a
forest. That is the one place to deviate from copying our existing tables wholesale.

### 4b. Use items as the reliable second layer

`10:13` Vital Water (XL), `10:32` Spiritual Water (XL), `10:157` HP Point (+1000),
`10:167` MP Point (+700), `10:171` Stamina. An endgame zone that funds its own
potion consumption is doing something useful even before the rare drops land.

### 4c. The imported Jrose sets as the signature drops

**This is the part that makes Karkia's loot feel like Karkia's.** The 35 shields
(`308–342`) and the 43 addressable back items (`957–999`) came from the same client
as Karkia itself, they are already in with art and stats — and **they currently have
no source anywhere in the game**. Karkia is where they should come from. It closes
`project_jrose_subwpn_import` and half of `project_jrose_back_import`.

Put them in the rarity buckets (columns 26–50), not the common section.

### 4d. Bosses

Each already has its own table (903–906). They should carry the best of §4c at a
much better rate, and are the natural home for anything we do not want in the field.

Note `NPC_DROP_MONEY` (`col 19`) is **0 across our entire game**, Karkia included —
nothing drops money today. Worth deciding deliberately rather than inheriting.

---

## 4e. Built — the numbers as shipped  *(DONE, 2026-09-08)*

`scripts/add-karkia-drops.py`. 9 tables + 2 zone mirrors, 11 rows written.

Measured by simulating `Get_DropITEM` faithfully over 600k kills per case, not
estimated:

| | Cemetery 900 (lv215-228) | Alphas 906 (lv228-240) | Boss 903 (lv240) |
|---|---|---|---|
| materials | 38.7% | 38.6% | 12.2% |
| use items | 7.3% | 3.8% | 3.8% |
| **shield** | **1 in 30** | **1 in 30** | 1 in 13 |
| **weapon** | **1 in 33** | **1 in 15** | **38%** (mythical) |
| money | 15.1% | 15.0% | 0% |
| nothing | 32.5% | 32.7% | 38.4% |

Three things worth keeping:

- **The zone rows 87 and 88 mirror 900 and 906.** `col 20` = 80 sends **20% of
  every roll to the row numbered after the zone**, so without the mirrors a fifth
  of all rolls hit an empty table and every rate above loses 20% — shields would
  be 1 in 37, not 1 in 30. This is the cheapest way to make the fallback harmless.
- **Money fires *instead of* an item**, so `col 19` is not free: at 15% it takes
  15% off the item rate. Bosses are set to **0** so it can never eat a mythical
  roll. A money drop pays ~2,000 z at lv215 and ~2,500 z at lv240; at 15% that is
  ~319 z/kill, so a 768,000 z shop weapon is ~2,400 kills. Raise `WORLD_VAR_DROP_M`
  (default 100) if that reads as too slow — it is a live GM-settable world var,
  not a table edit.
- **906 carries three redirects, not two**, so weapons run 1 in 15 there against
  1 in 33 in the Cemetery. Deliberate: it is the only cap-level farm and has twice
  the weapon pool (10 of the 13 lv235 against 5 lv220; the other Karkia tables take
  five-wide windows of the 13, so each one drops somewhere).

**Both Drakes were bosses sharing a trash table** — 2729 pointed at 900 and 2699
at 906, so they dropped exactly what the mobs around them did. They now have rows
907 and 908.

Verified that `CLIB_GameSRV::CheckSTB_DropITEM` zeroes **nothing**: all 9,570
decodable cells in the whole table survive it, including the 66 new wide-encoded
weapon cells. That check matters because the sanitiser deletes what it cannot
parse rather than ignoring it.

The level gap does the rest of the work: a level-240 player gets **100% nothing**
from the Cemetery, which is what makes 906 the only cap farm rather than a
preference.

---

## 5. Oro — built  *(DONE, 2026-09-16)*

Oro had the same defect and a worse wiring: every monster pointed at an empty table
in 773–870 (empty in every dump we own — RoseZA never authored them, and 667 keeps
per-monster drops inline in its own `LIST_NPC` cols 88–102 against its own item
numbering, so neither could be copied), the 19 species the 667 import added carried
`col 20` = 100 (every roll to the empty table), and the rest fell through 20% of the
time to rows 71–85 — the Eldeon Ikaness and Sikuku tables and the Junon town NPCs',
two of them still titled *"MVP Zone (1P-VS-Kings)"* and *"TVT Zone"*. Money was 0.

`scripts/add-oro-drops.py` does the rewire and the content in one pass, importing
`build_row`/`encode` and the item pools from `add-karkia-drops.py` so the two
continents share one encoding and one calibration. `scripts/import-oro-materials.py`
first brings nine RoseZA monster parts into `LIST_NATURAL` (Asper Fang, Snapper Beak,
Devourer Plate, …). Its lesson repeats §4a's: rows 273/274 are blank in the STB but
the STL still names them Archangel/Archdevil Feather and drop tables 63–71 reference
them, so the Snapper parts went to 285/286 and the rune piece to 287.

Design choices, and why:

- **One table per species**, not per roster (col 18 left alone): the materials are
  named for the family that drops them, and a shared table pays them from the wrong
  animal. The table is generated, so 57 rows cost the same as four.
- **Weapons are what Karkia left unused**: the Jrose lv210 set 1420–1425 with 1447/1450
  in the Golden Ring, then the shared lv220 and lv235 pools. Five-wide windows rotate
  through the pool per species. (Since 2026-10-20 the Ring pool is 1447/1450 alone:
  1424 became lv230 and the other five were erased -- see `weapon-ladder-pass.py`.)
- **The lv240 armour is Oro's signature**, as the mythical weapons are Karkia's. It was
  drop-only since the armour pass and dropped nowhere. Body and cap come from the
  Fossil Sanctuary's field buckets, gauntlets and boots from its four kings.
- **Oro pays a little less than Karkia, on purpose.** Field tables fill 10 slots
  (12 in the Sanctuary) against Karkia's 12, and a Sanctuary king's armour rate is
  held at a Karkia boss's mythical rate, not above it (a first cut with body/cap
  buckets on the kings too measured 50% and was taken back down).

Measured with `--simulate` (a faithful `Get_DropITEM`, zone fallback going to the
zone row), 300k kills per shape at level parity:

| | Ring/Wasteland field | Sanctuary field | Ring/Wasteland king | Sanctuary king |
|---|---|---|---|---|
| materials | 30.4% | 30.4% | 17.8% | 15.8% |
| use items | 8.3% | 8.3% | 5.1% | 4.5% |
| **shield** | 1 in 26 | 1 in 26 | 9.3% | 7.7% |
| **weapon** | 1 in 29 | 1 in 29 | **24.9%** | — |
| **lv240 armour** | — | **1 in 20** (body/cap) | — | **37.6%** (gauntlets/boots) |
| money | 15% | 15% | 0% | 0% |
| nothing | 39% | 34% | 43% | 34% |

Zone rows 71/78/79/81/83/85 hold a generic table for their tier so the 20% fallback
is live; the legacy tables that sat there moved to 949–954 with their users repointed
(the same relocation `rewire-karkia-drops.py` did for 86–144). Zones 72/73/80 spawn
nothing and were left alone.

`--restore` is cell-level for both STBs. After running: restart the servers and
re-bake the VFS (the Monster Inspector reads these tables client-side).

---

## 7. The 1-200 philosophy, measured  *(2026-11-19, roadmap 1.2)*

`python scripts/economy-audit.py drops` (exact odds of `Get_DropITEM` at level parity,
every spawned species once per zone; `--by zone`, `--gap`, `--weight regen`) and
`economy-audit.py zone-rows`. Field monsters of the reachable 10-199 maps, per kill:

| | 1-200 field | 200+ field (Oro, Karkia, crypts) |
|---|---|---|
| nothing | 17-37% | 33-40% |
| money | 5-30% (0 across Eldeon 170-209) | 15% |
| materials (a recipe uses them) | 25-45%, **58-64% at 170-199** | 26-30% |
| junk (sell only) | 0-14% (Luna / Eldeon) | 2-4% |
| quest items | 5-19%, ~5% past 150 | 0-3% |
| consumables | ~15% to 49, 3-7% after | 5-8% |
| jewellery | 0-2% | **0** |
| gems | 0-1% (bosses 2-6%) | **0** |
| gear | **1-4%**, ~5-12 levels under the monster | **6-13%**, 2-10 under |
| uniques (stat-gated, no level) | ~0 field, 0-4% bosses | **0** |

Bosses 1-200 pay gear 3-30% a kill; the 200+ bosses 22-33%.

**Map tables (the zone fallback rows) have a philosophy of their own**: in every retail zone
1-62 the row is ~60% materials, ~25% consumables, some quest items, **never gear**. From the
Sikuku zones on, ours carry gear: 63-65 are the Sikuku monster table, Oro's 78-85 are
generic tier tables with gear buckets, and Karkia's, the crypts', Shibuya's and Ulverick's
are copies of a monster table. Under the double drop (1.3) every kill rolls its zone row,
so a row with gear doubles the gear rate.

**Gaps the 200+ band has**: jewellery is graded by quality, not level, and none of the 55
pieces of quality 50+ (Platinum, Diamond) drops anywhere; seven gems of quality 70-90 have
no source; the 668 uniques (904-995) have none either.

**Built 2026-11-19** (the user's decisions: all five proposals, bosses ~90% gear):
`add-karkia-drops.py` and `add-oro-drops.py --rewrite`, shapes in the first docstring
section of the Karkia script. Per field kill, for the double drop's monster roll: gear
4.2% Karkia / 3.0% Oro, materials ~40-49%, consumables 6%, jewellery 1.5%, gems 0.8%,
money 15%, nothing ~23%; bosses (kings, skulls, the crypt and Hebarn bosses) ~90% gear;
the 200+ zone rows are map tables (~55-65% materials, 12% consumables, no gear). The
pools follow the re-levelled bands. Measured on today's single-roll server the field
gear is a fifth lower (the 20% zone fallback reads the gear-free map row): the tables
are meant to ship with the double drop. Gold stars (Dark Tower, Element Battler) keep
field tables. Backups of both STBs from before the pass: `build/drops-reshape-2026-11-19/`.

**Below Oro, 2026-11-19** (`scripts/drops-below-oro.py`; the user: "monsters shouldn't have
an empty table but something adequate to their level band", the Prison "fairly simple"):
the Sikuku tables left zone rows 63-65 (to 921-923) and those rows are map tables, as are
Shibuya's 126 and Ulverick's 34 (their importers build them with
`add-karkia-drops.map_from`); Executor Kera has a boss table (the 668 uniques on top, ~half
his kills; the vanilla lv200 weapons and armour below, which had no source); the other
prison monsters carry the Prison's map table; every other empty table copies the nearest
same-class table of its zone (Lunar Keepers 924-926, Tiger Riders, the Forest of Wandering
elites), gold stars with gear on top (Turak ~21%, Karkia's Dark Tower / Element Battler
~26%: "better than the rest but not up to boss level"); the uniques ride a bucket in every
lv180-210 table of zones 62-64 (about 1 kill in 300-1000 for a plain monster, more on
stars, 6.5% on the Moss Golem).

**Found, not fixed (2026-11-19):** Xita Refuge and the Shady Jungle (Eldeon, lv140-183)
point most of their redirects at empty buckets -- 71 of 104 and 79 of 89 redirect slots --
so their gear almost never drops (0.0-0.2% a kill). **Checked against the reference
clients the same day: nothing was lost.** ruff (whose rows ours match 30/30), 139, Evo 137,
Jrose and RoseZA leave the same groups empty: retail authored redirects into buckets it
never filled. Rose Brazil and QQ-iROSE fill some, their own way (Brazil single weapons
under ids that are other items here; QQ Sikuku materials in tables it rewrote) -- not
portable.

**And a server off-by-one, inherited from the original source:** ITEM_DROP's columns are
named Item 1-20 (1-20), Common 1-10 (21-30), Valuable 1-20 (31-50) -- four buckets of five
at 31-35, 36-40, 41-45, 46-50 -- but `Get_DropITEM` re-rolls a redirect at index
`26 + 5g + rand(5)`, read as column `27 + 5g + k`: bucket 1 at 32-36, bucket 4 at 47-51
(column 51 does not exist; the "slot 50 is dead" rule of add-karkia-drops.py). ruff fills
"Valuable 1" (column 31, never read) in 211 of its 228 tables with redirects, and
"Common 10" (column 30, read) in 286, so the common section is right and the buckets are
one column late: every retail table lost its first valuable item and its buckets bled
into each other. **Fixed 2026-11-19** (the user's go): `kRedirectBaseColumn` 26 -> 25
(`drop_item_code.h`, shared by `Get_DropITEM`, the start-up check and the Monster
Inspector); `CheckSTB_DropITEM` also walked indices 1-50, so column 1 was never checked
and a column past the end was read -- it walks 0-49 now. Every script-built table was
regenerated with the new base (`add-karkia-drops.py` / `add-oro-drops.py --rewrite`,
`import-shibuya.py --stage 5`, `import-ulverick.py --stage 4`, `import-cerberus.py --stage
4`, `import-hebarn.py --stage 2`, `drops-below-oro.py`). Client and server ship together:
an old server reads the new tables one column late.

**Eldeon's empty buckets, filled 2026-11-19** (`drops-below-oro.py` step 4): every empty
bucket a zone 61-65 table reaches holds five pieces of the zone's own bucket gear nearest
its usual level offset. Field gear a kill: Xita 0.1% -> 0.7%, Shady Jungle -> 1.4% -- the
low end of 1-200, because those tables keep their redirects in slots 24-29, which their
low col 20 (25-60) rarely reaches. The one table left with dead redirects in the whole
game is the Pumpkin's (108, Valley of Luxem Tower).

**The double drop, built 2026-11-19** (roadmap 1.3): `CCal::DropRoll` -- `Monster` (own table, money) and `Map` (zone row, no money) -- rolled one after the other on every kill (`cobjchar.cpp`); `server.toml [game] map_drop` (default 1; 0 = the retail single roll). The second item reaches clients through the sector broadcast only. Baked and deployed with 1.1 and 1.2 the same day, untested in game.

**Material variety, 2026-11-19** (the user's worry after testing 1.3): Oro paid 24 distinct
materials (four vanilla ones per tier plus the monster parts) and Karkia 25, against 63-114 in
every band below; and all 34 imported materials were class 421 / quality 62, copies of
Chromium, so a Scroll of Might crafted and broke up as ore. `material-classes-200.py` gives
them real classes (essence, stone, rare metal, metal, other) and qualities 72-80, above
anything below 200 (the class is what a recipe and break-up read; the quality is the
progress side of the craft roll). Oro's tiers draw eight vanilla materials across seven
classes each, Karkia's pools end with the top essence, powder and rare metal (Hermes and
Lisent (Ra) / (U) dropped nowhere before). Now Oro 38 distinct materials over nine classes,
Karkia 33 over six, 11-15 per zone. Oro also paid "[PvP] Tokens" on ~10% of kills: row
290's col-0 name is "Spiritual Stone of Oro", its STL text the Junon PvP token -- the Oro
pools no longer name it. Found, not changed: Eldeon's retail tables name rows 269 and 276,
which import-oro-materials.py filled with Asper Tongue and Devourer Horn, so two Eldeon
monsters drop Oro parts.

**The Artisan's 200+ crafts, 2026-11-19** (roadmap 1.4, `scripts/artisan-crafts-200.py`;
the split itself is in `add-karkia-drops.py`, CRAFT_*): retail crafting stopped near item
level 130, and at 200+ the imports had copied craft categories with empty recipes (category
57 on the armour, which no skill has). The Dealer and Bourgeois keep their ranks 1-5; the
Artisan (ranks 6+) gets 72 crafts, each with its own LIST_PRODUCT row (1700-1771):
weapons 225 / 230 split per type into craft-only (4), both (4) and drop-only (5) so every
type is craftable in one tier; Egyptian gauntlets craft-only, boots both; OG boots craft-only,
caps both; Jewel Item Craft (which listed nothing: no jewel had a category) makes the Platinum
and Diamond lines, Diamond necklaces craft-only; four craft-only backs (Antares Ward, Noble
Sacred Cross, Michael's Half-Wing, Chieftain's Heirloom Blade) need Fafnir's Magic Stone from
the Drakes. Craft-only items left every drop pool (checked: no drop table names one). 261
craft listings with empty recipes (lv200+, rank 11+, category 57) lost their category, so the
craft window no longer offers recipes that cannot be made. Hebarn (an easter egg) pays the
Norse set now; its Egyptian gauntlets were the Artisan's.

Outside the 200+ band, for the record (not changed): Skaaj pays nothing on 94% of kills
(blank zone row, empty tables), Mount Eruca nothing at all (unreachable), Sunshine Coast
70% nothing (event zone); Eldeon 190-209 pays gear ~50 levels under the monster and no money.

## 6. Open questions

- Should Karkia drop **our** endgame weapons (ids ≤ 999), or stay materials- and
  armour-flavoured so weapons stay a shop/quest reward?
- Money drops: leave at 0 like the rest of the game, or make Karkia the exception?
- The three placeholder zones (131/133/144) still spawn only a level-2 Jelly Bean
  (roadmap §4, decision 4). Their zone tables exist and are empty; leave them until
  the zones are populated.
