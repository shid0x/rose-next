# Multi-step chain quests — planning note

Status: **not started** — planned feature for a later session (agreed 2026-07-15).
Read `PROGRESS.md` first when resuming; this note captures what we learned about
the retail chain model while building the NPC overhead quest icons, so we don't
have to re-derive it.

## Goal

Let the editor generate **staged quests**: "talk to A → kill/collect → deliver
to B → B sends you to C → final reward", instead of today's single-stage
accept→complete (with multi-objectives). This is how the retail job-change and
story quests are built.

## The retail mechanism (verified against retail QSDs, 2026-07-15)

One *player-visible* quest is a **chain of LIST_QUEST SNs occupying a single
quest-log slot**, advanced by `REWD_000` quest ops:

| op | engine effect (`F_QSTREWD000`, io_quest.cpp) | use |
|----|----------------------------------------------|-----|
| 1  | `Quest_Append(sn)` — register in a new slot  | initial accept |
| 4  | select registered `sn` as current (fails trigger if absent) | put at the START of any step trigger's rewards |
| 2  | `SetID(sn, false)` — swap the slot's quest id, **keep** vars/items | advance to next step, carrying progress |
| 3  | `Init()` + `SetID(sn, true)` — clean restart into `sn` | advance with a reset (fresh vars/items) |
| 0  | `Init()` — clear the slot | final completion (also used by abandon options) |

Concrete retail example (EM01 dealer job quest): trigger `1003-01` has
conditions `[COND_000(1003), COND_004(items)]` and rewards
`[op4 select 1003, REWD_001 …, op2 swap → 1004]`; the follow-up NPC's dialog
fires `1004-01` which finishes with `op0` + rewards. Each step's trigger lives
in the dialog of the NPC where that step happens.

Other facts to reuse:
- **Step gating**: each step trigger's conditions are `COND_000(current sn)` +
  step requirements (`COND_004` item counts, `COND_001/002` quest-var checks).
  Because each step has a different SN, only the current step's trigger passes —
  no step-index variable needed (the SN *is* the step index).
- **Quest text per step**: each SN is its own LIST_QUEST row → per-step
  title/description in the journal for free (LIST_QUEST_S.STL rows per step).
- **Kill triggers** reference the *current step's* SN in their `COND_000`, so a
  hunt objective belongs to a specific step.
- **Abandon**: retail offers delete-only triggers (`op0`, no grants) from the
  giver dialog at any step. (The icon classifier deliberately ignores these.)
- **Icons come for free**: an op2/op3 step trigger classifies as *turn-in* in
  `classify.rs` / client `cevent.cpp`, so the overhead "?" automatically points
  the player at the next NPC when the step's conditions pass. Keep generated
  chains classifier-clean (the `verify.rs` icon check + `classify.rs` tests
  will catch regressions).

## Sketch of the editor feature

- **Spec model**: `QuestSpec.steps: Vec<Step>` where a `Step` has its own
  objectives (reuse `Objective`), NPC (dialog wiring), texts, and
  advance-mode (op2 carry / op3 reset). Final step completes with the existing
  `complete_reward_list` (op0 + rewards). Single-step specs must stay
  byte-identical to today's output (same backward-compat trick as
  `extra_objectives: #[serde(default)]`).
- **SN allocation**: N steps consume N consecutive LIST_QUEST rows
  (`next_free_quest_sn()` + i). Manifest stores all of them; delete must blank
  every row + un-wire every step's dialog + kill triggers.
- **Trigger naming**: keep `<sn>-1` accept / `<sn>-2/-3` complete per step SN
  so nothing else changes (`switch-check`, reconstruct, delete scans).
- **Dialog wiring**: each step's NPC gets a QEX1-appended option gated on
  `QF_findQuest(<step sn>) >= 0` + `QF_checkQuestCondition("<step trigger>")`.
  Multiple steps on the SAME NPC work naturally (different SNs gate them).
- **One-time switch**: only on the *first* accept (`COND_014`) and set on the
  *final* completion (`REWD_015`), like today.
- **Reconstruct/edit**: `write::reconstruct_spec` must learn to walk op2/op3
  links to rebuild the step list from data (or just require the manifest for
  chain quests, v1).

## Open questions for the implementation session

- Journal counter text per step ("0/N") — same cosmetic gap as single quests.
- What happens if the player abandons mid-chain: op0 clears the slot; re-accept
  restarts at step 1 (retail behavior). Acceptable? Add per-step re-entry?
- Wizard UX: a "steps" list mirroring the objectives editor, each step with an
  NPC picker (reuse the giver section + "already offers" info).
- Delete of a mid-state character (player currently ON step 2 of a deleted
  quest) — same stale-quest-id hazard as before; see the stringmanager crash
  fix (memory: stale deleted-quest crash). Consider a check note in the UI.
