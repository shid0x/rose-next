# Importing Back Items From Jrose

**Status:** first five imported as IDs **957–961**, awaiting in-game test.
**Date:** 2026-09-04.
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
append, the asset list and the field-drop model against a live Jrose row. **One** line
stops it (§4.1), and that line is a three-line fix.

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

All three are done. §4.1 and §4.3 were solved by the same change — a new
`--art-only` mode — rather than separately.

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
`DDSD_MIPMAPCOUNT`. It was regenerated (mtime 2026-08-28) after the last `add-item-icon.py`
run (`ITEM1.TSI` mtime 2026-08-14), and `dds_read_bgra`'s strict size check now rejects it.

This was **pre-existing and unrelated to Jrose**: `add_icon` fills the last extension sheet
before starting a new one, so *any* new item icon was blocked, from a PNG or from a source
atlas.

`dds_read_bgra` now validates the pixel format out of the **header** (512×512, 32bpp,
`RGB|ALPHAPIXELS`, no fourcc) and reads only the top mip, ignoring whatever trails it.
`dds_write` still emits no mip chain, which is what this script has always done and what
the client has always shipped — mips are meaningless for a 40px-cell atlas drawn at 1:1,
and generating them would bleed neighbouring cells together at the low levels, the same
hazard as the object lightmap atlas in the root `CLAUDE.md`.

Worth knowing: the 50 original sheets are **DXT5 with 10 mips**; our extension sheets are
uncompressed 32-bit. `bake_dds` in the pipeline forces DXT5, so it is not what regenerated
`icon51.dds` — that came from somewhere else. Tolerating mips on read is the durable fix
either way.

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

## 6. Where this stands

Done: the tooling changes (§4.0–4.2), and the five above imported and verified on disk.

Still to do:

1. **Test in game** — bake the VFS and deploy, then equip each on both a male and a female
   character and check the model sits on the back dummy point correctly at rest, running
   and mounted.
2. If they look right, the remaining **189 models** are the same command with different
   numbers. §4.4's sex-split list and the disabled-row levels are the only per-item traps.

Rollback is `scripts/remove-trailing-items.py`, one item at a time in reverse order
(961 → 957), since the five names share no common prefix. Dry-run verified.

Note `data/` is gitignored, so — as with the balance passes — **the scripts and this doc
are the only committed record** of anything imported.
