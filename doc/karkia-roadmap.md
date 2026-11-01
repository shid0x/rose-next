# Karkia Import Roadmap

Companion to [doc/karkia-survey.md](karkia-survey.md), which is the evidence. This is the
plan. **Stages 1-3 are built** (`scripts/import-karkia.py`, `scripts/add-karkia-travel.py`);
stages 4-6 are not. Stages 1 and 2a are validated in game.

Two scope decisions are settled going in:

- **No quests.** Karkia's are Japanese, and the RoseZA quest import was the painful half of
  `project_oro_import`.
- **Travel is NPC-driven, not gate-driven.** Karkia has no entrance at all. Rather than
  author warp-gate objects into maps we have never opened, the way in is a dialog option on
  NPCs that already do travel — a solved problem here, see §2.
- **The Memories cluster and the Tower stay unreachable, deliberately.** Jrose gated 133/134/144
  behind a quest about Karkia's past and 136 behind an endgame wave activity. They are content
  we will author our own way into, not holes to patch.

---

## 1. What "done" looks like

Nine zones a level-220-ish player can reach, fight through and leave, with monsters tuned
to our curve, loot worth taking, and no dependency on Jrose's quest chain or its
instanced-dungeon generator.

```
   Jones (Junon Polis) / Nova (Orlean Portal Temple)
                     |  "What of the dead world, Karkia?"
                     v
  86 Church  <--191--  87 CEMETERY  --192-->  88 SpireVil  --185-->  135 TowerPlace
  (arrival)              ^  |   ^               ^  |                     |
                         |  |   +----173--------+  |                     |
                         |  |                      +--------186----------+
                      178|  ^179
                         v  |
                  131 Burned Forest

  134 MemoriesBoss --180--> 133 Memories <--190-- 144 FlowerGarden     136 Tower
       [reached by a quest we have not written]                    [endgame activity]
```

The gates are Jrose's own, restored. The way in is one dialog option on two existing NPCs.
**The Church has no gate back out** — Jrose gave it one in and none back — so leaving is a
Return scroll until stage 6 places its NPCs and one of them hosts the return trip.

---

## 2. The travel design

`scripts/add-karkia-travel.py` writes one QSD trigger, `Karkia-TravelToChurch`, appended to
`QP401.QSD` as a `KarkiaTravel` pattern — the same file and mechanism Oro's travel uses. It
teleports to zone 86 at that zone's own `start` event position, read out of the `.ZON` rather
than pasted (the file stores `x, z, y` and both horizontal coordinates need a half-zone bias;
`add-oro-travel.py`'s `zon_event_positions()` already gets this right).

`quest-editor con-warp` then appends the dialog option to both NPCs that already do travel,
through the QEX1 appendix, so each keeps everything it already offered:

| NPC | where | already offered |
|---|---|---|
| 1104 `[Historian] Jones` | Junon Polis (zone 2) | the trip to Oro |
| 2101 `[Interplanetary Guide] Nova` | Orlean Portal Temple (zone 73) | the trip home, the Oro fate choice |

The Church rather than the Cemetery because it is Karkia's town: no monsters, and ten NPCs
once stage 6 places them. The Cemetery is a 7x7 field of level-211+ monsters.

**Verify a `.CON` edit by decoding, never by grep.** The payload is XOR'd
(`xor_key(len, file_size)`: len when odd, else file size), so a plaintext search reports a
correct file as broken — which it did here on the first check.

---

## 3. Stages

Same shape as `scripts/import-oro.py`: one script, `--stage N`, idempotent, `--dry-run`,
`--selftest` proving every writer round-trips byte-identically before anything is touched.
Each stage is independently testable in game and independently revertible.

`data/` is gitignored, so **the script's docstring is the only committed record** of what was
done and why.

### Stage 1 — terrain, art, zone rows  *(DONE + validated in game, 2026-09-07)*

Built as `scripts/import-karkia.py`. Result: 962 map files (59.96 MB), 208 terrain
tiles (12.12 MB), 16 object tables, 462 art files (13.01 MB), 2 sky textures; 88 `.IFO`
with entity lumps emptied, 9 `.ZON` given a `LUMP_ECONOMY`, `LIST_ZONE` grown to 145
rows, `LIST_ZONE_S.STL` +9 keys, `LIST_SKY` row 17 added. Then
`scripts/add-dds-mipmaps.py` gave mip chains to the 419 new power-of-two textures --
all 419 were files this import created, no pre-existing texture was touched.
`--verify` and a re-run (fully idempotent, 0 of everything) both pass.

In-game test found two defects, both fixed in e22e156e and neither in the map data:
`TEST_ZONE_NO` was 100, a hard ceiling that left zones 131+ with no `CZoneTHREAD`, so
86/87/88 worked and the rest dropped the player on teleport (see
`reference_zone_number_ceiling`); and the asset walker missed the `.zmo` an `.eft` names,
costing 15 motion files. Re-tested: all nine zones load, assets display. What it does:

Copy the 9 `.ZON`/`.IFO` sets with the MOB, REGEN, WARP and EVENT_OBJECT lumps emptied on
the way in (count = 0, lump table untouched) so later stages just refill them. Plus the 16
map `.ZSC`s, `3Ddata\KARKIA\`, the 208 terrain tiles, the new map meshes and textures,
`LIST_SKY` row 17 + the two `LUNAR\Sky02` textures, `LIST_ZONE` rows 86–144 with fresh
`LZON*` STL keys, and English zone names via `scripts/add-zone-name.py`.

Two things this stage must get right:

- **Append a synthetic `LUMP_ECONOMY` to each `.ZON`.** Karkia's are the stripped late-Jrose
  variant with no lump 4, so `CEconomy::Load` never runs and the server's zone economy runs
  on uninitialised heap. Copy the five ints from a comparable zone of ours.
- **Never overwrite an existing `.dds`.** 331 of the shared textures differ only because ours
  have mip chains and Jrose's do not. `copy_new()` never overwrites, which makes that safe by
  construction; the new textures then get chains of their own from
  `scripts/add-dds-mipmaps.py` (419 files, every one written by this import).

Also renumber on the way in: `WARP.STB` 170 and 172 are live Oro gates, and STL key
`LZON086` belongs to our zone 82.

**Acceptance:** GM-warp into each of the nine and walk around. No missing-asset lines in
`error.txt`, no black terrain, minimap draws, zone name shows above it. Run
`scripts/audit-zsc-bounds.py` afterwards — Karkia adds 16 ZSC tables and the cached bounding
boxes in every ZSC in the game are wrong (`doc/zsc-bounding-boxes.md`), so confirm nothing
under-covers badly enough to pop out of the frustum.

### Stage 2 — gates and travel

Split into two pushes, because the halves have very different risk. 2a is data through the
machinery stage 1 already proved; 2b needs a `.CON` built from scratch, which is new Rust.

#### Stage 2a — the internal warp gates  *(DONE, 2026-09-07)*

`--stage 2`. 10 `WARP.STB` rows and 11 gate placements back into the `.IFO` WARP lumps.
**170 and 172 were remapped to 191/192** — they are live Oro gates (`TOWN→ODE01`,
`ODRP01→ODE01`), so copying them verbatim would have silently redirected Muris. That puts
every Karkia gate in one contiguous 173–192 band; our table's last occupied row was 172.

Everything but the id remap and the English names is read from the source at run time — the
destination zone and event name from Jrose's `WARP.STB`, the placements from its `.IFO`s — so
a gate the script does not know about cannot go missing quietly. All 10 destination event
positions were verified to resolve byte-for-byte in the destination `.ZON` **before** anything
was written; a miss there is what produces an `IS_HACKING` disconnect rather than a failed
warp. `--verify` also asserts Oro's 170/172 still point at zone 82.

This makes each cluster walkable: 87 ↔ 88 ↔ 135, 87 ↔ 131, 87 → 86, and 133 ↔ 144, 134 → 133.
There is still no way *into* Karkia — that is 2b.

#### Stage 2b — the way in  *(DONE, 2026-09-07)*

**Scoped down from a per-zone Wayfinder to a single option on two existing NPCs.** The
Memories cluster (133/134/144) and the Tower (136) are not holes to patch: they are content
Jrose gated behind a quest about Karkia's past and an endgame wave/boss activity, and we will
author our own way in later. So no new NPC was needed, and no new Rust either.

`scripts/add-karkia-travel.py` writes one QSD trigger, `Karkia-TravelToChurch`, appended to
`QP401.QSD` as a `KarkiaTravel` pattern — the same file and mechanism Oro's travel uses. It
teleports to **zone 86, the Church**, at that zone's own `start` event position read out of
the `.ZON` rather than pasted. Then `quest-editor con-warp` appends the dialog option to both
existing travel NPCs through the QEX1 appendix:

| NPC | where | already offered |
|---|---|---|
| 1104 `[Historian] Jones` | Junon Polis (zone 2) | the trip to Oro |
| 2101 `[Interplanetary Guide] Nova` | Orlean Portal Temple (zone 73) | the trip home, and the Oro fate choice |

The Church rather than the Cemetery because it is Karkia's town: no monsters, and ten NPCs
once stage 6 places them. The Cemetery is a 7×7 field of level-211+ monsters.

**Known gap: the Church has no way out.** *(CLOSED 2026-09-08 — see stage 2c.)* Jrose gave
it a gate in (191, from the Cemetery) and none back, and its NPCs are not placed until
stage 6.

**Acceptance:** walk every gate in both directions, then teleport to all nine zones and back
out to Junon. No `IS_HACKING` disconnects — that is what a destination event position
resolving to NULL looks like, and it is why the Oro importer verifies every destination
byte-for-byte before writing.

#### Stage 2c — the way *into* Karkia  *(DONE, 2026-09-08)*

**This was a live bug, not a nicety, and it made the whole planet unreachable.** The
Abandoned Church has **zero warp triggers**:

| zone | warp triggers placed |
|---|---|
| **86 Abandoned Church** | **0** |
| 87 Desolate Cemetery | 3 (→88, →131, →86) |
| 88 Spire Village | 3 |
| 136 Tower of Despair | 0 (empty arena, by design) |

Gate 191 runs Cemetery → Church one-way. So a player took Jones' trip, landed in the
Church, and could reach **nothing** — not the Cemetery, not Spire Village, not any of the
content in stages 3–6. The only exit was Petri's teleport home. Everything built since
stage 3 was reachable only by GM teleport, which is exactly why it went unnoticed.

Fixed as a third leg in `add-karkia-travel.py` (`Karkia-TravelToCemetery`, landing on
zone 87's own `start` event at 5620,4725) plus a `con-warp` option on **[Church Guard]
Kashi (4145)**.

Kashi is the right host by more than casting: decoding his `.CON` shows Jrose *already*
built him as the Cemetery gate guard — he carries `TA_Goto_Cemetery`, `AT_Goto_Cemetery`
and a `Kakia-gotocemetery` trigger, all gated behind a `chk-churchout-qsw` quest switch
we never imported. His greeting is already "It's dangerous out there. Still going?". We
are re-opening a door the original design put there, not inventing one.

**And that gate was not harmless — Kashi was mute in game.** His root also carries
`TA_CanNotExit_Church`, and its bytecode carries an extra instruction the neighbouring
`TA_Goto_Cemetery` does not: it **negates** its quest check. So it is true precisely
*because* the `chk-churchout-qsw` switch was never imported. It ran after his greeting,
found no text, and closed the window the greeting had just opened — mute NPC, and the
new exit option appended to nothing.

Third instance of the pattern after Holk and Brown, and the one that showed the earlier
fix was too narrow: **"move the greeting last" is wrong once the menu ends in options.**
A speech node (`SC_MSG_NPCSAY`/`NEXTMSG`) opens a window and closes whatever was open;
an option node (`CLOSE`/`PLAYERSELECT`/`JUMPSELECT`) only appends a line to the window
already open. The greeting must therefore run **after every other speech node but ahead
of every option node** — which is what `target_slot` now computes, and what an appended
`con-warp`/`con-store` option makes necessary.

Do not import `chk-churchout-qsw` without re-checking this NPC.

**Petri and Nemo were hardened at the same time.** Neither was broken; both were
*fragile* — greeting at root[1] with gated speech nodes after it, working only because
every one of those gates happens to be false. Both carry something a player cannot do
without (Petri is the only way home; Nemo's shop hangs off her greeting's child menu),
so both were promoted. **Eleven other reachable NPCs are fragile the same way** and were
left alone deliberately: they carry only flavour, and each promotion changes a file that
currently works. The pessimistic simulation in the roadmap's tooling names them.

Simulated result:

> "It's dangerous out there. Still going?"
> 1. I'll come back later.
> 2. **Let me out to the Cemetery.** → "Out there? It's crawling with the dead, and they don't stay down. You're sure?" → [I'm sure. Open the gate.] / [On second thought, no.]

### Stage 3 — monsters, un-tuned  *(DONE, 2026-09-07)*

`--stage 3`. 38 `LIST_NPC` rows at native ids 2685–2731, 37 STL keys (2689 and 2731 share one), 36 AI rows and 34 `.aip` files, 38 `LIST_NPC.CHR` entries with +32 models / +16 meshes / +40 materials in `PART_NPC.ZSC`, 70 art files, 2 character effects, 2 mob-weapon presentation rows, and 2,011 spawn points into 77 `.IFO`s. Idempotent; `--verify` passes.

Names are authored, not copied — Jrose's `LIST_NPC_S.STL` is the legacy `I_NUM` dialect with Japanese text and no language blocks. Cross-checked against the `.aip` filenames where those name the creature (`kak_spider` → Murilo, `kak_neggolem` → Neg Golem).

Two things found by running it:

- **`import_characters` crashed on a sentinel.** Three Karkia monsters carry an anim entry of `(65535, 52685)` — `0xCDCD`, MSVC's uninitialised-heap fill, written out by whatever built these files. **Our own `LIST_NPC.CHR` has 112 of them** across 12,027 entries, so it is tolerated padding, not corruption. Out-of-range pool indices now pass through verbatim instead of being remapped (there is no string to intern) or dropped (which would make imported rows a different shape from the ones we ship).
- **Weapon row 1137 has no bullet effect in either table.** Harmless here: `UsesProjectileAttackPresentation()` is `weapon > 0 && bullet_effect > 0`, so an empty row just selects the melee hit frame — correct for its users, which are melee at 250 cm. The warning now applies the game's own test rather than firing on every empty row.

#### Stage 3b — the monsters' skills  *(DONE, 2026-09-07)*

Folded into `--stage 3` as step 3i. **36 of 36** skill ids the Karkia AI casts now resolve.

§6 said 23 ids with six needing attention. Both numbers were low: that survey walked a
hand-listed set of `.aip` filenames covering only the 31 *spawned* monsters, and the seven
AI-summoned-only ones have AI rows of their own carrying five more skills (3711, 3771,
3779–3781, on the two Hebarn Officers, the Corroded Golem and the Revived Veteran). Deriving
the AI file from each monster's `LIST_NPC` col 16 instead of a filename list is what found
them — and is what the verifier does now.

Final tally: **9 rows ported**, **2 re-pointed**, 25 already correct.

| | |
|---|---|
| ported into their own (blank) rows | 3613, 3616, 3627, 3711, 3771, 3779, 3780, 3781 |
| ported to a different row | 3685 → 3686 (3685 is our "GM Blessing") |
| re-pointed in the `.aip` | 716 → 361 Berserk, 846 → 1090 Tornado |
| assets | `FILE_EFFECT` +2 rows, `QUESTARUA_EXP.EFT` + chain (6 files) |

Two judgement calls. **Berserk** re-points to rank 1 because theirs is rank 1 of its family —
matching the rank position is the least invented choice, and stage 4 can raise it.
**Tornado** re-points to rank 10 instead, matched on *power*: theirs is power 500, and our
Tornado family only runs ranks 6–10, of which rank 10 is power 501. The rank numbering does
not correspond, so matching on it would have been meaningless.

Col 0 is authored rather than copied: theirs is a Japanese editor label and the cp932 bytes
become mojibake in our UTF-8 table. The server reads `SKILL_NAME` from col 0 while the client
shows the STL name, so an ASCII label is both correct and readable.

#### What stage 3 deliberately did not do

38 `LIST_NPC` rows at **native ids 2685–2731** (all free on our side, which makes the AI's
summon references resolve with no binary patching), the 29 `.aip` files, character
models/skeletons/motions with the usual index remap, and the REGEN lumps refilled.

Copy for identity — name, model, size, sounds, attack type, range, AI id. Leave the stat
columns as Jrose wrote them for exactly one play session, so we can see the fights before
we change them, then never again.

Zero AI opcodes here are new to our server, so nothing to implement. The `.aip` files copy
verbatim; six skill references need attention (§6) — three re-point at skills we already own,
three port across with one effect asset. None needs authoring.

**Acceptance:** monsters spawn, animate, path, attack, and die. `/dps` shows damage flowing.
The D=Seed's summon behaviour fires. Nothing crashes.

#### Stage 7b — populating the flashback  *(DONE, 2026-09-08)*

53 regen points across three zones held **id 1 in every slot**, and so did
**Jrose's own files** — they laid the points out and never assigned a monster.
Nothing here is restoration; the rosters are authored, and the only authority for
what belongs is what the NPCs ask for.

Seven monsters imported at native ids (2520–2560 was entirely empty on our side),
with **zero new art files** — every mesh and texture they need was already in the
tree:

| zone | roster | from |
|---|---|---|
| Memories 133 | Melitta, Melan Melitta, Calaplasinos, + Basilissa Melitta on the tactics list | Pormello's ecology quests |
| Garden 144 | Mukuroji, Nigaki, Beeberu | Nitraria's leaf collection |
| Burned Forest 131 | Woodnoid, Dark Tower, Evil Fairy, both Gargoyles | **not a flashback zone** — its gates run to and from the Cemetery, so it is present-day Karkia and reuses that roster |

Each point carries five basic slots and two tactics slots at a cap of seven, and
the tactics list is the escalation set — the right home for the swarm's queen.
`SPAWN_ROSTER` lives inside stage 3 for the same reason `SPAWN_THINNING` does:
the stage rebuilds every REGEN lump from source on each run, so an after-the-fact
edit would be silently undone by the next `--stage 3`.

Bands: Memories 230–238, Garden 235–240 — reached from the Church by a player who
already crossed Karkia, so they sit at the top of the planet.

**Three traps, all caught by `--verify` rather than by reading:**

- **Mukuroji casts on an animation it does not have.** It uses the `Pig1` model,
  which carries only the six basic clips in *both* dumps, and its `.aip` casts
  skill 3050 on `nMotion` **8** — so the missing pair is 8/9, not the 6/7 of the
  earlier cases. Same defect as 2692/2703 and the same fix. Left alone it is an
  invisible cast: damage with no animation.
- **The Burned Forest silently re-planned the Cemetery.** `zone_of` is a
  `setdefault` over an alphabetical glob and KBURNEDFOREST sorts before
  KCEMETERY, so five shared monsters were attributed to a zone with no band,
  dropped out of the Cemetery's median, and moved 14 other rows. `ZONE_ALIAS`
  now folds it into the Cemetery, which is the honest model anyway.
- **`add-karkia-drops.py --restore` wiped the new monsters.** It backed up
  `LIST_NPC.STB` whole, and four scripts write that file, so the copy went stale
  the moment the import ran — the restore reverted both the monster rows and
  Belfa's shop repoint. LIST_NPC is now restored **cell by cell** from the
  sidecar; only `ITEM_DROP.STB`, which is ours alone, keeps a file backup.

Drops: tables 909 (Memories), 910 (the queen), 911 (Garden), with 133/144
mirroring their own and 131 mirroring the Cemetery's. Materials are deliberately
**woods and weaves** — the ones the present-day tables refuse — so the two eras
read differently in your bag. Rates land on the house figures: shields 1 in 30,
weapons 1 in 32, and the queen 19% weapons. The seven also imported with
`NPC_DROP_ITEM` of 10–30 against the 80 the rest of Karkia uses, which at 10
makes the drop roll usually fail outright *and* inverts the table-vs-zone split;
raised to 80.

#### The `D=` prefix  *(fixed 2026-09-08)*

Twelve Devil Pest monsters were authored as `D-Victim`, `D-Ghoul Ein`, `D-Seed` and so on.
The source spells that prefix with an **equals sign** — Jrose's own player-facing rows are
`D=シード` and `Ｄ＝エラー` — and a line of dialog shipped in 6c quotes it directly:

> "So every monster with **'D='** in its name is a victim of the Devil Pest."

which the Field Medic says while standing next to twelve monsters whose names all began with
a hyphen. **The player was told to look for a prefix that appeared nowhere in the game.** Ten
more lines name `D=Seed`, `D=Victim`, `D=Error` and `D=Alma Core` the same way, several as
hunt instructions.

`scripts/fix-karkia-monster-names.py` corrects it and stays as a general re-sync tool. It is
separate from the import for a structural reason: `import-karkia.py` stage 3 writes a name
only into a row it is *creating* (`if our_npc.occupied(i): continue`, and likewise
`if our_stl.has(key): continue`), which is exactly what makes re-running it safe for the
balance passes — and also what makes it unable to ever *revise* a name. The rename pass
writes names and nothing else, so it runs against live rebalanced data without touching a
stat.

Both sides must be written, and nothing warns you if you miss one:

    server   NPC_NAME(I) -> g_TblNPC.get_cstr(I, 0)         LIST_NPC.STB col 0
    client   NPC_NAME(I) -> CStringManager::GetNpcName(I)   LIST_NPC_S.STL, keyed by
                                                            NPC_STRING_ID = col 40

Write only the STB and the server renames it — GM tools, logs — while every player still
reads the old name from the STL.

### Stage 4 — the balance pass  *(DONE, 2026-09-07)*

The real work, and the reason Karkia cannot ship on Jrose's numbers. Karkia DEF is 2,350 at
level 213 where ours is 737, which puts every player swing on the damage floor of 5 —
4,229 swings to kill a level-213 trash mob, which kills the player in three hits. Full
measurements in the survey §7.

Re-derive level, HP, ATK, DEF, RES, HIT, AVOID and EXP from our own curve with the existing
passes: `rebalance-endgame-curve.py --stat def|res` fits the trend from levels 60–199,
`rebalance-oro-bosses.py` sets boss HP to 10x the HP trend, `rebalance-exp-rewards.py`
handles the 133x EXP gap. **Mind the order dependency** recorded in
`project_monster_balance_passes`: the DEF/RES pass reads a monster's current level and the
boss sidecar, so any level change means restore, re-apply, re-verify all of them.

Write it as a `rebalance-karkia.py` in the same style — idempotent, sidecar next to the STB,
`--dry-run` / `--verify` / `--restore` — not as edits folded into the importer. The importer
should stay a faithful copy so it can be re-run.

The three placeholder zones stay empty for now (§4, decision 4).

**Acceptance:** re-run `scripts/balance-sim.py` against the written rows and check
swings-per-kill lands in the same band as our own monsters at that level. Then actually play
it.

**Result.** `scripts/rebalance-karkia.py` does levels, HP and ATK; DEF and RES are left to
`rebalance-endgame-curve.py`, which already caps every level-200+ monster at the trend
fitted from levels 60–199 and had simply never seen Karkia (it last ran in August). Measured
with the server's own PVM damage formula against a level-240 character:

| | before | after |
|---|---:|---:|
| swings to kill a trash mob | 3,481 | **67** |
| hits the player survives | 2.8 | **18.6** |
| Neg Golem, the tankiest field elite | 143,650 | **193** |

Two things that were nearly missed:

- **The DEF number in the paragraph above is the one that mattered, and it was the only one
  a new script did not need to touch.** Duplicating that cap in `rebalance-karkia.py` would
  have left two copies to keep in sync; restoring and re-applying the existing pass after
  the levels moved was the whole fix.
- **The boss sidecar is load-bearing.** `rebalance-endgame-curve.py` resolves a boss as
  "listed in a sidecar **or** `NPC_HP >= 1000`", and budgeting boss HP moves Karkia's bosses
  *below* that threshold while its ordinary trash (Neg Golem at 6,500) sits *above* it — the
  heuristic fails in both directions here. It now reads a list of sidecars and Karkia writes
  its own.

Ranking inside a zone is preserved, not flattened: each monster's ratio to its zone median
is square-rooted and capped, so the Neg Golem stays the tankiest thing in the Cemetery at 3x
ordinary trash instead of 41x. Levels are remapped linearly inside each zone so Jrose's own
progression survives. All seven balance passes `--verify` clean.

### Stage 5 — drops  *(wiring DONE 2026-09-07; content DONE 2026-09-08)*

Split, because the plumbing turned out to be broken in a way the content pass would
have inherited. `scripts/rewire-karkia-drops.py` fixed it and
**[doc/project-drops.md](project-drops.md) is the plan for the rest.**

Two collisions, both from one cause — a drop-table id and a zone id share a single
namespace in `Get_DropITEM`:

- Karkia's own tables (Jrose's 831/832/835/851–854) were live for our Desert
  Scavenger, its Agitated twin, and the **Fearsome Terrasaurus King**. Authoring
  Karkia loot into them would have handed it to Oro. Now 900–906.
- Karkia's **zone fallback** rows (86, 87, 88, 131, 133–136, 144) were live tables
  for thirteen of our own low-level monsters — rows 86 and 87 are named *"EVE Zone
  (Fishing System)"* and *"(Farming System)"*. With drop chance at 20, **~80% of
  Karkia's drops came from those tables**, which is what players were actually
  picking up. Those tables moved to 940–948 with their users repointed; chance is
  now 80, our own level-200+ median.

Karkia now drops **nothing**, deliberately: correct-and-empty is a better base than
plausible-and-wrong, and the content pass is now pure content.

Two things §5 of the drops doc records that change what is possible:

- **A drop cell is `type * 1000 + number`, so item numbers above 999 cannot drop at
  all.** The 73 imported Jrose weapons (1381–1453) are unreachable as loot; the 35
  shields and 43 of the back items are fine. The weapons want a Karkia NPC shop in
  stage 6 rather than an encoding change.
- **Oro has the identical defect** — all 40 of its level-200+ monsters point at empty
  tables and it has been running on its zone fallback since import. The endgame has
  never had an authored loot tier; Karkia only made it visible.

### Stage 6 — NPCs

Split into four, because only the first is mechanical.

#### Stage 6a — place them  *(DONE, 2026-09-07)*

`--stage 6`. 32 `LIST_NPC` rows with English names, 32 STL keys, 32 CHR entries,
42 `PART_NPC` models (+17 meshes, +25 materials), 66 art files, 32 `LIST_EVENT`
registrations, the 32 `.CON` dialogs, and the placements into the `.IFO` **MOB**
lumps — NPCs are a fixed placement carrying an AI row and a `.CON` name, not a
REGEN spawner. Ten in the Church, five in Spire Village, five at the Foot of the
Tower; twelve more wait in Memories and the Garden.

The dialogs came in **verbatim**, which is only safe because **none of the 32 offers
a quest** — checked with `quest-editor con-triggers` across all of them rather than
assumed, so nothing can dangle from the quest chain we deliberately skipped. They
speak Japanese until 6c. One of them (`EM03-001`, the Storagekeeper) calls
`GF_openBank`, so it works today.

Three things worth carrying forward:

- **Compare `.CON` references on the STEM.** The `.IFO` stores the reference *with*
  the extension for the `EM86` family and *without* it for the rest, while
  `LIST_EVENT` always holds a full path. Comparing basenames reported two thirds of
  the set as missing — twice, once against the filesystem and once against the
  table — and both were my error, not the data's.
- **`EVENT_FILENAME` is column 3.** Column 1 is a type marker. Guessing it produced
  a confident "none of these are registered", which was wrong.
- **A CHR entry is addressed by NPC id**, and `import_characters` copies index to
  index, so the nine remapped NPCs landed at their Jrose ids and had to be moved.
  It also *skips occupied slots*, so nothing of ours could be overwritten — and all
  nine were empty regardless, our own 1074 `[Wounded Traveler] Seth` having a
  `LIST_NPC` row but no CHR entry.

#### Stage 6b — the way back out  *(DONE, 2026-09-07)*

The oldest open gap, from §1: Jrose gave the Church a gate in and none back, so
leaving was a Return scroll. `add-karkia-travel.py` now carries **both legs** — each
its own QSD pattern, so either can be added alone and a re-run of either is a no-op
— and the return lands in **Junon Polis**, read from `JPT01.ZON`'s own `start` event
rather than hard-coded. Host is **[Explorer] Petri (4146)**, the Church's own
traveller: the same casting as Jones the Historian and Nova the Guide.

**And 19 NPCs were mute, which was not the Japanese text.** `zonefile.cpp` resolves a
dialog with `_stricmp` on the **full basename** of `EVENT_FILENAME`, extension
included, and Jrose stores the reference *with* `.con` for the `EM86` family and
*without* it for the other 19. Their server tolerated that; ours sets
`nQuestIDX = 0`, so those NPCs had no dialog at all. Stage 6 now rewrites each
placement's reference to exactly what our `LIST_EVENT` row holds — **32 of 32
resolve, up from 13**.

Verify a `.CON` edit by **decoding the QEX1 appendix**, never by grep: the payload is
XOR'd, so a plaintext search reports a correct file as broken.

#### Stage 6c — English dialog  *(DONE, 2026-09-07)*

`scripts/translate-karkia-dialog.py`. **Every Karkia NPC now speaks English —
all 32, 1,865 nodes, 0 missing** (reachable set done 2026-09-07; Memories and the
Garden finished 2026-09-08 once stage 7a made them reachable).

The flashback carries the whole backstory, and it is worth knowing before writing
any quest for those zones:

- **Hebarn was not Karkia's goddess.** She governs *planet Hebarn*, and took Karkia
  on when her younger brother, the god **Karkia**, vanished. She summoned a Hero
  from another world and — per Miranda and Bordeaux — fell in love with him.
- **Karkia did not vanish. He fell.** Nagia has the reveal: he was eaten through by
  the Devil Pest, forgot he had ever been a god, and became a demon that destroys
  everything. Hebarn and the Hero could not finish him, so **the Hero shattered the
  flame of his own life to seal him**. The seal is 無限牢獄, the **Infinite Prison**
  — which is zone 134, the second of the two "towers" that ship as bare arenas.
- **Lowe's ecological survey** finds the savage animals bear a blessing
  near-identical to Hebarn's but from an entirely different, considerably stronger
  god. **Bordeaux's last contact** with Hebarn is a broken sentence and the word
  "flee". That is the present-day Devil Pest seen from before the fall, and it ties
  directly into the Arua/Hebarn fate system.

Two notes on the rendering. 夢限 is a homophone pun on 無限 (*mugen*, "infinite")
and on Nagia's own title 無限のナギア, so the pair is rendered **Endless / Dreamless**
to keep it. And one line in plainly Steinia's voice names *Steinia* as the one
keeping watch on him — Jrose's own slip; translated without the name rather than
repeating it or inventing a different one.

Astraea's and Nagia's formulaic lines (40 reagent lists, 26 weapon names) were
generated from templates rather than retyped, with an alignment assertion per
family: a transcription slip inside a cost list is exactly the error that survives
review.

Not through QEX1, as this section originally assumed — **dialog text is not in the
`.CON` at all.** A conversation node carries a `str_id` that indexes
`ulngtb_con.ltb`: row = the string id, col 0 a key, cols 1..N the per-language
text as UTF-16LE (`GetEventString(id) = GetMbcsString(lang+1, id)`). So the whole
translation is rows in that table and not one byte of any `.CON`.

Scope was the work. Jrose ships **2,383 translatable strings / 67k Japanese
characters**, mostly branches for quest chains we never imported. Two filters make
it tractable:

- drop nodes gated behind a check function — quest state that can never be true
- **de-duplicate**: "I'll pass" appears 96 times, "Thank you" 55, "Understood" 49

That is 2,383 → **654 distinct strings**, and 147 of them alone covered 54% of all
nodes.

For once an id space was free by luck rather than arrangement: Karkia's ids run
21135–33071 and our table had 20876 rows. Verified after writing that all 20,876
pre-existing rows are byte-identical.

Translations live in `scripts/karkia-dialog-en.json`, committed — `data/` is
gitignored, so authored text kept only there would be lost. An untranslated string
stays Japanese rather than being guessed at.

#### Stage 6e — the six mute NPCs  *(DONE, 2026-09-07)*

`scripts/unlock-karkia-idle-dialog.py`. Six reachable NPCs opened no dialog at
all — Blago, Emil, Ragia, Jenner, Brown, Physalis. Not the Japanese text, and
not a broken import: it is `CEvent::Conversation` doing what it is told.

```cpp
case SC_MSG_NPCSAY:
    Del_ClickITEMS();
    g_itMGR.CloseQueryDlg();
    szMessage = this->ParseMESSAGE(...m_Message.Get());
    if (szMessage == NULL) break;      // no window, AND no recursion
    g_itMGR.OpenQueryDLG(...);
    Conversation(...m_lChildDataIDX);
```

The text is `g_LngTBL.GetEventString(iStrID)`, so **a node with an empty LTB cell
renders nothing and never reaches its child menu.** Every Karkia `.CON` opens on
a `BasicMenu` node that is deliberately empty (its own Japanese reads "normally
blocked off, do not use"), so an NPC is audible only if some *later* root node
both passes its check function and has text. The talkers have an ungated
greeting there; these six had every greeting behind a `TA_*`/`AT_*` check for a
quest chain we never imported, so the root loop skipped all of them.

The fix blanks the 32-byte check-function field on one existing greeting each,
promoting it to that NPC's default line. No node added, no string resized: the
field is a fixed slot in the 80-byte record, so it is a pure in-place edit of
the XOR'd collection. Verified byte-for-byte — file sizes, node trees and Lua
tails all identical, and **every changed byte is a character of the blanked
name** (11 = `TA_Normal01`, 30 = `AT_Kakia_EpisodeQ527_Before_01`).

Which greeting, and why — none of the six has a click function of its own, so
exposing one only ever shows text:

| NPC | line | subtree |
|---|---|---|
| Blago | "……." | 1 node, no clicks — reads as a captain who won't talk to you |
| Emil | "Why in god's name did they post me somewhere this dangerous…" | 1 node, no clicks |
| Ragia | "…It is still too early for you, child." | 1 node; `root[2]` rejected, its option fires `AT_Normal04` |
| Jenner | his first-meeting line, the right default for a player who has done none of his quests | 7 nodes, no clicks |
| Brown | the "lost child" greeting into the lore of the goddess abandoning Karkia | 37 nodes, no clicks |
| Physalis | "Hmm… Is there not a decent ring to be had anywhere…" | 8 nodes; one option fired `AT_Q547_02`, so that *click* field is blanked too |

Brown's `root[3]` was rejected for a second reason worth remembering: **all** of
its options are gated, so the box would have opened with no way to dismiss it.
An exposed greeting needs at least one ungated option under it.

The only two click functions anywhere in the exposed subtrees are
`AT_GotoJunon` (left gated, on a branch we do not expose) and `AT_Q547_02`
(neutered). Nothing un-gates a quest grant or a turn-in.

**Two survived that first pass, and the reason is worth keeping.** Holk and Brown
were still mute. `Conversation` does **not** stop at the first root node that
passes — it runs all of them, and each NPCSAY calls `Del_ClickITEMS()` +
`CloseQueryDlg()` *before* the empty-string test. So the **last** match wins, and
a later node that passes its check but has no LTB text silently closes the window
an earlier one opened.

Their final root node is gated on a **default-state** predicate — `TA_Normal`
(Brown) and `TA_Yuusha_inventoryfull` (Holk) — true precisely *because* no quest
is active, and both have empty text. Every other Karkia NPC's last gate is a
positive quest check (`_Yet`, `_End`, `_Check`, `_Finish`), false for us, which is
why 18 of 20 worked. Note the tempting predictor is wrong: "last root node has
empty text" describes **17 of the 20**, including every working one.

Also note `QST_RESULT_INVALID = 0` but `QST_RESULT_FAILED = 2` and `STOPPED = 3`.
`Conversation` tests `iResult < 1`, so a *missing* trigger is correctly false but
a trigger that exists and **fails** is truthy and shows its node.

The fix for those two moves the good greeting to the **end** of the root offset
table, so it wins whatever the gates do — no Lua evaluation required. Order comes
purely from `pMenuColl->m_SubMenuMMT[j]`, so it permutes four-byte entries and
moves no node body; still no size change. Verified against the pessimistic bound
"assume every gate is true", under which both now speak. Trade-off, deliberate:
if Karkia quests are ever imported, this default overrides their greetings and
the two `PROMOTE` entries should be dropped.

Addressing gotcha: `TARGETS` names nodes by **`str_id`, never item index** —
promotion permutes the table, so an index would name a different node afterwards.
That is how the first `--verify` broke.

Knock-on: `translate-karkia-dialog.py` now reads conversation nodes from **our**
`data/3DDATA/EVENT`, not the Jrose originals. It skips gated nodes, so a node
this script exposes has to be collectable from the files we actually ship or its
line stays Japanese. That added 9 strings (1,131 → 1,140).

#### Stage 6d — shops  *(DONE, 2026-09-07; Memories sellers added 2026-09-08)*

`scripts/add-karkia-shops.py`. Five working shops, 5 tabs, 84 items.

**This section's original plan was wrong in two ways, both found by reading the
client rather than the tables.**

First: **a shop only exists if the NPC's `.CON` calls `GF_openStore`.** Exactly three
reachable Karkia conversations did — EM86-001 (Nemo), EM86-013 (Gelt) and EM02-114
(Orentark). **Belfa did not**, so the claim above that the Master Smith gives us "a
working shop for free" was false: his four tabs could never open no matter what was in
them. Worse, they were our low-level rows 478–481, whose stock is **level 38–77** —
junk in a level 215–240 zone. **Fixed in 6f below.** Orentark's Materials and Dealer
Skill tabs genuinely are free and are kept.

All three sellers call `GF_openStore(owner, 0)`, and the client draws the fourth tab
only for `bSpecialTab = 1` — so **only `LIST_NPC` cols 21–23 are usable**.

Second: **the imported Jrose weapons were sellable all along.**
`Rose::Store::encode_store_item` has a wide form, `type * 100000 + no` for ids above
999, supported by client, server and the shop editor. The 999 wall is a **drop-side**
limit only. See `doc/project-drops.md`.

A dangling tab row turned out to be harmless: `STBDATA::value` bounds-checks the flat
index and returns a default, so rows 584/585 read as an empty nameless tab rather than
out of bounds. (`CStringManager::GetStoreTabName` guards with `iIndex > row_count` —
a real off-by-one that lets `iIndex == row_count` through, harmless for the same
reason.) So rather than pad `LIST_SELL` out to row 585 with 22 dead rows, four rows
were appended at 561–564 and the two NPCs repointed.

| row | caption | stock | seller |
|---|---|---|---|
| 561 | Karkia Arms lv215 | 13 weapons, every weapon type | Gelt |
| 562 | Karkia Arms lv225 | 13 weapons, every weapon type | Gelt |
| 564 | Ammo/Arrows | 13 arrows, 18 bullets/shells, Repair Hammer | Gelt |
| 563 | Potions | 13 endgame consumables + Junon return scroll | Nemo |

Rows 563/564 are numbered to land on `LSEL563` "Potions" and `LSEL564` "Ammo/Arrows",
two **orphan STL keys already in `LIST_SELL_S.STL` with no matching STB row** — which
is why the potion tab is the lower number. The script asserts a reused key's text
matches the stock behind it, so a stale caption cannot silently sit over our tab.

The weapon tiers are picked so Karkia never duplicates Oro, whose merchant Huzam sells
210 and 230: **Oro 210/230, Karkia shop 215/225, Karkia drops 220/235/240.** The
level-240 mythical set (Bahamut, Phoenix, Griffon, …) is deliberately held back for
loot, and `--verify` proves none of the 34 reserved weapons leaked into a shop.

**The Memories sellers, 2026-09-08.** Memories became reachable in 7a and stage 8
imported the materials its NPCs talk about, so the three remaining dangling tabs were
resolved. Reading each `.CON`'s Lua constant table settled who could have a shop at
all:

| npc | `.CON` | calls | outcome |
|---|---|---|---|
| Ginias 4089 | EM03-002 | `openStore`, `repair`, `openUpgradeNormal/Durability` | repointed 593 → **563 + 564** |
| Astraea 4108 | EM03-011 | `openStore`, `SwapItem` | repointed 594 → **565**, union cleared |
| Ash 4088 | EM03-001 | `openBank` only | correct as-is — he is the storagekeeper, a bank and no shop |
| Bordeaux 4097 | EM03-010 | **nothing** | tabs 512–515 are inert exactly as Belfa's were; left alone |

**Astraea was broken twice, and the second one is the interesting one.**
`NPC_UNION_NO(I)` is `#define`d to `NPC_DROP_ITEM(I)` — game col 20, *the same cell
that means "roll my own drop table" on a monster*. Both `CStore::ChangeStore` and the
server's trade handler refuse a shop whose union is non-zero and not the player's.
Astraea shipped with **20**, while `CObjAVT::SetCur_UNION` rejects anything
`>= MAX_UNION_COUNT` (10) and the DB column defaults to 0 — so **no player can ever
hold union 20**, and fixing only her tab would have produced a shop that still refused
to open. She is the only Karkia NPC with a non-zero union. `--verify` now fails any
seller whose union is unreachable.

Armour is still unstocked — there is no armour import yet. Astraea sells **materials**
instead: she is the one NPC whose own dialog already lists them.

| row | caption | stock | seller |
|---|---|---|---|
| 565 | Karkia Materials | 12 of the 25 stage-8 materials | Astraea |
| 566 | Refined Steam lv225 | 16 — 4 classes x cap/body/arms/foot | Astraea |
| 567 | Unit Core lv230 | 16 — 4 classes x cap/body/arms/foot | Astraea |

**Armour, 2026-09-08.** Astraea is the only armour seller Karkia has — the
[Starsteel Armourer], and the only NPC there whose `.CON` opens a store whose role
fits — so both mid tiers hang off her two free tab columns rather than being split.
The level-240 Egyptian tier is deliberately **not** sold; it is the cap tier and is
reserved for loot, the same split the weapons use.

Both tabs are laid out as a grid rather than poured in from slot 0, matching Muris'
Azim: **one set per column, one body part per row** — cap over body over gloves over
boots, classes left to right. A tab is 8 columns x 6 rows (`dlgstore.xml` stacks six
41px `STORE_MIDDLE` strips over 48 slots) and the client places each icon at its slot
index, so pouring 16 items in sequentially would run all four caps along the top row
and wrap each set across a line break. `armour_grid()` writes the sparse map, and
`NEW_TABS` now accepts `{slot: item}` as well as a flat list.

The materials split follows the weapon precedent: the ordinary reagents (Black Iron
Gear, the four lesser Scrolls, the Talisman, the four colour cores, Stella Libra,
Tamahagane) are bought; the 13 the dialog treats as hard to come by are **not for sale
at any price** — Graphistone is "found only in the Tower of Despair", Starlight is what
the armourers demand you bring *them*, the Tomes are asked for fifty at a time, and the
Sacred Demon Crystals break Nagia's seal. Selling those over a counter would contradict
the lines that make them worth having, so they wait for drops or a craft. `--verify`
proves none of the 13 leaked into any shop row.

**Stocking the tabs was not enough for Gelt** (found in game). His entire service
menu — `GF_openStore`, `GF_openBank`, `GF_repair`, `GF_openUpgrade`, all four
registered in `game_func_reg.inc` — hangs off a single root node gated on
`TA_Kakia_EpisodeQ532_Finish`, the plague-cure arc we never imported. Until that node
is exposed he answers with the fallback "………" and one "(He's barely breathing.)"
option, and the shop is unreachable however well stocked. `unlock-karkia-idle-dialog.py`
now exposes and promotes it, so Spire Village gets **storage, shop, repair and refine**
in one NPC. Nemo needed nothing — her store node was already ungated.

So a shop needs *three* things, not two: a `LIST_SELL` row with stock, a `LIST_NPC`
tab pointing at it, **and a reachable dialog node that calls `GF_openStore`**. The
third is the one that is invisible in the tables.

Cost of the call: Gelt reads as cured while Sulfa and Dinos still groan beside him.
Accepted — it is the same trade already made for the other five mute NPCs, and there
is no in-game way to run the arc that would cure him.

#### Stage 6f — Belfa's shop  *(DONE, 2026-09-08)*

Belfa turned out not to be a shopkeeper at all. His 173 menus are **entirely
`GF_SwapItem` weapon-exchange trades** — three families (`vice_*`, `Schwarz_*`,
`dwpn_*`, 13 weapon types each) that swap a weapon plus Graphistone for an
upgraded one, every branch gated on a `*_have` quest trigger we never imported.
So his whole dialog is dead apart from the greeting, the forging-info chain, and
"I'll pass."

Two changes, because a shop needs **three** things and he had none of them right:

1. **A reachable node that opens it.** New `quest-editor con-store` appends an
   ungated `SC_MSG_CLOSE` option to the root menu whose click function is a QEX1
   appendix wrapper:

   ```lua
   function QSbelfa_OPEN(E)
       GF_openStore(QF_getEventOwner(E), 0)
       return 1
   end
   ```

   CLOSE rather than SELECT is deliberate and copies retail: `Click_ITEM` runs
   the click function, then `Conversation(-1)` returns 0, which shuts the
   conversation window and leaves the shop dialog on screen. `E` is the CEvent
   handle the click passes in; `QF_getEventOwner` turns it into the NPC index.

   Built on the existing `append_warp_option` machinery rather than a second
   codec in Python — `convo.rs` warns against inlining copies of this logic, and
   a structural `.CON` rewrite is exactly where that would bite.

2. **Tabs worth opening.** Repointed 478–481 → 561/562/564, the same arms and
   ammunition Gelt sells. Deliberately shared: Belfa is at the Foot of the Tower,
   the staging zone, and gear should be buyable where you prepare rather than
   only in the deep field. Crune keeps 478–481 untouched.

**He also offers refining** (`--service upgrade`, 2026-09-08). Karkia's own forge is
*not* revivable — see §8 — but our ordinary refine system is, and Belfa is the right
NPC for it. `con-store` now takes `--service store|bank|repair|upgrade`; all four are
registered client functions used by shipped retail conversations, differing only in one
Lua line.

Verified the rebuild preserved everything: the main Lua blob is **byte-identical**
(30,323 bytes, still valid Lua 4 bytecode, the exchange functions intact), nodes
went 264 → 265 exactly, every original `str_id` survives, and the forging-info
chain — real translated content explaining Graphistone — is untouched. It was the
reason not to repurpose an existing option instead.

**Gotcha:** `con-store` writes `.bak` files *into* `data/`, which `pack.rs` would
bake into the `.vfs`. They are moved to `build/data-bak-archive/` after every run.

---

### Stage 8 — materials  *(DONE + validated in game, 2026-09-08)*

Twenty-five Jrose materials at `LIST_NATURAL` rows **740–764**, by
`scripts/import-karkia-materials.py`. The selection is not a sample of what the
source has: **every one is already named in dialog we translated in stage 6c**, so
each one turns a line that referred to nothing into a line that refers to an item.
Belfa explains Graphistone is the forging reagent; Astraea's price lists ask for
Starlight, the four Tomes, Arcane Sigils, Black Iron Gears, the four Latin colour
cores, Stella Libra and Sol Niger Horns; Nagia's seal chain runs on Phil Tempest's
Magic Stones and seven Sacred Demon Crystals; Lowe pays in Starlight.

Flavour now, craft inputs later. Two tool changes were needed first, and both are
about ceilings rather than convenience:

- **`import-item.py --target-row`.** A packed item code is `type * 1000 + no` in
  three places — drop cells (`Get_DropITEM`), QSD rewards (`REWD_001` →
  `tagBaseITEM::Init(int)`) and recipe inputs (`PRODUCT_NEED_ITEM_NO`) — so an item
  numbered **above 999 can be neither dropped, granted by a quest, nor used in a
  craft**. The wide form added in stage 6e reaches drops and shops, not these three.
  `LIST_NATURAL` is 981 rows, so appending leaves 19 usable slots and then walks
  past the ceiling. In-place writing into the table's blank middle is the only way
  these can ever be craft inputs, which is the whole point of importing them.
- **`--price`.** `--art-only` is mandatory here (Jrose's `LIST_NATURAL_S.STL` is the
  legacy `I_NUM` dialect our strict reader rejects) and it clones *every* stat
  column from one template — so without this every material would cost the same.
  For a material the price is most of what distinguishes it: Graphistone is 50,000z
  against a Black Iron Gear's 100z.

**A blank STB row is not a free row.** The first placement was 460–484, which is
blank in `LIST_NATURAL.STB` — and still *named* in `LIST_NATURAL_S.STL` ("Yellow
petals", "Big green herb", "Perfect green herb"). Those are retail materials whose
STB rows were lost in our dump, and writing over them would have destroyed the only
surviving record of what they were, then displayed Graphistone under a key reading
Yellow petals. The free test is blank in the STB **and** unnamed in the STL **and**
unreferenced by any drop cell, shop slot or recipe input: 478 rows pass, 735–899 is
the largest clean run, and 740–764 is taken from its middle.

**Two bugs found by running it**, both worth keeping in mind for the next in-place
import:

- `import-item.py`'s post-write verification was **append-shaped** —
  `assert vrows - 1 == new_id + 1` — so it fired on every one of the 25 rows *after
  writing them correctly*. The row count is the wrong invariant for an in-place
  write; it now asserts the table did **not** change size instead. `--target-row` is
  also refused outright for a type with a model ZSC, since `zsc_build_append` can
  only append and the object would land at the end of the ZSC rather than at the
  target row.
- That aborted run left **25 orphaned atlas cells** (8808–8832): it allocated icons
  before dying, and rolling back the STB/STL did not roll back `ITEM1.TSI`. The
  clean re-run then took 25 *fresh* cells holding byte-identical art. Reclaimed by
  repointing the rows down and trimming the 25 trailing sprites, so the data now
  matches what a clean run of the committed script produces. **An import that fails
  is not an import that did nothing** — the atlas is written before the tables are.

---

## 4. Decisions taken

Settled 2026-09-06.

1. **Karkia is an alternative to late Oro, not a sequel to it.** Same band, different
   flavour, for players who do not enjoy the Wasteland. Level targets in §5.
2. **The raid tier stays a raid.** The Drake → Hebarn Executive escalation is kept, tuned
   down until a coordinated party can take it. Numbers in §5.
3. **No fate-system hook** for now. Karkia reads as Hebarn territory but is open to everyone;
   gating it on the Arua/Hebarn markers can be layered on later.
4. **The three unpopulated zones are deferred.** They ship walkable and quiet; population is
   a later pass.
5. **NPCs are the last step of v1**, after monsters. The way *in* is the exception — it is
   stage 2, because travel is not optional. (The per-zone Wayfinder that stage 2 was first
   planned around was dropped; see §2 and stage 2b.)
6. **Monsters are the priority.** Stages 3 and 4 are what makes Karkia real.

The monsters' skill references, which the survey listed as 23 open items, turned out to be
almost a non-issue once checked properly — see §6. Copy the `.aip` files verbatim; six
references need attention, not twenty-three, and none of the six needs authoring.

---

## 5. Balance targets

Ballpark, derived rather than guessed — but derived from `scripts/balance-sim.py`'s
deliberately pessimistic baseline (no refine grades, gems, passives or buffs, and about 20%
low on ATK against a measured character). **Treat every number as a floor and expect to
tune down after the first real fight.**

### Level bands

Oro measured from its own spawn lumps runs 201–240, with late Oro (Wasteland 1 and 2, Ruins
Path, Gates of Muris) at 208–240. Karkia should overlap that, not sit above it:

| | authored | proposed | pairs with |
|---|---|---|---|
| 87 Cemetery | 211–221 | **215–228** | Wasteland 1 (208–225) |
| 88 Spire Village | 225–231 | **228–238** | Wasteland 2 / Ruins Path (213–232) |
| Deadly Drake 2729 (Cemetery) | 250 | **238** | — |
| Deadly Drake α 2699 (Village) | 250 | **240** | Gates of Muris bosses (240) |
| Hebarn Executives 2685/2686 | 250 | **240** | above anything we have |
| Corroded Golem 2687 | 245 | **238** | — |
| Revived Veteran 2688 | 240 | **235** | — |

This keeps Karkia's own internal progression (Cemetery → Spire Village → bosses) instead of
squashing it, and needs no level-cap change.

### The three stats that matter, and the order they matter in

Reference: our current top boss is the level-240 Fearsome Terrasaurus King —
**87,360 effective HP** (`level x col 8`), ATK 2997, DEF 997, AVOID 504.

- **DEF is a trap, not a difficulty knob.** Measured party DPS against a level-238 boss:
  DEF 1100 costs 6%, DEF 1300 costs 26%, DEF 1500 costs 36%. Beyond that it stops making the
  fight harder and starts making hits *read* as doing nothing — the documented failure mode
  in `doc/balance-analysis.md`, and exactly what Karkia's authored DEF of 2300–3600 does.
  **Cap boss DEF at ~1,250 and trash on the normal curve.**
- **ATK is already at its ceiling.** Our existing boss at ATK 2997 kills a baseline
  level-238 Champion in 4.2 swings (6.5 s) and a Mage in 2.5 (4 s). Karkia's authored
  4,323–5,325 is a one-to-two-swing kill on anything. **Keep raid-boss ATK at 2,600–3,000**
  and put the threat into adds and positioning, which is what "coordinate" can actually
  answer. Raising it past 3,200 just makes the healer's reaction window shorter than the
  server round trip.
- **HP is the honest knob**, because it is the only one that lengthens a fight without
  distorting how it reads.
- Leave **AVOID** at our tier (~500). Raider accuracy is already the weak link — it lands
  6–24% of swings at this level — and raising AVOID punishes one class disproportionately.

### Boss HP budget

Party cap is 7 (`MAX_PARTY_MEMBERS`). At the pessimistic baseline a level-238 player does
~34–46 DPS against a boss in this DEF band; a geared, buffed one plausibly 1.5–2x that.

| | proposed effective HP | `col 8` at that level | sanity check |
|---|---:|---:|---|
| Cemetery zone boss (Drake 2729) | **180,000** ≈ 2x our top boss | 756 @ lv238 | long solo fight; comfortable for 3–4 |
| Raid encounter total (Drake α + 2 Executives) | **~450,000** | — | ~17 min for a geared 5, ~12 for 7; 2+ hours solo, i.e. not soloable |
| — Drake α 2699 | 120,000 | 500 @ lv240 | the opener |
| — each Hebarn Executive | 165,000 | 688 @ lv240 | the payload, x2 |

Against Jrose's authored values that is a uniform ~7–8x reduction (their Drakes are
1.3–1.7 M, their Executives 6.5 M each), which is about the ratio between their players'
output and ours — so the shape of their encounter survives; only the scale changes.

### Spawn density is authored on a different philosophy from ours

Reported from a first visit: *"Spire Village is an absolute hell, there is so many
monsters."* Reported again on 2026-09-12, after the 75% thin below: both main maps
still play as a wall, with "hundreds" of monsters chasing at once.

**Everything measured in this section before 2026-09-12 was 4x low.** The table
below counted `sum(m_iLimitCNT)` as a zone's monster count, because `Proc()` only
spawns while `m_iLiveCNT < m_iLimitCNT` and that reads like a cap. It is not one.
`CZoneTHREAD::RegenCharacter` has **no cap check at all** — the only gate is the
early return at the top of `Proc`, so a tick that decides to spawn three monsters
into a point with one slot free spawns all three. How many a tick decides on comes
from the escalation branch, which is chosen by

    iVar = ((limitCNT*2 - liveCNT) * curTactics * 50) / (limitCNT * tacticPoint)

and **both main maps ship `tacticPoint = 1`** — the only two zones in the whole
dataset that do; every other zone we ship uses 50-200. At `tacticPoint = 1` an empty
point computes `iVar = 100` on its very first tick, lands in the *top* branch of the
table, and spawns `basic[4] + tactics[0]+1 + tactics[1]` = **four bodies into a cap
of one**, where it then sits.

Simulating `CRegenPOINT::Proc`'s real branch table against the shipped bytes:

| zone | points | Σ limitCNT | **real bodies** | |
|---|---:|---:|---:|---|
| **Karkia Cemetery** | 847 | 847 | **3,388** | 4.00x |
| **Karkia Spire Village** *(at 25% thin)* | 278 | 278 | **1,109** | 3.99x |
| Junon JG07 (our densest field zone) | 81 | 798 | 838 | 1.05x |
| Eldeon EZ01 | 2,118 | 2,118 | 2,118 | 1.00x |
| Oro ODE01 | 25 | 241 | 241 | 1.00x |

So the Cemetery was **1.6x** our densest zone and Spire Village **1.15x** — not the
0.35x this document used to claim, and not something a 75% thin had fixed.

Two further facts decided the shape of the fix:

- **Every point is single-species.** All 847 Cemetery points and all 1,111 Spire
  points repeat one npc id across all five basic and both tactics slots. So the
  escalation machine bought nothing, variety came purely from which points were
  near you — and `thin_regen`, which is greedy on nearest-neighbour distance and
  completely species-blind, *deletes species*: at 15% keep it drops Evil Fairy,
  Orgeid and D=Seed out of the Cemetery entirely.
- **Ours are clumps, Karkia is a carpet.** One monster per point 5 m apart, against
  our 5-10 per point 20-45 m apart. That part of the original analysis was right.

**Done, 2026-09-12: both maps consolidated into mixed-species camps**
(`SPAWN_CAMPS` in `scripts/import-karkia.py`, applied where stage 3 writes the REGEN
lump so a re-run cannot undo it). Rather than deleting points, neighbouring points
are *merged*: bin the zone on a 60 m grid, keep one record per bin at an authored
position, and give it the species of everything that bin absorbed, at `limitCNT 5`,
`tacticPoint 100`, 20 s tick, 12 m radius.

| | points | bodies | was | per 100 m cell | vs JG07 | camp gap (median) | species |
|---|---:|---:|---:|---:|---:|---:|---|
| Cemetery | 113 | **565** | 3,388 | 10.7 | 0.33x | 34.7 m | **21/21** |
| Spire Village | 83 | **391** | 1,109 | 14.0 | 0.43x | 29.3 m | **10/10** |

Population falls 6.0x and 2.8x while **diversity rises**: 85% of the camps come out
multi-species where every source point was a clone. Merging also cannot lose a
species the way thinning does, which is the whole reason it was chosen. Spire's six
Deadly Drake Alpha points (`tacticPoint 100`, 15-20 minute timers, their own point
names) are passed through untouched — the old thin had cut them to one.

`SPAWN_THINNING` is now empty. It remains the right lever for a zone whose density
genuinely *is* its point count, which is what the three flashback zones needed.

**Measure bodies by simulating `Proc`, never by summing `limitCNT`.** And measure
per unit area: the earlier 200 m-radius mesh-draw model was a real improvement over
the 60 m one it replaced, but it was fed a body count that was 4x wrong, so it
confidently endorsed a zone that was 1.15x our densest as 0.35x.

**Eldeon EZ01 is still worth a look** — 2,118 bodies over 48 cells is 44.1 per cell,
1.37x JG07 and above both Karkia maps now. Its monsters are low level and simple, so
it may not bite the same way, but it is ours, not an import.

### The summon chain needs its own pass

Karkia's monsters summon adds constantly, and two of those summons are boss-tier:

| summoner | summons | authored HP | proposed |
|---|---|---:|---|
| `ks_2699` Drake α | 2685 + 2686 Hebarn Executives | 6,500,000 each | 165,000 each — **this is the raid, keep it** |
| `ks_2692`, `ks_2697`, `ks_2698` *(trash)* | 2687 Corroded Golem | 2,793,000 | **~60,000** — an elite add, well below zone-boss tier |
| `ks_2692`, `ks_2697`, `ks_2698` *(trash)* | 2688 Revived Veteran | 672,000 | **~45,000** |
| `kak_summonseed` D=Seed | 2705 D-Pollinosis x6 | 10,850 | scale with the tier |
| most Cemetery mobs | 2731 / 2689 Ghost Seed | 14–16 k, ATK 1 | scale with the tier |

Trash summoning a boss is not something coordination can answer, so 2687 and 2688 come down
hard. 2704 D=Seed is a stationary spawner (ATK 10 — it never attacks) at 215,000 authored
HP; bring it to ~40,000 so clearing it is a real objective rather than an endurance test.

**This table is incomplete — read §7 before tuning any of it.** Decoding the AI found a
second link it does not show: the Corroded Golem has a 15% chance *each* of summoning both
Hebarn Executives when it dies, so the chain runs trash → Golem → 6.5 M HP raid boss with
no boss kill in it. And none of these summons can despawn on our server.

EXP is 133x ours across the board; `scripts/rebalance-exp-rewards.py` owns that.

---

## 6. The monsters' skill references

The survey flagged 23 skill ids the Karkia AI casts through `AIACT_24` and warned they might
mean different things here. Checked properly — every one of our 87 columns, both sides — the
picture is far better than that:

| | rows | what happens if we copy the `.aip` verbatim |
|---|---|---|
| **identical on both sides** | 12 — 3548, 3549, 3557, 3572, 3574, 3575, 3579, 3580, 3582, 3585, 3586, 3599 | works exactly as authored |
| **same skill, different numbers** | 5 — 2910, 2911, 2913, 2941, 2954 | right effect, our duration and magnitude (mostly *stronger*: 2941 is rate 15 / 35 s here against rate 7 / 15 s there). 2910/2911/2913 have no casting or action motion on our side, so the effect lands with no animation |
| **blank on our side** | 4 — 716, 3613, 3616, 3627 | the monster spends an AI action doing nothing. `CObjAI::SetCMD_Skill2OBJ` does not validate the row, so an empty skill executes as an empty skill — no crash |
| **genuinely different** | 2 — 846, 3685 | see below |

**Settled as of stage 3b: all of them work.** Re-walked from `LIST_NPC` col 16 across
all 38 monsters (the 23 above came from a hand-listed set of `.aip` filenames that
covered only the spawned ones), the AI casts **36** distinct skills and **zero** of them
are dead on our server. The four "blank" rows were filled by the port; the five the
bosses cast that this table never saw — 2914, 3594, 3596, 3597, 3598 — turn out to be
present and **cell-for-cell identical to Jrose** across all 50 mechanical columns.

Careful with that check: those five have an **empty name in column 0** and a fully
populated row behind it. Column 0 is an editor label the server never reads, so testing
it reports a working skill as blank — which is exactly the false alarm this note exists
to stop. Test `SKILL_TYPE` (col 5) and the mechanical columns, never the name.

Those two are the only real losses, and neither is harmful:

- **846** is トルネード (Tornado) there — type 17, radius 700, power 500 — and **Spell Mastery**
  here, a type-15 passive. The AI casts it *at its attack target*, so instead of a 500-power
  AoE the monster fires a passive at the player: a no-op, or at worst a trivial gift.
- **3685** is a radius-1500 self-buff there and **GM Blessing** here — which despite the name
  is an empty row (no type, no ability pairs). The AI casts it *on itself*, so nothing
  happens.

So the blast radius is **10 of the 38 monsters**, and they pair up because the α set shares
its AI with the Cemetery original:

| monsters | affected rows | what they lose |
|---|---|---|
| 2703 / 2692 Revived Quarantine Member | 716 (self), 846 (target) | its self-Berserk and its Tornado — the biggest loss |
| 2716 / 2697 Evil Eye | 3685 (self), 3627 (target) | a self-buff and a silence |
| 2715 / 2696 Murillo | 3613 (target) | a 5 s stun |
| 2725 Evil Fairy | 3616 (target) | a 100-power nuke |
| 2714 / 2695 Deadly Wolf | 2941, 2954 | nothing — magnitude only |
| 2719 Woodnoid | 2910, 2911, 2913 | nothing — magnitude, plus no cast animation |

### None of the six needs authoring

Three of them we **already have**, because they are *player* skills the AI borrows —
`LIST_SKILL` col 35 (Job) and the parent-skill and weapon columns give them away:

| row | Jrose | ours | action |
|---|---|---|---|
| 716 | バーサク, a Champion 2H/spear/axe self-buff (job 62, parent 711, level 120) | **Berserk**, rows 361–380 | re-point the `.aip`. Import nothing |
| 846 | トルネード, a Mage staff nuke (job 42, parent 841, level 70) | **Tornado**, rows 1086–1090 | re-point |
| 3627 | `kleitos_沈黙`, a silence | **Silence**, rows 1071–1075 | re-point, or port theirs if the numbers differ enough to matter |

Two caveats on those three. Our copies are *ranked families* (Berserk has 20 ranks), so pick
a rank suited to a level-215+ monster rather than defaulting to rank 1. And the job/weapon/
parent-skill columns are learning gates, not casting gates — a monster ignores them — but
that is worth confirming in game rather than asserting.

The other three are genuine monster skills, and they **port by straight copy**, because the
support tables are index-aligned and Jrose only ever appended to them:

| table | rows we both have | identical path | **rows only they have** | rows only we have | different |
|---|---:|---:|---:|---:|---:|
| `FILE_EFFECT` | 3162 | 496 | 484 | **0** | 26 |
| `FILE_SOUND` | 2001 | 1283 | 75 | **0** | 1 |
| `TYPE_MOTION` | 621 | 163 | 296 | **0** | 4 |
| `FILE_MOTION` | 1271 | 350 | 284 | 21 | 0 |

"0 rows only we have" is the property that matters: wherever we have a row, they have the
same row, so a ported skill's effect / sound / motion index either resolves to the identical
file here or lands on a row we left blank — never on a *different* asset. (`FILE_MOTION` is
the exception with 21 rows of ours they lack, and 26 `FILE_EFFECT` rows genuinely differ;
none of the six skills touches any of them, but a broader skill import should check.)

Checked index by index for the three:

| row | what it is | ports cleanly? |
|---|---|---|
| 3613 | `蟻スタン（5秒）` — type 17 AoE, radius 3000, power 400, 5 s stun | **yes.** Motions 8/9, effect 1611 `_s_spin_attack02.eft`, sound 131 all resolve to the same files here |
| 3685 | type 8 self-buff, radius 1500, status effects 20 + 22, 50 s | **yes.** `LIST_STATUS` 20 and 22 are identical in every numeric column — the only difference is that they kept the Korean name and we translated it to "Def Increased" / "Magic Resistance Increased" |
| 3616 | `questarua` — type 7, radius 3000, power 100 | **yes, plus one asset.** Its impact effect is `FILE_EFFECT` 1460 `questarua_exp.eft`, which is blank here; the file exists in the Jrose tree (`3Ddata\EFFECT\QUESTARUA_EXP.EFT` and its `EFFECTMESH\QUESTARUA_EXP` folder), so write the row and copy the assets |

**Plan: copy the `.aip` files verbatim, re-point three `nSkill` fields at skills we already
own, and port three rows plus one effect.** Copying cols 0–85 is safe — the layouts align by
meaning across that whole range. **Col 86 does not**: ours is the STL key, theirs is
`AVAILABLE_STATUS`, which is exactly the trap `doc/jrose-survey.md` §2.2 warns about. Leave
our col 86 alone.

Worth doing rather than deferring: the Cemetery roster's identity is partly its casting, and
a silence plus a stun is most of what makes the Evil Eye and Murillo interesting.

---

## 7. The bosses, decoded

Vouched for from the data rather than in game, since summon-only bosses are awkward to
reach. Everything below was read out of the `.aip` bytes against
`src/sho_gameserver/src/ai_lib/` — the **structs** in `cai_file.h`, not the comments
above the functions, which name fields the code does not use (`AICOND27`'s comment says
`cChrType`; the code reads `btIsAllied`, and the two `short`s after it are 2-byte
aligned, so a naive offset reads the padding and prints nonsense level ranges).

Dispatch is `g_FuncCOND[Type & 0xff]` / `g_FuncACTION[Type & 0xff]` on tables that start
at index 1, so a file's `Type` *t* is `F_AICOND_(t-1)` / `F_AIACT(t-1)`.

**Max HP is `level × col 8`** (`m_iOriMaxHP = NPC_LEVEL * NPC_HP`), which is why the
table below reads 26,000 where §5 quotes 6,500,000.

| | level | col 8 | effective HP | ATK | DEF | RES |
|---|---:|---:|---:|---:|---:|---:|
| 2699 Deadly Drake α | 250 | 6,759 | 1,689,750 | 4,526 | 1,281 | 3,120 |
| 2685 Pazugenti | 250 | 26,000 | 6,500,000 | 4,323 | 1,250 | 2,600 |
| 2686 Scylla Mira | 250 | 26,000 | 6,500,000 | 4,323 | 1,250 | 2,600 |
| 2687 Corroded Golem | 245 | 11,400 | 2,793,000 | 3,380 | 600 | 1,500 |
| 2688 Revived Veteran | 240 | 2,800 | 672,000 | 2,980 | 600 | 1,500 |

### What they actually do

A hit only runs the damaged pattern some of the time — 40% for the Drake α and the
Golem, 30% for the rest — and *then* each event rolls its own chance, so the rates below
compound. The officers hold an AI variable that acts as a cooldown counter: casting adds
to it, an idle event decays it, and the cast is gated on it staying under a ceiling.

- **2685 Pazugenti** — 40% Ferdinand Stun (AoE r4000, 200 power, 4 s) on the target,
  above 5% HP; 30% self-buffs 3596 when the target has a buff up, 30% self-buffs 3597
  when the target has no debuff. Flees home if dragged 80 from spawn. On death, at
  night only, five Ghost Seeds.
- **2686 Scylla Mira** — identical, with Duke Vlad Counter (AoE r5000, power 10) in
  place of the stun.
- **2687 Corroded Golem** — the busiest. Idle: 25% KS Defence Down (AoE r1800, 300, 60 s)
  when two or more players are within 17. Damaged: 5% self-buff 2912, 20% self-buff 2914
  after a hit ≥ 100 damage, 20% Defence Down, 10% KS Stun (AoE r1500, 250, 5 s), and
  below 30% HP a 10% chance to summon a Revived Veteran. Below 40% HP it calls a nearby
  idle ally onto its target 60% of the time.
- **2688 Revived Veteran** — 50% KS Poison (single target, 350, 30 s) on a target with
  no debuff, twice over; 30% self-buff 3598; the same two officer self-buffs.
- **2699 Deadly Drake α** — no skills at all. It is a stat check and a summoner.

**Tornado is switched off (2026-09-07).** Skill 1090 on the Revived Quarantine
Officer (2703) and its Alpha (2692) was reported in game as damage with no visible
cast, and a run of "blank hits" ending in a death. That is not bad luck:
`kak_livingdead.aip` carries a damaged-rate of **100%**, so 2703 rolled it on 10% of
every hit it took. Filling in its missing animation pair fixed the *monster's*
animation, not the skill's presentation. It is disabled by zeroing the percentage on
the `AICOND_07` gating its event — the same way the source data disables its own
dead events — via `AIP_DISABLE_SKILLS` in the importer, so a re-run cannot restore
it. One byte to put back.

**Back on (2026-11-01), `scripts/fix-quarantine-officer-skills.py`.** The missing
presentation was the release clip: the plain swing filled in above carries only event
21, and an area payload drains on 24/34/25/35/26/10/20/56/66, so nothing showed the
hit. And 1090 is the *player's* rank 10 -- power 501, 14 m -- about 1,000-1,250 a cast
at these levels. Both officers now cast on slots 8/9, which hold the player's own
Tornado clips retargeted onto their skeleton, and a monster row of their own (7202: 7 m,
power 200) at 4% of hits taken, gated on range. Their Berserk keeps slots 6/7, which
now hold the player's Berserk clips the same way (it had shown its buff only at the end
of the plain swing). Re-run that script after the importer's AI or CHR stages.

Worth noting what makes it the odd one out: 1090 is one of only **two re-pointed
skills**, meaning one of *our player* skills handed to a monster rather than a row
ported from Jrose. Its sibling 361 Berserk is a self-buff and presents its own
effect, so it looks fine. 3613 Karkia Stun was also never seen in game and is
deliberately **left enabled** — 35% damaged-rate × a 10% roll × "target has no
harmful status" is ~3.5% of hits taken, which explains it without a defect, and its
motion pair is verified present.

Motion is clean: 76 of the 80 `AIACT_24` records already resolved to a complete
casting/skill anim pair, and the four that did not were the Revived Quarantine Officer,
fixed in `a86dbbff`. Every boss animates.

### Two things worth knowing before tuning them

**The despawn rule was dead, so summoned bosses were permanent — now fixed.** All four
carry the same idle event: *if `<condition 31>` and no player within 40 → kill itself*
(`F_AIACT23` is `Add_DAMAGE(HP + 1)`). We had no `F_AICOND_30`; the id fell on
`F_AICOND_NULL`, which returns **false** on the server (the client's returns *true* —
they disagree), so the event never ran and the boss never cleaned itself up. The only
other lifetime path did not apply either: `CObjCHAR::Create_PET` attaches
`FLAG_ING_DEC_LIFE_TIME` **only if the summoner already had it**, which a field-spawned
trash mob does not — and our `status_effects.cpp` no longer drains on that flag anyway.

This was **not** an import defect and not Karkia-specific — our own 466 `.aip` files use
condition 31 twenty-nine times, so it had always been dead here. Karkia is just the first
content that summons enough for it to show.

Condition 31 is now implemented as `F_AICOND_30`, "has been alive at least N seconds".
Nothing documented it — the meaning was reconstructed from how the data uses it. 33 of
its 35 uses gate suicide from the idle pattern, and the value follows the rank ladder of
three independent summon families:

| | rank 1 | rank 2 | rank 3 |
|---|---:|---:|---:|
| `manaflame` | 60 s | 90 s | 120 s |
| `murthflame` | 60 s | 90 s | 120 s |
| `sur_fire` (BoneFire) | 60 s | 90 s | 120 s |

which is what identifies it as a lifetime in seconds rather than a distance or a count.
Karkia's `-1200` is a disabled timer — always true — leaving the bosses' rule as pure
"no player within 40 → despawn".

Two things about the implementation:

- **Player summons are deliberately exempt.** They run the same idle pattern, so a
  faithful implementation would have started expiring BoneFire at 60–120 s and
  Elemental/Wolf at 300 s — a change to existing gameplay nobody asked for, and one
  those skills no longer expect since we removed the summon HP-drain. A player's summon
  is bounded by the summon gauge; a monster's summon is bounded by nothing, which is the
  accumulation this condition exists to stop. `CObjSUMMON` overrides `GetCallerObjIDX()`
  to return the owning *user*, so its `Get_CALLER()` resolves to an avatar while a
  monster-summoned pet resolves to a mob or to NULL — that is the discriminator.
- **No field monster is affected.** Every one of the 40-odd rows carrying a suicide
  timer is summon-only; none appears in any map REGEN lump. Checked before shipping,
  because a field monster on a 60 s timer would vanish and respawn forever.

**The summon chain is worse than §5 recorded.** §5 has trash summoning the Golem. It
does not have what the Golem does on *its* death:

| on death | chance | summons |
|---|---:|---|
| 2687 Corroded Golem | **15% each** | 2685 Pazugenti **and** 2686 Scylla Mira |
| 2688 Revived Veteran | 1% each | the same two |
| 2699 Deadly Drake α | 4% each | the same two |

So the live chain is *trash → Corroded Golem → 15% → a level-250, 6.5 M HP raid boss
that cannot despawn*, with no boss kill anywhere in it. That is the strongest argument
yet for §5's "bring 2687 and 2688 down hard", and it wants a decision on the officer
summons themselves — drop the rate, gate them behind the Drake α, or cut them from the
trash-fed rows.

**Some events are dead in Jrose too.** Several are gated on a `0%` roll
(`Get_RANDOM(100) < 0` is never true): both officers' low-HP self-heal 3594 and their
"call for help". That is their authoring, not our import — leave it or author over it,
but do not go looking for a bug.

---

## 8. Still open

This section was written before any code and has been trimmed as its questions were
answered in the build. What is left is one item, and it is art, not engineering.

- **Whether the α set gets re-themed** — *narrowed to art only, 2026-09-08.* 10 of Karkia's
  31 monsters are the Cemetery roster again at higher level with an α suffix, and they are
  **the same art**, proved at the binding rather than at the label: `LIST_NPC` carries no
  model column at all (the `.mon` text in col 1 is vestigial, exactly as it is for weapons —
  only 3 of these 10 even have a folder by that name). A monster's appearance comes from its
  own record in `LIST_NPC.CHR`, indexed by the row id. Each α row *has* its own record, and
  every one of the ten lists **the same `PART_NPC.ZSC` part indices as its base** — 797, 802,
  `[803, 804]`, `[808, 809]` and so on, identical in all ten cases. So Spire Village shows
  the same creatures, ~13 levels up.

  That also sizes the fix: no new CHR records are needed, because the α rows already have
  their own. It is new ZSC part objects pointing at recoloured textures, and then repointing
  each α record's `models` list at them.

  The **names, however, are not ours to change.** The source rows carry only dev placeholders
  (`KSヴィクティムf`), but the dialog names the creatures outright — `D=ヴィクティムα`,
  `蘇った防疫団員α` — and two quests instruct the player to kill thirty of each *by that
  name*. Renaming them would desynchronise shipped text. Nor is the duplicate "D=Victim Alpha"
  on 2690/2691 a defect: 186 names in `LIST_NPC` are shared by more than one row (Candle Ghost
  by 32), and those two are simply the male and female models.

  So what remains open is **art**: colour variants for the ten, which is real work. Shipping
  as-is stays honest ("the same horrors, worse") and free.

  Renaming them *did* surface a real bug — see the `D=` prefix note under stage 3.

### Settled since this section was written

Kept as a record of what was decided, and where the decision actually lives.

- **Where the entrance NPC stands** — the proposal here was Muris. It shipped as
  **[Historian] Jones** in Junon Polis and **[Interplanetary Guide] Nova** at the Orlean
  Portal Temple, with **[Explorer] Petri** in the Church as the way home
  (`add-karkia-travel.py`, `HOSTS`). Muris was passed over because both of those NPCs are
  already the game's established "take me somewhere" casting and needed no new writing.
- **A level requirement on the warp** — **none**, for exactly the reason Oro has none: the
  real gate is the monsters. Recorded at `add-karkia-travel.py:45`.

---

## 9. Effort shape

Rough, for sequencing rather than scheduling. The engineering is small and the authoring is
most of it.

| stage | engineering | authoring |
|---|---|---|
| 1 terrain/art | moderate — mostly reusing `import-oro.py` writers | zone names |
| 2 gates + travel | small — the tooling exists | dialog text, entrance placement |
| 3 monsters | moderate — CHR/ZSC/motion remap, again reused | none |
| 4 balance | small — one rebalance script | **large: the whole tier** |
| 5 drops | trivial | **large: a Karkia loot tier** |
| 6 NPCs | small | large: 32 dialogs + 14 shop tabs |

No server or client code change is required by anything above. The six skill references in §6 are
data too, so that stays true.

## 10. Standing notes

Written as a pre-start checklist; still the things to remember on every pass.

- Re-read [doc/jrose-survey.md](jrose-survey.md) §2 — all three silent-failure traps apply.
- Re-read §5 — the balance targets are a floor from a pessimistic model, not a spec.
- **No rollover concern, and the margin is wider than this line used to say.** The ceiling
  moved from 2 GB to **4 GB** on 2026-08-29 when `FileEntry::lFileOffset` stopped being
  signed (`reference_vfs_offset_limit`), and `pack.rs` now splits to `rose_2.vfs` at 4.2 GB
  on its own. Bake with `scripts/pack.ps1` regardless: it prefers the freshly built packer
  over the stale copy in `Exes/`, self-verifies the archive, and since 2026-09-08 refuses to
  bake at all if a `.bak` is sitting under `data/`.
- Servers cache STBs at startup — restart them after every stage, and rebake + redeploy the
  client for anything that touches `data/`.
