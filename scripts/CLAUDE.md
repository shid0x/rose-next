# CLAUDE.md — Game data scripts (scripts/)

Codecs, importers, audits, repair tools, baking, and the monster/skill/AI/spawn/balance
data rules. Loaded when a file under scripts/ is read; the root CLAUDE.md index points here.

### Map Files: Readers, Writers And Tools (`scripts/mapgen/`, merged 2026-10-04)

**Reuse these before writing any new parser.** The six map formats (ZON, HIM,
TIL, MOV, IFO, LIT) have codecs that round-trip retail files **byte for byte**:
`python scripts/mapgen-roundtrip.py --selftest` proves it on every map file under `data/3DDATA/MAPS` (9,035 at the 2026-10-04 merge; re-run after any import, since there are 9,401 now).
The binary layouts, and the client/server code that consumes each field, are in
[docs/mapgen/FORMATS.md](../docs/mapgen/FORMATS.md).
Import them with `sys.path.insert(0, "scripts")` and `from rosefmt import zon, chunk, ifo, lit` (the `mapgen.*` names still work as shims; 2026-10-12).

| File | Module | API |
|---|---|---|
| `<zone>.ZON` | `rosefmt/zon.py` (`mapgen.zon` shim) | `parse(bytes)` / `build(zon)`; info, event points, textures, tiles, economy |
| `x_y.HIM` / `.TIL` / `.MOV` | `rosefmt/chunk.py` (`mapgen.chunk` shim) | `parse_him` / `build_him` (+ `compute_him_bounds`), `parse_til` / `build_til`, `parse_mov` / `build_mov` |
| `x_y.IFO` (all 13 lumps) | `rosefmt/ifo.py` (`mapgen.ifo` shim) | `parse(bytes)` / `build(ifo)`; `m.lump(ifo.OBJECT)` -> records (`obj_id`, `pos` zone-centre cm, `rot`, `scale`); NPC/MOB, sound, effect, regen, ocean, warp, collision, event lumps; `None` = lump absent |
| ZON/IFO lump container | `rosefmt/container.py` (`mapgen.container` shim) | unknown lumps kept raw, so a rewrite is byte-identical |
| `LightMap/*.lit` | `rosefmt/lit.py` (`mapgen.lit` shim) | `parse_lit` / `build_lit` (`LitObject.obj_index` 1-based ordinal, `LitPart` cell fields) |
| `LIST_*.ZSC` (read, geometry view) | `mapgen/catalogue.py` over `rosefmt.zsc` | `read_zsc(path)` -> meshes + objects/parts (mesh, texture, transform, parent, collision, material flags); `rosefmt.zsc.decode_props` / `material_flags` are the decoders |
| `LIST_*.ZSC` / STB / STL / CHR / LTB (write) | `scripts/rosefmt/` | `Stb`, `Stl`, `Zsc`, `Chr`, `Ltb` (see Data File Codecs); `rosedata` re-exports them with the table schema |
| `.ZMS` | `rosefmt/zms.py` | `parse`/`build` (v5-v8, byte-exact on every shipped ZMS -- 7,842 when last run; `rosefmt-roundtrip.py` re-checks), `Mesh.positions_cm` / `triangles_cm` / `bbox_cm`, `rigid_v8` to make one; `catalogue.mesh_*` and `objlight.read_zms` are views over it |
| `.eft` effects | `rosefmt/eft.py` | `read` / `build` (every field; byte-identical on all 609 readable effects -- `rainbow01.eft` is truncated), `with_mesh_flags(blob, i, z_test=0, ...)` edits in place; a new effect is a `read`-shaped dict (offsets in cm, a character's front is -Y). **A flat ground mesh needs `z_test` 0** or it z-fights the terrain and flickers (Jrose's Star Gazer) |
| `.DDS` | `rosefmt/dds.py` + `lighting.encode_dds` | header `parse_header`/`build_header`/`info`/`side`, uncompressed BGRA sheets `read_bgra`/`write_bgra`; DXT encoding is texconv (`lighting.encode_dds`, `make-minimap.py`) |
| editor STB lists | `tiles.read_stb_cells` | any STB's cells as text |
| a whole zone's heights | `prefab._zone_field(zdir)` | heightfield (row 0 = south) + SW chunk slot; `mapgen/relight.Zone` loads a zone the way the client does (chunks, IFOs, ZSCs, water) |

Coordinates: chunk folder `x_y` is slot `(x, 64 - y)`; IFO positions are
cm relative to the zone centre (world = pos + 520000); 1 chunk = 160 m,
64 cells of 2.5 m. `objlight.client_accepts` is the client's own
"is this record inside its chunk" test.

Tools built on them:

- `scripts/mapgen-zone.py` -- the map generator: a layout (Claude writes it in
  chat) -> `compile` -> `preview` / `build` / `install --zone N` / `verify` /
  `uninstall` (cell-level manifests in `build/mapgen/installed/`, an install lock)
  / `shots` (editor screenshots). Plan, decisions and history:
  [docs/mapgen/PLAN.md](../docs/mapgen/PLAN.md), design rules
  [docs/mapgen/DESIGN.md](../docs/mapgen/DESIGN.md).
- **Map editor screenshots without a person**: `data/Map Editor.exe --shots job.txt`
  (`Engine/ShotRunner.cs`), driven by `mapgen-zone.py shots` and
  `relight-zone.py shots --zone N [--at X,Y]` -- the way to *look* at a map from a
  script (it renders object and terrain lightmaps).
- Lightmap bakers: `mapgen/lighting.py` (ground) and `mapgen/objlight.py`
  (objects), fitted against retail; `scripts/relight-zone.py` applies them to any
  existing zone (see Data Repair Tooling); `scripts/mapgen-lightmap-survey.py`
  measures retail lightmaps and scores a bake against retail
  (`compare` / `compare-objects`).
- `scripts/audit-lightmap-index.py` -- the lightmap crash audit, also a quick way
  to list records that name objects a ZSC does not have (missing in game).

### Data File Codecs (`scripts/rosefmt/`, 2026-10-12)

**Every file format has one reader and one writer, in `scripts/rosefmt/` -- the map codecs too since 2026-10-12 (`mapgen.zon`/`chunk`/`ifo`/`lit`/`container`/`binio` are re-export shims).**
Never write another STB/STL/ZSC/CHR/LTB/AIP/TSI/QSD/CON parser in a script, and
never load another script to get one**. `import rosedata as oro` (`scripts/rosedata/`,
2026-10-12) gives the codecs plus the table schema (`NPC_*_COL`, `DEFAULT_PVP_STATE`, the
data-relative paths), the file helpers (`rel_path`, `copy_files`, `effect_chain`), the ZON
questions (`zon_tiles`/`zon_events`/`zon_zone_type`) and `import_characters`: everything the 73
importers and passes used to load `import-oro.py` for. Rules, inventory and what was deliberately left alone:
[doc/data-codecs.md](../doc/data-codecs.md). Import with
`sys.path.insert(0, dirname(__file__))` then `from rosefmt import Stb, Stl, ...`.

| Format | Module | API |
|---|---|---|
| `STB1` tables | `rosefmt/stb.py` | `Stb(path, encoding="latin-1")`: `get/s/i` (game indices), `set/set_i`, `occupied`, `colname/rowname`, `key_column()`, `grow_to`, `add_column(name)` (rebuilds the header; exact on all 83 parsed shipped headers), `to_bytes`, `save(dry)` |
| STL strings, both dialects | `rosefmt/stl.py` | `Stl(path, encoding)`: `has/index/name/field`, `append/set_name/remove`, `lang/by_key/by_id`, `to_bytes`; `legacy` flag; `read_table`/`write_table` |
| ZSC model tables | `rosefmt/zsc.py` | `Zsc`: `meshes/materials/effects/objects`, `to_bytes(extra…)` appends, `rebuild()` rewrites objects; `serialize_object`, `prop_paths`, `asset_refs` |
| `LIST_NPC.CHR` | `rosefmt/chr.py` | `Chr`: `skeletons/motions/effects/chars` (hole = `None`), `to_bytes`, `save` |
| LTB dialog text | `rosefmt/ltb.py` | `Ltb`: `text/set_text/set_all_langs/grow_to/to_bytes`; `wide` (ULNGTB) vs narrow (LNGTB) detected |
| `.aip` AI | `rosefmt/aip.py` | `parse(b)` -> (header, title, patterns, tail); `build(...)` |
| TSI atlases | `rosefmt/tsi.py` | `read(path, strict)`, `write`, `iter_sprites`, `texture_entry`, `sprite_entry(texid, x1, y1, x2, y2, label)` |
| QSD quests | `rosefmt/qsd.py` | `parse/build`; `walk`, `find_entity`, `has_trigger`, `build_trigger`, `append_pattern`, `drop_conditions`, `item_refs`, `remap_items`, `quest_refs` |
| `.CON` dialogs (bytes) | `rosefmt/condialog.py` | `split/join` (Lua + QEX1, keyed on the final size), `str_ids`, `get_item`, `set_click`. Not `con.py`: a reserved name on Windows |
| IFO as raw lumps | `rosefmt/ifo_lumps.py` | `read_ifo`, `read_lump`, `build_object_lump`, `build_ifo`, `LUMP_*`; a view built on **`rosefmt.ifo`, the structured codec for new work** (record layout lives there only) |
| ZON / HIM / TIL / MOV / IFO (records) / LIT | `rosefmt/zon.py`, `chunk.py`, `ifo.py`, `lit.py`, `container.py` | the map codecs (table above); `rosefmt.CODECS` maps extension -> (parse, build) |
| `.ZMS` meshes | `rosefmt/zms.py` | `parse`/`build`, `header`, `Mesh.positions_cm`/`triangles_cm`, `rigid_v8` |
| `.DDS` headers / BGRA sheets | `rosefmt/dds.py` | `parse_header`/`build_header`, `split`/`join`, `info`, `side`, `read_bgra`/`write_bgra` |
| ZMO clips | `rosefmt/zmo.py` | `read_header`, `events`, `event_set`, `with_events`, `frame_values`, `truncated`, `sliced(blob, start, end)` (a frame range as its own clip, events moved with it; identity on all 480 avatar clips) |
| `.ptl` particles | `rosefmt/ptl.py` | `parse`/`build` (byte-exact on all 719), `Sequence.update_coord` (0 world = left behind by a moving effect, 1 rides it), `events_of(type)` |
| `data.idx` | `rosefmt/idx.py` | `parse` (read-only; unsigned offsets; tolerates a stale table offset, as in EVO's index), `read_files(idx, names)` pulls files out of the archives beside it |
| primitives | `rosefmt/binio.py` | `pstr8/pstr16/vstr/cstr` read+write, `FormatError`, `read_file`; `Reader`/`Writer` cursors (the map codecs') |

**Before and after any codec change** run `scripts/rosefmt-baseline.py run <dir>` and `diff`
the two captures (65 read-only script commands + reader hashes; ~25 min, `--quick` for most of it).
`python scripts/rosefmt-roundtrip.py` re-serialises every shipped file of every
format and must stay all-OK (`--root <dump>` for a reference client, `--selftest`
for synthetic edit checks). Things that will bite: a `con.py` cannot be imported
on Windows (the console device); `LIST_STRING_S.STL` has five language slots on
one block, and a writer that does not keep the sharing quadruples the file; the
retail `LNGTB_*.LTB` count bytes where `ULNGTB_*.LTB` count wide chars; an empty
LTB cell can carry a stale non-zero position; `Stb.s()` does not strip.

### Data Repair Tooling

Our `data/` is a translated iROSE dump with gaps; the reference dumps in `C:\Users\Thomas\Desktop\Testclients\` (QQ-iROSE, RoseZA, titanRose) are intact, so diffing a single field across all three is a fast, high-confidence way to find and fix them. The repair scripts below are idempotent, take `--dry-run`, and verify after writing; the audits among them are read-only. `data/` is gitignored, so **the script is the only committed record of the change** — put the reasoning in its docstring.

- `scripts/fix-zms-bone-slots.py` (2026-11-01) — **run after importing a skinned model from another
  dump.** Some exporters write 0xFFFF in an unused (weight 0) bone slot; `load_mesh_8` resolved every
  slot through the mesh's bone table unchecked, so tsuki's Cerberus `HEAD01.ZMS` crashed the client in
  the lair (only when the page 128 KB past the table was unmapped -- most loads read garbage and went
  on). The engine now clamps and logs once; the script zeroes the slots in `data/` (originals in
  `build/zms-bone-slots/orig/`) and reports, without touching, a bad slot that carries weight.
- `scripts/audit-spawned-npcs.py` (2026-10-12) — **run after any import that touches monsters, NPCs, AI or
  maps**; read-only, two seconds for the whole game. Walks every zone row's REGEN and MOB lumps, follows
  every AI action that spawns or morphs into a monster (Change_CHAR, Create_PET, CallToAttack, Summon,
  SummonMaster*), and checks each reachable id through `LIST_NPC` (blank row, AI file on disk, hand
  items against `LIST_WEAPON`/`LIST_SUBWPN.ZSC`), `LIST_NPC.CHR` (entry, skeleton), `PART_NPC.ZSC`
  (models in range with parts, every mesh and texture on disk, part motions) and the motion/effect
  pools (the 0xCDCD padding excepted). ERROR = crash or invisible body (a blank row reached by an AI
  action, a missing CHR entry or model, a missing mesh), WARN = degrades with a log line (textures,
  clips, effects, AI files, hand items past their table, blank rows in regen lumps -- the server
  refuses those; eleven retail ones are known). Exit 1 on an ERROR. Every case it checks was found by
  hand first (Ghost Seed, Basilisk Captain, Ikaness Worker, Gangster Pangs, the retry storm). First
  run: 817 reachable ids, four gaps, all closed by `scripts/fix-spawned-npc-gaps.py` the same day
  (see below). **A placed NPC spawns only if its row's NPC_TYPE (col 27) is 999** --
  `zonefile.cpp` skips every other placement silently, so a stub row (name, no columns) is an
  NPC that never appears: ERROR. A placement whose `.CON` has no LIST_EVENT row is a mute NPC:
  WARN (34 of them on 2026-10-13, the Desert of the Dead's EM29-*.con among them; left for custom dialogs).
  The two other stubs it found -- [Vicious Captain] Ruven 1125 (Eucar) and [Cosmic Vassal] Little
  Angel of Junon 1208 (Sunshine Coast, a placeholder model standing in the sea) -- were **erased**
  on 2026-10-13 (row, STL key, CHR entry and placement; `fix-spawned-npc-gaps.py` item 7).
  **A row exists only if LIST_NPC col 0 has a name** -- the server's `NPC_NAME`, null for a blank
  cell, and every spawn path (regen, AI and skill summons, `Change_CHAR`, placements) refuses on it,
  silently. The audit first called a row blank only with no name *and* no model, so a complete row
  with an STL name but a blank col 0 passed while the server refused it (2026-10-13: the Junon Ent
  under 73 Sunshine Coast points, the Woopie Hooligans; now an ERROR wherever reached). The GM item
  browser's "the server refuses /mon: no table name" is the same rule, not a /mon-only quirk. Built
  on `rosedata` + `rosefmt`; nothing is parsed in it. `--zone N`, `--json`.
- `scripts/fix-nameless-npc-rows.py` (2026-10-13) — names the four rows live content reached
  that the server refused for a blank col 0. **322 is the Junon Ent** (`NPC/etc/ent`, a tree
  creature), not Mr. Cannie (that is 299, `mrcan`): it borrowed 299's STL key, retail's mistake,
  and gets "Junon Ent" with its own key LNPC0322; it brings back all 73 Sunshine Coast summer-event
  points (each also lists 323/324, blank here and in every iROSE dump -- 667/RoseZA's "Mr. Large
  Cannie/Baggie" -- left out by choice). **306-308 Woopie Hooligan** (lv 35/75/105) are retail's
  dormant seasonal event, not the low-level Woopies (101-103, 205): seven AI files spawn them on
  death when **[Event Info] Judy (1201)'s NPC var 0 == 1** (var 0 == 3 is the Cherry Smouly event,
  309-313), each tier from monsters of its level -- hence a Vulcan dropping one. Nothing sets the
  variable; a GM starts it with `/npc 1201 var 0 1`. Cell-level undo in `build/nameless-npc-rows/`.
- `scripts/erase-dead-npc-rows.py` (2026-10-13) — **erases the other 219 rows the GM item browser
  called broken** (it showed 223; now 0): rows the server can never spawn (blank col 0, no model,
  level 0, junk HP, no CHR entry) -- 206 nothing references, 13 only dead content does (Zombie 253 via
  QP103 trigger 122-04, whose REWD_008 it removes; 678/679 via CL_SEED.AIP, loaded by no FILE_AI row;
  741-750 via GHOST_M3.AIP, whose only user GhostSeed 704 never spawns -- `audit-ai-monster-refs.py`
  strips those actions afterwards). Fully erased per the house rule (cells, CHR hole, STL key) except
  an **STL key a surviving row shares stays** (12 of 55: 226/227 carry monster 41's, 298 Mr Baggie's,
  373/374 the Dreadnaughts'). Cleaning the references matters because an erased row looks free and
  importers pick free rows by hand. Refuses a listed row that can spawn now or any live reference
  (re-scans maps, AI, QSD, summon skills); cell-level undo in `build/erase-dead-npc-rows/`.
  `fix-dreadnaught-model.py` lost its donors 373/374 to it and now only checks 375/376's skeleton.
- `scripts/fix-spawned-npc-gaps.py` (2026-10-12, validated in game) — closes the audit's
  first findings. **Sikuku Elite Fighter 562** (Turak's SummonMaster) has no CHR entry in *any* dump,
  but our rows 540/1708 are the same monster with a model: 562 gets a copy of 540's entry.
  **[Astronomer] Boyren 1175** (placed in Mount Eruca) exists in no dump and had no model column at
  all; he takes 667's astronomer "[Stargazer] Nubo" (row 2129, `Bond.mon`) through
  `rosedata.import_characters`. **Ikaness Warden's weapon 1158** comes from 667's same-named row
  (`MOB_ICANES_WARDEN.zms`, two files copied); **Evil Fairy's 1156** from Jrose's object (the Fairy is
  Jrose's, hand column copied verbatim; `mob_can.zms`, files already here) -- 667's 1156 is a
  different item -- and in game the Fairy still fights unarmed: Jrose's `mob_can.zms` is a 0.2 cm
  placeholder, the row exists for the attack presentation. Second round after the in-game test:
  **Boyren needed a real row, not just a model** (type 999, Cornell's columns, STL key LNPC1175
  appended), and **Turak's morph (559) and death-summon (560) rows** were "???(??)" with a key the
  STL lacks -- now **"Alpha Turak"** with their own key LNPC0559 (a Turak turning into a Turak read
  as nonsense; the rows are the bigger form). Whole-file originals in `build/spawned-npc-gaps/orig/`,
  `--restore` refuses if a table changed since (hash in the manifest). Servers load the CHR and
  LIST_NPC: restart and bake. **A morph (AI Change_CHAR) now ships its HP** (2026-10-12, validated):
  `gsv_CHANGE_NPC` carries the new form's HP, the server switches state before building the packet,
  and the client applies it through `Reconcile_HP` -- before, `CObjMOB::Create` seeded the rebuilt
  character with the row's per-level HP value (259, not 259 x level) under a recomputed max, so the
  bar read near zero on a full-health monster until the next hit (trace: `visible 259 -> hp 47915`).
  The swap itself is abrupt (model deleted and recreated, no effect); cosmetic, left as is.
- `scripts/fix-mob-bullet-effects.py` — restores `WEAPON_BULLET_EFFECT` (`LIST_WEAPON.STB` game col 38). Empty there means a ranged monster fires **no projectile and lands no visible hit**: the client's `Get_BulletNO()` skips `Add_BULLET`, and the server's `UsesProjectileAttackPresentation()` picks `MeleeHitFrame`, which nothing on a bow/gun motion consumes.
- `scripts/import-arrow-bullets.py` — the player-side sibling: an arrow/bullet's projectile is `NATURAL_BULLET_NO` (`LIST_NATURAL.STB` game col 17, a `LIST_EFFECT` row). Angelic Arrow (320 → 597) and Vengeance Arrow (319 → 600) pointed at **blank** `LIST_EFFECT` rows, so they fired nothing (alpha #2). Only the tsuki dump has those rows; the script writes them in place (plus FILE_EFFECT 32/97 and the `arrow_effect_1`/`elect_hit_02` chain) through `import-monster-skills.py`'s `Plan`. Client-only data.
- `scripts/restore-zone-weather.py` (2026-10-22) — zone weather is `LIST_ZONE` col 27, mapped to an effect by `SE_WeatherEffect` in `SCRIPTS/GLOBALSCR.LUA`. **Weather numbers are per-client** (type 5 is Luna clover in 667/668, rain in QQ, mist in Jrose), so a zone row imported from a dump needs that dump's Lua branch and effect files too. Restores Eldeon's leaves (type 3, stubbed out long ago "for performance"; validated in game, cheap). Karkia's zones carry Jrose's type 5 = `rm_mist.eft`; that mist was tried and **rejected for looks**, so type 5 stays unmapped and `--verify` fails if it gets mapped. Patches the CP949 Lua as bytes. Client-only; re-bake.
- `scripts/add-zone-name.py` — appends entries to `LIST_ZONE_S.STL`. A blank name above the minimap is **not** a minimap bug: the name comes from the STL keyed by `ZONE_STRING_ID`, never from the STB name column. Documents the `ITST01` layout, incl. the 7-bit varint lengths and the fact that the client ignores the per-entry offset table and reads strings sequentially.
- `scripts/rename-shop-tabs.py` — renames shop tabs from a reviewed `RENAMES` table (2026-10-20: Pavrick's 521 "Arrows" -> "Distance Weapons"). The caption players see is the LIST_SELL_S.STL entry keyed by LIST_SELL col 1 (English = language block 1); col 0 is kept in step. Refuses a key another tab shares (it would rename both) and any caption that is not the expected old one. Add a row for the next rename.
- `scripts/check-shop-tabs.py` — read-only audit of every shop tab an NPC references, for the two ways one opens broken: **no caption** (the client resolves it through the `LIST_SELL.STB` STL key in game col 1, never from the col 0 name, so a well-named row can still draw blank) and **no stock** (all 48 slots zero). `--compare` diffs the stock count against the reference dumps, which separates "we lost this row" from "retail shipped it empty". Twelve of our live tabs are nameless and fifteen are itemless (2026-10-13); rows 323/327 (Reene, Cassirin) are stocked in every reference dump and 386/387 (Punwell) in 667 and RoseZA, and all four are empty here. **Never grep an STL for keys** — each key is followed by a u32 id whose low byte is often an ASCII digit (id 304 → `30 01 00 00`, and `0x30` is `'0'`), so `LSEL\d+` reads `LSEL304` as `LSEL3040` and reports a present key as missing. Parse the table.
- `scripts/fix-chr-skill-slots.py` — applies `import-karkia.py`'s `CHR_MOTION_OVERRIDE` to `LIST_NPC.CHR` without a `--stage 3`. A monster casts with the AI action's `nMotion`: slot `nMotion` casts, slot `nMotion+1` releases, and only the release clip's action frames (24/34 launch, 26 fire, 25 hit) make the skill land on screen. Orgeid (2723) had the no-event casting clip in both slots with the real release clip unused in slot 7 — inherited from Jrose's CHR, not an import artefact — so its bolt never fired and its damage showed on a later melee frame with no visual. The table lives in the importer so a re-import keeps it; backups in `build/chr-skill-slots/`.
- `scripts/fix-quarantine-officer-skills.py` — **the Revived Quarantine Officers (2703, Alpha 2692) cast Tornado again, and Berserk with a real animation** (2026-11-01; Tornado off since 2026-09-07). Both skills shared slots 6/7, whose release was a plain swing (event 21 only), so nothing presented: Tornado's area hit never showed and Berserk's buff came at the end of the swing; the re-pointed Tornado was also the player's 1090 at power 501 / 14 m (~1,000-1,250 a cast at lv 223, sim). Now: the **player's own clips retargeted onto the officer skeleton** -- Berserk's on 6/7, Tornado's on 8/9 -- by EVO-REFORGED's `AnimRetarget/retarget.py` run as a subprocess (their code, used with their authorisation; its `rosefmt.py` would shadow ours in-process -- needed only to apply, `--verify` uses our codec); a monster row of their own, **LIST_SKILL 7202** (7 m, power 200, ~290-510 a cast); 4% of hits taken on both, gated on the target within 6 m. **The retargeter maps by bone name**: 221 of our 299 NPC skeletons are 3ds Max bipeds with the avatar's 21 `b1_*` bones, so a player clip moves onto them cleanly (extra bones, here a head plume, take the monster's own idle clip via `--fill`). `import-karkia.py` still disables 1090 at its AI stage: re-run this script after it. `--dry-run`/`--verify`/`--restore`; manifest in `build/quarantine-officer-skills/`.
- `scripts/fix-mob-weapon-presentation.py` — fills a **blank mob weapon row's** presentation columns (LIST_WEAPON 39 hit effect, 40 swing sound, 42 hit sound) from a donor row. A monster with a weapon presents its hits through the weapon row and the NPC-level fallbacks (cols 31/33) are skipped, so a blank row is worse than no weapon: digits, no impact, no sound. Moon Sister Inguz's staff 1130 (shared with Melendino 1473) was blank in every dump we own; it takes the Sikuku Jailer's 1143 values (2026-09-15). Only all-blank rows are touched; backups in `build/mob-weapon-presentation/`.
- `scripts/fix-npc-hand-models.py` — clears LIST_NPC hand cells that index past their model table: **the right hand (col 5) resolves in `LIST_WEAPON.ZSC`, the left hand (col 6) in `LIST_SUBWPN.ZSC`**, so a weapon id typed into the left hand logs `Invalid model data index[ N / 343 ] in LIST_subwpn.ZSC` on every launch. Gangster Pangs 1456/1457 carried weapon 1080 there (a retail slip; RoseZA/667 cleared it). The left hand is visual only, as the server reads col 5 alone. Scans both hands of every row, refuses newcomers without `--allow-new`, per-cell undo in `build/npc-hand-models/`. Run `--verify` after an import that brings NPC rows.
- `scripts/fix-zmo-attack-frames.py` — rewrites the **action-frame events of a monster attack clip** whose family is wrong. A normal attack lands at frame 21 (melee) or fires at 22/23 (bow/gun); a bare-handed monster on a 22/23 clip has nothing to fire, so before 2026-09-22 the client never called `Hitted()` and the player took the damage with no digit, no impact and no HP movement (alpha #2: Hebarn Officer Pazugenti 2685). The Lich01 attack clip is 23/33 in the RoseZA/667/Evo lineage our copy came from and 21/31 in every other dump; the script flips the two trailer shorts (backups in `build/zmo-attack-frames/`, `--dry-run`/`--verify`/`--restore`). The client now also presents a fireless 22/23 frame as a melee hit (`CObjCHAR::PresentFirelessRangedFrame`), which carries the 40 rows `--audit` lists (2026-10-13) — Candle Ghost, Bebeg, Sikuku Tiger Captain and the non-combat Oro vassals sit on bullet-less ranged clips in every dump we own.
- `scripts/water-restores.py` — **Vital/Spiritual Water tiers, flat + % heal and cooldowns** (2026-10-15). S/M/L/XL open at levels 1/30/70/120 and heal 60+8% ... 400+15% HP (MP 40+8% ... 250+15%); 8 s between two HP waters, 3 s between two MP waters, enforced by the server (`classUSER::Use_InventoryITEM`, 300 ms slack) and mirrored by the client's item delay and icon sweep. The XXL pair heals flat + 100% with no level or cooldown and left shop tab 563 (Ginias, Nemo) -- GM-only. Also clears the stray col-26 `1` on 15 event foods, which would otherwise start the HP timer. Cell-level undo in `build/water-restores/`; restart the server and re-bake.
- `scripts/food-ladder.py` — **food (heal over time) in level-gated tiers to 240** (2026-10-20, the 668 model the user chose over Jrose's per-rate status rows). One HP and one MP tier every 15 levels; each heals LIST_USEITEM col 20 over **col 21 seconds** (20 s food; a heal-over-time row's col 21 was unused -- the waters read it as their %), so a tier only changes how much it heals. The code reads it through `UseItem_PotionDurationMS` (`citem.h`: server `Use_pITEM`, client status icon/prediction/tooltip; blank col 21 = the retail status-row rate). **One heal over time runs per HP / MP, and a new one replaces it only if it heals at least what that one has left** (`UseItem_PotionCanReplace`; the server refuses before consuming, the client says so in chat) -- retail ranked them by status-row index instead. The Health/Mana Vials and Bottles are the fast tier (10 s, ~4x the price, steps at 1/35/.../210). New rows 139, 140, 194-199, 229, 230: 668's top foods use icons already in our atlas at 668's own indices (`upgrade-item-icons.py` brought them), levels 225/240 three Jrose icons appended by TSI label. The client also refuses a use item under its level now (`CTCmdItemUseInInventory`; the server always did, silently). Rebuilds 11 shop tabs (other items keep their order). Cell-level undo in `build/food-ladder/`; needs the new client and `sho_gameserver`; restart, re-bake.
- `scripts/audit-use-items.py` / `scripts/erase-dead-use-items.py` — **use items that cannot do anything** (2026-11-02, tester report: the Halloween Tree is kept on use and does nothing). The audit follows `classUSER::Use_InventoryITEM`: a class-313 item casts LIST_USEITEM col 20 and the server returns *before consuming* unless that skill is self/target/warp/emote (a blank row is none), a 314 book teaches col 20, a 322 box needs a package `FindUseItemPackage` declares, other classes need col 24 or col 19 or are eaten for nothing; it lists where each finding is obtainable (shops, drops, packages, QSD rewards). 73 found, all GM-browser-only; the user retired them, the erase blanked the rows + their LUSE keys (refuses on any shop/drop/craft/QSD/AI-17/package reference, reads holdings from the DB named by `Exes/server.toml`). Undo `--restore`, round-trip tested. **Its first package parser lost every `Package(...)` after a comment line** and five working costume boxes were erased until the user caught it -- strip comments before regex-parsing C++ tables, and re-check a destructive list from the code before running it.
- `scripts/add-diamond-jewel-icons.py` — **the 18 Diamond rings/earrings/necklaces (LIST_JEWEL 282-370) had icon 0** (2026-11-02): sprite 0 is icon01's near-white top-left cell, so they drew as white squares and the GM browser hid them (it skips icon 0). The tier is ours (no dump has it; Platinum + quality 60 + 1 stat), so each icon is its Platinum icon with the metal re-coloured ice blue, appended to `icon55.dds` (9132-9149, labels `ljewNNN_diamond`). A blank description never blanks an icon -- only col 9 does; a white item icon is col 9 = 0 first. `--preview`, `--verify`, `--restore`.
- `scripts/armour-ladder-pass.py` — **the armour ladder after the user's look survey** (2026-10-20). Levels stay 200/210/220/230/240 (Unit Core was the 50% point between Crystal and Egyptian, so no monster rebalance). **Vanity sets are cosmetics**: Steam / Refined Steam / Unit Core (rows 250-261 of every armour table, same ids) are **level 10, class-locked, sold nowhere** -- the costume slots run `Check_EquipCondition`, so a set is a costume for its whole class line -- at 90% of the vanilla lv10 class set, 220+ bonuses dropped. A first cut at 70/90/110 sold in Junon Polis was rejected by the user ("the level limit was a bad idea", a shop "just confusing"); a re-run puts any cell the plan no longer covers back to its recorded original. The combat sim skips them (`combatsim/autobuild.COSMETIC_ARMOUR`). **Crystal (220) wears Rose Brasil's "Legendary Crystal"** (its rows 1040-1043; art `career_a_009-012` was already in our data, unreferenced, as DXT5 re-encodes of the same pictures): our Crystal had **empty female caps**; objects rebuilt in place, stats untouched, Brasil's icons cropped in. **The eight lv210 caps had empty female objects too**: QQ-iROSE's geometry (LIST_WCAP 256-263, `_F` meshes copied into our folders) with our male textures. Astraea (Karkia) sells Legendary Crystal (566) and Legionnaire (568); 567 is empty and unreferenced (`add-karkia-shops.py` planned to match). Legionnaire keeps its trade profile for now. `add-karkia-shops.armour_grid` now takes per-table rows, and `rosefmt.zsc.Zsc.rebuild_with` (append paths + rebuild objects in one write) came with it. Cell-level undo, whole-file ZSC originals hash-checked, in `build/armour-ladder-pass/`; `gm-item-browser --preview` renders an outfit to check models headlessly. Restart the servers, re-bake.
- `scripts/armour-endgame-sets.py` — **two Jrose sets join the endgame and the 220-240 armour ladder steps every 5 levels** (2026-10-20): 220 Legendary Crystal, **225 Egyptian** (down from 240, "very Oro-coded"; its Oro drops still fit), **230 the OG cinematic outfits** (Fighter / Magical / Thief / Gunner, Jrose's no-level Replica rows; rows 266-269), **235 Legionnaire** (up, scaled by the ladder's 235/230 ratio so its DEF-for-RES trade survives), **240 Jrose's Norse line** (Fenrir / Eir / Sleipnir / Forseti -- one second-job set per class line, the user's picks; rows 270-273). Every tier is a point on the Crystal -> old-240 line, so the curve's ends do not move; the numbers are frozen in the script. Art-only through `import-item.py`, every column cloned from the matching Egyptian piece except level/DEF/RES/price/model/icon/name, and **a cap's hair setting (LIST_CAP col 33) taken from the source** -- it belongs to the model. **Two Jrose texture names hold full-width letters** (cp932 `ｔ`/`ｆ`): `import-item.py` cannot copy them and the client cannot open them, so they are copied under their ASCII meaning and the material records repointed (`ASCII_NAMES`); `--verify` flags any non-ASCII or missing file on the imported objects. All 84 new textures came without mips (`add-dds-mipmaps.py --subdir` per avatar folder). Drop-only: OG off Karkia's lv228-240 field tables, Norse off its bosses (`add-karkia-drops.py`). Undo: whole-file originals with hashes in `build/armour-endgame-sets/`.
- `scripts/import-tsuki-weapons.py` — **five tsuki weapons fill the katar / dual / 2H gaps** (2026-10-20, the user's picks): Justice Blades (dual 220, row 125), Frostbitten Knuckles (katar 220, 424; tsuki row 377, not the 337 noted), Elemental Dual Swords (dual 225, 126), Stormcrystal Claws (katar 225, 425; tsuki's "Katar 1"), Asmodeus Ritual Blade (2H 235, 124). **Rows below 1000 in place** (`--target-row`): an id above 999 can drop (wide encoding) but no quest can grant it and no recipe use it. tsuki has 57-column tables and **its own type numbering** (its dual swords are 251, our katar), so it is art-only: model, icon, ground model and **the gem anchor (col 43), which indexes the model's own dummies** -- Frostbitten Knuckles' is 1 of 4, the template's 4 would show no gem glow. Every other column from the type's next tier down; tier columns interpolated to the type's next tier up. In `weapon-identity-pass.py`'s SKIP. No shop or drop yet. Undo: whole-file originals with hashes in `build/import-tsuki-weapons/`.
- `scripts/asmodeus-stance.py` — **one weapon with its own idle / walk / run** (2026-10-20): the Asmodeus Ritual Blade (124) wears tsuki's two-handed stance (`THS_STAND` + female `THS_STAND_F1`, `THS_WALK`, `THS_RUN`; tsuki gives it to every 2H sword, the user wanted it for this blade alone). **A weapon's animation set is one TYPE_MOTION column** -- LIST_WEAPON col 34 is the column index, rows are actions, cells FILE_MOTION rows, read by client and server `Get_MOTION` -- so it gets **column 21 "Asmodeus Blade"**, a copy of column 5 (2H sword) with only rows 0-3 (stop 1/2, walk, run) replaced: attacks, skills and swing timing are the 2H sword's on client, server and sim. A new column, not an unused one ("Shield", "Empty Skill" may mean something). 21 bone channels against our 22 plays fine (as 668's Voltage Crush); a blank female clip falls back to the male. The recipe for any future signature stance.
- `scripts/weapon-ladder-pass.py` — **the weapon ladder after the same look survey** (2026-10-20). The lv210 line (Arcidian / Befoul ..., with Jrose's Photon Cannon 1424 as its launcher) and the plain lv230 line (Viper / Akela / Matrix / Oasis, 1355-1367) **trade tiers** type by type: price, quality, level, stat requirement, both bonus slots, durability and ATK move, model/speed/effects stay. **Lv230 non-red weapons have no doubles** (user): the five other Jrose lv210 rows (1420-1423, 1425) load byte-identical meshes and textures to five Arcidian-line weapons from another folder, and **no weapon may wear a higher tier's look**, so they are **erased** (`erase-retired-weapons.py`, below) after leaving every shop and the Golden Ring pool (`add-oro-drops.py` `WPN_ORO_210`, re-planned in place with its new `--rewrite`; Photon Cannon left it too, being lv230). `stock-muris-shops.py` leaves red boss loot out of its level counts. The red dungeon set (1456-1468) and Dual Tornado Rifles (381) stay. **The Rose set (1382-1418, step 3) is a level-100 sidegrade at full stats** -- weapons cannot be costumes (the costume weapon slots were never wired) -- interpolated on the vanilla per-type ladder, each type's own bonus; sold in Luna (Pavrick 520/521, Sergei 525, Pabel 529). Huzam's tabs follow, Gelt and Belfa lose tab 562 (`add-karkia-shops.py`). The re-tiered rows are in `weapon-identity-pass.py`'s SKIP. Undo per cell in `build/weapon-ladder-pass/`; restart the servers, re-bake.
- `scripts/erase-retired-weapons.py` — **fully erases** LIST_WEAPON 1420-1423 and 1425 (Strike Hammer, Assault Sword, Final Axe, Hyper Railgun, Force Wand; 2026-10-20): they wore the lv230 Arcidian line's models from a second folder, and the user's "retire" means **strip the row** -- a stub is confusing. Rows blanked (numbers kept), LWEA keys removed, ZSC objects emptied and the 14 mesh/material records only they used removed with `rosefmt.zsc.Zsc.without` (every other object re-indexed; the script and a by-value comparison of all 1,465 other objects proved nothing else moved), the five folders moved to `build/erase-retired-weapons/`. Refuses while any STB cell or other ZSC still names them; **check the character DB for owned copies first** (`item.type_id = 8`, `game_data_id`) -- none existed. `--dry-run`/`--verify`/`--restore`.
- `scripts/erase-dual-guns.py` — **the dual guns are gone** (2026-10-26): LIST_WEAPON 361-381 (type 253) never had a stance or attack clip (TYPE_MOTION column 20 -> blank FILE_MOTION 278-281). QQiROSE's `dualpistols_*` clips from EVO-REFORGED's client made them work in game, and the user then judged them confusing and not worth it, so they are **fully erased**: rows, LWEA keys, ZSC objects (a dual gun's right-hand mesh is the gun line's file under its own record: the record goes, the file stays), the `dgu\` folder. Huzam's lv210 tab drops Dual Tornado Rifles (13 items, `stock-muris-shops.py` expects 13); Cerberus's two (377 common, 376 group 3) became Ballista Rifle 251 / White Night Gun 249 (`import-cerberus.py`'s drop_table matches). Code paths for type 253 stay (dead, harmless). `--dry-run`/`--verify`/`--restore`, undo in `build/erase-dual-guns/`.
- `scripts/replace-235-weapons.py` — **the lv235 weapon tier is 668's dinosaur set** (2026-10-25): the Jotunn line (1437-1446, Jrose) read as recolours of the lv240 mythicals and is **erased** (rows, LWEA keys, ZSC objects + their 14 mesh/material records, files moved out **file by file** -- its `t_unique_*` folders also hold the lv240 line's art). 668's level-250 set (its rows 1476-1488, `3DDATA/WEAPON/ZATANA/`) is **byte-identical, mesh and texture, to retail's level-200 unique series**, so it lands at those retail ids (908, 915 ... 999: blank here, under 1000) with that series' icons, already in our ITEM1.TSI at the same indices (icon25.dds 4095-4108). Art-only: the ten types the Jotunn line had take its row exactly; 2H sword / spear / axe template on their lv240 weapon x the line's 235/240 medians (requirement x0.929, ATK x0.952). Extinction Ruin is the Devastation mace that 668 types as a 1H sword: it is our 1H mace (212). Asmodeus (124) stays, so 235 has two 2H swords. 668's ground models name the lv200 paths (same bytes again), so they are repointed at ZATANA after the import. Drops: `WPN_235` in `add-karkia-drops.py` (five-wide windows of 13), re-planned with `--rewrite` there and in `add-oro-drops.py`. Order, undo and checks in the docstring.
- `scripts/replace-210-weapons.py` — **the lv210 weapon tier is tsuki's 220 set** (2026-10-25, the user's pick): Blaze Dancer, Glacier Cutter, Warmaster Sword, Deadly Avarice, Lava Axe of Athoum, Carnival Bow, Mana Shooter, Plasmatic Fusion Launcher, Oblivion Staff, Void Conjurer, Ritual Wrist Blade, Resilience Dual Wield, Nepheramun Crossbow, each at **its own tsuki row number** (27 ... 452: blank here, under 1000), every tier column inherited from the Viper-line weapon of its type, icons copied from tsuki's atlas, tsuki's descriptions. The Viper / Akela / Matrix / Oasis line (1355-1367) is **erased**; `--erase` first restocks Huzam's "Weapons lv210" tab the way `stock-muris-shops.py` plans it. `weapon-ladder-pass.py` drops those rows and that tab from its frozen plan (`SUPERSEDED`), for --verify, re-runs and --restore alike. Both tier scripts run on **`scripts/weapon_tier.py`**: import (art-only, `--target-row`), restat, erase (also refuses while a monster's hand, LIST_NPC col 5, holds an old row), verify, restore. **A new row's gem anchor (col 43) is always the source's** -- it indexes the model's own dummies; the 235 import first took the template's and put two gem glows on the wrong point. **Never chain `--restore` into a command line**: on 2026-10-25 a stray `weapon-ladder-pass.py --restore` reverted that pass and deleted its manifest; the cells came back from the last bake (`rosefmt.idx` over `ROSEProject\data.idx`) and the manifest was rebuilt from the pass's own plan functions (`build/ladder-restore-recovery/`).
- `scripts/limit-endgame-backs.py` — gives the four level-180 backs (LIST_BACK 953-956, class-free in every dump and far above their peers) a class by theme (2026-10-15): Snapper Shell (DEF 307 + 80, max HP +1600) -> Knight Job (61), Doctor's Back Arms -> Dealer Job (44), Hecado's / Ardemia's Grace -> Muse Job (42). Stats untouched. Rows checked by name; cell-level undo in `build/endgame-backs/`; restart the server and re-bake.
- `scripts/skill-rank-floors.py` — **first-job "spark": weak first ranks lifted** (2026-10-15). SKILL_POWER (LIST_SKILL col 9) of Leap Attack, Lightning, Ice Bolt, Screw Attack, Holding Arrow (floor 0.35) and Gun Smash (0.25): rank k is at least floor x rank-10 power at rank 1, rising linearly to rank 10, which is unchanged; nothing is lowered. Front-loading harder (0.55) made first-job openers one-shot plain monsters -- their big skills already take ~100% of a same-level monster's HP per cast by level 30-50 -- so the user asked for "slightly, just feeling better". Cell-level undo in `build/skill-rank-floors/`; restart the server and re-bake.
- `scripts/hawker-spirit-attack.py` — **Hawker Spirit gives Attack Power as well as Accuracy** (2026-10-16). Its free second status slot (LIST_SKILL 1451-1460, cols 12/24-26) gets Attack Power Up (status 18, Berserk's) at 6% -> 15% over ranks 1-10. The Raider was the sweep's bottom class from level 120 (0.57-0.77x the median; the Champion 2-2.6x through Berserk's permanent +52%, which the user keeps): the dual-sword Raider now sits at 0.85-1.0x. Critical Up was tried first and did almost nothing -- crit chance is near saturation at those levels. The Scout does not keep the buff up late, so this is in effect a Raider change. Katar Raiders stayed at 0.6-0.75x; the katar skill crit below fixed that. The texts are in `fix-skill-descriptions.py` (LSkill1451) and `fix-skill-book-names.py` (book 754). Cell-level undo in `build/hawker-spirit-attack/`; restart the server and re-bake.
- `scripts/dealer-tree-layout.py` — **owns the classic Dealer tree placement** of Calibrated Burst, Photon Cannon, Exceed Break and Jewel Item Craft (2026-10-17, the user's layout): the gun skills under **Gunshot Mastery** (Calibrated Burst needs Gunshot Mastery r10, Photon Cannon follows it), Exceed Break after **Gun Smash** r5, and Jewel Item Craft -- which had **no prerequisite at all**, so UI2 drew it in the lone lane -- under **Craft Mastery** r6. Grows the Gunshot page canvas 512x128 -> 512x256, erases the boxes the moved skills left, and migrates prerequisite cells on **every rank that carries one** (a rank's level-up checks its own row). The three import scripts no longer touch the tree. Run it after them; undo in `build/dealer-tree-layout/`.
- `scripts/import-mage-knight-skills.py` — **two Mage spells and two melee-Knight skills** (2026-10-18), looks picked in game from GM test rows: **Diabolos' Curse** (Jrose 4251; the Mage's own new clips `skill_magic_abnormality_*`; a bolt that **dispels** -- LIST_STATUS 37 ING_CLEAR_GOOD, only monster skills did -- and slows; lv 110, Silence r3; rows 7137-7146), **Shadow Servant** (Jrose 6061; type-7 dark-cross nuke, Magic Resistance + Accuracy down; lv 140, Luna Stone r5; 7147-7156), **Shield Bash** (667 575; our never-played `s_shielda_m1`; **requires a shield** and hits with it -- the server's shield-skill rule, +2% damage per point of the shield's own DEF incl. refine, cap +250%; Attack Power + Attack Speed down; lv 90, Shield Barrier r6; 7157-7166), **Gaia Presser** (Jrose 6031; the stock spin with Jrose's swirl, 1H sword or mace -- the clip is in every column, Jrose's mace-only was data; Defense down + slow; lv 110, Impact Wave r5; 7167-7176). **Casting-effect slots fire on frame events** (`ActionSkillCastingFrame`: col 56 on 44, 59 on 64, 62 on 74, 65 on 84): an imported row's effects can sit in a slot our clips never fire -- Gaia Presser's showed nothing until they moved to 59/62, Spin Attack's layout. Diabolos' pentagram (flat, at the feet) patched to `z_test` 0. Books 863-866 (Muse tab 464 cols 23/7, Soldier tab 462 cols 31/32), icons 529-532. Undo in `build/mage-knight-skills/`. Needs the new `sho_gameserver`; restart, re-bake, copy `skilltree_muse.xml` and `skilltree_soldier.xml` loose.
- `scripts/import-scout-knight-skills.py` — **two Scout and two crossbow-Knight signatures** (2026-10-18), looks picked in game from GM test rows: **Arrow Shower** (667 1870; the Scout's first area skill, bow or crossbow, type 7, Poison III; lv 90, Poison Arrow r5; rows 7097-7106), **Deadly Snipe** (Jrose 6111; bow finisher, longest Scout shot, Dodge Down; lv 120, Hawk Shot r6; 7107-7116), **Volcanic Shot** (Jrose 5202; crossbow, Burning + Attack Power Down; lv 100, Range Bow Shot r5; 7117-7126), **Star Gazer** (Jrose 6121; crossbow area slow + Defense Down; lv 130, Slow Shot r8; 7127-7136). **A skill's two statuses share one duration (col 14) and one ratio, each rolling on its own** -- why Deadly Snipe dropped its planned short stun. **Triple-shot clips multiply damage by 3** (attack frames 56/66/26), so Deadly Snipe / Volcanic power is per arrow (100 -> 230, 40 -> 85). Arrow Shower's clip is at FILE_MOTION 669 / TYPE_MOTION 98 (667's 681/221 collide with Jrose's rows); Star Gazer's ground rune is patched to `z_test` 0 (`rosefmt.eft`). Books 859-862 (Scout tab 466 cols 30/31, Knight tab 462 cols 45/46); icons 525-528 (667 / Jrose); classic boxes on `HOWKER_BOW_MASTERY.DDS` and `SOLDIER_CROSSBOW_MASTERY.DDS`. Undo in `build/scout-knight-skills/`. Restart the server, re-bake, copy `skilltree_howker.xml` and `skilltree_soldier.xml` loose.
- `scripts/import-raider-skills.py` — **the Raider's two weapon signatures from Jrose** (2026-10-18), picked in game from GM test rows: **Eruption** (Jrose 4321, katar, lv 100, Power Burst r5; rows 7077-7086) is an around-you area strike on the new `hawker_u_skill_katar03` flurry, and **Shun'ei-zan** (Jrose 4311, dual, lv 120, Prime Hit r5; rows 7087-7096) a single-target strike on its own new `dsw01-03` set with a **critical burst**: LIST_SKILL cols 49/50, unused by every row and all code before, are now `SKILL_CRITICAL_ROLL` / `SKILL_CRITICAL_RATE` (`io_skill.h`; server `CCal::Get_SkillDAMAGE`, sim `formulas.skill_damage`) -- the skill crits when `Get_CriSuccessRATE` < 30 (a swing's is 20) for 200% damage, in place of the katar rule. Size a crit-burst skill **after** doubling: the first cut matched Jrose-ish power and added +75-135% DPS. `dsw03` had ~1 s of stillness at its end that read as the character stuck: our copy is cut to 61 frames with `rosefmt.zmo.truncated` (frames, events, header count, 3ZMO interpolation field). Books 857/858 in Darren's tab 466 cols 8/9; icons 523/524 = Jrose's (U badge kept); classic tree boxes on `HOWKER_BLOW_MASTERY.DDS` (Shun'ei-zan *under* Prime Hit -- the page is 512 wide). Undo in `build/raider-skills/`. Restart the server (new `sho_gameserver` for the burst), re-bake, copy `skilltree_howker.xml` loose.
- `scripts/import-exceed-break.py` — **finishes the Dealer line**: **Exceed Break** for the Bourgeois (Jrose 6151; launcher only, lv 100, Gun Smash r5, costs Zuly + MP; rows 7067-7076, book 856 tab 468 col 25, icon 522 = Jrose's) and **Calibrated Burst's "Nightmare" look** (cols 56/71). **Version 2 (2026-10-18) is Jrose's frontal barrage**: type 7 over a **frontal area** (packed scope 25x15 -> 40x25 m, see Frontal areas below), Burning on every monster hit, power 600 -> 1400 (v1 was a 800 -> 1800 single-target fireball shot), and a **rolling wave** effect authored with `rosefmt.eft.build` (`exceed_break_wave.eft`, FILE_EFFECT 2900). **An area skill's digits pop when its action motion starts**: the server applies skill damage at action start (`Do_SKILL`) and the client shows a hit at once once the cast is over, whatever the clip's events say -- so Jrose's `skill_cannon_m03`, which fires at frame 20 of 68, is **split at its firing frame** (`rosefmt.zmo.sliced`): wind-up = the cast loop (cols 54/55), shot = the action (FILE_MOTION 609/610, TYPE_MOTION 99/100); Photon Cannon's shared clip is untouched. Run on a v1 install it **upgrades in place** (its rows are no longer last in LIST_SKILL), recording old cells. Undo in `build/exceed-break/`.
- `scripts/import-earthsplitter.py` (was `add-earthsplitter-test.py`) — **Earthsplitter, the Champion's two-handed-sword area skill** (2026-10-24; LIST_SKILL 7178-7187, lv 90, Voltage Crush r5, book 696 in tab 462 col 33, icon 533 = the user's art, classic tree box under Voltage Crush; power 480 -> 880, 14 -> 11 s, sized against Dance of Flames): the first skill made with **SlopStudio's `rose-anim-toolkit`** (the SlopRose repo, `C:\Users\Thomas\Desktop\Rose\sloprose`; a Claude skill that builds player clips from stock ones -- retimed segments, world-space exaggeration, the left hand IK'd onto the grip -- and mesh effects that rise out of the ground). The toolkit runs on our data with two patches (its VFS reader cannot parse our `data.idx`, so it reads loose `data/`; our Junior cap names dummy 65535). Two-handed sword, **an aimed cone in front** (v2, the user's idea: type 7, cone scope `100000000 + 800*10000 + 60`, so `CSkillAim` previews it from the player toward the mouse; v1 was a type-17 circle); the clip is split **before the hop**, not on the impact: on the impact the blade sat in the ground through the cast loop's wait for the server, and the rocks came a beat late. Hit effect = the toolkit's rock cluster, hit sound `point_heavy01` (1551); a fissure of 11 clusters (44 mesh entries) runs down the cone on event 74, a row per 1.5 m, 100 ms apart. **A skill's own noise is its hit sound on what it hits** (Impact Wave is silent with nothing in range): the client plays no casting-slot sound column (`SKILL_CASTING_SOUND`, cols 58/61/64/67, is read by nothing), and a `.eft` sound plays only if the file carries one. **The local avatar's type-7 payload arrives in `Recv_gsv_DAMAGE_OF_SKILL`'s immediate branch** (the caster no longer holds the skill in `m_nActiveSkillIDX`/`m_nToDoSkillIDX`), which showed only the digit -- no hit effect, no hit sound -- until `CObjCHAR::ShowSkillHitFeedback` (2026-10-24); Exceed Break had the same silence. **An effect with more than ~20 mesh entries needs the 2026-10-24 client**: `CEffectDATA::Load` read every string into a `CStr` ring slot that `Add_MATERIAL`'s `Printf` wrapped onto, giving some meshes the material `_00101.000`. Began as a GM test row, validated in game, then made real at the user's request. Sources in `build/earthsplitter/src/`, icon-style reference in `build/earthsplitter/icon-reference/`; `--verify`, `--restore`.
- `scripts/import-skyrend.py` (was `add-skyrend-test.py`) — **Skyrend, the Raider's level-150 dual-sword skill** (2026-10-25; LIST_SKILL 7188-7197, Raider, Battle Mastery r15 -- the first Raider skill gated on a passive, and our first level-150 active; book 867 in tab 466 col 25, icon 534 = the user's art, classic tree branch 4 off the HOWKER_COMBAT_MASTERY spine): the second, more elaborate SlopStudio toolkit skill, built on its aerial chain builder (`aerial.py`'s `Aerial`): a 0.5 s cast, then six ground blows, a launch to +250 cm (height baked into the clip), two air blows and a diving slam (3.8 s). Nine blows carry 10/20 (the client's hit-dummy effect + sound, cols 77-82: alternating crimson / violet `_WIND_02` slash streaks turned by a rotation key) and the landing 25; casting effects 44/64/74/84 (the apex one on dummy 3 so it rides up with the character). **Sizing a many-hit melee skill: the server multiplies by the clip's attack frames and the Attack Power term rides every one**, so ten blows at power 10 already deal ~6060 per cast at level 150 and each point of power adds ~26 -- power tunes a narrow band; cooldown (20 -> 16 s) and MP carry the balance, and rank 10 (power 20) stays under a plain level-150 monster's HP (power 30 let crits one-shot Hellhounds). Builders `skyrend.py` / `skyrend_fx.py` beside the sources in `build/skyrend/src/`; `--verify`, `--restore`.
- `scripts/import-artisan-photon-cannon.py` — **Photon Cannon, the Artisan's bazooka** (2026-10-17), from Jrose's "Photon Wave" (4271): new animation set `skill_cannon_m01-03` and a real bazooka mesh effect (`at_skill_cannon.eft`, 25 files from Jrose). **Not Jrose's type 7**: a type-7 skill aims at the target's spot *at the key press* (`CSkillCommand::Execute`) and detonates there after the cast, missing anything that moved, and fires no projectile (`CFireToPositionSkill::Action` needs a target). It is type 3 with mode 1, and **a gun skill's bullet launches on frame event 26** -- Jrose's shot clip carried 24/34 (made for its type 7), so our copy has frame 20 rewritten to 26 (`rosefmt.zmo.with_events`; 26 is a server attack frame too). Accuracy Down spreads around the target through the type-3 SKILL_SCOPE rule. Rows 7057-7066, lv 100, Calibrated Burst r5, book LIST_USEITEM 855 in tab 468 col 49, classic tree via `dealer-tree-layout.py`; icon 521 = Jrose's own (with its "U" badge, user's call) via `add-skill-icon.py`. Undo in `build/artisan-photon-cannon/`.
- `scripts/add-calibrated-burst-book.py` — **Calibrated Burst (7002) had no skill book** (found 2026-10-17): rank 1 of a class skill is learned only from its book, so the Artisan's only exclusive attack skill was GM-only since `import-artisan-skill.py` (2026-09-01). LIST_USEITEM 854 (blank; 851-853 are pruned dead-book stubs) is a copy of Refine Item's book 849 teaching 7002, sold in Darren's second-job tab 468 col 48 beside the Artisan crafting books. **Any new class skill needs a book row + a shop slot**, or players cannot learn it. Cell-level undo in `build/calibrated-burst-book/`; restart the server, re-bake.
- `scripts/import-champion-skills.py` — **three Champion weapon-signature skills from 668** (2026-10-17): Voltage Crush (2H sword, lv 70, Attack Speed Down), Dance of Flames (spear, lv 85, area + a new Burning row, LIST_STATUS 62 at 150/s -- row 58's 30/s is noise past 85 and monster bolts use it), Final Strike (axe, lv 100, HP cost, Defense Down on every enemy within SKILL_SCOPE *of the target*: a type-3 skill with a scope damages its target only, `Skill_ChangeIngSTATUS` spreads the status). All three need Two-Handed Mastery (221) at r6 / r8 / r10, tracking their level gates; classic tree = a bracket under it. Rows 7027-7056, books LIST_USEITEM 693-695 in Darren's tab 462 (**rank 1 of a class skill comes only from a book**: `CLI_SKILL_LEARN_REQ` is compiled out -- Calibrated Burst 7002 has no book, so players cannot learn it), `AT_LEVEL` gate in cols 45/46. Voltage Crush keeps 668's own new animation (`ths_pstrike_*`, 21 channels vs our 22, plays fine); Final Strike uses the never-shipped `skill_Longinus_01` clip + `holy_sword_01`, picked in game from four GM test rows. Debuff ratios climb with rank because a Champion's ~15 INT makes the land chance decay with level. Classic tree boxes painted; originals, cell undo and copied files in `build/champion-skills/`. Restart the server, re-bake, copy `skilltree_soldier.xml` loose.
- `scripts/terror-knight-taunt.py` — **the Terror Knight taunts** (2026-10-16). The Bourgeois's summons drew almost no attacks (monsters spent 0-6% of a fight on them) and added 6-21% of its damage. LIST_SKILL row 7025 is a summon-only copy of the Knight's Taunt rank 3 (13 s, success 120 -- a summon's INT is its level, so the roll is the owner's level against RES/AVOID: ~always on regular monsters, ~93% on the Giant Fiery Snapper); `sur_guard.aip` (Terror Knight rows only) taunts when it joins the owner's fight (STOP), on a Ctrl+click order (ATTACK-MOVE, AI var 3 set by `CommandSummons_Attack`) and while the monster hits it (DAMAGED, after Dread Sweep, carrying Dread Sweep's cooldown tick). Sim, typical play: from level 120 monsters spend 75-92% of a fight on the summons and the Bourgeois takes 30-80% less damage, DPS unchanged; on the Snapper only 14% taunt uptime -- once a taunt drops the boss returns to the player and stops hitting the knight, so on bosses Ctrl+click is the re-taunt (user: "Ctrl+click matters"). **The client must have row 7025 (re-bake) before the server casts it.** Manifest in `build/terror-knight-taunt/`.
- `scripts/weapon-identity-pass.py` — **LIST_WEAPON speed, ATK and quality for play without server.toml's combat bonuses** (2026-10-15). One speed per weapon type (katar 0.60 s ... launcher 1.28 s swing), ATK = original x 0.95 x a type factor x a shared level curve (calibrated in the combat sim: the 2H sword at 0.9x today's normal DPS from level 70, 0.5x at 1-20, the other types at their agreed index), quality x1.10 for the accurate types and x0.90 for the axe, and crossbows ATK_SPD +29 in their free slot. Pairs with the code change `WEAPON_HIT_PER_QUALITY` 0.6 -> 1.4 (`citem.h`) and server.toml's ATK/HIT/ASPD bonuses at 0. Staffs, wands and dual guns untouched; weapons with no level requirement take the level of the same-type weapon nearest in ATK; the GM Sword is skipped. Values are computed from the manifest's originals, so a re-run is a no-op. Cell-level undo in `build/weapon-identity-pass/`; server reads LIST_WEAPON (restart) and the client needs a re-bake. The dry-run method (per-type DPS bisection in the sim) is described in the docstring.
- `scripts/prune-dead-skill-books.py` — clears NPC shop slots holding a **skill book whose skill row is blank** (pre-alpha #2: Darren sold Advance Crossbow Mastery, Crossbow Speed Mastery, Brave Howl, Concussor and the four stub Knight crossbow books, all "no class required"). A book has no class gate of its own — the tooltip and `Skill_LearnCondition` read the *target skill's* LIST_SKILL col 35, so a blank row reads as class 0 and the learn fails with `INVALID_SKILL`. Only "the skill row is effectively blank" (no name, no STL key, no type, no icon) is a trigger: `col 35 == 0` and "not in a skilltree XML" are **not**, because Leonard and Pony legitimately sell the class-less emote/basic books. Carries the reviewed `(tab, slot, item)` set and refuses newcomers without `--allow-new`; undo is cell-level (`build/dead-skill-books.json`). Darren's tabs 462-468 are shared with the four Akram Ministers and Arua's Fairy, so one cell fixes every seller.
- `scripts/fix-skill-book-names.py` — makes every skill book's name **the name of the skill it teaches** (alpha #2: *Rain of Arrows* taught Range Bow Shot, *Knuckle Mastery* taught Combat Mastery). The book name (`LIST_USEITEM_S.STL`) and the skill name (`LIST_SKILL_S.STL` via `LIST_SKILL` col 86, one key per line) are resolved independently and the tooltip never prints the taught skill, so the book name is the only thing that tells a player what they are buying. Of 203 books 61 disagreed: the same-lineage dumps (ruff, QQ, titan) prove col 20 is right everywhere and only the strings drifted, so nothing is re-pointed. The skill is the anchor, except where the skill string is broken (Lightening, Puri, Luna Strone, Combat Matery, ` Sub Weapon Craft`) or the book's English is better (Gather → Pick Up, Calling Hawk → Call Hawk, Magickal Knife → Magic Knife): those 44 skill lines (35 from the book survey, nine more found 2026-09-28) are renamed in the STL **English block only** — block 0 of `LIST_SKILL_S.STL` is real Korean for 228 of 234 keys — with `LIST_SKILL.STB` col 0 mirrored (the server's `SKILL_NAME`, only ever used as a blank check), and the book follows. 45 books whose skill has no STL key at all (dead rows, the GM block 871-896) are report-only. Carries the reviewed outcome (`EXPECTED`) and refuses newcomers without `--allow-new`; the sidecar stores the previous bytes so `--restore` is exact. Both STLs ship in the VFS.
- `scripts/fix-dreadnaught-model.py` — Luna's Freezing Plateau (LP04) Dreadnaughts were **Mini Mammoths**. Official dumps have Dreadnaught/King at LIST_NPC 373/374 (`trample`) and Mini Mammoth/Mammoth at 375/376 (`husehorn`); the iROSE Luna re-level cloned the Dreadnaught name and stats over 375/376 but kept the mammoth CHR entries and left cols 30-35 (sounds, death effect) blank, and LP04 spawns 375/376. The script copies the CHR entry and cols 30-35 from 373/374 onto 375/376 (tsuki's repair) rather than re-pointing the spawns (ruff/QQ/titan's), because the EXP and drop passes already tuned 375/376. The server loads `LIST_NPC.CHR` too, so restart it as well as baking. Undo is per CHR entry and per cell (`build/dreadnaught-model/`).
- `scripts/fix-basilisk-captain-model.py` — the **Basilisk Captain (LIST_NPC 372) had no `LIST_NPC.CHR` entry** (a hole, as in most dumps) while Luna's LP03/LP04 spawn it at 31 points: no skeleton, parts or motions to build it from. ruff/titanRose/tsuki fill the hole; we take tsuki's look -- 371's skeleton, motions and effects with `PART_NPC` 406/407 (`BASILISK2`), which we already shipped and no entry used. Meshes byte-identical to tsuki's, our `BASILISK_BONE.ZMD` identical to tsuki's/titan's, 18-bone hierarchy matching `BASILISK2_BONE`. It also clears the Captain's skill 3017 from the AI audit's release-clip warning (slot 9 is now `action_skill01`). Server loads the CHR too: restart and bake. Undo puts the hole back (`build/basilisk-captain-model/`). Validated in game 2026-10-08: visible, animates, 3017 cast presented on its frame. A scan of every id in every REGEN lump (459) found no other named row without a model; the rest are blank rows (80, 90, 146, 458, 1520, 1573, 1723/1724, the SUM_EVENT 323/324) that `RegenCharacter` refuses to spawn.
- `scripts/fix-target-marks.py` + `scripts/fix-monster-marks.py` — the icon beside a focused monster's name is `TARGETMARK.DDS` cell `NPC_TYPE` (LIST_NPC col 27, used as-is). Retail meaning, pinned by 667's labelled names: 1 Basic, 2 Guard, 3 Fighter, 4 Warrior, 5 Ranger (bow), 6 Hunter (crossbow), 7 Magician (staff), 8/9/10 Leader/Captain/Sub-boss (bronze/silver/gold star), 11 King (crown), 16 Elite (skull — ours: unique bosses). Our sheet had the crown blanked and muddy art; the first script rebuilds it from 667 (+ Jrose's 12-14), uncompressed so neighbouring cells stay exact. The second applies a reviewed per-row table (89 rows, 2026-10-07: kings → crown, unique bosses → skull, normal-strength monsters wearing stars → role, shooters → bow/staff, stars graded by HP) and refuses rows that changed since review; re-run it after an import that rewrites LIST_NPC. **Compare image cells by bytes** — `ImageChops.difference(...).getbbox()` on RGBA looks at alpha only.
- `scripts/fix-coplanar-object-overlaps.py` — separates map-object **placements** whose faces are exactly coplanar (the camera-move wall flicker); see "Coplanar Placements Flicker" below. Position bytes only, ordinals untouched, manifest in `build/coplanar-overlaps/`.
- `scripts/set-deco-collision.py` — gives walk-through map decorations collision. It sets ZSC part collision levels from its `CHANGES` table. Since 2026-10-29: the Eldeon jungle set (`LIST_DECO_EJ.ZSC`, zones 61-64), with level 12 like the set's trees, on the giant flower (all parts), the hut's frame and the two banner poles (not the cloth). Low wide ferns stay walk-through, the user's choice. Mesh names are checked per part, and every other object is proven byte-identical. `--dry-run`/`--verify`/`--restore`, original in `build/deco-collision/`. Afterwards re-bake the zones' walk grids (`nav-bake.py`) and re-pack the client data.
- `scripts/audit-lightmap-index.py` — read-only crash audit of every zone's object lightmap index files (`x_y/LightMap/*.lit`), 3 s for the whole game. `CMAP::LoadLightMapINFO` checks nothing: an entry for an absent IFO lump dereferences NULL, an ordinal past the lump reads past the heap array, a part outside the model indexes past `m_pHNODES`/`m_pLightMapMaterial`, a grid width of 0 divides by zero. It also grades records `CMAP::AddObject` drops for lying outside their chunk (their object slot goes to the next object, which then receives the dropped entry) and flags entries **baked for another mesh** than the record now holds (the tga name carries the mesh) — the trace of a map edited in the editor after its bake, since the editor never renumbers entries. Run it after any import that brings map files and after editing a map in xadet. First run (2026-10-03): 3 crash-class entries, all retail bugs present in every reference client (JD04 32_34; Goblin Cave B1 JZ01_1 31_30 and 32_31, shifted by records inserted after the bake), fixed by `scripts/fix-lightmap-index.py` (reviewed `FIXES` table, each move checked against the baked mesh names; backups in `build/lightmap-index/`, `--dry-run`/`--verify`/`--restore`; client-only data, re-bake). Left as INFO: 21 retail objects sit on or past their chunk's far edge and are simply missing in game. **Desert of the Dead (zone 29) was missing 561 of its 1006 decorations and all 6 buildings**: our `LIST_DECO_JD.ZSC` / `LIST_CNST_JD.ZSC` were an older cut of the same tables (every object we had is identical in ruff/139/Evo/Jrose/QQ), so the map's records named empty objects and the client created nothing. Fixed 2026-10-04 by `scripts/fill-zsc-gaps.py` (see below).
- `scripts/fix-karkia-phantom-shadow.py` — **a shadow on the ground with nothing to cast it** is a plane lightmap baked before an object was deleted, not a missing object: check the IFOs and our reference dump first, then render the `x_y_PLANELIGHTINGMAP.DDS`. The Karkia Cemetery's (zone 87, ~(4857, 5537) m, a ~110 m tree-shaped blot, Jrose's own bake) is repainted in 30_30/29_30/30_29 only: our ground bake (`relight-zone.py bake --out`) supplies the relief under it, scaled to Jrose's; only the changed DXT1 blocks are swapped, every mip level, everything else byte-identical. Hash-gated, `--dry-run`/`--restore`, originals + `preview.png` in `build/fix-karkia-phantom-shadow/`; client-only, re-bake. Copy its mask/fill/splice for the next one rather than relighting a whole zone.
- `scripts/fill-zsc-gaps.py` — fills the ZSC objects a zone places but our table leaves empty (default zone 29; `--zone N` for others), from ruff, taking an object only if the 139/Evo/Jrose/QQ witnesses agree. Writes empty slots only (other zones on the same table keep their bytes), remaps dummy-point **effect indices** into our effect list (never verbatim: the client indexes it unchecked), and copies missing `.eft` chains — JD04 needed 7 `.eft` plus a `.ZMS`/`.ZMO` mesh effect; the meshes and textures were already here. `--dry-run`/`--verify`/`--restore`, backups in `build/zsc-gaps/`; client-only, re-bake. Confirm with `audit-lightmap-index.py --zone N`: each `.lit` entry names the mesh it was baked for, so a clean audit proves the ids landed on the right models. Validated in game.
- `scripts/relight-zone.py` — bakes the ground and object lightmaps of an **existing** zone (made or edited in the map editor) with the map generator's bakers; artist guide in [doc/relighting-maps.md](../doc/relighting-maps.md). Every look setting is exposed (`settings` lists them), saved per map in `scripts/mapgen/relight/<FOLDER>.json` (commit it: `data/` is not in git). Originals go to `build/relight/<FOLDER>/` on the first bake, so `restore` always returns to them; each bake is checked with the audit's rules and rolled back on any crash-class entry. Refuses zones installed by `mapgen-zone.py` (those with a manifest in `build/mapgen/installed/`; their lighting is in the layout). Re-bake the VFS afterwards; servers do not read lightmaps.
- `scripts/restore-warp-gates.py` — re-inserts `LUMP_TERRAIN_WARP` (type 10) objects into map `.IFO`s. `WARP.STB` is usually fine; the missing piece is the trigger object the player walks into, often replaced by a dead `warpbox` event object whose trigger exists in no QSD. **Also check the landing point**: gate 43 (Desert of the Dead -> Anima Lake) needed `WARP-JD04-JG03` put back into `JG03.ZON`, which the server reads (restart it). Covers Eldeon and, since 2026-10-04, Junon gates 40/43. Has a `--selftest` that proves the container rewrite is byte-identical before it touches anything; originals in `build/warp-gates/`, `--restore`.
- `scripts/upgrade-skill-icons.py` — **our `SKILL01-03.DDS` came DXT5-compressed** (every
  retail-lineage dump ships them uncompressed; a 40 px icon keeps a third of its colours through
  DXT5, and the engine draws the file as-is at 1:1, so the file is the whole story). `--recover`
  pastes the same art from tsuki's 24-bit sheets (same layout, sheets 1-2) or 667's by art match
  (667 keeps our legacy art uncompressed around its sprites 1016-1248) and rewrites the three
  sheets as 32-bit BGRA, indices untouched; 269 of 272 real sprites had a twin. `--modernize`
  appends 667's *redesigned* icons for our skills matched by name (22; 667 renamed most of its
  tree, and a name shared by two of our skills -- Triple Shot, Combat Mastery -- is skipped) and
  `--pick ROW=IDX667` does it by hand. **`--recover` plus four hand picks are applied** (2026-10-10: the three summons Employ Warrior / Employ Hunter / Terror Knight and Berserk, sprites 513-516): the user tried
  `--modernize` plus 86 hand picks in game and rejected the rest -- 667's redesigned icons clash in
  style with the classic set, and they carry colour-coded frames. Crisp same-design art is the
  house look; a 667 redesign is a one-off `--pick` when one genuinely fits. The reviewed pick list
  is kept in `build/skill-icon-quality/picks.txt`. **667's 1521 sprites include 550 gray placeholders**
  (button, checker, stone): detect by pixels (interior colourfulness < 10; real art bottoms out
  at ~16), never by the table -- 264 named 667 skills point at one. 667 also colour-codes its
  frames by skill type; `--recover` keeps our frame (interior-only paste) when the twin's frame
  differs. Survey sheets in `build/skill-icon-quality/`, undo in `build/skill-icons-667/` +
  `LIST_SKILL.icons-667.json`, review sheets `report-*.png`. Client-only; re-bake.

- `scripts/upgrade-item-icons.py` — **the item sheets ICON01-50 were DXT5 too** (2026-10-19), the
  skill sheets' problem on 50 sheets: ~300-400 colours per 40 px icon. Our items come from several
  clients and every reference dump numbers and lays out its sheets differently (668's ICON22 holds our
  ICON33), so **twins are found by picture, never by position**: `--survey` compares every cell's
  4x4 block means against every cell of the uncompressed sheets of 668 (32-bit), tsuki (24/32),
  667/Jrose/RoseZA/139/ruff (16-bit), and writes review sheets to `build/item-icon-quality/`. The
  full-pixel diff is DXT noise and does not separate twins; the block-mean distance does (<= 6 match,
  <= 12 doubtful). **A label drawn on the icon defeats every average** ("+10" vs "+50", a scroll's
  town name): the worst 4x4 block catches it (~110 for a different label, <= ~60 for a true twin),
  and such pairs are not twins. Candidates that are themselves decoded DXT (<= 4 colours in almost
  every 4x4 block) are skipped. `--apply` pastes the *reviewed* `plan.json` (whole cell, RGB, our alpha
  kept) and rewrites each sheet as 32-bit BGRA with no mip chain; `--verify` re-reads every pasted cell.
  Applied 2026-10-19 after the user reviewed the sheets: 5,855 icons on 49 sheets (ICON21 had no
  twin; 1,076 icons with no twin stay as they were). Originals in `build/item-icons-crisp/`,
  `--restore` (whole file). Client-only; re-bake.
- `scripts/fix-hair-block0.py` — **hair ids are zero-based blocks of five** starting at 0, 5, 10 ... (base, three
  under-a-hat variants, an empty "no hair" fifth slot), and a cap adds its hair type
  (LIST_CAP game col 33, 0-4) to the character's hair id to pick the model under it. Our
  `newhair` set had style 1 shifted to rows 1-4 of `LIST_HAIR.STB` / `LIST_MHAIR.ZSC` /
  `LIST_WHAIR.ZSC`, creation offered hair 1 and the world server refused 0, so under any
  type-4 cap (wigs: Dimple Bonnet and 90 others) the client drew row 5, the *second* style's
  full hair, through the wig -- which read as a broken hair texture. The script moves style 1
  to 0-3 and creation to hair 0 (2026-10-11); the server accepts 0, migration 0004 moves the
  hair-1 characters to 0. Ship client, data and servers together. Faces are one-based too
  (`LIST_MFACE.ZSC` object 0 empty) but have no block structure and are left alone.
- `scripts/fix-skill-action-mode.py` — **what a character does after a skill fires is
  LIST_SKILL game col 29 `SKILL_ACTION_MODE`** (0/blank stop, 1 attack the target, 2 return to
  the command running when the skill was ordered), read by `SetNewCommandAfterSkill` on the
  client and by `CObjAI::Do_SKILL` on the server. Retail ships eight player attack skills blank -- Lightning, Wind Storm, Tornado, Luna
  Stone, Ice Bang, Curse, Red Cloud, Zuly Storm -- so casting one mid-fight stopped the character
  (alpha, 2026-10-11); the rest of the damage family is 2. Sets every rank of the eight to 2
  (sidecar `LIST_SKILL.action-mode.json`, `--dry-run`/`--verify`/`--restore`). Not 1: the server
  sets `CMD_ATTACK` with no target check, so 1 is for targeted skills only. **Mode 2 alone did
  not fix the type-7 pair**: the client sends a type-7 skill as a *position* skill
  (`CFireToPositionSkill` -> `CLI_POSITION_SKILL`, cast at the target's spot), and
  `SetCMD_Skill2POS` clears the target on both sides, so the restored `CMD_ATTACK` had nothing to
  attack and `ProcCMD_ATTACK` stopped the character. `CObjAI::m_iRestoreTargetIDX` (server and
  client twins) now carries the attack target across a position skill and the SA_RESTORE branch
  puts it back; `ProcCMD_ATTACK` still checks it is there and alive. A new member on `CObjAI` is a
  class-layout change: clean-rebuild `sho_gameserver` and `client`.
- `scripts/fix-diamond-gem-effects.py` — **socketed Diamonds showed no glow** (2026-10-12). A gem's glow is `GEMITEM_ATTACK_EFFECT` (LIST_JEMITEM col 21), a LIST_EFFECT row whose `EFFECT_POINT` cols 2-5 name FILE_EFFECT rows; the families sit on a five-step ladder (Garnet 1-3 … Pink Opal 36-38, retail Diamond 31-33) but our Diamond rows carry tsuki's pointer, 74-76, and those rows were blank here: tsuki's newer `_JEMITEM_DIAMOND02/12/22.eft` set came across as files (shared with Pink Opal) but not as table rows. Writes LIST_EFFECT 74-76 and FILE_EFFECT 1188-1190 in place through `import-monster-skills.py`'s `Plan`; cell-level undo in `build/diamond-gem-effects/`. Also from that session: **a weapon with no dummy points drops the gem glow silently** (`CreateGemmingEffect` checks `WEAPON_GEMMING_POSITION` = dummy 2 against the count) — the Vengeance Staff is such a weapon; `import-glb-weapon.py` now writes the three retail anchors (trail tip, trail low, gem) on every custom weapon.
- `scripts/repackage-subweapons.py` — re-files the Jrose "shields" (LIST_SUBWPN 306-334) that are not shields. **The retail table already has the classes**: col 4 is 261 Shield, 262 Support Tool (the Book of Standards → Magic Pearl caster ladder, rows 61-82, INT-gated, +Max MP/+MP regen) and 263 Dolls (renamed "Charm" in `STR_ITEMTYPE.STL`). Class 261 does exactly four things in code: the tooltip's DEF/RES line (now printed for any sub-weapon with non-zero DEF/RES — the server sums every slot regardless of class, so a tool's RES was invisible), the Soldier/Knight *Shield Barrier* / *Endure* requirement, 40% of incoming durability wear, and a shield passive no skill grants. Attach point is the ZSC dummy, never the class. Four real shields remain, one per tier (Garm 210, Golden Angel 220, Ancient Davion 230, Righteous Shield NEO 240), sized at 12-13% of the armour set's DEF; the mirrors are 262 (INT gate = the old STR value), the rest 263 with a DEX (avoid/crit) or STR (max HP/hit) flavour. **Every 230/240 import had cloned the Davion template's +150 DEF / +45 STR options** — stripped, Davion included (its options re-authored on a curve). Drops: the `OFF_*` lists in `add-karkia-drops.py` (re-applied with `--rewrite`; its `--restore` is a whole-file ITEM_DROP copy, never use it after Oro/Shibuya ran) and `add-oro-drops.py`; Huzam's tab is pinned in `stock-muris-shops.py`. Cell-level sidecar, `--dry-run`/`--verify`/`--restore`.

Note `src/pipeline/src/pack.rs` walks the data tree filtering only *hidden* entries plus the `ignore <glob>` lines of the manifest `scripts/pack.ps1` writes. That exclude list (`$excludes`, 2026-10-04) keeps development files out of the `.vfs`: the map editor that runs from `data/` (exe/pdb/ini/log, dll, `Content\`, `ESTB\`), every `*.json` sidecar, `*.md`/`*.py`/`*.tmp`, and a stray retail `AGIT01\TEMP`. Globs are case-sensitive with forward slashes, `*` crosses folders, and a pattern cannot contain a space. A `.bak` is not excluded: `pack.ps1` refuses to bake while one exists — move it to `build/`. A new kind of tool file under `data/` belongs in `$excludes`.

### The .vfs Offset Limit (2 GB -> 4 GB)

**A `.vfs` archive cannot exceed 4 GB, and could not exceed 2 GB before 2026-08-29.** `FileEntry::lFileOffset` in triggervfs was a *signed 32-bit* `long`, so a file stored past byte 2,147,483,647 got a negative offset, the client seeked to garbage and read binary noise. It is now unsigned, which doubles the ceiling — the on-disk field is the same four bytes, only the interpretation changed.

Three things had to change together, and each failed differently:

- `FileEntry::lFileOffset` and `VFileHandle::lStartOff`/`lEndOff` are unsigned. `MapViewOfFile` already took the offset as a DWORD low + DWORD high(0) pair, so the engine's memory-mapped path needed nothing else.
- **`vfread` had a `(signed)` cast** on the end-of-file comparison that actively defeated the unsigned field, plus a `long` offset and a 32-bit `fseek`. The client opens `"r"` (plain fseek/fread) while the engine opens `"mr"` (memory-mapped) — **completely different code paths**, so verifying one proves nothing about the other. The mapped path worked as soon as the field was unsigned; the plain path silently returned **zeros** until `_fseeki64`.
- `vfseek`'s clamps must be **signed 64-bit**. In 32-bit unsigned, `lStartOff + offset` with a negative offset wraps and the underflow guard silently fails.

**Bake with `scripts/pack.ps1`** (`data/` -> `Exes/`). A full bake of the 2.5 GB tree
took ~7 min until 2026-10-11; it takes **14 s** now, byte-identical output. Two causes,
neither of them I/O: `pack.rs` recompiled its two glob sets *inside* the file loop
(77,000 compilations for 38,655 files -- ~4.5 min), and it printed one line per file
while `pack.ps1` collects stdout through `Start-Process -RedirectStandardOutput`, which
PowerShell drains line by line (~140 s for a packer that ran in 14 s). The glob sets are
built once, the archive goes through an 8 MB buffer with arithmetic offsets, and the
packer prints a count instead of a line per file. The verify and the stray-file walk
cost 3 s together. Before blaming disk speed on any bulk tool here, check what it prints. It now runs `verify-vfs.py` itself and fails the bake if the archive is unaddressable, and it prefers `bin/release/pipeline.exe` over the copy in `Exes/`. That default matters: `Exes/pipeline.exe` had gone **four months stale** (2026-04-06, against a rollover added 2026-08-17), so every bake ran a packer built before the size guard existed — which is the entire reason `rose.vfs` sailed past the split threshold with no `rose_2.vfs` while the code looked correct. Checking the timestamp of `bin/release/pipeline.exe` says nothing about what actually packed the archive.

Verify manually with `scripts/verify-vfs.py <game-dir>` after any bulk bake — it re-derives everything from the bytes on disk, so a packer bug cannot hide from it. `scripts/make-oversize-vfs.py` builds a sparse >2 GB archive with a deterministic payload and `vfs_buffer_tests.exe --bigtest <dir>` reads it back through the real triggervfs in **both** modes, checking content it re-derives from the file name — an independent oracle, not a comparison of two paths that share the bug.

The failure looks nothing like its cause. Whichever files happen to land past the boundary are simply the tail of the archive — when it first hit, that was `SCRIPTS\INIT.LUA`, so the client died at startup with a Lua `invalid control char near 'char(6)'` parse error followed by `assert: failed. zz_shader::check_system_shaders()`. Nothing pointed at the archive.

`pack.rs` now **rolls over to `rose_2.vfs`, `rose_3.vfs`, …** at 4.2 GB (`VFS_MAX_BYTES`; the margin below the 4 GiB ceiling absorbs the largest single asset, since the check runs before writing — it was 1.9 GB while the offset field was still signed) and hard-errors if an offset would still overflow. Both the `.idx` format (`VfsIndex::file_systems` is a list) and the runtime (`CVFS_Manager::m_vecVFS`, searched by `OpenFile`) already supported multiple archives — only the packer was hardcoded to one. **Ship every `rose*.vfs` alongside `data.idx`**, not just `rose.vfs`.

Diagnosing a suspected bad bake: `scripts/verify-vfs.py <game-dir>` (or `rosefmt.idx.parse`) reads the `.idx` FAT and checks every entry's offset + size against its archive; offsets are unsigned, so look for entries past 4 GB or past the end of their `rose*.vfs`, not for negative ones.

### Item Import Tooling
`scripts/import-item.py` imports an equipment item from another ROSE data dump (e.g. an evo-era private server) as a new appended ID: STB row, model ZSC object (with mesh/material dedup), ground-drop model (`--copy-field-model`), STL name/desc key, and any missing mesh/texture files. Always start with `--dry-run`; it makes `.bak` backups and verifies after writing. **It leaves `<table>.import-<id>.bak` beside each table it touches, and `pack.ps1` refuses to bake while a `.bak` sits under `data/`: move them to `build/` before a bake.** Undo a run with `scripts/remove-trailing-items.py` (trailing rows only, matched by `--name-prefix`).

**Prefer `--art-only --template-row N` for anything from an unfamiliar dump.** It takes the model (plus `--copy-icon` / `--copy-field-model`) and clones every stat column from one of *our* rows, reading no source cell at all. That is a safety property, not a convenience: item bonus/requirement columns are ability ids fed straight into `m_iAddValue[nType] += nValue` with **no bounds check** on the client (`src/client/common/cuserdata.cpp`; the gameserver's `accumulate_item_bonuses` in `mounted_item_bonuses.h` now skips ids outside `0..AT_MAX`). `m_iAddValue` is `int[AT_MAX]` with `m_nPassiveRate`/`m_btRecoverHP`/`m_iDropRATE` directly behind it, so a modern affix id (Jrose uses 174/175/184/185/195) silently corrupts adjacent character state instead of erroring. `--art-only` also skips the source STL, sidestepping foreign dialects — Jrose writes the legacy `I_NUM` header that our strict reader rejects. See [doc/jrose-back-import.md](../doc/jrose-back-import.md). `scripts/add-item-icon.py` adds a custom item icon from a PNG (any size, auto-downscaled to a 40×40 cell) to the `ITEM1.TSI` atlas and prints the new global icon index (`--weapon-row N` also patches the STB); requires Pillow. `scripts/add-skill-icon.py` is the same tool for **skill** icons (`SKILLICON.TSI`, extension sheets `skill04.dds`+, original indices 0–506, extensions from 507; `--skill-row N` patches `LIST_SKILL.STB` col 51). Note the two TSIs use different sprite-rect conventions (item `x..x+40`, skill `x..x+39`) — each script matches its atlas. Both docstrings document the underlying binary formats — read them before editing STB/ZSC/STL/TSI by hand.

**Castle gear and cart parts are `--type pat`** (`LIST_PAT.STB` + `LIST_PAT.ZSC`, one model
table indexed by item number). A part rides the skeleton of its pet type (col 16), so check the
source's skeleton for that type is byte-identical to ours before importing art. Jrose's castle gear
is, and `scripts/import-jrose-castle-gear.py` (2026-10-14) imports its 23 sets plus 5 extra arms
into the blank band at rows 512-585 with baseline stats. The three ranged arms bring their own
projectile rows. Undo is whole-file (`--restore`). Two traps from its first test: **an
arm's col 40 picks the motion set of the whole gear** (0 punch, 1 drill, 2 sword wheel, 3 cannon,
4 spear: a TYPE_MOTION column), and Jrose leaves it blank on arms that are clearly spears or axes,
so clone the retail arm the model looks like. Also, **a foreign effect mesh can be ZMS pool 0**,
which pops the `zz_morpher` `get_dynamic()` assert on first use; set it to pool 1.

**Original weapons come in through `scripts/import-glb-weapon.py`** (2026-10-11; first item:
the Meshy-generated "Celestial Frost Scepter", LIST_WEAPON 1469, validated in game the same day; since 2026-10-12 that row *is* the moving version below -- the static one was dropped). It
reads a glTF binary (Meshy's `glb` export — the only format on its list that needs no Blender
and carries geometry + UVs + the base-colour texture in one file), writes a ZMS0008 rigid mesh
(format 0x86: position, normal, uv0; no bones, no material ids, no strip, pool 0) plus a DXT5
DDS with a full mip chain under `3DDATA\WEAPON\CUSTOM\<slug>\`, and registers it through
`import-item.py`'s own appenders by handing them an in-memory one-object source table, with
every stat cloned from `--template-row`. Conventions measured on the stock staffs and baked into
the geometry (the ZSC part transform is ignored for character parts): metres, shaft along +Z,
head up, origin = right-hand grip 22% up from the foot, wide axis X; glTF's Y-up maps as
`(x, -z, y)`, which preserves the counter-clockwise-out winding both formats share, and no UV
flip. Meshy's retopologised export is ~10k triangles (stock: 100-2,500) — fine for a showpiece,
and the script refuses anything over the 16-bit count. `--max-triangles N` decimates with
MeshLab's texture-aware quadric collapse (`pymeshlab`, installed 2026-10-11; imported only when
used): positions are **welded first** — a glTF splits every vertex on a UV seam, so unwelded the
collapse treats each seam as a boundary and stalls (7,644 of a 3,000 target) — and the UVs ride
as wedge coordinates, split back per vertex for the ZMS. 10,140 → 3,000 was invisible in a
textured render; the shipped scepter is deliberately left at 10k. Only the diffuse is used; normal /
metallic / roughness maps are dropped. The icon is the `add-item-icon.py` path (the first one is
the Meshy screenshot keyed and rotated over the median stock icon frame); its `ITEM1.TSI.bak-icons`
lands in `data/` and must be moved to `build/` before a bake. Backups and a manifest go to
`build/glb-weapons/<id>/`. Software-render the written ZMS beside a stock mesh before baking: that
is what proved the orientation here, not the game. **Every custom weapon gets the three retail
anchor dummies** (trail tip, trail low, gem point = index 2, LIST_WEAPON col 43): a weapon without
them silently shows no socketed-gem glow (`CObjAVT::CreateGemmingEffect` bounds-checks the index) --
the Vengeance Staff template has none.

**Moving pieces on a custom weapon** (2026-10-12, validated in game on the Celestial Frost Scepter,
LIST_WEAPON 1469, slug `celestial_frost_scepter`): a piece of the model becomes a rigid mesh inside its own `.EFT`, turned by
a generated node-rotation `.ZMO` (loop count 0) and attached by a `POINT_EFFECT_ORIENTED` dummy
on the weapon object -- the Jrose animated backs' mechanism, never used on a weapon in retail.
Three selections: `--spin INDEX:SECONDS[:circular]` (a detached shell; the dry run prints the
shell table), `--spin-above Z:SECONDS --collar CM` (plane cut with clipped triangles plus a band
hiding a non-round seam) and `--spin-frame Z:SECONDS --keep-hue LO-HI --keep-radius R --sleeve
R:Z0:Z1` (whole faces by texel hue + radius above Z, islands cleaned: the frame turns around a
still crystal, the sleeve hides the fused stem). Traps: effect meshes must be **ZMS pool 1** or
`zz_morpher` asserts `get_dynamic()`; `CEffect::IsFinish` asked only the *morpher* (a rigid piece
has no morph) -- it now counts a running node animation, and an older exe deletes these effects
on sight; `CObjCHAR::New_EFFECT` never created model dummy effects for weapon parts (now it does,
after the item-table effects; retail weapon dummies carry no effect, so nothing else changes);
Meshy welds touching parts into one shell, so what can move is decided by the geometry, not the
wish. Cost per visible copy: +1 draw call per piece, nothing per frame. Known gaps: the pieces
are drawn unlit and take no refine glow; `CJustModelAVT` (character select, Alt+click preview)
creates no part effects, so the puppets show the weapon without its moving pieces, as they already
omit the Phoenix Wings' particles; the bone-effect budget registers them but tiers only particles.

**Jrose's particle-only back items have no model** — a 4-vertex placeholder mesh, the look
being an effect placed by its item tables (`LIST_BACK` col 51 → `LIST_WEAPONEFFECT`: col 2
`FILE_EFFECT` row, cols 4-7 quaternion ×0.01, 9-11 position, 13-15 scale ×0.01, read out of
`TRose.exe`). `import-item.py --attach-effect-row N` turns that placement into a ZSC dummy
point of type `POINT_EFFECT_ORIENTED` (3), which `CObjCHAR::New_ModelDummyEFFECT` places with
rotation and scale; type-0 points stay position-only, as Phoenix Wings and Portable Fireworks
were validated that way. See `doc/jrose-back-batch7.txt`.

**Putting a new skill in the skill tree needs an art edit**, not just an XML node: the boxes and connectors are painted into the per-category `DEALER_*.DDS`, and `CSkillTreeDlg` has no line-drawing code at all — the XML only positions a 40×40 icon, at coordinates that are **absolute inside the dialog**, not relative to the parent node. The xml loads loose via `fopen` (never the VFS) while the DDS loads from the VFS and ignores a loose copy, so the two halves deploy differently. Recipe, coordinate system and traps: [doc/skill-tree-art.md](../doc/skill-tree-art.md).

Key facts: weapon visuals come from `LIST_WEAPON.ZSC` indexed by item number (1:1 with STB rows) — the STB "model file" text column is vestigial and never read by the game. Ground-drop visuals are a separate index (STB col 10) into `LIST_FieldITEM.ZSC`. Icon indices are global sprite positions in `ITEM1.TSI` (originally 50 sheets × 169 cells = 0–8449; extension sheets `icon51.dds`+ continue from 8450). After data edits, restart servers (they cache STBs at startup) and the client.

### Monster Spawning: A Regen Point's Cap Is Not A Cap

`CRegenPOINT::Proc` is a **tactical escalation state machine**, not a spawn list. Its
five "basic" slots plus two "tactics" slots are steps, and each tick it picks one to
three of them — never all five — from an 11-branch table indexed by

    iVar = ((limitCNT*2 - liveCNT) * curTactics * 50) / (limitCNT * tacticPoint)

Two things follow, and both have bitten:

- **`tacticPoint` decides whether `limitCNT` means anything.** `CZoneTHREAD::RegenCharacter`
  has **no cap check** — the only gate is `Proc`'s early return while
  `liveCNT >= limitCNT`, so whatever branch fires spawns its full count regardless of
  how much room is left. `tacticPoint` scales the whole table: at 100 (the house value;
  every zone we ship uses 50-200) an empty point starts at `iVar == curTactics == 1`,
  walks up from the bottom branch and fills to exactly `limitCNT`. At **1** the first
  tick computes `iVar = 100`, lands in the *top* branch, and spawns four bodies into a
  cap of one. Karkia's two main maps shipped that way and held 3,388 and 1,109 monsters
  against summed caps of 847 and 278 — **every density figure measured from the tables
  was 4x low.** Never derive a zone's population from `sum(limitCNT)`; simulate `Proc`.
- **Per-slot count must stay well under `limitCNT`.** Count 5 against a cap of 5 means
  slot 0 fills the point on tick one and slots 1-4 are unreachable until a player
  clears the nest by hand — which reads in game as "this zone only has one monster",
  then a second species, then a third. Retail runs counts of 1-2 against caps of 2-13.
  Weight a species by **repeating its slot**, never by raising the count. `interval`
  paces the escalation as well as the respawn: one tick is one step.

Karkia's rosters, caps, intervals and camp layout live in `scripts/import-karkia.py`
stage 3 (`SPAWN_ROSTER` / `SPAWN_CAP` / `SPAWN_INTERVAL` / `SPAWN_THINNING` /
`SPAWN_CAMPS`), which **rebuilds every REGEN lump from source on each run** — so
editing an `.IFO` by hand or in the map editor is silently undone by the next
`--stage 3`. Note the two main maps are authored as a *carpet* of single-species
points, so spatial thinning deletes species; `SPAWN_CAMPS` merges neighbouring points
into mixed-species camps instead, which cannot.

**A point spawns on its first tick** (2026-10-05, `CRegenPOINT::Load` back-dates
`m_dwCheckTIME` by one interval). Before, the zone clock started at 0 and every point
waited a full `interval` after a server boot, so a 30-minute lone boss was absent for
the first half hour.

Separately: the server no longer reads `*.MOV` (2026-10-30; the files are in
`build/retired-mov/`). Monster walks use the walk grid (`scripts/nav-bake.py`), so a new or
re-edited map needs a bake, not a MOV. See the gameserver `CLAUDE.md`.

### A Missing NPC Row Must Degrade, Not Kill (Client/Server)

The sibling of the engine's "Missing Assets Must Degrade, Not Kill" rule (src/engine/CLAUDE.md), on the data side. Six AI action types carry a
monster id as a `WORD` at offset 8 — 09 `Change_CHAR`, 10 `Create_PET`, 18, 20,
36, 37 — and an imported `.aip` routinely names monsters that were never
imported with it. The summon actions were always safe by accident
(`CZoneTHREAD::RegenCharacter` rejects an unknown id), but **`Change_CHAR` had no
guard on either side** and crashed the client. The 2026-09-12 Karkia session:

- `kak_ghost_cemetery.aip` (NPC 2731 "Ghost Seed", summoned by the whole Burned
  Forest roster) does `Change_CHAR` → 2734-2738, all **blank rows** in our
  `LIST_NPC`.
- Client `CObjMOB::Change_CHAR` calls `DeleteCHAR()` and *then* `Create()`.
  `CreateCHAR` returns at its part-count check, so the object stayed alive with
  **no engine model node** — invisible, unclickable, and logging
  `interface: getVisibility() failed` every frame.
- `CCharMODEL::DeleteBoneEFFECT` took `CEffect**` **by value**, so
  `SAFE_DELETE_ARRAY` nulled only the local copy and left `m_ppBoneEFFECT`
  dangling. Invisible on the normal path because `CreateCHAR` reassigns it a few
  lines later — but a *failed* `Create()` never does. Zone teardown then walked
  freed memory and freed the array a second time.

Things that will bite:

- **Heap corruption produces no crash dump.** It fail-fasts
  (`STATUS_HEAP_CORRUPTION`) without unwinding, so `SetUnhandledExceptionFilter`
  is never consulted and `CrashHandler` wrote nothing. A vectored handler now
  catches the three fail-fast codes; anything else is handed straight back,
  because a VEH sees every first-chance exception including ordinary handled ones.
- **`NPC_NAME` is NULL on the server and `""` on the client** — `STBDATA::get_cstr`
  returns `nullptr` for a blank cell, while the client goes through
  `CStringManager`. A guard written as `if (!NPC_NAME(i))` is correct server-side
  and useless client-side.
- **`error.txt` accumulates across sessions and is buffered**; `client.log`
  flushes per record. Read `client.log` first — here it named the exact object
  (`client_idx 1217, type 7, char_no 2735`) and stopped mid-`FreeZONE(131)`,
  which located the crash. `error.txt` only had 434 subject-less engine lines.
- Count broken *objects*, not log lines: one object spams every frame.

`scripts/audit-ai-monster-refs.py` is the data tool — read-only with
`--dry-run`/`--verify`, strips the dead action records otherwise, and has a
`--selftest` that proves the container rewrite is byte-identical across every
`.aip` before touching anything. Backups go to `build/ai-monster-refs/`, **not**
beside the `.aip`, because `pack.ps1` hard-errors on a `.bak` under `data/`. Run
it after any `import-*.py` that brings in AI files. It found 278 dangling
references in 45 files: Oro (2236/2237, 3001-3003), Karkia's ghosts and Flower
Garden, and three pre-existing retail ones. Since 2026-10-13 a row with a `.mon`
but a **blank name** is fatal too (the server's guard reads the name cell only;
the check used to need both blank and passed the Woopie Hooligans): it stripped
GHOST_M3's ten Change_CHARs and CL_SEED's two summons after the erase below.

**The same rule holds for skills.** AI action 24 carries a `short` skill id at
offset 10 and nothing validated it either: RoseZA's `OR_THORNIE.AIP` (Fearsome
Terrasaurus King) casts 3603 "Charge" and 3604 "Fireball", both blank rows in our
`LIST_SKILL`. The server broadcast the cast, `Skill_START` switched on
`SKILL_TYPE 0` and did nothing, and every client played the boss's casting
motion for nothing — which also replaced the attack motion of a swing the server
had already applied, so the client discarded that hit and the killing blow
presented death from a bar ~1000 HP too high (2026-09-14). `F_AIACT24` now
refuses a blank/out-of-range skill with a once-per-id warning, the client
presents a self-pre-empted swing instead of folding it (client `CLAUDE.md`,
"Self-pre-empted swings"), and `scripts/audit-ai-skill-refs.py` strips the casts
(same CLI and backup scheme as the monster audit; backups in
`build/ai-skill-refs/`). Nine file/skill pairs, 16 records, in six files. The
docstring records what each id is in RoseZA/667 so a later skill import can
`--restore` first and put the casts back. `import-oro-667.py` step 3h checks that
a cast *animates*, not that the row exists. **The proper fix is
`scripts/import-monster-skills.py`** (first used for this boss: Charge 3603 +
Fireball 3604, with FILE_EFFECT 1881-1884, LIST_EFFECT bullet 476 and eight
`.eft`/`.ptl` files): it resolves the whole chain a monster skill needs, writes
rows **in place at the source index** because our effect/sound/bullet tables are
the same lineage as RoseZA's (it refuses if an index is occupied by something
else), and **does not copy `SKILL_POWER`** — RoseZA's 3500/2000 would one-shot at
our stat scale, our own monster attack skills run 25-100, and for a 2900-ATK
boss the weapon formula floors at ~1400 regardless of power, so both use the
magic formula. The first cut (450/350, sized on balance-sim's synthetic Knight)
measured 2x too hot on the real tester (6.4 damage per power point); they are
now **230/190**, which landed Fireball at 1436-1447 on a 6102-HP character.
Sequence: import → `audit-ai-skill-refs.py --restore --only FILE.aip` for each AI file whose casts the import makes valid (then redo that file's `--remap`/`--remotion`) → `audit-ai-skill-refs.py` again; a bare `--restore` reverts every file's recorded strips and re-points. Second use: Nigaki's Voltage Jolt (871, `--skills 871`) — **Karkia's AI
is Jrose's, so its skill ids are Jrose's** (RoseZA's 871 is a different row);
the row is the Mage player skill, so the importer's per-skill `clear` list
blanks its learn-tree columns and its STL key (which would alias one of ours).
Every effect/bullet/sound index already matched here and the chain was present.
**The quiet failure is an id that is occupied here by a different skill**, which
the blank-row check cannot see: Nigaki also cast Jrose's 923 (rank 3 of the
Cleric's ally Heal, type 11) and our 923 is the player's Healing (type 10,
self) — the Flower Garden's Nigakis spam-cast it on each other with a spell
sound and never fought back. The audit now maps an AI file to its source dump by
filename prefix (`kh_`/`kak_` → Jrose, `or_` → RoseZA) and flags a cast whose
`SKILL_TYPE` differs between the dump's row and ours (the Karkia importer's own
ports and re-points are allowlisted in `DELIBERATE`); its `--remap
FILE.aip:OLD=NEW` re-points casts, and the importer's per-skill `dest` puts a
colliding row at the table's tail (Heal Ally at **7012**, `set={21:16, 22:370}`
because Jrose keeps the ability in its columns 89/90). The Devourer's 3613 was
the other collision (RoseZA long-range damage vs our Karkia Stun port) and is
stripped. Mukuroji's 30-per-tick "poison" is its retail burn (3050 → LIST_STATUS
58 Flame Heat), not a defect. **The third failure class is a cast that can never
present itself**: the client plays CHR slot `nMotion` to cast and `nMotion+1` to
release, and a projectile skill launches only from frames 24/34 (or 26/25) of
the release clip. Orgeid's Jrose CHR held the event-less casting clip in both
slots (bolt never fired, damage landed on a later melee frame with no visual,
status payload timed out); Mukuroji's release is the pig attack clip (frames
21/31). The audit **warns** about these on every run and never strips them —
the fix is a CHR slot (`CHR_MOTION_OVERRIDE` in `import-karkia.py`, applied
without a `--stage 3` by `scripts/fix-chr-skill-slots.py`); `--strict-motions`
makes them fail `--verify`. Current list: Mukuroji 2539 (kept aside, its donor
AI and model disagree) and the seven nameless, unspawned `sur_mon_s1.aip` rows
944-959.
**The fourth failure class is an AI authored against a slot layout the model
does not have.** Grand Master Devourer 2226 (third importer use: 3609 Dispel
Buffs, 3610 Area Slow, 3611 Stun Blast, RoseZA's 3613 Range Attack at **7013**
because ours is Karkia Stun) casts on nMotion 7 and 9, but its model's clip
pairs sit at 6/7 and 8/9 — with our cast = `nMotion` / release = `nMotion+1`
rule (client `CObjMOB` and server `CObjNPC` agree; 1498 of 1565 shipped casts
follow it) the boss released on an event-less charge clip or on no clip at all,
and its self-buffs on 2 released on the hit clip. A parked payload with no
action frame is not lost, it is *folded silently* after the 3 s abandon grace
(`ProcTimeOutEffectedSkill`), so the failure reads as damage with no cast. The
fix is the AI, not the CHR: `audit-ai-skill-refs.py --remotion FILE.aip:OLD=NEW`
(7=6, 9=8, 2=6 here), recorded in the manifest like `--remap`; `--restore
--only FILE.aip` undoes one file without reverting every other file's strip and
remap. Two more data traps from that kit: **RoseZA/667 author a skill's status
in column 88/90**, which our 87-column table never reads (a verbatim copy is a
stun that does not stun — the importer's `set` puts the id back into column
11, and a slow also needs `AT_SPEED` 23 in 21 and a rate in 23, house 30-70%);
and Stun Blast, like the source, has no success ratio, so the stun always lands
when the event fires. The audit's release-clip warning now covers every cast,
split into *projectile* (bullet never fires) and *payload* (no frame drains it;
since 2026-10-08 the client presents it at the end of the cast if that is within
1.5 s, else folds it silently); ~110 retail self-buffs are in the payload list
and are left alone.
First test (2026-09-14): the four imported casts never rolled — they live on
the *attack-move* pattern (evaluated only while the boss chases) behind 25/8/8/5%
rolls and extra gates (a buff on you; a second attacker; an enemy in reach) —
and what read as "a spell with no status" was the boss's when-damaged
self-casts 3596/3597, the same RoseZA rows imported nameless by the old Oro
import with the column-88 status lost; re-imported over them (`overwrite`),
which also repairs the Eldeon/Karkia casters sharing them. **Eldeon's skill
rows are ruff's, not RoseZA's** (byte-identical to the ruff dump, an older
authoring; ruff's LIST_STATUS is our numbering), so a row that "disagrees"
with RoseZA's column 88 is usually doing what ruff meant, just differently —
policy (2026-09-15): keep what works even where it differs, fix what would
bug, fix what makes no sense. Eight rows qualified and are corrected in place
by the importer's `patch` mode (name + status + ability columns only, the
balance passes' damage columns untouched): a self-buff that muted its own
caster (3588), two self/area casts with no status at all (3593, 3598), and
five "damage + stun" rows with no stun (3551, 3572, 3582, 3595, 3527). 3572 is
shared by the Ikaness Engineer and Karkia's Murilos, and a stun on a fast,
numerous spider is a nuisance — so `patch` + `dest` copies our row to **7014**
with the stun for the Engineer (`--remap ed_icanes6.aip:3572=7014`) and the
Murilos keep the plain row. EZ01 validated in game 2026-09-15; EJ02/EJ03 rows
applied, pending a fight. Details and what was deliberately left alone: the
importer docstring.
**The fifth failure class is a gate nobody can pass**: even at a 100% roll
(`--rechance FILE.aip:SKILL=PCT`, for testing) the two damage casts never
fired, because their condition 02 ("N enemies within D m with level diff in
[lo, hi]") is authored as [100, 100] — our server reads it through the 2004
`short nLevelDiff/nLevelDiff2` layout, and a target exactly 100 levels below
the boss does not exist. RoseZA's server evidently read that as "any". Fixed
with `--rewindow FILE.aip:SKILL=LO,HI` (-100,100); the audit warns about any
cast behind a lo >= hi window (only these two in the tree; 18 more such
windows gate retail movement). Also: a pattern's events are first-match-wins,
so a 100% event starves everything after it — bump one at a time. All four
casts plus the self-casts validated in game 2026-09-14: bolt ~1200 with a
real projectile, Stun Blast ~820 with the stun, slow and dispel with icons.
The last stripped casts — Inguz 654 (3044, an area stun), Penguin
Artillery 1458 (2980) and Gangster Pangs 1456/1457 (2979), none spawned — are
imported too (2026-09-15, powers re-based by the casters' ATK; all three validated
in game the same day — RoseZA's 3044 needed a scope, it was authored at 0, and a
4 s duration, since a cast's status is presented 1-3 s after the server applies it),
so nothing is stripped for a missing row any more. Mini-Devourer 2225 casts 3042 from a
model with no skill clip at all (slots 0-5); with no 3D artist, its casts are
removed (`--strip or_minidevourer1.aip:3042`) and it fights with normal attacks. Two client rules came out of validating the kit: a remote caster's queued
skill command must be marked valid when it is queued (`SetValid(true)` at push), not on `GSV_SKILL_START` (a mob's second cast was
being deleted, its lethal projectile then died by the 6 s timeout), and a lethal
legacy `GSV_DAMAGE_OF_SKILL` payload arms pending death at receive (else the
avatar's swings in the 2 s before the caster's action frame are silent instead
of MISS). Both in client `CLAUDE.md`.

### Buffs And Passives Are Percentage-Based

`LIST_SKILL.STB` carries every ability effect in two columns: a **flat** value
(`SKILL_INCREASE_ABILITY_VALUE`, game col 22/25) and a **percentage**
(`SKILL_CHANGE_ABILITY_RATE`, col 23/26). Our buffs and passives now use the
percentage column — flat values were sized for a level-100 cap and decayed to
irrelevance by level 240 (Power Support's +70 ATK is 19% of a level-100
character and 7% of a level-240 one). Converted by
`scripts/convert-buffs-to-percent.py` (idempotent, sidecar,
`--dry-run`/`--verify`/`--restore`); its docstring is the record of what changed
and what was deliberately left flat.

Things that will bite:

- **A percentage must be taken off the *pre-buff* stat.** `Get_SkillAdjustVALUE`
  calls `Get_BaseAbilityValue`, not `Get_AbilityValue`/`Get_DefaultAbilityValue`.
  The current-value accessors already include the running buff, and
  `StatusEffects::IsEnableApplay` rejects a recast only when it is *weaker*, so a
  percentage off the current value compounds on every recast and settles at
  `rate/(1-rate)` — a declared +30% delivered +43%, +50% delivered +100%, and
  >=100% grew until the `(short)` cast overflowed. Keep the server
  (`CObjAVT::Get_BaseAbilityValue`) and client (`CObjUSER::Get_BaseAbilityValue`)
  lists in sync or the two sides disagree on a buff's magnitude.
- **The two columns follow opposite rules.** Passives are either/or
  (`InitPassiveSkill` / `Skill_LEARN` do `if (RATE) … else FLAT`, so a non-zero
  rate makes the flat value dead data); buffs are additive
  (`Get_SkillAdjustVALUE` sums both terms, so a leftover flat applies *as well*).
  Always zero the flat column when writing a rate.
- **Heals must stay flat.** `Get_SkillAdjustVALUE` resolves `AT_HP`/`AT_MP` to
  *current* HP/MP, so a percentage heal scales with what you have left — useless
  exactly when you need it. `AT_MAX_HP` is the one that means max HP.
- **HIT is deliberately still flat.** Accuracy is a cliff (see the level-gate
  section below), and a percentage hands *less* HIT to the low-CON builds already
  pinned at the 7% floor.
- Passives are re-derived from the table on load (`InitPassiveSkill` rebuilds
  from zero), so a data change needs only a server restart, no migration.
- `Get_BaseAbilityValue` is **virtual on `CObjCHAR`**, the base of every character
  object on both sides. Touching it is a vtable-layout change in a widely
  included header — clean-rebuild `client` and `sho_gameserver`, never
  incremental (see the class-layout warning under Common Pitfalls).

Full measurements and the rest of the balance plan are in
[doc/balance-analysis.md](../doc/balance-analysis.md); `scripts/balance-sim.py`
re-derives every number in it against the live STBs.

### Combat Simulator (`scripts/combatsim/`, 2026-10-14)

"How does this build fare against this monster": `python scripts/combat-sim.py run
BUILD.json --mob <row|name>` (also `stats`, `new --job --level`, `find`, `skills`, `ai
--mob` = a monster's AI file as readable rules), or
**Tools > New/Open Combat Build** in Rose Dev Studio (same JSON files, in
`scripts/combatsim/builds/`). Monte Carlo over a tick-by-tick port of the zone loop;
every formula carries its server `file:line`, and what the sim decides itself is printed
under "Assumptions" with every report. The monster runs its AI file (`combatsim/ai.py`
over `rosefmt.aip`).
`balance-sim.py` predates it and misses the first bullet below. Facts it is built on,
each of which was wrong in someone's head:

- **`server.toml [game]` adds `base_attack_power`/`base_hit`/`base_attack_speed` to every
  player** (`classUSER::total_*`), monsters nothing. They were +70/+80/+35 until
  2026-10-15 and are 0 now (`base_move_speed` stays): `scripts/weapon-identity-pass.py`
  moved them into the weapons, and a weapon's hit rate is quality x 1.4
  (`WEAPON_HIT_PER_QUALITY`, `citem.h`; retail 0.6). A percentage buff is taken off the
  stat *without* them (`Get_BaseAbilityValue` -> `stats.*`).
- **A normal swing lasts one attack motion**: `(frames-1)*1000 / (fps * aspd/100)` ms, the
  clip from `TYPE_MOTION[8][WEAPON_MOTION_TYPE]` -> `FILE_MOTION`. Damage lands when the
  swing *starts*, times the clip's attack-event frames. Skills pay MP at cast start and
  land on frame 0 of the action motion; cooldown is `RELOAD*200-100` ms from the cast.
  Any two skills are at least **1 s** apart: the server's `SKILL_DELAY_TIME` is 500 ms but
  the client's `CSkillCommandDelay` will not send a skill within 1000 ms of the last.
- **MP cost is deducted by the server** (`Skill_UseAbilityValue`); INT does not reduce it,
  only `AT_SAVE_MP` items and the Save-MP passive.
- **The second passive range** (`AT_PSV_RES/HIT/CRITICAL/AVOID/SHIELD_DEF`, 98-103) was
  zeroed by the server's `Cal_BattleAbility` on every recompute until 2026-10-14, so
  Weapon Research's crit never applied -- while the client, whose `Cal_BattleAbility`
  re-runs `InitPassiveSkill`, showed it. Percentage STR..SEN passives are still only
  computed in `Skill_LEARN` (lost at login); no skill uses one, and the sim's
  `--fix-passive-bugs` shows what they would give.
- **A debuff's percentage is off the monster's *current* stat** (`CObjMOB` has no
  `Get_BaseAbilityValue`), so recasting an active debuff usually fails `IsEnableApplay`.
  Harmful states roll `SUCCESS*(LV*2+INT+20)/(RES*0.6+5+AVOID) > 1+rand(100)`.
- **Potions:** food and the Health/Mana Vials heal over time through LIST_STATUS, col 20 over
  col 21 seconds since the food ladder (2026-10-20; blank col 21 = the status row's rate); the
  auto-build eats the best shop food its level may use. The Vital/Spiritual Waters are *instant restores* (no status row, adds HP/MP):
  since 2026-10-15 they heal LIST_USEITEM col 20 + col 21 % of the max and col 26 is a
  server-enforced cooldown in ms, one HP timer and one MP timer (`UseItem_*` in `citem.h`,
  `scripts/water-restores.py`: 8 s HP, 3 s MP, level tiers; the XXL pair is a GM-only full heal).
- Monsters have no HP regen; player regen runs every 2 s and is not paused in combat
  (it is paused while poisoned). Sitting regen (`Get_RecoverHP/MP(100)`) is ~20x standing.
- **Item HP/MP recovery counted 0 until 2026-10-14**: `Cal_RecoverHP/MP` read passive +
  item x passive *rate*, and no recovery passive (Meditation, Rest Mastery) has a rate, so
  Recovery/Meditation Rings, recovery gems, Orlean Saga (+300) ... did nothing. It now adds
  the item value like `GetCur_SaveMP`, in a `short m_nRecoverHP/MP` (the BYTE wrapped at
  256; the party packet still carries a clamped BYTE). MP cost reduction always counted.
- **Poison is a fixed tick, not a skill-scaled one**: `StatusEffects::Proc` deals the
  applied LIST_STATUS row's value (`STATE_APPLY_ABILITY_VALUE`, col 6: Poison I-V = 10, 20,
  30, 44, 60/s; Burning, row 58, 30/s) each second while ticks remain, through
  `Give_STATUS_DAMAGE`, which never kills (it leaves 1 HP), wakes nobody and runs no AI.
  The rank picks the row and the duration (Poison Fang 20 -> 30 -> 44/s, Curse 44/s for
  only 3-8 s). Burning is STATE_TYPE 3 too, so it shares poison's slot; only monster skills
  apply it (Fire/Magma Burn are Defense Down, not a burn). A drain (type 19, Blood Attack) that lands heals the caster
  `FLAT*(INT+300)/315` (+ a percentage of its live HP), capped at max HP.
- **Monster AI in a fight is the DAMAGED pattern**: it runs on every player hit *or miss*
  and on every `SKILL_HARM` status (so a damaging debuff runs it twice), never while the
  monster is stunned or asleep, and -- once the monster has a target -- only when the
  file's second header field (`iSecondOfAttackMove`, misnamed: a percent) passes
  `rate >= rand()%100`. ATTACK-MOVE runs only while it chases; STOP only while it stands
  in `CMD_STOP`, once per `iSecond`. Events are first-match-wins.
- **A monster cast costs nothing and has no cooldown**: the only throttles are those odds,
  one skill at a time, and its motions (cast clip = CHR slot `nMotion`, action clip
  `nMotion+1`, damage at the action clip's frame 0). `SKILL_ACTION_MODE` (col 29) decides
  what follows: 1 attack, 2 resume, **0/blank = stop and forget the target** -- the monster
  then stands idle until an AI pattern re-targets it. A stun or sleep does the same
  (`SetCMD_STOP` clears the target). An attack order given before a pending cast starts
  replaces it.
- **A player summon is a monster row on the player's side** (LIST_SKILL col 28; capacity
  50 + Summon Mastery / Employ Troops vs LIST_NPC col 21, checked only by the client). Its
  stats are rescaled at the call by `CObjSUMMON::SetCallerOBJ` from the skill rank and the
  owner's level (HP from `NPC_HP` alone, not level x HP); its level is the owner's *level*,
  and so are its INT and SENSE (`CObjMOB::Get_INT/Get_SENSE` return the level -- never the
  owner's INT stat); critical stays `NPC_LEVEL*0.6`. No lifetime or HP drain any more. It
  runs its own AI file: combat summons attack the owner's target from their STOP pattern
  (every 1 s); the Butterfly never swings, it only casts an accuracy debuff. The monster
  turns on a summon only through its own AI ("attack the attacker" when the summon hits
  it) -- or a **Taunt**: a landed Taunt turns the monster on the caster and, for its
  duration, blocks its AI's attack orders against anyone else (`Is_TauntSTATUS`; skill
  casts still pick their own target). The Terror Knight taunts since 2026-10-16
  (`terror-knight-taunt.py`). A Ctrl+click attack order sets the summon's AI variable 3 and
  runs its ATTACK-MOVE pattern once (`CObjSUMMON::SUMMON_ORDER_AI_VAR`); the sim has no
  Ctrl+click. The sim runs the enemy and the summons through one controller (`fight.Mob`).
- **Skills never crit -- except a katar Hawker's or Raider's** (2026-10-16, user: "they rely
  on crit"). `CCal::Get_SkillDAMAGE` rolls `Get_CriSuccessRATE` for a player of job 311/321
  holding a katar (weapon type 251) and multiplies any damage skill by 1.5 on a crit, setting
  `DMG_BIT_CRITICAL` (the client's crit hit effect + camera shake). Normal attacks were only
  ~10% of a Raider's damage, so the katar's crit identity never showed: katar Raider 0.47-0.76x
  the median class -> 0.70-1.00x (dual swords 0.85-1.00x); katar Hawker +10-22%. The Scout
  and anyone else holding a katar are excluded on purpose. Sim: `formulas.KATAR_*`,
  `Combatant.katar_crit`.
- **Monster skills can dwarf monster swings**: Goblin Sword (1403, lv 92) self-casts Flame
  Hawk for ~580 against a level-92 Knight whose normal hits from it are ~37 -- 85% of the
  damage it deals.
- **Frontal areas (2026-10-18)**: a `SKILL_SCOPE` of 10000 or more is **length x 10000 + width** (cm), a strip from the caster toward the spot a type-7 skill was aimed at -- Jrose's packing (`40002500`); every scope below 10000 is a plain radius as before (`io_skill.h` SKILL_SCOPE_FRONTAL / _RADIUS / _WIDTH; server `Skill_DamageToAROUND`; the client tooltip prints "L x W m"). Exceed Break is the first user. The combat sim is one-on-one and does not model it.
- **The auto-build ranks damage skills by raw `SKILL_POWER` and ignores the action clip's attack frames**, while the server (and the fight) multiply damage by them: a triple-shot skill (`bow_3attack` / `bowgun_3attack`, frames 56/66/26) deals 3x its power plus 3x the formula's Attack Power term, so the auto-build underrates it and may never pick it. Size such a skill by putting it in the rotation yourself and comparing damage per cast (2026-10-18, Deadly Snipe / Volcanic Shot: the first cut, sized as whole casts, one-shot for 9999).
- **Auto-builds follow what players take** (`combatsim/autobuild.py`, user guidance
  2026-10-14/15): CON near the level; skills taken far rather than wide (core first, at
  most `EXTRA_ACTIVES` other actives); two routes, blended by `ROUTES` in a sweep --
  *weapon* (90%: the weapon's own signature skills, Screw Attack / Power Burst / Prime Hit,
  then Double / Power Attack) and *support* (10%: buffs and weapon-free attacks such as
  Flame Hawk, Mana Blood, Red Cloud -- weaker on paper -- with its buffs up before each
  pull). The weapon route also starts its best combat buff and best debuff early at ~30%
  of their ranks (`STARTER_SHARE`) and raises them only with spare points. Only combat
  buffs count (ATK, attack speed, HIT, crit, DEF, max HP/MP, RES -- not move speed or
  stealth). Summons are core for Bourgeois, Dealer and Scout; a profile can name skills its
  players skip (the Raider's Rest Mastery; the Muse/Mage skip the Cleric branch's support
  spells and keep only the heals). `sweep --route` runs one route alone.
- **The default player plays perfectly** (`--play skilled`): every skill the moment the 1 s
  delay allows, buffs and debuffs recast the tick they drop, a ranged player never inside a
  monster's area skill. `--play typical` (`run`, `sweep`; `PLAY` in `sim.py`, the worker's
  `req["play"]`) adds a 0-1 s reaction per skill, recasts a dropped buff/debuff 2-8 s late
  (a fight's first cast is on time) and lets 1 in 3 of the monster's area skills catch a
  player standing out of range. User, 2026-10-16: "you assume people play perfectly" --
  report crowd-control uptime and ranged safety from both. Giant Fiery Snapper (5-player
  split): the Mage's stun uptime 60% -> 49%, silence 87% -> 71%; the Scout's damage taken
  23 -> 33 HP/s; Bourgeois and Artisan stop dodging Searing Burst and win 35-45% instead of
  100%. DPS barely moves: most skills are cooldown-bound, not reaction-bound.

**Checking it in game** (`scripts/combatsim/scenarios.json`): `combat-sim.py
seed-scenarios --password X [--replace]` writes each test build into the dev DB as a GM
character (all on account `simtest@local`; the client wants a password of 8+ characters;
gear/refine/gem/ammo/skills/hotbar exactly as the
build; `seed` does one build); `scenarios` prints the predictions; `compare-trace
client-<date>.log --scenario N` reads the client's CombatTrace lines (`[LOG]
LEVEL=debug`; one-second timestamps, crits unflagged) and flags any measure more than 3
standard errors off the sim. Scenarios 1-4 use monsters that never cast; 5-6 (SimKnight,
SimChamp) check the AI against casters; 7-9 (SimHawk, SimMerc, SimSword, on account
`simsummon@local`: **the character select shows 5 characters per account**, a 6th is
unreachable) check player summons -- `compare-trace` takes whatever else hits the player's monsters as the summon
(a GM-spawned monster fights nobody else). Seeded characters get 1,000,000 zuly (Employ
Warrior costs 5,000 a call) and their summons on the hotbar first. Monster skill damage reaches the log only as
`GSV_DAMAGE_OF_SKILL` lines (server ids, `value` = damage, `success_bits` = states landed)
-- the client sometimes abandons presenting one ("cast abandoned") but the server applied
it. `--since/--until` cut one character's run out of a shared log; `--exclude N ...` drops
fights by their "Per fight" number (a wandering monster joined). AIP record types are the AICOND/AIACT number **+1** (`0x19` = act 24
use-skill; summons 0x15/0x25/0x26). Reading the log: the
server hands a dead monster's object id to the next spawn (a kill closes a fight), and
`GSV_TARGET_SKILL` lines use server ids while damage, `projectile impact` and
`SkillStatusTrace client effect` (success_bits = which state slots landed) use client ids.
First run (2026-10-14, Champion/Mage/Scout, 16 kills): every damage, land, crit, swing
and skill measure within noise of the sim. Scenario 4 (SimArtisan, Weapon Research rank
20) confirmed the passive-range fix: 94% of landed hits crit, against 89% predicted and
76% for the old server. Scenarios 5-6 (monster AI, 20 kills): Goblin Sword's self-cast
Flame Hawk 1.55 casts/fight vs 1.71 predicted, 584 vs 582 damage; Elder Sikuku's 3056 415
vs 412 damage, silence landed 40% vs 38%; monster seconds-per-swing (casts and stops
included) within 0.15 s. Scenarios 7-9 (player summons, 25 kills: 1 Hawk, 2 Mercenary
Warriors, 3 Phantom Swords): summon swings/fight, land rate, damage spread and the
monster's swings at them all within noise (Hawk 128 vs 130, Warrior 87 vs 87, Sword 16 vs
16 per hit). Known gap: at the pull a summon can land the first hit and draw the monster
for a swing (~1/fight); the sim starts with the monster on the player.
The GUI runs the engine in `combatsim/worker.py` (a process, not a thread: pure Python
holds the GIL); `Report.summary()` is what crosses the pipe.

### Monster Balance And The Level Gate

Combat math lives in `src/common/calculation.cpp` (server-only, behind `#ifdef __SERVER`). Two properties dominate how hard content feels, and neither is obvious from the numbers in `LIST_NPC.STB`:

- **The normal-attack gate is level-proportional.** `Get_SuccessRATE` discards an attack outright when `(player_lv + 10) - monster_lv * kLevelGateScale + rand(1..50)` is non-positive. Because the monster term is *scaled*, the required level surplus grows with absolute level — at the original 1.1 it was ~2-3 levels in Luna, 6-10 across Eldeon, and 20+ in Oro, where it exceeded our own 240 character cap (Gates of Muris wanted 243; the level-240 bosses wanted 254). It is now **1.05**. Anything that reads as "I can't hit this" is usually this gate, not accuracy — check the level difference before touching HIT/AVOID.
- **Skills deliberately use a gentler, non-proportional gate** (`Get_SkillDAMAGE`): weapon `lv + 20 - mlv`, magic `lv + 30 - mlv`. A level-140 raider lands 0% of auto-attacks on a level-173 monster but ~66% of skills. Casters therefore stay effective at a level deficit where auto-attacks stop connecting entirely — that asymmetry is intended, keep it.

Also worth knowing: **damage is proportional to `(ATK - DEF + 250)`**, so a monster whose DEF approaches the player's ATK sits on a damage floor — the fight gets long *and* reads as though the hits do nothing, and small gear changes swing the result wildly because the two terms nearly cancel. And `Get_DropITEM` returns false once `player_lv - monster_lv >= 10`, so lowering a monster's level to make it easier can silently kill its drops.

**Status 2026-10-15:** five of the passes below already failed their own `--verify` before the role layer existed (Karkia 35/45, Oro 667 67/67, endgame DEF/RES 133 and 109 rows, Oro bosses, Eldeon 1571) -- a trend shift from later imports; the user considers these passes outdated (made without proper tooling). Treat them as history, not as a checklist to re-verify.

Idempotent balance passes correct the data, each with a sidecar next to the STB and `--dry-run` / `--verify` / `--restore`. **Re-running them has order dependencies**: the DEF/RES pass looks up the trend at a monster's *current* level and consults the boss sidecars to identify bosses, so if a level or the boss set changes, restore and re-apply DEF/RES afterwards; `rebalance-oro-bosses.py` changes HP that `rebalance-exp-rewards.py` keys on, so **bosses before EXP**. Then re-verify **every** pass, not just the one you touched: the monster passes (Karkia, Oro 667, endgame curve, Oro bosses, Eldeon outliers) recompute their targets from a trend fitted on the live table's level 60-199 rows, and the EXP pass its tier medians the same way. The Karkia and Eldeon passes also exclude Oro's id band 2100-2399 (the Shadow Ghost ladder sits at 183-219), and the Karkia pass 3001-3003 (event summons with placeholder stats); the endgame-curve and Oro-boss fits have always included the Oro band. Any future import into that window goes into `balance-trend-exclude.py` (one list every pass reads), or every verify drifts by a few points.

- `scripts/rebalance-monster-roles.py` — **monster identity by icon, applied last as a layer** (2026-10-15). Per icon (col 27) a factor moves each stat's median onto a target multiple of the level trend (Guard a wall: HP/DEF 1.2, ATK 0.9; Warrior ATK 1.15, HP/DEF 0.9; Magician RES 1.4, DEF 0.8; stars 1.5 / 2.2 / 4.0x HP, the gold star being two-player content); monsters keep their quirks; Basic's extremes tidied into 0.67-1.5x the trend; kings, skulls and trend-excluded rows untouched. Monster damage grows much faster than ATK, so the ATK gaps are narrow (sim: Guard 0.71x damage taken, Warrior 1.62x). Clan-field bosses (Junon Guardians, Astarot, Astarot King) are clan content: HP x2.5, ATK x1.25. The Luna Clan Field's regulars are two-player content (HP x4.5, ATK x1.6 on their role). **Kings and skulls** (`BOSS_HP`, frozen per boss from the sim): early kings below 80 and kings for three players (~45 s / ~90 s fights), skulls for five (~3 min) -- scaled back on 2026-10-16 from five / ten (the alpha pool is 5-6 players); HP only, normal attacks unchanged. Four field elites that wore a skull are silver stars (`fix-monster-marks.py`). The Desert of the Dead's four bosses (PvPvE) are left for later. Bosses that never cast a skill get ATK x1.15 (`NOSKILL_BOSSES`). **The passes below read LIST_NPC without the layer** (`balance-trend-exclude.without_roles`) and refuse to write while it is applied: restore the roles, run the pass, re-apply. EXP stays priced on pre-role HP. Manifest in `build/monster-roles/`.
- `scripts/boost-boss-skills.py` — a boss skill that hits **weaker than the boss's own swing** and carries no debuff gets a boss-only stronger copy (2026-10-15). Only #3042 qualified (area magic, power 90, cast by 31 monsters): it is copied to a new LIST_SKILL row (7024) at power 240 and the Crowned Asper King's and Giant Fiery Snapper's own AI files cast the copy (sim: 0.6x -> 1.7x / 1.6x their swing); the other 29 casters keep #3042. Debuff skills (Deader Rex's Toxic Rain, Silencing Wave) are fine weaker. Restore blanks the row and puts the two AI files back (`build/boss-skills/`).
- `scripts/fix-boss-cast-cooldowns.py` — **boss skills that locked after a cast** (2026-10-15). RoseZA-lineage AI files gate casts on an AI variable (`var0 <= 10`, +14 per cast) that only the STOP pattern lowers -- the stop pattern runs while the monster idles, so in a fight the counter never comes back: one cast, then nothing. Adds one first event to the DAMAGED pattern (`combat cooldown`: 7% -> var0 -4, or -1 for +1/+2 counters) in the Giant Fiery Snapper, Crowned Asper King, Grand Master Devourer, Wise Clown and Ikaness Mecha/Heavygear files. Left alone as intended charges: the Kera / Ulverick / Hebarn family's Dispel Buffs + Area Slow (5 per fight) and low-HP self skill (4); their Kera Stun Blast already resets in combat. The same lock sat in 30 regular monsters' AI files (the Oro Asper/Snapper families, Sikuku fighters and trackers, Hooks, Yeti Riders, Beeberu, clan-field Tirwin 397, the Karkia ents): fixed too. Kept by design: the Sikuku prison family's once-per-fight self-buff (Prison Slumber, the Cannibal buff: the counter is *set* to 1, the cast needs < 1) and the Infiltrator's 5 Dispel/Slow charges. Restore removes the event by name.
- `scripts/name-monster-skills.py` — **readable English labels for monster skills, for our tools** (2026-10-16). Tools show LIST_SKILL_S.STL when a row has an entry and col 0 otherwise; monster skills have no name-table entry, and of the 247 rows monster AIs cast, 140 had a blank col 0 ("#3042") and 49 a Korean template label shared by dozens of skills (셀프지속형 "self buff"). Writes col 0 as `<what it does>[: <LIST_STATUS names>] (<casters>)` -- e.g. "Area Blast (Asper, Asper Assassin, Asper Commander ...)" -- and translates the five Korean descriptions in the last column. English labels (55) and player skills untouched. **Players never see col 0**: the client names skills only from the name table (`CStringManager::GetSkillName`: LIST_SKILL col 86 key -> LIST_SKILL_S.STL; empty otherwise). Cell-level undo in `build/monster-skill-names/`.
- `scripts/name-monster-skills-players.py` — **player-facing names for the 243 skills only monsters cast** (2026-10-16): col 86 = `LMobSkill<row>` and a name-table entry per row (the same English name in all five language slots, translated old Korean descriptions as descriptions). Names chosen per skill ("Searing Burst", "Battle Fury", "Rock Throw"); good English names kept (Kera Stun Blast). Data- and display-only: the server never reads the name table, and col 86 is read by nothing else. The combat sim shows both: `Searing Burst [Area Blast (Asper, ...)]`. Restore puts col 86 back and drops the LMobSkill entries.
- `scripts/rebalance-oro-667.py` — the 667-build Oro (see [doc/zones/oro.md](../doc/zones/oro.md)): level bands per zone, HP/ATK/HIT/AVOID onto the sub-200 trend with the zone spread square-rooted and capped, bosses by hand, the Shadow Ghost ladder. Writes `LIST_NPC.oro667-bosses.json`. Replaced `rebalance-oro-accuracy.py`, whose factor was fitted to RoseZA's scale.
- `scripts/rebalance-karkia.py` — Karkia levels/HP/ATK + `karkia-bosses.json`.
- `scripts/rebalance-endgame-curve.py --stat def|res` — caps level-200+ DEF/RES at the trend fitted from levels 60-199. Bosses (three sidecars) get `BOSS_MULTIPLIER` (1.2).
- `scripts/rebalance-oro-bosses.py` — boss HP to 10x the HP trend and level to the 240 cap, scoped from the Oro REGEN lumps by HP column. Since the Oro pass budgets its own bosses under that threshold it now only reaches `EXTRA_BOSS_ROWS` (Luna's Behemoth King).
- `scripts/rebalance-eldeon-outliers.py` — two hand-picked monsters that escaped the passes above through scope gaps.
- `scripts/rebalance-exp-rewards.py` — the EXP column from the levelling pace.

Their docstrings carry the reasoning and record what was deliberately left alone (Luna's Astarot King and Gem quartet, the big-HP piñata field monsters, unspawned duplicate rows with billion-HP columns). Since `data/` is gitignored, **the script is the only committed record of the change** — put new reasoning there, not just in a commit message.

### Coplanar Placements Flicker; Depth Precision Cannot Save Them (Data)

A wall that "flashes" while the camera moves and settles a few seconds after it
stops is two **placements** of one map object with faces in exactly the same plane.
Junon Polis tiles its fountain-square wall with one 20 m segment (DECO 161
`portstairwall01`) at 10-19 m strides, so consecutive copies overlap by 0.6-9.8 m at
0.000 cm separation; the visible shape is the overlap region (a 61 cm stride
remainder reads as a thin line, a 6.35 m one as a big square), and it shows only
where the copies' per-placement lightmap cells disagree, since the mesh and texture
are otherwise identical. It reproduces in every ROSE client because the data is the
same, and the 24-bit depth buffer cannot touch it: the separation is zero, and a
depth bias has no way to tell the two apart. The "few seconds" is
`zz_camera_follow::interpolate_camera`, which approaches its target exponentially
and keeps creeping by sub-pixel amounts after the mouse is released; a bit-stable
camera gives a stable (if arbitrary) winner per pixel.

`scripts/fix-coplanar-object-overlaps.py` (2026-09-22; `--dry-run`/`--verify`/
`--restore`, manifest in `build/coplanar-overlaps/`) scans every zone's IFOs against
real ZMS geometry and moves one placement of each fighting pair by the smallest
multiple of 0.5 cm along a direction chosen per node, so every pair ends >= 1 cm
apart, then re-checks the moved geometry with the client's own float32 rounding
before writing. It rewrites only the twelve position bytes of a record. Across 60
zone rows it found 612 fighting pairs in 25 map folders (Union War, Golden
Colosseum, Junon Polis, Sikuku Prison, Junon Cartel and Forgotten Temple B1 carry
almost all; field maps are clean) and moved 594 records, most by 1.5-2 cm, none by
more than 7.5 cm. Applied 2026-09-22, validated in game. Run it after any
`import-*.py` that brings in map files, then bake.

Things that will bite:

- **Never delete a duplicate record.** `CMAP::LoadLightMapINFO` keys objects by
  1-based ordinal within the lump, so removing a record shifts every lightmap after
  it. An exact duplicate (same object, transform and position) is coplanar on
  every face and cannot be separated by any nudge; the script **sinks** the later
  record 100 m instead.
- **A record whose pivot lies outside its own chunk is never drawn.** `CMAP::AddObject`
  deletes it at load. A building written into two neighbouring chunk files is
  therefore not a duplicate at all, because only the copy in the pivot's chunk exists
  in game. The script once sank that copy (Junon Polis DECO 153, 36_32), which left
  an invisible wall with only its baked shadow on the ground. It now skips placements
  the client drops (`loaded_by_client`).
- **Back-to-back faces are not a fight.** Map materials default to
  `ZZ_CULLMODE_CW`, so one placement's top lying on another's bottom never draws
  against it; the detector keeps the facing sign (dropping it doubled Union War's
  count and pushed a stone wall 6 cm into the ground to dodge a phantom).
- **Near pairs are constraints.** Two copies 1-13 cm apart are not fighting, but a
  naive move of a neighbour makes them so; the planner carries signed per-plane
  separations for every pair inside that band and plans against 1.2 cm to absorb
  the rounding of normals and of float32 positions (an ulp is 1/16 cm at world
  scale), which is what left 0.86-0.92 cm gaps on the first attempt.
- Same-placement part overlaps (`road01`/`road01top`) are out of scope: they are a
  ZSC matter, already non-zero, and covered by the depth-buffer work (src/engine/CLAUDE.md, "Depth Buffer Precision").

## Lessons From Zone Imports

General traps found while importing a particular zone; the zone histories are in `doc/zones/`.

- **667's `NPC_TYPE` codes are composite** — 42 Leader-Guard, 53, 72 King-Guard, 75 King-Ranger … — and `CNameBox::DrawTargetMark` uses that column *directly* as a sprite index into `TARGETMARK.TSI` (36 sprites), so those monsters showed no mark beside the focus bar. The importer folds kings (7x) to the crown (11) and elites (8x) to the skull (16), and the other composites (leader/captain/sub-boss) to the units digit (the role); the server only compares the column against 900. Bare-handed monsters present their hit through `NPC_HAND_HIT_EFFECT` (game col 33, a `LIST_EFFECT` row): 667 left it blank on the Golden Scarabs and the Hungry Desert Scavenger, which landed no visible impact; 403 is the retail default and the importer fills it.

- **Walk the summon closure at import time.** Nineteen Oro monsters `SummonMaster` Shadow Ghosts 2236/2237 and Ghost Seed's `ghost_m5.aip` `Change_CHAR`s the whole 2230-2239 ladder; none is in any spawn lump and all were blank here (the crash class from `audit-ai-monster-refs.py`). The importer walks `act_monster` ids transitively from the roster and imports whatever a dump has. Scope a monster as blank by **name and model**, not `occupied()`: rows 6/7 carried stray cells and were skipped by the first dry-run.

- **Trigger names are global across every QSD.** Wedgy's exits fire `gotoJunon`,
  which our TUTORIAL.QSD already defines. The fix is not a bytecode patch: the QEX1
  appendix runs after the main blob in the same `lua_State`, so redefining
  `AT_gotoJunon` / `AT_gotoGrassland` / `TA_gotoJunon` there wins. The same
  appendix defines `TA_Hidden` (returns 0), which four option nodes are re-pointed
  at because their Lua reaches functions our client never registered
  (`GF_openDeliveryStore`, `GF_openSpotBank`, `GF_PetDepositOpen`, `GF_IsWorldName`)
  -- a missing Lua function pops an ErrorBOX. Check-function fields are patched in
  place, the way `unlock-karkia-idle-dialog.py` does it.

- **Every Jrose check is rewritten, not trusted.** Each node with a check function
  becomes `TA_Hidden` except the one branch per NPC in `SHOW`, and every `AT_*`
  click the Lua names is redefined in the QEX1 appendix (services get our bodies,
  the rest do nothing). Same reason as Skaaj: some Jrose checks are inverted.
- **A MOB placement's `.CON` name has no extension in the source** (`EM02-089`);
  the gameserver compares it to the LIST_EVENT basename exactly, so it is
  rewritten to `EM02-089.con` or the NPC loads with no dialog.

- **AICOND17 ("select NPC N") is a server-wide lookup** (`g_pZoneLIST->Get_LocalNPC`),
  so an instance's AI keeps reaching its controller NPC wherever she stands: the
  King's idle event selected Lithia (1156, placed in JD04) and **killed itself**
  whenever her slot variable was 0, i.e. always without the instance. Every event
  gated on condition 17 is cut; they were all instance bookkeeping.

- **A warp object must sit within a few metres of the ground**: the first import put
  the entrance gate at RoseZA's event-box depth, 15 m under the desert, and nobody
  could get in. Retail gates sit 0.5-3 m below the surface.
- **A failing quest trigger is silent** (client re-check, server reply alike), so any
  priced warp needs a dialog that says "you cannot pay" -- `con-toll`.

- New monsters in levels 60-199 go into `balance-trend-exclude.py`, and
  `rebalance-eldeon-outliers.py` now honours it too (531/534 moved its DEF fit
  by one point on 1571). **Pricing new spawns' EXP is `rebalance-exp-rewards.py
  --restore` then a plain run** (EXP is no input to its plan, so every other row comes
  back identical); a re-run of stage 2 keeps whatever EXP that pass wrote.

Monster AI patterns run at different times, and a cast placed in the wrong one never
fires or fires badly: "attack move" runs only while the monster **chases** (never while
it stands and swings), "damaged" on a share of the hits it takes. Gate every cast on the
target already being in the skill's range -- a cast ordered from further away makes the
server chase first, and the client abandons a remote cast that has not started within
3 s, so its damage and status land with no animation. A cast's status shows 1-3 s after
the server applies it (cast clip + release hit frame): size stun durations for that, or
speed the clips up (LIST_SKILL cols 53/69 apply to monster casts on both sides).

**Before adding a monster, read [doc/making-monsters.md](../doc/making-monsters.md)**: the
row, model, skill, animation-event and AI traps in one checklist. **Importing one from
another dump** follows [doc/importing-monsters.md](../doc/importing-monsters.md): survey,
rows, model, weapon, stats, the three skill variants (telegraphed / legacy / none), AI,
deploy; `import-shadow-soldier.py` is the template. Start with `survey-monster.py <dump> <row>`
(read-only, ~10 s): names, siblings, free rows, anim slots with events and the skills using
each clip, motion profiles, the weapon against ours, and the source's AI when it ships
(**Jrose ships ~1,200 `.aip` files**, tsuki none; taking one or authoring ours is the user's call per monster).

**Monster speech (AI action 28) is the server's** (2026-10-09): the overhead bubble is a
local chat the server sends for every line (`Recv_gsv_CHAT` shows a monster's as a red
bubble only). The client used to raise it from its own copy of the AI, whose conditions
past 26 (monster variables included) are stubs returning true and whose dice are not the
server's -- a line gated on a variable fired on almost every hit. Casting effects (LIST_SKILL
cols 56-67) play only on clip event frames 44/64/74/84; most monster clips have none, so a
spell that hits nobody shows nothing unless the caster gets clip copies with them (the
Warden of the Seal's `SEAL_CLIPS` in `import-cerberus.py`).

- **A part added to a map object must be a child of its root part.** The client applies a
  placement's rotation and scale to the model's root part only (`CObjFIXED` on
  `m_nRootPART`), and `CMODEL::Load` takes the **last** parentless part as that root: a second
  parentless part steals the rotation and every other part stands unrotated. The map editor
  transforms every part, so its renders look right (Shadow Canyon's bridge stitches,
  2026-10-24: `import-valley002.py` `child_props`). Also from that map: the client's ground ray
  falls through plank joints a few cm wide, and its slope rule applies whenever the foot is
  within 15 cm of the terrain (`AVATAR_FOOT_RADIUS`), even on an object floor.
- **A map whose floors are objects over flat terrain must be walked, not just seam-checked**
  (the Mysterious Cavern, 2026-10-24: every HIM at 0 m, floors at 7-31 m). Wherever a floor
  stops short of its wall you fall to the hidden terrain and stay under the map; per-mesh and
  cross-mesh hole checks missed all of it. `import-valley002.py`'s `Walker` floods a 50 cm grid
  from the arrival point with a body-wide ray test (one ray slips through wall-mesh cracks) and
  found 105 void steps; `void_fill` closes bands that end at a wall with invisible floor and
  open ones (a cave mouth) with an invisible collision-12 wall. Reuse it for the next such map.
- `quest-editor con-toll` gained `--open-when N --closed T`: an opening-hours gate on
  the NPC's event value 0, read on the client with
  `QF_getNpcQuestZeroVal(QF_getEventOwner(E))` because COND_011 always passes there.
