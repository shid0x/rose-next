# NPC / Monster Weapon Models — How They Work & The "Invisible Weapon" Fix

Reference notes from diagnosing the **Ikaness Worker (NPC 1576) invisible pickaxe**.
Covers how monster weapons resolve, the data gaps that make them invisible/silent,
the tool that fixes them, and the deployment gotcha.

---

## 1. How a monster gets a weapon

A monster's right/left weapon is set in `LIST_NPC.STB`:

| Macro (`src/common/include/rose/io/stb.h`) | Column | Resolves against |
|---|---|---|
| `NPC_R_WEAPON(I) = get_int32(I, 5)` | "Weapon Right Hand" | `3DDATA/WEAPON/LIST_WEAPON.ZSC` |
| `NPC_L_WEAPON(I) = get_int32(I, 6)` | "Weapon Left Hand"  | `3DDATA/WEAPON/LIST_SUBWPN.ZSC` |

The value is used **directly as a ZSC object index** (no item-table indirection for NPCs):

- `cmodelchar.cpp` (`Load_MOBorNPC`): `SetPartMODEL(BODY_PART_WEAPON_R, NPC_R_WEAPON(nI))`
- `SetPartMODEL(part, idx)` → `g_DATA.m_pMD_CharPARTS[female][part]->GetMODEL(idx)`
- `io_basic.cpp` loads `LIST_weapon.ZSC` into the WEAPON_R slot and `LIST_subwpn.ZSC` into WEAPON_L.

The weapon mesh is attached to **dummy point 0** (the right-hand dummy `p_00` on the
monster skeleton). `GetMODEL` bounds-checks: an **out-of-range or empty** object returns
`NULL`, so the weapon part is never created → **invisible weapon, no crash** (the body
and animation still play because those are separate skinned parts).

### ⚠️ STB column off-by-one (the trap that cost us hours)

The game's STB reader (`src/common/src/io/stb.cpp`) **skips the root/ID column**
(`col_count = read_uint32() - 1`, data offset skips column 0). So:

> **game `get_*(row, col)` column N == the column an STB editor shows at display position N+1.**

Confirm with `NPC_NAME = get_cstr(I, 0)` → that's the editor's *Description* column, not *ID*.
For NPC 1576: `NPC_R_WEAPON` (col 5) = the editor's **Weapon Right Hand = 1122**; the raw
6th cell (`120`) is actually **Mob Size**. When hand-parsing an STB, skip the first physical
column (or add 1 when translating a `get_*(I, col)` macro to a raw cell offset).

---

## 2. Root cause of the invisible weapons

This fork's weapon tables are an **older / truncated** revision, out of sync with the
NPC weapon indices, which were authored against a fuller (retail-like) data set:

| File | This fork | Reference (RoseZA) |
|---|---|---|
| `LIST_WEAPON.ZSC` objects | 1354 | 7876 |
| `LIST_SUBWPN.ZSC` objects | 306 | 3306 |
| `LIST_NPC.STB` rows / cols | 3033 / 45 | 4016 / 50 |

NPC 1576's right weapon = object **1122**. In our `LIST_WEAPON.ZSC` object 1122 is
**empty (`parts=0`)** and the mesh (`mobwpn/mob_icanes/mob_icanes.zms`) is absent from
our `data/` entirely. The whole high-index `mobwpn/` block (icanes, gargoyles, mummies,
penguins, far/unik wielders, …) was empty, so **many** monsters had invisible weapons.

**Do NOT "fix" this by swapping in the reference ZSC wholesale.** Player weapon *items*
index the **same** `LIST_WEAPON.ZSC` by item number
(`cobjchar.cpp` ~L4860: `SetPartMODEL(part, itemNo)`), so reordering objects would shift
every player weapon model. The object index space is shared between NPCs and players.

---

## 3. The fix tool — `scripts/zsc_weapon_fix.py`

Populates the **empty** ZSC objects our NPCs reference, sourcing the object definition
and mesh/texture files from a working reference client (RoseZA).

Key properties:
- **Byte-faithful & append-only.** Unmodified objects re-emit verbatim; new model/texture
  entries are appended at the end so **no existing index shifts** → player weapons and all
  other NPCs are untouched. Only previously-empty objects are populated.
- Target set = union of objects referenced by **our** NPC STB and the **reference** NPC STB
  (robust to test edits; over-filling an unreferenced empty object is harmless).
- Copies **every part's** mesh + texture (objects can have >1 part — e.g. `u_tsw8` has
  `u_tsw8_1` + `u_tsw8_2`; copying only part 0 leaves a "Material File not found" error).
- Modes: `--selftest` (zero-modification round-trip must be byte-identical),
  `--dry-run` (report), `--apply` (writes `.bak` backups, then patches).

Applied result: 41 objects populated in `LIST_WEAPON.ZSC` + 1 in `LIST_SUBWPN.ZSC`,
~82 mesh/texture files copied. Validated: tables append-only, only empty→populated objects
changed, object counts unchanged.

```
python scripts/zsc_weapon_fix.py --selftest
python scripts/zsc_weapon_fix.py --dry-run
python scripts/zsc_weapon_fix.py --apply
```

---

## 4. Attack sound (and other weapon properties)

The swing sound is read from the **weapon's** `LIST_WEAPON.STB` row, not the monster's
(`cobjchar_actionframe.cpp` action-frame case 31):

```c
nHitStartSound = WEAPON_ATK_START_SOUND(Get_R_WEAPON());   // weapon index's sound
if (!Get_R_WEAPON() && IsA(OBJ_MOB))                       // ONLY if weapon index == 0
    nHitStartSound = NPC_ATTACK_SOUND(charIdx);            // fall back to monster's own sound
```

Relevant columns (`stb.h`, game-column indices):
- `WEAPON_ATK_START_SOUND(I) = get_int32(I, 40)`  (`5 + 35`)
- `WEAPON_ATK_FIRE_SOUND(I)  = get_int32(I, 41)`
- `WEAPON_ATK_HIT_SOUND(I)   = get_int32(I, 42)`
- `NPC_ATTACK_SOUND(I)       = get_int32(I, 31)` (only used when weapon index is 0)

Because the Ikaness weapon index is 1122 (nonzero), it uses **weapon 1122's** sound — and
our `LIST_WEAPON.STB` row 1122 was **entirely empty** (no name/model/sound), so the swing
was silent. Same truncation as the ZSC, on the STB side. **Fix = populate the weapon's
`LIST_WEAPON.STB` row** (attack-start sound, plus the sound/shock effect fields). Use a
sound index that exists in *this* build's sound table — don't blindly copy the reference's
number, the sound lists can differ between builds.

> Resolved here by hand-editing `LIST_WEAPON.STB` row 1122 (attack-start value, sound
> effect, shock effect) in an STB editor.

---

## 5. Deployment gotcha — the client reads a packed VFS

The running client loads assets from a **VFS packed into the EXE folder**, *not* the repo's
`data/` directly. So after editing `data/` (ZSC objects, new mesh/texture files, STB rows)
you **must repack the VFS** for changes to appear.

Also watch for files landing in the wrong subfolder — one DDS had to be moved to the path
the ZSC texture record points at before it resolved.

---

## 6. Known remaining items

- **"Wrong-but-visible" weapons** (not invisible): where our object index is occupied by a
  *different* mesh than intended. E.g. NPC 1578/1579 (Ikaness Leader/Guard) point at object
  1123, which in our ZSC holds a magic staff (`mto1`), not `mob_Icanes_O`. Fixing those means
  repointing the NPC to a different object (overwriting an occupied object risks a player
  weapon item that shares the index) — a separate, more careful pass.
- Other mob-weapon `LIST_WEAPON.STB` rows are likely also blank (no sound) like 1122; fill
  the sound fields per weapon if the silent swing matters.
