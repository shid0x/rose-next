# Quest Editor — Roadmap

V1 (committed) creates **Hunt** quests ("kill N of monster X" → exp/zuly/item),
accepted/completed via GM commands, with a named token quest-item. Append-only
writers with `.bak`, dry-run, and an egui wizard. Known limit: one quest per
monster (the col-41 ownership rule), and no NPC dialogs.

Below is the planned work, by tier. **Tier 1 complete; Tier 2 #5 (chaining) done.
Next decision: Tier 2 #4 (NPC dialogs).**

## Tier 1 — Quick wins (finish the MVP, low risk)

1. **Fetch quests ("bring N of item X")** — ✅ done (2026-06-21).
   `COND_004` item check + `REWD_001` op-0 remove on turn-in. No monster/col-41/
   token. 2 triggers. The spec is now a unified `QuestSpec { kind: QuestKind }`
   (`Hunt` | `Fetch`); the wizard has a Hunt/Fetch toggle and an item picker; CLI
   adds `create-fetch`. Verified in a temp copy.
2. **Audit the `Init` overload bug elsewhere.** — ✅ done (2026-06-21).
   Swept every 2-arg `.Init(`; found one more instance besides `REWD_001`:
   `F_QSTREWD032` (acquire quest item) — same `unsigned` first arg, fixed with
   `(int)` cast. All other call sites are `int`/`short` (safe) or 3-arg explicit
   `type,no,qty`. See `reference_item_init_overload_trap` memory.
3. **A `verify`/lint step before writing.** — ✅ done (2026-06-21).
   `verify::verify(ds, spec, gen) -> Vec<Issue>` (Error/Warning): count ≥ 1, quest
   SN is the append slot, QSD round-trips, QSD file absent, Hunt token id ≤ 999 +
   monster col-41 free, Fetch/reward items exist + id ≤ 999. The wizard shows
   issues live and disables Create on any Error; the CLI prints them and refuses
   to write on errors.

## Tier 2 — The big features (real player-facing quests)

4. **NPC dialog accept/complete (`.CON` subsystem).** The largest gap: today only
   GM commands accept/turn-in quests. Letting an NPC give the quest needs the
   `.CON` conversation system (a second binary format + `LIST_EVENT` + zone NPC
   event wiring). Two sub-options: (a) append a quest branch to an NPC's existing
   `.CON` (medium), or (b) generate a new dialog from scratch (largest). Multi-week;
   deserves its own go/no-go decision before starting.
5. **Trigger chaining (use *any* monster).** — ✅ done (2026-06-21).
   A Hunt quest can now target a monster that already has a dead-event hook. The
   writer finds the host `.QSD` holding the monster's col-41 trigger and splices
   our kill trigger in **right after** that entry: our trigger inherits the
   entry's original `check_next`, and the entry's `check_next` is flipped to 1 —
   so the original chain is preserved and ours is just tried next on a
   conditions-mismatch. col-41 / LIST_NPC is left untouched. The generator emits
   the kill trigger separately (`GeneratedQuest::host_kill_trigger`) so this
   quest's own QSD holds only register + complete. Host QSD is backed up to `.bak`
   and the modified file is re-verified to round-trip. The wizard auto-detects
   occupied monsters (`[+chain]` tag), sets the chain flag, and shows a warning
   that an existing quest file will be modified. Caveat: if a player holds two
   quests on the same monster, the kill credits whichever trigger comes first in
   the chain (retail behaves the same).

## Tier 3 — Polish / nice-to-haves

- **Icon picker** for the token (visual grid; reuse the shop editor's icon code).
- **Edit / delete a generated quest** (today it's append-only).
- **Live "0/N" counter in the journal** — switch to / add the counter-var model
  (`REWD_002`) instead of pure item-collection. Cosmetic.
- **Repeatable / one-time flag** — engine supports it; just wiring.
- **Multiple objectives** (kill A and B; fetch X and Y).

## Recommended order

Tier 1 in full first (Fetch → Init audit → verify step) — finishes the original
MVP and hardens the tool, ~2–3 days, low risk. Then a deliberate decision on
Tier 2 #4 (NPC dialogs), which is the highest value but by far the highest cost.
