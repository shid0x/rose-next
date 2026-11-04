# ROSE GM Browser (items and monsters)

`gm-item-browser.exe` is a read-only Windows companion for alpha testers. It
shares the NPC shop editor's item categories, icon atlas reader and DDS decoder.

## For testers

1. Keep the supplied `data` folder beside `gm-item-browser.exe` and launch it.
   The loose data loads automatically, without a game installation or VFS.
2. Search by name, ID, or `type:ID` (for example `8:1453`). Multiple search words
   must all match. Use item type, subtype, and minimum/maximum stat filters to
   narrow the list; blank bounds are unlimited.
3. Click **Copy** on a row, then paste the command into game chat. Select an item
   name to see its description, stats, and quantity control for stackable items.
4. The **Monsters** tab lists every `LIST_NPC` row a tester can `/mon`. Search by
   name or ID (`#123` matches exactly ID 123), filter by level, max HP and status,
   set the spawn count, and click **Copy** for `/mon ID COUNT`.

**Ctrl+F** jumps to the current tab's search box, and double-clicking a name
copies its command.

### Broken monsters

Broken rows are never hidden: their names are drawn in **rose**, and selecting one
lists every problem. Each check mirrors real code:

- **Server refusals** (`MobRowRefusalReason` in `cheatcmd.cpp`): blank table name,
  empty model column, level 0, junk HP. `/mon` whispers "Spawn refused" for these.
- **Model chain** (the client's `CCharModelDATA::Load_MOBorNPC` → `CModelDATA::Load`):
  no `LIST_NPC.CHR` entry or no body parts (invisible and unclickable), skeleton,
  mesh or texture file missing, an index outside `PART_NPC.ZSC` or the CHR's lists
  (unchecked on the client), and a missing idle, walk, attack, hit or death motion.

An amber status marks minor problems that do not stop a fight: a missing weapon
prop (columns 5/6 → `LIST_WEAPON.ZSC` / `LIST_SUBWPN.ZSC`), a missing body effect
or optional motion, a blank or missing AI script (the monster stands still), or no
`LIST_NPC_S.STL` name (blank in game). Town NPCs (type 900+) are hidden by
default because `RegenCharacter` silently ignores `/mon` for them. **Max HP** is
level × the HP column, as the server and client compute it.

### 3D item preview

Selecting anything worn (helmet, body, gauntlets, boots, face item, back item,
weapon or off-hand) opens a **3D preview** pane and puts the item on a
character, male or female. Items stay on as you select others, one per slot, so
an outfit builds up; **Wearing** lists them, **Take off** removes one, **Clear**
keeps only the selected item. Empty slots show face 1, the first haircut and the
bare body. The character plays the motion the client would for the chosen
action (idle, battle stance, walk, run, attack, sitting) with the weapon worn,
or stands in the bind pose. Drag to turn, scroll to zoom (the view closes in on
the selected item), double-click to reset. Anything that could not be drawn is
listed under **Not shown**.

It builds the model as the client does (`src/preview.rs` has the source refs):
the item number is the object index in the slot's `LIST_*.ZSC` for that sex;
hats swap the haircut by `HELMET_HAIR_TYPE` (LIST_CAP column 33); face and hair
hang off the head bone, hats, masks and back items off skeleton dummies, weapons
off the hand/shield dummies their parts name; the motion is
`TYPE_MOTION[action][weapon motion type]` -> FILE_MOTION (female file, else the
male one), skinned on the CPU over `MALE.ZMD` / `FEMALE.ZMD`; texture alpha is
honoured only where the engine honours it (alpha flag on, not specular). It
works on reference dumps too: open Jrose's loose `3Ddata` folder (its `data.idx`
is encrypted). Vehicles and item glow effects are not shown. The tester package
ships no models, so it shows no pane.

### 3D monster preview

Selecting a monster opens the same pane for that `LIST_NPC` row. The motion list
offers only the slots its `LIST_NPC.CHR` entry has (idle, walk, attack, hit,
death, run, cast/skill 1 and 2, extra), plus the bind pose; the chosen motion is
kept as you move to the next monster, so attacks compare side by side. The
camera re-frames each monster, because they run from a jelly bean to a dragon,
and the view holds the whole motion still (a jumping attack or a bouncing idle
makes the model smaller in the pane). Below the view: the table scale (column 4)
and roughly how tall the monster stands in game (its first idle pose times that
scale), the hand weapons by item name, and **Not shown** for anything missing.

The model is built as the client builds it (`src/preview_monster.rs`): the CHR
row's skeleton, its `PART_NPC.ZSC` objects linked like avatar parts (a part's own
bone or dummy tag, otherwise skinned), columns 5/6 as `LIST_WEAPON.ZSC` /
`LIST_SUBWPN.ZSC` objects on their own dummies, and a slot listed twice keeps
the last file. Bone effects (auras, flames) are not drawn, and mesh-animated
parts (morphers such as flapping wings) stay in their stored shape.

Use **Open data folder** to choose another loose data set, or **Open VFS** to
select a game's `data.idx`. The tool also accepts an extracted folder's `3DDATA`
or `STB` subfolder, or a path on the command line. It reads every archive listed in `data.idx`,
including `rose_2.vfs` and later parts, without extracting them. A packed source
uses its archive data consistently; choose the extracted folder to inspect
unpacked development changes. Use **Reload** after updating game data.

Automatic loading prefers `data/` beside the executable, then data directly
beside the executable (including `data.idx`), then those same locations under
the working directory. An explicit command-line path overrides automatic loading.

Names and descriptions come from the English STL tables, with raw STB names as
a fallback. Equipment variants include their translated `STR_ITEMPREFIX.STL`
prefix (for example, **Golden Trunket Armor**), which is also searchable. If the
prefix table is missing from an older package, prefixed equipment uses its raw
table name and the browser shows a data warning.
The item ID is the table row position used by the game. The type
number shown beside the category is the first `/item` argument; **subtype** is
the separate numeric equipment/item class stored in the table.

**Stats:** level is the character-level requirement (0 means no level gate).
Attack, defense and resistance are base table values, before item bonuses,
refinement, gems and character calculations. A dash means a stat does not apply;
setting a range excludes those rows. Vehicle attack and consumable requirements
use their own table layouts. Invalid ranges show an error and match no items.

**Commands:** `/item type ID` creates one item. Stackable quantities are limited
to the server's range of 1–100. Equipment commands omit the third argument,
because the server interprets it as an appraisal stat. Rows outside IDs 1–2047
or with no icon have no Copy action; uncheck **Spawnable items only** to inspect
them. GM access is still required in the game; this tool does not grant access
or connect to the server.

## Build and run

From the repository's `src` directory:

```powershell
cargo +stable-i686-pc-windows-msvc build --release -p npc-shop-editor --bin gm-item-browser
../bin/release/gm-item-browser.exe ../data
```

To build a portable package, run from the repository root:

```powershell
./scripts/package-gm-item-browser.ps1
```

This creates `dist/gm-item-browser/` with the following layout:

```text
gm-item-browser.exe
README.txt
data/
  ASSET_MANIFEST.TXT (model files the monster tables reference and that exist)
  3DDATA/
    STB/           (14 item tables, their translations, STR_ITEMPREFIX.STL,
                    LIST_NPC.STB, LIST_NPC_S.STL, FILE_AI.STB)
    NPC/           (LIST_NPC.CHR, PART_NPC.ZSC)
    WEAPON/        (LIST_WEAPON.ZSC, LIST_SUBWPN.ZSC)
    CONTROL/RES/   (ITEM1.TSI and the icon sheets it references)
```

Only the required loose assets are copied, and their hashes are verified against
the source. The package carries no meshes, textures or motions, so the packager
runs `gm-item-browser.exe --package-monsters <data> <package-data>`, which copies
the monster tables and writes `ASSET_MANIFEST.TXT`, then reloads the package and
fails unless every row's verdict matches the source exactly. A loose folder with
neither model files nor a manifest turns the missing-file checks off with a data
warning, rather than reporting every file as missing.

Optional `-DataRoot` and `-OutputDir` parameters select different source/output
folders. Regenerate and redistribute the package after changing item or monster
data so testers see the same catalog as the server.

Testers need no Rust installation or editor. VFS loading remains optional;
keep all the game's `rose*.vfs` archives alongside the selected `data.idx` when
using that option.

The existing editor still runs with `cargo run -p npc-shop-editor`.

## Browsing another client's dump

"Open data folder..." on a reference dump (Jrose, RoseZA, QQ-iROSE, China Rose...)
works the same as on ours. Three things make it work and all are worth knowing:

- **Text codec.** roselib decodes every string as Korean EUC-KR and only reads
  the modern `NRST01`/`ITST01` string tables, so Jrose (Shift-JIS, legacy
  `I_NUM`/`N_NUM` tables) came out as boxes with stray punctuation and 16 data
  warnings. The browser now reads the tables itself (`src/text.rs`) and picks
  the codec per file from the bytes: lead-byte classes first (kana sits on
  leads 0x81-0x9F, Hangul on 0xB0-0xC8 with high trails), decoded script as a
  tiebreak. China Rose added GBK (its STBs) and UTF-8 (its STLs). GBK reads as
  EUC-KR without an error, so it is told apart by where the leads fall: Hangul
  leads stop at 0xC8, Chinese puts 20-65% of its pairs on 0xC9-0xF7, and every
  Korean dump on hand measures 0% there. Row labels are left out of the vote
  (China Rose kept the original Korean ones over its GBK columns). The `Text:`
  box in the top bar overrides it (`--encoding
  auto|western|korean|japanese|chinese|utf-8` on the command line); it should
  never be needed. `tests/codec_dumps.rs` (ignored; set `ROSE_DUMPS`) prints
  what Auto picks for every item table of a list of dumps -- run it before and
  after touching `detect`.
- **Fonts.** egui ships Latin-only fonts, so a correctly decoded name still
  draws as boxes without a CJK font. At startup the browser appends whatever
  Windows has (Yu Gothic, Meiryo, MS Gothic, Malgun Gothic, Microsoft YaHei,
  SimSun) as fallbacks; `ROSE_GM_FONT=<path>` puts a font of your own first.
  A machine with none of them gets a line in Data warnings.
- **Weapon tables.** iROSE has one `LIST_WEAPON.ZSC` / `LIST_SUBWPN.ZSC` for both
  sexes. China Rose (`_FK_CHAR_SYSTEM`) has none, only `LIST_MWEAPON` /
  `LIST_WWEAPON` and `LIST_MSUBWPN` / `LIST_WSUBWPN`; `WeaponTables::detect`
  (`src/monsters.rs`) switches when the shared table is absent and the male one
  present. Monsters use the male tables, as in its client. About 440 of its
  spawnable items still show "model N is empty": those objects really are
  empty in its ZSCs (rosefmt agrees, and the meshes are nowhere in the dump),
  so its own client draws nothing either.

`gm-item-browser.exe --check <folder or data.idx> [--previews] [--encoding ...]`
loads a folder headlessly and prints the item and monster counts, a few sample
names, the data warnings and which fallback fonts loaded (it renders one probe
frame, so a font egui cannot parse fails here rather than at the window). Exit
code 1 means a catalog failed to load; warnings alone exit 0. `--previews` also
builds every spawnable item's preview (both sexes) and every monster's, without
drawing, and groups what could not be shown with the numbers taken out, so a
cause shared by a thousand items is one line (about a minute on a full dump).

`gm-item-browser.exe --preview <folder> <out.png> <type:id>[,<type:id>...] [--female]
[--motion idle|battle|walk|run|attack|sitting|bind] [--time seconds] [--yaw degrees] [--zoom z]`
renders an outfit (the last item is the one looked at) to a PNG on the CPU, with
the same scene, motion and camera as the pane but nearest-pixel texturing, and
prints the motion file and anything not shown; exit code 1 if anything was not
shown. Use it to check placement and motions without the window.

`gm-item-browser.exe --preview-monster <folder> <out.png> <id>
[--motion idle|walk|attack|hit|death|run|cast1|skill1|cast2|skill2|extra|bind]
[--time seconds] [--yaw degrees] [--zoom z]` does the same for a `LIST_NPC` row
(idle, or the row's first motion, by default) and also prints its scale, hand
weapons and motion slots.

## Validation

From `src`:

```powershell
cargo +stable-i686-pc-windows-msvc test -p npc-shop-editor
cargo +stable-i686-pc-windows-msvc test -p npc-shop-editor workspace_catalog_and_icons_from_loose_and_packed_data -- --ignored --nocapture
cargo +stable-i686-pc-windows-msvc test -p npc-shop-editor workspace_monster -- --ignored --nocapture
cargo +stable-i686-pc-windows-msvc test -p npc-shop-editor workspace_package_manifest -- --ignored --nocapture
```

The second check requires this workspace's `data/` and `Exes/` assets. It loads
the catalog and decodes every referenced item icon from both sources. Unit
fixtures cover mixed stat schemas, high item IDs, command arguments, filters,
multiple archives, deleted entries, truncated archives, and offsets above 2 GB.

`workspace_monster_survey` prints a histogram of monster problems for `data/`,
`Exes/` and any `ROSE_GM_CATALOG_TEST_ROOT` (point it at a reference dump).
`workspace_package_manifest_matches_full_data` builds a tester-style package in a
temporary folder, proves its verdicts equal the full data's for every row, then
drops one texture from the manifest and expects that monster to turn rose. Monster
fixtures cover the CHR/ZSC readers, every server refusal, out-of-range model
indices, exact `#ID` search and the town-NPC filter.
