# Importing Back Items From Jrose

**Status:** **131 back items imported** — IDs **957–1027** (batches 1–4, confirmed in game),
**1028–1061** (batch 5, awaiting an in-game look) and **1062–1082** (batch 6, user-picked,
the first back items above level 155: 14 of them fill levels 160–240 — stat method in
[jrose-back-batch6.txt](jrose-back-batch6.txt); awaiting an in-game look). Every new wing
*model* Jrose had is in. Model-carried effects (wing trails) implemented and confirmed.
**1083–1087** (batch 7): the five particle-only backs, whose look is a Jrose
`LIST_WEAPONEFFECT` placement (col 51) rather than a model — imported with
`import-item.py --attach-effect-row` as a ZSC dummy point of type `POINT_EFFECT_ORIENTED`
(3), which the client places with rotation and scale; column meanings were read out of
`TRose.exe` (see [jrose-back-batch7.txt](jrose-back-batch7.txt)). Still out: Jrose's
mantles (cloth system, 3-vertex dummy mesh).
**Date:** 2026-09-04, batches 3-4 2026-09-05, batch 5 2026-09-06, batches 6-7 2026-10-05.
**Source:** `C:\Users\Thomas\Desktop\Testclients\Jrose` (loose `3Ddata\`).
**Prerequisite reading:** [doc/jrose-survey.md](jrose-survey.md) for the dump as a whole.

**Scope decision:** we take the **art only**. Jrose's stats, affixes and item
semantics are not wanted — we re-author those ourselves. That is not just taste: it
also deletes the one genuinely dangerous finding here (§4.3) by construction, because
no source cell is ever read.

---

## 1. Verdict up front

Yes, and **back items are the easiest import target in the entire dump** — easier than
weapons, far easier than armour. Three structural facts do all the work:

| | |
|---|---|
| `LIST_BACK` columns 0–33 | semantically **identical** to ours, in the same order |
| Back meshes | **rigid, zero-bone** on both sides — no skeleton dependency at all |
| Attachment point | hardcoded in *our client*, not in the data |
| Sex-split | **none** — `LIST_BACK` is one table shared by both sexes |

`scripts/import-item.py --type back` already exists and already understands this table.
It got all the way through the STB column check, both ZSC object-count checks, the model
append, the asset list and the field-drop model against a live Jrose row. One line stopped
it (§4.1), and one latent bug in it crashed the client on load (§4.5).

Their variety is real: **2,864 occupied back rows to our 144** (19.9x, the largest ratio of
any table in the dump), across **194 distinct models whose art we do not hold** — every
one of them present on disk as loose `.ZMS` + `.DDS`.

---

## 2. Why this table is structurally safe

Everything below was verified against the real files, not assumed from the survey.

### 2.1 The columns line up, and the STL key is where the tool expects

Ours is 35 data columns with the STL key last (col 34). Jrose is 53 with the key last
(col 52). Columns 0–33 match one-for-one — `防御力`/Defense at 31, `抗魔力`/Magic
Resistance at 32, icon at 9, field model at 10, and so on. Jrose's col 34 is
`名前仕分け` (a name-sort field), *not* the key.

`import-item.py` builds `row = src[0:ocols-2] + [new_key]`, i.e. source cols 0–33 plus our
own key at 34. That is exactly right for this table by construction, not by luck.

### 2.2 Back meshes are not skinned, so the skeleton is irrelevant

Every mesh in both `LIST_BACK.ZSC` files reports a **zero-length bone table** — 153 of
ours, all 1,769 of theirs. Control check: body armour meshes from `LIST_MBODY.ZSC` report
8–17 bones with max index up to 20, so the parser is reading the field correctly. Back
meshes carry vertex format `134`; skinned body meshes carry `182`.

This is the single biggest risk that *isn't* here. A skinned import would need Jrose's
avatar skeleton to match ours bone-for-bone; a rigid one needs nothing.

### 2.3 The attach point is in our code, not in their data

`src/client/io_basic.cpp:69` loads the table with an explicit default:

```cpp
m_pMD_CharPARTS[0][BODY_PART_KNAPSACK]->Load("3Ddata\\avatar\\LIST_BACK.ZSC",
    -1, DUMMY_IDX_BACK);
```

`CCharPART::Load` (`src/client/io_model.cpp`) only reads tags 5 (bone) and 6 (dummy) for
avatar parts and skips everything else, then falls back to those defaults when the part
leaves them at -1. **All 150** of our parts write -1/-1, and the overwhelming majority of
Jrose's parts carry an *empty* property blob — both land on the caller's default. The 5
Jrose parts that do set a dummy index explicitly set **3**, and
`DUMMY_IDX_BACK == 3` in `src/common/shared/datatype.h:167`. Exact agreement.

Neither side uses mesh-animation tags (8–28), so no `.ZMO` motion files are implied — which
matters because `copy_assets` only ports meshes and material textures.

### 2.4 Everything else already matches

- **Textures** are DXT1, DXT5 and uncompressed RGB32 — all D3D9-native, nothing modern.
- **ZMS versions** are ZMS0007/ZMS0008 on both sides, the same mix we already ship.
- **ZSC material blocks** are the same 36-byte layout.
- **Field-drop models** port cleanly — row 1005's drop model resolved to their object 248
  (`di53.zms` + `di52.dds`, both present) and appended as our 1042.
- **ID headroom** is fine: our next back ID is 957 against the 11-bit limit of 2047.

---

## 3. What they have that we don't

194 distinct models, all with art on disk. A sample of what is in there:

| Kind | Examples |
|---|---|
| Wings | Phoenix, Archangel/Fallen, Metatron, Red Dragon, Dragonic, Wings of Light, Bee, Insect, Clockwork/mecha, Frozen |
| Back-worn gear | Quiver, Buster Blade, Zweihänder, Grimoire, Scroll, Powered-suit wings |
| Capes / mantles | Wanderer, Barca, White Wolf, Supreme, Vampire |
| Novelty packs | ~40 animal rucksacks (bear, rabbit, cat, elephant, penguin, panda, fox, wolf, owl, lion, gorilla…) |
| Props | Electric guitar, drum kit, parasol, shaved ice, beer stein, koinobori, teru-teru bozu |

Of the 194, **165 have at least one row that imports with no stat sanitising at all**; the
other 29 need an override on every row (§4.3).

---

## 4. What had to be fixed

All done. §4.1 and §4.3 were solved by the same change — a new `--art-only` mode —
rather than separately. §4.5 is the one that got through and crashed the client.

### 4.0 `--art-only`: take the model, author the row

`scripts/import-item.py --art-only --template-row N` takes the ZSC object, its
meshes/textures and (optionally) the icon and ground-drop model, then clones **every
stat column from one of our own rows**. Nothing is read from the source row.

That is the right default for any unfamiliar dump, not just this one:

- A foreign schema cannot misalign, because no source cell is read. The column-
  alignment check is skipped entirely in this mode.
- A foreign ability id cannot reach the unbounded array write in §4.3.
- The source STL is never opened, which is what made §4.1 moot.
- The new row can only hold values this build already handles, since they came from
  a row the game is already running.

It requires `--name` (there is no source text to fall back on) and `--template-row`
(there is no sensible default — the template picks the item's stat neighbourhood).

### 4.1 FIXED — `import-item.py` could not read a legacy source STL

```
AssertionError: b'I_NUM'
```

`stl_read` asserts `ITST01`. Jrose writes the older `I_NUM` dialect (survey §2.1). The read
happens unconditionally, before `--name` is consulted, so it fires even when you intend to
supply your own text.

Sidestepped rather than fixed: `--art-only` skips the source STL entirely, since the
name and description are ours to write anyway. `stl_read` stays strict for **our** file,
which is the one place it matters. If a future import does want source text, teach it the
legacy dialect via `scripts/rose-data-reader.py`, which already parses both.

### 4.2 FIXED — our own `icon51.dds` had grown a mip chain

`--copy-icon` correctly located and cropped the source sprite (index 4517 →
`icon27.dds` at (240,360)) and then died in **our** atlas:

```
AssertionError: data\3DDATA\CONTROL\RES\icon51.dds is not one of our uncompressed extension sheets
```

`icon51.dds` is 1,398,228 bytes where an uncompressed 512×512 BGRA sheet is 1,048,704.
The difference is exactly a full mip chain — header says `mipcount 10`, flags carry
`DDSD_MIPMAPCOUNT` — and `dds_read_bgra`'s strict size check rejected it.

This was **pre-existing and unrelated to Jrose**: `add_icon` fills the last extension sheet
before starting a new one, so *any* new item icon was blocked, from a PNG or from a source
atlas.

**What put the chain there is `scripts/add-dds-mipmaps.py`** — the sweep that gives every
mip-less DDS under `data/` a real chain, because `D3DXCreateTextureFromFileInMemoryEx`
otherwise *builds* one at load time, which is the expensive path (196 of 196 slow texture
creates in a captured session had `src_mips=1`). So the two scripts are in a loop:

| | writes | leaves |
|---|---|---|
| `add-item-icon.py` | one sprite into a sheet | a plain DDS, **no mips** |
| `add-dds-mipmaps.py` | mips onto anything lacking them | a sheet the icon tool must still read |

Before the fix that loop was broken in one direction: once the sweep had run, the icon tool
could never open the sheet again. `dds_read_bgra` now validates the pixel format out of the
**header** (512×512, 32bpp, `RGB|ALPHAPIXELS`, no fourcc) and reads only the top mip,
ignoring whatever trails it — so the two compose in either order. Verified by running the
sweep and re-reading sprite 8594 out of the re-mipped sheet.

`dds_write` deliberately still emits no chain. That is correct **because** the sweep exists
to add it: the icon tool should not duplicate that logic, and a stale chain written
alongside a freshly-pasted sprite would be wrong anyway. The consequence is simply that
**`add-dds-mipmaps.py` must be run after any icon add**, before a bake.

Worth knowing: the 50 original sheets are DXT5 with 10 mips; our extension sheets are
uncompressed 32-bit, and the sweep keeps each file's own format. `bake_dds` in the pipeline
forces DXT5 and is *not* involved.

### 4.3 The one that would actually hurt: out-of-range ability ids

This is the `reference_stb_columns_as_array_indices` trap, live in this data.

Item bonus columns 24/27 are ability ids. Both sides do this with **no bounds check**:

```cpp
nType = ITEM_ADD_DATA_TYPE(nItemTYPE, sITEM.m_nItemNo, nI);
this->m_iAddValue[nType] += nValue;
```

— `src/client/common/cuserdata.cpp:159` and `src/sho_gameserver/src/common/cuserdata.cpp:159`.
`m_iAddValue` is `int[AT_MAX]` (~105–109), and in `CUserDATA` it is followed immediately by
`m_nPassiveRate[AT_MAX]`, `m_btRecoverHP`, `m_btRecoverMP`, `m_iDropRATE`
(`src/common/shared/cuserdata.h:655`). The gem path has an `_ASSERT`, compiled out in
release; the item path has nothing.

Jrose's back table uses the modern affix vocabulary from survey §5 — **174** (39 rows),
**175** (64), **184** (14), **185** (13), **195** (14). Importing one of those verbatim
writes 65–90 ints past the end of the array, silently corrupting adjacent character state
on both client and server. It is not a crash you would trace back to the item.

Caught while shortlisting: row 1548 "Dragonic Wing (Red)" carries `185:1`.

**Solved by `--art-only` (§4.0), which never reads those columns.** No clamp was added to
the stat-copying path, so that path is still only safe against a dump whose ability ids are
in our range — if you ever copy stats from Jrose, add the guard first.

### 4.4 Smaller data traps

- **Item type (col 4).** Ours is 160–163. Jrose adds `1000161`, `1000163`, `50161`,
  `30161` (80 rows). `ITEM_TYPE` feeds `CStringManager::GetItemType`, a string-keyed STL
  lookup, so a stray value shows a blank type in the tooltip rather than crashing — but
  clamp it anyway.
- **Requirement ids (cols 19/21).** 66 rows require stat **168**, outside our range.
- **Disabled rows.** A required level of 1254/1259 is Jrose's take-it-out-of-circulation
  trick, not a real requirement (survey §7). Several attractive rows are disabled this way.
- **Names must come from `--name`.** Without it, `row[0]` keeps raw cp932 bytes and the
  *server* reports mojibake (the client reads the STL, the server reads col 0 — override
  both, which `--name` already does).
- **12 meshes are sex-split** (`*_m_*` with an `*_f_*` twin on disk). Since our
  `LIST_BACK` is shared between sexes, importing one puts the male mesh on female avatars.
  Avoid these until someone decides how to handle it:
  `hirosuit_m_scalf_0830`, `mouse_back_m_1226`, `kirisuna_back_m_0917`,
  `avatar_back_m_0521`, `browndust_back_m_1029`, `kitsune_back_m_white_0715`,
  `metalequipment_back_m_0804`, `clothequipment_back_m_0804`, `kame_back_m_20230518`,
  `leatherequipment_back_m_0804`, `barcamant_m_1225_cr`, `summer_back_m_20220818`.

---

### 4.5 FIXED — a copied dummy-point effect index took the client down at the title screen

The first attempt at these five **hard-crashed the client**, with nothing in `error.txt`:
the engine log is buffered, so the session was lost and the file still ended at the
previous run's `log: end.`. `client.log` survived (it flushes per record) and showed
startup reaching RmlUi init and stopping, which put the fault in the phase right after it.
`scripts/debug-client-crash.ps1` named it exactly:

```
rosenext!CMODEL<CCharPART>::Load+0x218        IO_Model.h:187
rosenext!CModelDATA<CCharPART>::Load+0x3dc
rosenext!CBasicDATA::Load3DDATA+0x21a
rosenext!CGame::Load_BasicDATA+0xa7
```

Line 187 is `m_pDummyPoints[nP].m_uiEftKEY = (nListIDX >= 0) ? pEftKEY[nListIDX] : 0;`

`zsc_build_append` remapped mesh and material indices but copied **dummy points verbatim**,
effect index included — so a source-table index was written into ours. `LIST_BACK.ZSC`
declares **no effects at all**, which makes `pEftKEY` NULL, and the Phoenix object's dummy
referenced its source table's effect 0. Null read, before the title screen.

Latent from the day the tool was written, not new. It only fires when a source object has a
dummy point carrying an effect, and nothing imported before this had one — only 2 of
Jrose's 5,001 back objects do.

Fixed two ways: the index is remapped (reuse our entry when the path matches, else drop the
dummy, or port it with `--copy-effects` per §5a), **and** a post-write check now scans the
whole table for any index out of range for the list it points into — meshes, materials and
dummy effects alike. The client bounds-checks none of the three, so each is a crash rather
than an error.

**The general rule this leaves:** never emit an index into a list the importer did not also
write. The mesh and material paths got that right from the start; the effect path did not,
and nothing caught it because the crash was three subsystems away from the cause.

---

## 5. The five that were imported

Chosen to cover both single- and multi-part objects, DXT1 and DXT5, and both wing and
non-wing silhouettes — so a first pass exercises the whole path rather than five variations
on one case. Stats are **ours**, sized against the neighbouring items in our own curve
(Astarot Wing lv125 is 16/20; Hook Wing lv145 is 19/24), not Jrose's.

| Our ID | Name | Jrose row | Template row | Parts | DEF / RES | Req lv | Icon |
|---|---|---|---|---|---|---|---|
| 957 | Phoenix Wings | 1005 フェニックスウィング | 227 Angel Wing | 2 | 20 / 26 | 130 | 8594 |
| 958 | Wings of Light | 953 光の翼 | 227 Angel Wing | 1 | 18 / 24 | 120 | 8595 |
| 959 | Clockwork Wings (Brass) | 1230 機械仕掛けの羽(真鍮) | 891 Ancient Backshield | 2 | 30 / 18 | 135 | 8596 |
| 960 | Hunter's Quiver | 1200 矢筒 | 245 Lion Backshield | 1 | 14 / 6 | 60 | 8597 |
| 961 | Unknown Grimoire | 1179 未知の魔導書 | 212 Magic Cubic | 1 | 12 / 22 | 110 | 8598 |

Spawn with `/item 6:957` … `/item 6:961` (GM access level 2048).

Verified by re-reading the files from disk, not by trusting the importer: STB rows ==
ZSC objects (962 each), every referenced mesh and texture present, all five STL keys
resolving in **all five** language blocks, and each new icon carrying real art
(310–726 distinct colours, none blank).

`--copy-field-model` was deliberately **not** used. The template already supplies a
ground-drop model that is valid in our own ZSC, and porting Jrose's would have appended
duplicate objects for a mesh we already hold. Two of the five have no source drop model
at all (index 0) — `--copy-field-model` now degrades to the template's instead of aborting.

Also clean and worth having next: 1060 レッドドラゴンウィング (Red Dragon Wings), 1008
デモン ジブリールウィング (Fallen Gabriel Wings), 1170 バスターブレード (Buster Blade),
858 ゴールドセラフウィング (Gold Seraph Wings), 938 大烏の翼 (Great Crow Wings).

The command used:

```powershell
python scripts/import-item.py --type back `
    --source "C:\Users\Thomas\Desktop\Testclients\Jrose" --source-row 1005 `
    --art-only --template-row 227 `
    --name "Phoenix Wings" --desc "Wings wreathed in flame that never goes out." `
    --def 20 --res 26 --req-level 130 --copy-icon
```

---

## 5a. Model-carried effects (wing trails)

A ZSC dummy point holds an effect key. `CMODEL<CCharPART>::Load` has always read it into
`m_uiEftKEY`, and **nothing on the avatar side ever consumed it** — the only consumer in
the tree is `CObjFIXED` (world objects: braziers and the like). The character equivalent,
`CCharPartEffect::CreatePartEffect`, is commented out in its entirety. Retail never needed
it: `LIST_WEAPON.ZSC` carries 2,074 dummy points and exactly **one** effect, `LIST_SUBWPN`
66 dummy points and **none**.

Two things came out of that.

**It crashed the client** (§4.5), because the importer wrote a source-table effect index
into a table whose effect list is empty, and `pEftKEY[nListIDX]` is an unchecked read off a
null pointer.

**It is now implemented.** Almost nothing had to be built — `CObjCHAR::Add_EFFECT`,
`Del_EFFECT`, `Link_EFFECT` and the `m_pppEFFECT[part][point]` storage were all already
live, and `New_EFFECT` already runs for every part on equip. Back items just fell into
`default: return;`, since the switch only handles weapons (which take their effect from the
item tables). That default branch now spawns from the part model's own dummy points.

- Effects are registered with **`CBoneEffectBudget`** — they are passive, bone-attached,
  looping cosmetics, exactly what it was built for, so a crowd in the same wings degrades
  to a cheaper particle tier instead of multiplying emitters.
- `Del_EFFECT` now **unregisters**; the budget holds raw `CEffect*` and would otherwise
  keep a dangling record on every item swap. It is a no-op for unregistered effects, so
  the weapon path is untouched.
- `import-item.py --copy-effects` appends the effect to our list and copies the `.eft`
  plus everything it pulls in. **Without the flag the dummy point is still dropped** —
  never emit a dangling index.

The dependency walk matches the path shape anchored on the data root rather than searching
for an extension: these blobs store a bare filename and the full path back to back
(`flying_ef.ptl` then `3DDATA\EFFECT\PARTICLES\flying_ef.ptl`), so searching for `.ptl`
first lands on the name and silently misses the path behind it. `.ptl` files also write
their separators **doubled** (`3DData\\effect\\…`), which the walk collapses.

Phoenix Wings ships `flying_ef.eft` + `flying_ef.ptl`; `twinkle_03.dds` we already had.

### 4.6 FIXED — icons on the last row/column of an atlas sheet drew a torn strip

Reported in game on Vampire Mantle, Bee Wings and Megadrake Wings: about a fifth of the
icon replaced by a torn slice, to the right or downwards.

`ITEM1.TSI` lays each **512x512** sheet out as a **13x13 grid of 40px cells**, and
13 x 40 = **520**. The last column and last row hang **8px** off the texture; the engine's
default address mode is `ZZ_TADDRESS_WRAP`, so those 8px sample from the opposite edge.
8/40 is exactly the fifth that was reported.

**The geometry is retail's and retail is unaffected anyway** — every shipped sheet has the
same 25 bad cells and paints art into all of them, but of 1,953 retail icon indices
actually referenced by items, **zero** are bad cells. The defect has been dormant in the
shipped data since launch.

Our allocator woke it: `add_icon` filled cells densely from 0, so once a sheet passed cell
155 it started handing them out. **21 items** had landed there — 14 from batch 2 and 7
older RoseZA imports, which is why this had been seen before and never explained.

- `add-item-icon.py` now allocates only cells that fit — **144 per sheet, not 169** — and
  picks by which cells are *occupied* rather than by counting entries.
- `scripts/fix-icon-atlas-overrun.py` repairs what shipped: it relocates each over-hanging
  sprite and rewrites the entry in place, so **every sprite keeps its index and no STB
  column 9 changes**. That works because the client reads sprites in flat order (index =
  position, so a sprite must never change block) but resolves the texture from the
  per-sprite id, not the block (`io_imageres.cpp`). Retail's 1,250 dormant ones are left
  alone.
- Art on a bad cell was also physically short — `add_icon` pasted 40x40 at the origin and
  PIL clipped at the image bounds, so only 32x40 / 40x32 / 32x32 was ever written.
  Relocation moves what survives; the missing strip has to come from the source atlas.
  Batch 2's 14 were re-pasted from Jrose via the manifest.

**All 21 referenced icons now carry full-width art.** The 7 older ones had no recorded
source, so `scripts/find-icon-source.py` identified it by matching their surviving 32
columns against every `ITEM1.TSI` under the test-client directory. Our copy is the decoded
source pixels stored uncompressed, so a correct hit scores **MAD 0.000** and everything
else is far away — an identification, not a similarity search.

Searching every dump rather than the assumed one mattered: five were in RoseZA, but
**Akela Bowgun and Golden Angel Shield were in QQ-iROSE**, and a RoseZA-only search had
confidently offered wrong answers for them at MAD 7.4 and 29.4. Two further clipped
sprites match nothing anywhere (MAD ~9–11) and are referenced by no item, so they are left
alone — most likely added from a PNG rather than cropped from a dump.

---

### 4.7 A source model can be a placeholder — refuse it, and the candidate filter is too strict

Reported in game: **Wanderer's Mantle** (id 997) equipped and showed nothing.

Its model is Jrose's `mant_dammy_m.zms` — "dammy" is *dummy*. **3 vertices, bounding box
0.0014 units across, 150 bytes.** **76** of their back rows use it: Jrose draws mantles
through some other system (those textures live under `3Ddata/NPC/MANT/`, away from the
avatar art) and the back slot only holds a stand-in. The import was faithful; the source
asset is empty.

`import-item.py` now refuses a source object **whose every part is degenerate** (`≤8`
vertices *and* `<0.05` extent), with `--allow-placeholder` to override. Judged per object,
not per mesh — plenty of good models have a tiny second part (a strap, a charm), so a small
mesh on its own means nothing. A sweep of the whole Jrose back table finds exactly **two**
such objects; the other, `skeleton_back_0212_wa.zms` (4 verts, extent 0.0), was never
imported.

**Removing the item was the wrong repair.** 997 is not the last row, and item ids are baked
into inventories, banks, drop tables and shop stock, so dropping it would have renumbered
the 19 items above it — which had already been spawned and tested. `scripts/replace-item-model.py`
swaps the model **in place** instead: same id, same STB row, same STL key, so nothing moves.
Its ZSC writer is proved by `--selftest`, which re-serialises the whole file with no
substitution and asserts byte-identical output — objects are variable-length records, so a
subtly wrong writer would corrupt every object *after* the one it touched, and that reads as
unrelated items losing their models rather than as a bad write.

Id 997 is now **Cloak of the Defeated** (Jrose row 1510, 378 verts).

**The candidate filter is stricter than `--art-only` needs.** Finding a replacement turned up
two perfectly usable cloaks the scan had never offered — rows 1510 and 1572 — rejected for
`itemtype 1000161`, a disabled-row level of 1259, and ability id 195. **`--art-only` reads
none of those columns.** The filter's stat checks only matter for the stat-copying path, so
the "models remaining" figure is understated; when hunting for a specific look, search by
mesh and name rather than trusting the clean list.

---

## 6. Where this stands

Done: the tooling changes (§4.0–4.2), the five above imported and verified on disk,
`scripts/add-dds-mipmaps.py` run afterwards, the load crash fixed (§4.5), and
model-carried effects implemented (§5a). **Tested in game: all five look correct, and the
Phoenix Wings particle trail renders.**

**Always run the mip sweep after an import.** Five of the seven textures that came from
Jrose shipped with **no mip chain** (`houou_wing`, `houou_wing02`, `light_wing2`,
`yadutsu`, `magicbook`; only the Clockwork pair had one), against 9 levels on every one of
our existing back textures. A mip-less texture is not a cosmetic issue here — it forces
D3DX to build the chain at load, which is the documented cause of 20–140 ms texture-create
spikes. The sweep also had to put back the chain on `icon51.dds` that `--copy-icon`
stripped (§4.2). Seven files, 2.4 MB, verified for brightness as well as presence.

Still to do:

**Batch 2 (2026-09-04): 25 more as IDs 962–986**, manifest in
[doc/jrose-back-batch2.txt](jrose-back-batch2.txt) — that file is the only record of what
was imported and with what stats, since `data/` is gitignored, and it carries the replay
and rollback commands in its header.

Chosen for spread: 10 wings, 7 back-worn items, 3 mantles, 5 novelty pieces, across
required levels 30–150. Two things it deliberately exercised:

- **The `icon52.dds` transition.** `icon51` had exactly 20 free cells, so items 1–20 filled
  it to cell 168 (index 8618) and items 21–25 opened `icon52` at cell 0 (index 8619).
  `add_icon` starts the new sheet on its own; the TSI went 51 → 52 textures and 8,599 →
  8,624 sprites, and sprites either side of the boundary carry real art. **Ship
  `icon52.dds`** — it is a new file in `3DDATA/CONTROL/RES/`.
- **A second, richer effect.** Portable Fireworks (Jrose row 1243) carries
  `FireWork_Star02.eft`, which pulls in **4** particle files and **3** textures against
  Phoenix's single `.ptl`. The recursive walk resolved all of them. Those are the only two
  of Jrose's 5,001 back objects with a dummy point.

Verified as before: STB rows == ZSC objects (987 each), no dangling indices table-wide, no
missing assets, all 25 STL keys resolving in all five language blocks, and the client
reaching zone loading with no exception.

**Batch 3 (2026-09-05): 30 more as IDs 987–1016**, manifest in
[doc/jrose-back-batch3.txt](jrose-back-batch3.txt). One of the thirty shipped a placeholder
model and was repaired in place — see §4.7. Weighted toward what batches 1–2
skipped — 10 wings, 2 mantles, 6 back-worn pieces, 7 animal packs, 3 props, across
required levels 12–145. Uneventful: no tooling changes were needed, the icons stayed
inside `icon52` (30 of 144 good cells were in use, 114 free), and verification was clean
first time — STB rows == ZSC objects (1017 each), no dangling indices, no missing assets,
all 30 STL keys in all five language blocks, all 30 icons carrying real art.

Worth noting for future batches: the candidate scan excludes already-imported models
**by mesh signature**, not by row, so it cannot re-offer something a previous batch took
even under a different Jrose row.

**Batch 4 (2026-09-05): every remaining new wing *model*, 11 of them, as IDs 1017–1027**,
manifest in [doc/jrose-back-batch4.txt](jrose-back-batch4.txt). Four of them
(1017/1019/1021/1022) complete the "Skyborn" set begun by id 996.

### Count models, not variants — or rather, know which you are counting

Sizing "how many wings are left" exposed a measurement error running through batches 1–3.
The candidate scans grouped by **mesh signature**, which collapses recolours: Jrose ships
many wings that share a mesh and differ only in texture, and those are separate items to a
player. Grouped the other way, by **mesh+texture**, the picture is:

| | Total | Was missing |
|---|---|---|
| Distinct wing **meshes** | 52 | **11** |
| Distinct wing **mesh+texture variants** | 198 | **142** |

Of those 142, **124 were recolours of meshes we already held** and 7 more were extra colours
of the 11 new meshes — leaving 11 genuinely new shapes, which is this batch. A recolour is
cheap by construction: verified on Jrose row 795, importing one copies **only the `.dds`**,
because `zsc_build_append` dedupes the mesh by path.

So quote both numbers, and say which is which. "Models remaining" in the batch 1–3 notes
means *meshes*, and understates the available variety by roughly 4x.

**Batch 5 (2026-09-06): 34 more as IDs 1028–1061**, manifest in
[doc/jrose-back-batch5.txt](jrose-back-batch5.txt) — the 5 remaining shield-ish models, the
15 best of the rest, the 11-flag sashimono banner set, and 3 novelty pieces.

The banner set is the cheapest variety in the dump and worth knowing about as a pattern:
**one 400-vertex mesh with 11 different textures**, eight Sengoku army crests plus three
motto flags. Verified before importing that these are distinct *designs* rather than
recolours — ten of the eleven cost only a `.dds`. When hunting value, a high variant count
on one mesh is worth inspecting rather than dismissing as a reskin.

That leaves **131 wing variants** (recolours, plus colours of the 11 taken in batch 4) and
roughly **73 non-wing models**, most of them animal packs, Japanese seasonal pieces and
food props. Same command with different numbers; §4.4's sex-split list and the
disabled-row levels are the only per-item traps, and `--copy-effects` is free to pass
always (it is a no-op for the models with no dummy point — only 2 of Jrose's 5,001 back
objects have one, and both are already in).

Per-import checklist: `import-item.py --art-only [--copy-effects] …` →
`add-dds-mipmaps.py` → delete any `.bak` under `data/` → bake → deploy.

Rollback is `scripts/remove-trailing-items.py`, one item at a time in reverse order
(961 → 957), since the five names share no common prefix. Dry-run verified.

Note `data/` is gitignored, so — as with the balance passes — **the scripts and this doc
are the only committed record** of anything imported.
