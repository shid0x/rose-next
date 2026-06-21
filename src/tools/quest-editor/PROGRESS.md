# Quest Editor — Design & Progress Log

A living document so we don't get lost. Read this first when resuming work.

## Goal

A **template-based** quest editor (not a full quest scripting IDE) that generates
safe, append-only ROSE quest data for a small set of proven patterns:

- "Talk to NPC to start"
- "Kill monster X, N times" (Hunt)
- "Bring N of item X" (Fetch)
- "Talk to NPC to complete"
- Simple rewards: EXP, zuly, item
- (later) repeatable / one-time flag

Delivered as a wizard: pick giver NPC → Hunt/Fetch → pick monster/item → amount →
title/start/progress/complete text → rewards.

## Hard rules (safety model)

1. **No game C++/engine source edits.** All *tool source* lives under
   `src/tools/quest-editor/`.
2. The tool's *purpose* is to write game **data** files (unavoidable — see data
   model). All data writes are **append-only** (new rows / new files) or
   **merge-only** (NPC trigger strings); we never rewrite an existing `.QSD`.
3. Every touched data file gets a `.bak` backup; a dry-run/preview shows exactly
   what will change before committing.

## How the game loads a quest (the data model)

A working quest spans **five** data files (verified in
`src/sho_gameserver/src/lib_gsmain.cpp:462` and
`src/common/shared/io_quest.cpp`):

| File | Role | Our write strategy |
|---|---|---|
| `3DDATA/STB/LIST_QuestDATA.STB` | col 0 of each row = path to a `.QSD`; all rows are loaded and their triggers merged into one global hash | **Append** a row pointing at our new QSD |
| `3DDATA/QuestData/*.QSD` | the trigger logic (conditions + rewards) | **Create a new file per quest**; never touch existing ones |
| `3DDATA/STB/LIST_Quest.STB` | quest metadata indexed by Quest SN (time limit, owner type, icon, name/desc strID) | **Append** one row → new Quest SN |
| `3DDATA/QuestData/Quest_s.STB` + `LIST_QUEST_s.stl` | UI text (title/desc/start/progress/complete) | **Append** new string keys |
| `3DDATA/STB/LIST_NPC.STB` col 41 | NPC click/death → trigger name | **Merge** (read existing, add our trigger; never clobber) |

Trigger names (NOT pattern names, NOT file names) are the globally hashed keys
the NPC death/click strings reference.

## Phase plan

- **Phase 0 — Scaffold.** ✅ Crate created, added to workspace.
- **Phase 1 — QSD codec + round-trip harness.** ✅ DONE (see status log).
- **Phase 2 — Game-data readers.** Load LIST_Quest / LIST_NPC / monster + item
  STBs + STL for searchable pickers (reuse `npc-shop-editor/src/data.rs`).
- **Phase 3 — Template generator.** Map Hunt/Fetch templates → concrete trigger
  graphs (register / progress / complete). Allocate Quest SN, var slots, trigger
  names. **Must first nail down the real retail hunt-counting mechanism — see
  Open Questions.**
- **Phase 4 — Writers + safety layer.** Append-only STB/STL insert, new-QSD
  emit, LIST_QuestDATA registration, NPC col-41 merge, `.bak`, dry-run preview.
- **Phase 5 — egui wizard UI.** (adds eframe/egui/roselib/rfd deps)
- **Phase 6 — In-game validation.** Generate test quest, restart servers (they
  cache STB at startup), verify accept→progress→complete→reward.

Deferred (not MVP): branching/dialogue, timers, party logic, editing existing
quests.

## QSD binary format (verified)

Flat little-endian. Source of truth: `CQuestDATA::LoadDATA` +
`CQuestTRIGGER::Load` in `src/common/shared/io_quest.cpp`. Implemented in
`src/qsd.rs`.

```text
u32  size_field        # game reads into ulSize, never uses it. CONSTANT 12 in all retail data.
u32  pattern_count
str  description        # i16 len prefix + bytes; game fseeks over it
repeat pattern_count:
  u32 trigger_count
  str pattern_name      # i16 len prefix + bytes; game fseeks over it
  repeat trigger_count:
    u8  check_next
    u32 condition_count
    u32 reward_count
    str trigger_name    # i16 len prefix + bytes; INCLUDES trailing NUL
    repeat condition_count: entity
    repeat reward_count:    entity

entity:
  u32 size              # TOTAL size incl. this 8-byte header
  i32 type              # game dispatches on (type & 0xffff)
  u8[size-8] payload    # raw C struct bytes, INCLUDING padding
```

### Fidelity rule (important)

Retail `.QSD` files contain MSVC uninitialized-memory fill bytes (`0xCC`/`0xCD`)
baked into struct **padding** — real garbage that got serialized. So a byte-exact
round-trip requires keeping each condition/reward payload as a **raw blob** and
re-emitting verbatim. The codec does exactly this. Typed interpretation of
payloads (for *generating* new quests) is a separate layer to be added in
Phase 3; it must never be needed for round-trip fidelity. New entities we
generate will use clean (zeroed) padding — that's fine, the game ignores it.

Entity struct layouts (per type) are documented in
`src/common/shared/io_quest.h` (`STR_COND_xxx` / `STR_REWD_xxx`).

## The hunt (kill-count) mechanism — RESOLVED

Investigated because the feasibility estimate's assumption ("kill = REWD 31") was
wrong (REWD 31 has **zero** occurrences in retail). Traced the real flow through
the server + client. Conclusion:

**Killing a monster runs a per-monster "dead-event" trigger named in
`LIST_NPC.STB` column 41 — it is CLIENT-DRIVEN, not a server kill counter.**

Flow (verified):
1. `LIST_NPC.STB` col 41 = a quest **trigger name** string per NPC/monster.
   Server macro `NPC_DEAD_EVENT(i)` = `get_cstr(i, 41)`; client macro
   `NPC_DESC(i)` = `get_cstr(i, 41)` (same column; "DESC" is a misnomer).
   (`src/common/include/rose/io/stb.h:258,267`)
2. At server startup (`lib_gsmain.cpp:283-293`) each NPC's col-41 name is looked
   up in the loaded quest data: if the trigger doesn't exist it's **cleared**; if
   it exists the server walks the trigger's `m_pNextTrigger` chain tagging each
   with the owning NPC index.
3. On a kill (`cobjchar.cpp:985-988`): if the dead monster has a col-41 event,
   the server sends `GSV_CHECK_NPC_EVENT(monsterNo)` to the killer (party-aware).
   The server does **not** touch quest vars here.
4. Client receives it (`recvpacket.cpp:5084-5087`) and calls
   `QF_doQuestTrigger(col41_name)` — runs the client-side quest engine for that
   trigger against the player's active quests.
5. If the trigger's conditions pass (player owns the quest, counter not yet full,
   …) the trigger executes; the server re-checks it authoritatively via
   `Do_QuestTRIGGER` → `CheckQUEST(bDoReward=true)`. The kill counter is a quest
   **variable** incremented by **REWD 2** (set/add var); completion is gated by a
   **COND** on that variable. The trigger name itself binds the monster, so
   `COND_013 (iNpcNo)` is not required to identify the kill.

### Ownership constraint (the editor's #1 risk — now precise)

A monster has **exactly one** dead-event trigger-name slot (col 41). Multiple
quests that hunt the same monster must **share** that one trigger, via the
`m_pNextTrigger` chain (built from trigger-name links / `check_next`). Therefore:

- **MVP-safe rule:** a Hunt quest may target a monster **only if its col-41 slot
  is currently empty** → we set col 41 to our own new trigger name and own it
  fully (append-only, zero risk to existing quests).
- If col 41 is already occupied, the editor must **warn and refuse** (chaining a
  new trigger onto an existing chain means editing an already-shipped QSD, which
  breaks our append-only guarantee). Trigger-chaining support is **deferred**.
- Fetch quests don't have this problem: they use the giver/complete NPC's
  conversation, not a monster dead-event.

### Implication for the generator (Phase 3)

- **Hunt** template emits: a register trigger (talk to giver, COND_000 not-yet-
  registered → REWD_000 add quest, init counter var to 0), a per-kill trigger
  named into the monster's col 41 (COND_000 quest registered + counter < N →
  REWD_002 increment counter), and a complete trigger (talk to giver, COND on
  counter ≥ N → rewards + REWD_000 delete/finish quest).
- **Fetch** template emits register + complete triggers only; completion checks
  item possession (COND_004) and consumes (REWD_001 with remove op / REWD_005).
- Counter display/progress text reads the same quest variable.
- Still TODO before coding Phase 3: confirm the exact var slot encoding + the
  talk-trigger wiring against a **real** retail hunt quest, once the Phase-2
  LIST_NPC reader lets us map a col-41 name → its QSD trigger.

## The "talk to NPC" mechanism — investigated, has a BIG cost

The two quest interactions have **very different** implementation costs:

| Interaction | Mechanism | Cost |
|---|---|---|
| **Kill monster** (Hunt progress) | `LIST_NPC.STB` col 41 dead-event trigger (data only) | **Cheap** — QSD + one STB cell |
| **Talk to NPC** (start / complete) | NPC conversation **`.CON` script** | **Expensive** — separate binary scripting format |

Talk-to-NPC flow (verified):
- Clicking an NPC runs `CObjMOB::Check_EVENT` → `g_pEventLIST->Run_EVENT(obj,
  m_nQuestIDX, …)` (`cobjchar.cpp:4688`).
- `m_nQuestIDX` indexes `LIST_EVENT.STB`; `EVENT_FILENAME(i) = get_cstr(i,3)`
  (`stb.h:454`) → a **`.CON`** conversation file (e.g. `EM01-001.CON`).
- `CEvent::Load` parses a **compiled binary**: `SSC_CONV_HEADER` + message-string
  block + **script-data block** (per-node conditions/actions). The dialog's
  buttons call `QF_doQuestTrigger` / `QF_appendQuest` / `QF_deleteQuest` etc. to
  drive the QSD-side quest (`cevent.cpp:50,142+`).
- The NPC must also be *assigned* an event index — wired in zone NPC placement
  data (`zonefile.cpp:198`, `io_terrain.cpp:1772` both read
  `EVENT_FILENAME(row_idx)` per spawn).

So a full "talk to start" quest needs FOUR things beyond the QSD:
1. a generated `.CON` conversation (new binary scripting format to RE), 2. a
`LIST_EVENT.STB` row, 3. an NPC→event-index assignment (zone IFO / placement
data), 4. the dialog's script to call the right `QF_*` functions.

**This is the scope fork.** The QSD/kill half is data-cheap and largely solved;
the `.CON` conversation half is a second editor-sized subsystem. Pending a user
decision on MVP scope (see status log 2026-06-20 scope decision). Phase 2 reader
work is paused until that's resolved, since it determines which tables we read.

## MVP scope DECISION (2026-06-20): Hunt-first, defer dialogs

User chose **Hunt-first, defer NPC dialogs**. The MVP builds the data-cheap,
fully-proven half end-to-end and uses existing GM commands to drive accept/
complete during testing — **no `.CON` work, no game-source edits**.

What the MVP generates (all append-only / merge-only data):
- A `.QSD` with: a **register** trigger (init quest + counter var = 0), a **kill**
  trigger named into the monster's `LIST_NPC` col 41 (counter++ while < N), and a
  **complete** trigger (counter ≥ N → rewards + finish).
- `LIST_Quest.STB` row (new Quest SN), `LIST_QUEST_s.stl` + `Quest_s.STB` text,
  `LIST_QuestDATA.STB` row registering the new QSD.
- `LIST_NPC.STB` col 41 set on the chosen monster **only if currently empty**
  (the ownership rule).

How accept/complete is tested **without dialogs** (existing GM cheats, verified):
- Register/grant a quest by id: GM `… QUEST <questId>` → `Quest_Append`
  (`cheatcmd.cpp:610`).
- Fire any trigger by name: `/QUEST <triggerName>` → `Cheat_quest` →
  `CheckQUEST` (`cheatcmd.cpp:986,1501`).
- Kill counting happens automatically via the col-41 dead-event path.

Deferred to a later phase (not MVP): real `.CON` quest-giver dialogs (the whole
talk-to-NPC subsystem), repeatable flag, branching, timers.

## Decoded real Hunt template (quest 105) — validated against live data

Cross-validated the whole mechanism end-to-end: monster 21 **Pumpkin** has
`LIST_NPC` col 41 = `"105-31"`, and quest **105** = "치유의 손". Found the kill
trigger in `QN-021.QSD` and decoded its bytes against the `STR_*` structs:

```
trigger "105-31"  (runs on Pumpkin's death)
  COND_000  iQuestSN=105                         # quest 105 must be registered
  COND_004  item=13129 where=13 count=10 op=3(<) # have FEWER than 10 of the token
  REWD_000  iQuestSN=105 op=4(select)            # make quest 105 the current quest
  REWD_001  item=13129 op=1 dup=1                # give 1 token quest-item
```

So retail "kill N monsters" is the **item-collection model**: each kill (while
you hold `< N` of a token quest-item) grants 1 token; the quest completes when
you hold `N`. It uses only the four most-common entity types (COND 0/4, REWD
0/1). Cleaner than a counter var; this is the template the generator will emit.

### Operator codes (from `Check_QuestOP`, io_quest.cpp:44)
`0: ==`, `1: >`, `2: >=`, `3: <`, `4: <=`, `10: !=`. (Used by COND item/var ops.)

### REWD_000 quest-op codes (from `F_QSTREWD000`, io_quest.cpp:1230)
`0` reset current quest · `1` **append/register** quest · `2` set-id (no reset) ·
`3` reset+set · `4` **select** an already-registered quest as "current". (Delete/
finish op is past case 4 — confirm exact code in Phase 3.)

### Quest SN == LIST_QUEST.STB row index (not a stored label)
`LIST_QUEST.STB` = 5502 rows × 6 cols (`Quest Name, Time, Application, Icon
Number, STL Link`); the **root column is empty**. A quest's id/SN is its row
index — quest 105's name lives at row 105. QSD `iQuestSN` and `Quest_Append(id)`
use this row-index SN. **Append a new quest = append a row; its SN = old row
count.** Trigger-name convention is `"<questSN>-<NN>"`.

### Item SN encoding in quest data
`uiItemSN = type*1000 + id` (same as citem.cpp / shop editor). `13129` = quest
item `13:129`. (Validate the id exists in the item DB when generating.)

## Status log

### 2026-06-21 — Tier 3: edit / delete (+ sidecar manifests)

Created quests now drop a sidecar manifest `QUESTDATA/_quest-editor/QX-<sn>.qe.json`
(serde JSON of the `QuestSpec`; the game ignores the `_` subdir). New `manifest.rs`
(write/read/list). `write::apply_quest` writes it on success.

`write::delete_quest(root, sn, dry_run)` reconstructs the undo from the SN + naming
(works with or without a manifest): removes `QX-<sn>.QSD`, drops the LIST_QUESTDATA
row, blanks the LIST_QUEST row + the token LIST_QUESTITEM row (reclaims the id), and
un-wires the monster — clears col-41 if it equals `<sn>-2` (claimed), else finds
`<sn>-2` in a host QSD and removes it, handing its `check_next` back to the
predecessor (chained; exact inverse of the splice). Removes the manifest. All `.bak`.
Refuses non-editor quests (guards on `QX-<sn>.QSD`). STL keys are left as harmless
orphans (removal would shift row indices).

CLI: `list <root>` and `delete <root> <sn> [--write]`. Wizard: a **Create /
Manage** tab pair; Manage lists editor quests with Edit / Delete (two-click
confirm). **Edit** = `load_spec_into_form` prefill → on save, delete-old +
create-new (fresh SN; edit always commits). `CreatedSummary` gained
`effective_dry` / `was_edit`.

`write::list_editor_quests` scans LIST_QUESTDATA for `QX-<sn>.QSD` rows (∪ orphan
manifests), so the Manage list shows **every** editor quest, not just ones with a
manifest. Only template Hunt/Fetch quests the tool made are listed; retail QSDs are
never touched (no template to regenerate).

`write::reconstruct_spec(root, sn)` rebuilds a `QuestSpec` from the generated data
when there's no manifest, so **pre-manifest quests are editable too**: the complete
trigger (found by its REWD_000 Finish) gives count + checked-item (COND_004) and
rewards (REWD_005 exp/zuly, REWD_001 give/consume); Hunt-vs-Fetch from a token
LIST_QUESTITEM row; monster from col-41 (claimed) or by walking the host chain back
to its head (chained); text from the `QE_<sn>` STL row (keys/rows are parallel).
`list_editor_quests` falls back to it (`manifest.or_else(reconstruct)`). Validated:
the real committed quests #5502 / #5503 (no manifests) reconstruct with the correct
monster (incl. the chained #5503 → monster 12), count, and type. Best-effort: if the
monster can't be resolved it comes back unselected for the user to re-pick.

Validated on temp copies: create (Hunt-free / Hunt-chained / Fetch) → list → delete
each → **host QN-001.QSD restored byte-exact**, all 222 QSDs round-trip, manifests
removed, list empty. Edit (delete #5504 + recreate #5505 count 9) leaves clean,
round-tripping data. 7 unit tests pass. **Repeatable one-time flag deferred** to the
NPC-dialog work (needs the global character-switch pool `COND_014`/`REWD_015`; no
enforcement point without an NPC offer; all quests are repeatable today).

### 2026-06-21 — Tier 2 #5: trigger chaining (use any monster)

A Hunt quest can now target a monster that already has a dead-event trigger.
Mechanism (verified against `io_quest.cpp` CheckQUEST + `lib_gsmain.cpp` startup
walk): the dead event fires the col-41 trigger and, on a conditions-mismatch with
`check_next` set, follows `m_pNextTrigger` to the next trigger *in the same
pattern*. So all kill triggers for a monster must live in one pattern, chained via
`check_next`. There is no cross-QSD fail-chain.

Implementation:
- `gen.rs`: `QuestKind::Hunt` gains `chain_into_existing: bool`. When set,
  `generate_hunt` puts only register + complete in this quest's QSD and returns
  the kill trigger in `GeneratedQuest::host_kill_trigger` for splicing.
- `write.rs`: `find_host_qsd()` scans `QUESTDATA/*.QSD` for the trigger named by
  the monster's col-41. The kill trigger is inserted right after that entry; our
  trigger inherits the entry's original `check_next`; the entry's is set to 1.
  col-41 / LIST_NPC is left untouched. Host QSD backed up + rewritten.
- `verify.rs` / `ui.rs` / `main.rs`: occupied monsters are allowed (auto chain
  flag); wizard shows `[+chain]` + a "modifies the existing quest file" warning.

Validated on a temp copy: chaining onto Mini-Jelly Bean (col-41 `5033-32`, host
`QN-001.QSD`) produced chain `5033-32 → 5503-2(ours) → 5051-33 → 5051-34`, the
original order preserved, all 222 QSDs still round-trip byte-exact, LIST_NPC
unchanged. 7 unit tests pass (added `chaining_hunt_splits_out_the_kill_trigger`).
**Validated in-game on 2026-06-21:** created a new Hunt quest on a monster that
was already used by another quest, killed it, and the chained kill trigger
credited correctly.

### 2026-06-21 — Tier 1 #3: verify/lint step (Tier 1 complete)

Added `src/verify.rs`: `verify(&DataSet, &QuestSpec, &GeneratedQuest) ->
Vec<Issue>` (Error|Warning). Checks: count ≥ 1; quest SN == next append slot
(data not stale); QSD round-trips; the new QSD file doesn't already exist; Hunt —
token id ≤ 999 / type 13 / matches next free token, monster exists + col-41 free;
Fetch — item decodes + exists + id ≤ 999; reward item exists + id ≤ 999. The
wizard runs it live in the preview, lists issues (red ✖ errors / amber ⚠
warnings), and disables Create on any error (replacing the old eligibility
check). The CLI (`create`/`create-fetch`) prints issues and bails on errors.
Verified: free monster passes clean; nonexistent fetch item → warning. The writer
keeps its own guards as the final authority. **Tier 1 done.**

### 2026-06-21 — Tier 1 #2: `Init` overload audit

Swept every 2-arg `.Init(` call in the tree for the overload trap (unsigned first
arg → binds to `Init(type,no,qty)` instead of `Init(int packedSN,qty)`). Found
**one** more instance besides the already-fixed `REWD_001`: `F_QSTREWD032`
(acquire-quest-item reward) in `io_quest.cpp:2469` — fixed with
`static_cast<int>(uiItemSN)`. Everything else is safe: `int`/`short` first args
promote/bind to overload 1, and reward/cheat give-item paths that pass explicit
type/no use the 3-arg `Init(type,no,qty)`. Engine rebuilt clean. Memory
`reference_item_init_overload_trap` updated.

### 2026-06-21 — Tier 1 #1: Fetch quests ("bring N of item X")

Added a second quest type. Refactored the spec into a unified
`QuestSpec { quest_sn, kind: QuestKind, count, rewards, text }` where
`QuestKind` is `Hunt { monster_id, token_* }` or `Fetch { item_sn, item_name,
consume }`. `gen::generate(spec)` dispatches; `generate_fetch` emits 2 triggers
(register + complete) — no kill, no token. The complete trigger checks
`COND_004(item >= N)` and, when `consume`, removes them with `REWD_001` op 0
(normal-inventory items aren't cleared by Finish/Init, unlike Hunt tokens).

`write::apply_quest` (renamed from `apply_hunt_quest`) does the common writes
(QSD, LIST_QuestDATA, LIST_Quest, LIST_QUEST_S.STL) for both kinds, and only the
Hunt-specific writes (token row, col-41 merge, token STL) for Hunt — backing
up/writing just the files that change. Quest STL key is now generic `QE_<SN>`.

Wizard: a Hunt/Fetch toggle at the top; section 1 shows a monster picker (Hunt)
or item picker + "consume on turn-in" (Fetch); labels/preview/auto-text adapt.
Token-name fields show only for Hunt. CLI: `create-fetch <root> <item_sn>
<count> [exp] [zuly] [--write]`.

Verified on a temp copy: `create-fetch … 10060 5 300` → QSD with
register + complete(COND_004 item 10060 >=5 → exp 300, remove 5, finish); only
LIST_Quest/LIST_QuestDATA/STL touched. 6 unit tests (incl. fetch) + round-trip
green. ROADMAP.md documents all tiers.

### 2026-06-21 — Token item now has a real name (+ icon override)

The collected token quest-item showed **blank** in-game: the client resolves item
names from `LIST_QUESTITEM_S.STL` keyed by the item table's last column (STL Link,
col 33), which the writer left empty. Fixed: the writer now (step 7) appends a
`LIST_QUESTITEM_S.STL` Item-row (`token_name` / `token_desc`) keyed
`QITEM_HUNT_<SN>` and sets the token row's col-33 STL link to it, plus an optional
icon override (col 10; `None` keeps the cloned template icon). `HuntQuestSpec`
gained `token_name` / `token_desc` / `token_icon`; the wizard exposes token name +
info in the text section (auto-derived as "<monster> Mark") and the icon override
under Advanced. Verified on a temp copy: token row → name + STL link set, STL key
present, `.bak` made, keys==rows aligned. Also added `default-run = "quest-editor"`
so `cargo run -p quest-editor` runs the CLI (wizard is `--bin quest-editor-ui`).

### 2026-06-20 — Phase 5 complete: friendly creation wizard (egui)

Built `src/ui.rs` + `src/bin/quest-editor-ui.rs` — a GUI wizard for non-devs,
shipped as a **separate binary** (`quest-editor-ui.exe`, `windows_subsystem =
"windows"`) so the `quest-editor` CLI stays a plain console app. Deps added:
`eframe`/`egui` 0.27 + `rfd` 0.14 (same as npc-shop-editor).

Flow (single scrollable form): pick data folder → 1) searchable monster picker
(only col-41-free monsters by default, occupied ones hidden/flagged) → 2) kill
count → 3) rewards (exp, zuly, optional searchable item picker, id ≤ 999) → 4)
quest text (auto-derived from the monster name, editable) → live preview → **Create
Quest**. Result screen lists the file changes + the GM test commands and offers
"Create another" (which reloads data so the next quest gets fresh SN/token ids).

Dev knobs behind a collapsed **Advanced** section: show-all-monsters (incl.
in-use), dry-run (preview without writing), and the next SN / token-id readout.
Reuses the Phase 1-4 engine unchanged (`DataSet`, `generate_hunt`,
`apply_hunt_quest`). Run: `cargo run -p quest-editor --bin quest-editor-ui`, or
`bin/release/quest-editor-ui.exe`. Tests still green.

### 2026-06-20 — Phase 6 complete: validated in-game (+ engine bug fixed)

Generated quest 5501 ("Hunt: Royal Jelly Bean", monster 4, kill 3, 500 exp +
1000 zuly) into `data/`, baked to VFS, ran servers. Register worked, but **kills
granted no token**. Long debug session (server-authoritative path is correct;
client deployment was correct). Root cause was an **engine bug**, not the quest
data:

**Bug:** `F_QSTREWD001` (give quest item) called
`sITEM.Init(pREWD->m_Rewd001.uiItemSN, nDupCNT)`. `uiItemSN` is `unsigned int`,
so 2-arg overload resolution bound it to `Init(unsigned item_type, unsigned
item_no, ...)` instead of `Init(int packedSN, qty)` — the packed SN (e.g. 13966)
was treated as the item **type** (13966 > `ITEM_TYPE_RIDE_PART` 14 → `IsValidITEM`
fails → empty item → reward returns false → trigger `Proc` returns false →
`CheckQUEST` returns INVALID). This silently broke **every** quest that gives an
item via REWD_001. `COND_004` uses the 1-arg `Init(uiItemSN)` which correctly
resolves to overload (1), which is why the *condition* passed but the *reward*
didn't.

**Fix (kept, permanent):** `io_quest.cpp` F_QSTREWD001 —
`sITEM.Init(static_cast<int>(pREWD->m_Rewd001.uiItemSN), nDupCNT)`. One line.
Fixes all item-giving quests. See [[project_quest_rewd001_init_overload_bug]].

**How it was found:** temporary `LOG_INFO("QUESTDBG ...")` traces at the
dead-event send (`cobjchar.cpp`), `Do_QuestTRIGGER` (`gs_user.cpp`), and inside
`Proc` + `F_QSTREWD001` (`io_quest.cpp`). The decisive line was
`QUESTDBG REWD001: SN=13966 type=0 no=0 header=0 qitemRows=1001` — table had 1001
rows yet the item came out type 0, proving the SN was being read as a type. All
`QUESTDBG` logs have been removed; only the one-line fix remains.

**Result:** token drops on each kill, completion pays exp + zuly. The quest
editor's generated data was correct throughout. **MVP validated end-to-end.**

Debugging notes for future quest testing:
- `/quest <triggerName>` (cheat) can't fire an NPC-owned dead-event trigger:
  `CheckQUEST`'s server guard rejects it because the cheat passes `iEventNpcIDX=0`
  but the trigger's `m_iOwerNpcIDX` = the monster row. Test kill triggers by
  actually killing the monster.
- The current client build writes no usable `client.log` for this path; the
  server LOG_INFO (Info level) is the practical trace point.
- `F_QSTREWD000` op 1 (register) returns true even if `Quest_Append` fails, so a
  successful `/quest <register>` doesn't by itself prove server-side registration.

### 2026-06-20 — Phase 4 complete: append-only/merge writers

Built `src/write.rs` — `apply_hunt_quest(root, spec, gen, dry_run)` turns a
`GeneratedQuest` into real data edits. Uses roselib STB + STL writers
(`RoseFile::from_path`/`write_to_path`).

**The 6 changes (all append-only / merge-only):**
1. CREATE new `.QSD` (`QX-<SN>.QSD`) in 3DDATA/QUESTDATA.
2. APPEND LIST_QUESTDATA row → col1 = `3DDATA\QUESTDATA\QX-<SN>.QSD`.
3. APPEND LIST_QUEST row at index == SN (clone a real individual-quest template;
   override name col1, time col2=0, STL link col5).
4. SET LIST_QUESTITEM placeholder row == token id (clone a real quest-item
   template; override name col1, belonging-quest col32=SN, STL col33="").
5. MERGE LIST_NPC col-41 (roselib col 42) on the monster row.
6. APPEND LIST_QUEST_S.STL key + one QuestRow per language.

**Validation guards (all checked before any write):** SN == LIST_QUEST row count
(append index); token type==13 and id<=999 and row is an empty placeholder;
monster col-41 free (ownership rule); QSD file doesn't exist; STL key unique. Plus
`.bak` of every existing file (once) and a **dry-run** that writes nothing.

**Column maps discovered (roselib indices):**
- LIST_QUEST (6 cols): 1=Quest Name, 2=Time, 3=Application(0=individual),
  4=Icon, 5=STL Link. Client reads name/desc via STL keyed by col 5
  (`CStringManager::GetQuestStringData` → `get_cstr(I,4)` game col = roselib 5).
- LIST_QUESTITEM (34 cols): 1=Item Title, 5=Type, 10=Icon, 32=Belonging Quest,
  33=STL Link.
- LIST_QUESTDATA (2 cols): 0=label, 1=Quest data file (path).

**STB format insight (roselib stb.rs):** the stored `row_count` field =
`data.len() + 1` (a phantom row); roselib reads `row_count-1` and writes
`data.len()+1` — round-trip safe. Confirms game quest SN == roselib data index
(quest 105 = row 105, matched Pumpkin's `105-31`). **Append at end ⇒ SN = old row
count, never overwriting existing rows.**

**Token-id ≤ 999 constraint:** `tagBaseITEM::Init(int)` decodes `iItem/1000`
(type), `iItem%1000` (id) — the legacy `type*1000+id` format (citem.cpp:80). So a
token must reuse an empty quest-item placeholder row ≤ 999, not append past 1000
(which would corrupt the encoding). `next_free_quest_item_id` = max named id + 1.

**Bug caught + fixed:** the first template-clone picked **row 0** (the STB
schema/description row whose non-key cells are comment strings like
`"(78~82)"`). Fixed `template_row` to require a genuine data row (numeric
icon/type, Application==0 for quests). Quest row now clones correctly:
`["", "Hunt: …", "0", "0", "81", "QEST_HUNT_5501"]`.

**Validated end-to-end** on a temp copy of `data/` (`create <tmp> 4 10 500 1000
--write`): all 6 files written + 5 `.bak`s; re-read confirms quests 236→237,
total rows 5501→5502, monster 4 col-41 = "5501-2", new QSD round-trips byte-exact,
STL key present (keys==rows aligned, correct text across 5 languages). New CLI:
`create` (dry-run unless `--write`), plus `stbcols`/`stlcheck` debug commands.
All tests green.

**Not done (later):** the token's own name in LIST_QUESTITEM_S.STL (inline name
only for now — the token is a hidden collection item); real in-game test (Phase 6)
— remember the data must be VFS-baked + deployed and servers restarted (they
cache STB at startup) before the quest appears. See [[reference_asset_deploy_workflow]].

### 2026-06-20 — Phase 3 complete: entity builders + Hunt generator

Built `src/gen.rs`: typed builders that emit `qsd::Entity` payloads byte-exact to
the `STR_*` layouts, plus `generate_hunt()` assembling the 3-trigger graph.

Builders (all validated against retail bytes): `cond_quest_registered` (COND_000),
`cond_item_count` (COND_004), `rewd_quest_op` (REWD_000), `rewd_give_item` /
`rewd_remove_item` (REWD_001 op 1/0), `rewd_exp` (REWD_005 tgt0/eq0),
`rewd_zuly` (REWD_005 tgt1/eq3). Enums `CmpOp` (0/1/2/3/4/10) and `QuestOp`
(0 finish / 1 register / 4 select).

Hunt graph (`<SN>-1` register, `<SN>-2` kill→col41, `<SN>-3` complete):
- register: REWD_000 op1 (Quest_Append)
- kill: COND_000 + COND_004(token `< N`) → REWD_000 op4 select + REWD_001 give 1
- complete: COND_000 + COND_004(token `>= N`) → select + exp/zuly/item rewards +
  REWD_000 op0 finish (Init clears quest slot + tokens) LAST.

**Reward equations** (CCal::Get_RewardVALUE, calculation.cpp:78): exp uses eq 0,
zuly uses eq 3 — both "base value + standard charm/fame/level bonus" (the retail
quest-reward behavior; not a flat number). Item rewards use REWD_001 for an exact
count. (eq 2 money is count×var and needs a quest var preset — avoided.)

**iWhere** in COND_004 = 0 (EQUIP_IDX_NULL, outside equip range `[1,12)`) so a
quest-type item routes to the quest inventory (Check_QuestITEM). Retail used 13 —
behaviorally identical.

**Token quest-item:** one **new** quest-item per quest (append-only, no
cross-quest pollution). `DataSet::next_free_token_item_sn()` allocates it
(`type 13 * 1000 + nextId`). Created in LIST_QUESTITEM by the Phase-4 writer.

Tests (5 lib + round-trip, all green): `matches_retail_quest_105_kill_trigger`
proves our builders reproduce real `QN-021.QSD` "105-31" payloads byte-for-byte;
`generated_hunt_round_trips_through_codec` proves the assembled QSD survives the
codec; plus reward-omission. Added `quest-editor gen <root> <monster> <count>
[exp] [zuly]` — a **preview** command (writes nothing) that loads real data,
enforces the col-41 ownership rule, and prints the plan + decoded QSD + the exact
GM test commands.

Verified live: `gen ../data 4 10 500 1000` → quest 5501, token 13966, monster 4
(Royal Jelly Bean, col-41 free), all payloads decode correctly.

Still no UI deps. Next: **Phase 4** — the append-only/merge writers (QSD file +
LIST_Quest row + LIST_QuestDATA row + LIST_QUESTITEM token + STL text + NPC col-41
merge) with `.bak` backups and a dry-run, turning `GeneratedQuest` into files.

### 2026-06-20 — Phase 2 complete: game-data readers + real-data validation

Built `src/data.rs` (uses `roselib`, mirrors the shop editor's column
conventions). Loads monsters + giver NPCs (`LIST_NPC.STB`), existing quests
(`LIST_QUEST.STB`), and all item tables. Added the `quest-editor data <root>`
CLI summary.

**Results (`data ../data`):**
- 927 monsters, 167 giver NPCs, 236 named quests (5501 rows → next free SN 5501),
  3943 items.
- **175 monsters already have an occupied col-41 dead-event** → not eligible hunt
  targets under the MVP ownership rule (the editor must surface this).
- Fixed quest reader: SN = row index, not the (empty) root label.
- Decoded the real Hunt template + op codes + SN/item encodings (sections above).
  This also closes Phase 1's residual "validate against a real hunt quest" item.

Deps: added `roselib` (git). Still no UI deps. `cargo test -p quest-editor`
green (codec round-trip + unit tests unchanged).

### 2026-06-20 — Phase 2 start: "talk to NPC" cost discovered (scope fork)

While scoping the Phase-2 readers, traced the talk-to-NPC path and found it
requires the `.CON` conversation subsystem (see section above) — far heavier than
the col-41 kill path. Surfaced a scope decision to the user before building
readers. No code changes.

### 2026-06-20 — Hunt-mechanism investigation (pre-Phase-3)

Resolved the kill-count mechanism from source (see section above). Net result:
REWD 31 is a dead end; retail uses the **col-41 dead-event trigger → client
QF_doQuestTrigger → REWD_002 counter var** model. Recorded the ownership
constraint and the MVP-safe "empty col-41 only" rule. No code changes this pass
(investigation only); the codec/tests from Phase 1 are unchanged.

### 2026-06-20 — Phase 0 + Phase 1 complete

Created crate `src/tools/quest-editor/`:
- `Cargo.toml` — lib + bin, dep = `anyhow` only (UI/data deps deferred to keep
  the build fast while de-risking the format).
- `src/lib.rs` — exposes `qsd`.
- `src/qsd.rs` — structural codec (`QsdFile`/`Pattern`/`Trigger`/`Entity`),
  parse + to_bytes, raw-blob payloads, + unit tests.
- `src/main.rs` — diagnostic CLI: `verify` / `stats` / `dump`.
- `tests/roundtrip.rs` — acceptance test: every retail `.QSD` must round-trip
  byte-exact.
- Registered in `src/Cargo.toml` workspace members.

**Results (`cargo run -p quest-editor -- verify/stats ../data/3DDATA/QUESTDATA`):**
- **219 `.QSD` files — 100% parse + byte-exact round-trip. 0 failures, 0 drift.**
  The format is fully accounted for. ✅ This was the main risk; it's retired.
- 632 patterns, 1765 triggers total.
- `size_field` is **always 12** → new files use 12.
- Condition type usage (top): COND 0 (quest-registered) ×1001, COND 4 (item)
  ×864, COND 3 (ability) ×646, COND 13 ×378, COND 11 ×318, COND 10 ×277,
  COND 22 ×213, COND 8 ×187, COND 2 (switch) ×176, COND 1 (var) ×173.
- Reward type usage (top): REWD 0 (quest add/del) ×1649, REWD 1 (give item)
  ×871, REWD 5 (EXP/zuly/item via btTarget) ×771, REWD 11 ×263, REWD 3 (ability)
  ×240, REWD 9 ×177, REWD 4 (char var) ×172, REWD 2 (set var/switch) ×159,
  REWD 15 ×100.

**Key surprise:** **REWD 31 = 0 occurrences.** My feasibility estimate assumed
"monster kill progress = REWD 31". Retail data uses it **zero** times, so that is
NOT how retail hunt quests count kills. The real mechanism is almost certainly:
the monster's NPC death-event trigger runs a trigger that **increments a quest
var/switch (REWD 2)**, and the completion trigger checks that var (COND 1/2).
Must be confirmed in Phase 3 before generating hunt quests (see Open Questions).

## Open questions / TODO for later phases

- **[RESOLVED]** How does retail count monster kills? → col-41 dead-event
  trigger, client-driven, REWD_002 counter var. See "The hunt mechanism" section.
  Residual (non-blocking, do in Phase 2/3): validate the exact var-slot encoding
  and talk-trigger wiring against a real retail hunt quest once the LIST_NPC
  reader can map a col-41 name → its QSD trigger.
- **[Phase 2]** Confirm LIST_Quest.STB column meaning for name/desc strID and how
  Quest SN maps to the QSD COND_000/REWD_000 `iQuestSN`. (`io_quest.h:822-824`:
  col1=time limit, col2=owner type, col3=icon; name/desc via STL.)
- **[Phase 4]** How are new STL string keys allocated/encoded in
  `LIST_QUEST_s.stl` + `Quest_s.STB`? (Check roselib STL write support.)
- **[Phase 4]** NPC col-41 string syntax: how multiple triggers are listed on one
  NPC, and click-trigger vs death-trigger distinction.
- **[Phase 3]** Typed builders for the entity payloads we emit: COND 0/1/2/4,
  REWD 0/1/2/5 (+ whatever the kill-counter answer turns out to be). Mirror the
  `STR_*` structs as `#[repr(C)]`, unit-test build→parse→equal.

## How to run

```powershell
cd src
cargo test  -p quest-editor                                   # unit + round-trip tests
cargo run   -p quest-editor -- verify ../data/3DDATA/QUESTDATA # round-trip report
cargo run   -p quest-editor -- stats  ../data/3DDATA/QUESTDATA # type histograms
cargo run   -p quest-editor -- dump   ../data/3DDATA/QUESTDATA/QN-001.QSD
```
