# Vendetta monsters -- implementation plan

Status: **plan only, nothing built** (2026-11-07). Built from a code survey (7 areas), an adversarial review
of a first draft (server core, client and wire, roaming plus a data audit, combat-sim tuning, an abuse
red-team, a game-design critic) and a completeness pass that settled the reviewers' contradictions
against the code. Every file:line below was read by at least one reviewer.

The idea as proposed:

> When a monster kills a player, it's promoted: it gets a name and a title ("Gorgo, Slayer of Thomas"),
> more levels, and it roams the zone. A bounty goes up, and whoever kills it gets a reward scaled to its
> kill count.

The plan keeps all of it except **roaming, which moves to v2** (section 4 explains why). Decisions that
belong to the user are collected in section 12, each with a recommended default.

---

## 1. Facts that shape the design

- **One kill funnel.** Every player death passes through `classUSER::Dead(pKiller)`
  (`gs_user.cpp:878`). The only caller that can hit a user is `Apply_DAMAGE`'s `pTarget->Dead(this)`
  (`cobjchar.cpp:1749`). `pKiller` is never null and is always the acting object. DoT ticks cannot
  kill (`Give_STATUS_DAMAGE` stops at 1 HP, `cobjchar.cpp:1870-1895`), and the server has no fall damage.
  `if (CObjCHAR::Dead(NULL))` (`gs_user.cpp:919`) runs exactly once per real death.
- **Same thread.** A player's death and the monster that killed them both run on that zone's thread.
  So the zone's own state needs no lock; only server-wide state does.
- **Kill shown late.** The server applies a normal hit at swing start, but the client shows it at the
  hit frame, a median 1 s later and up to 3 s. Anything sent at once would put up "Gorgo, Slayer of
  Thomas" while Thomas is still visibly standing.
- **Monster objects are not pooled**, despite comments in `cai_lib.h:29-30` and `cobjnpc.cpp:72-74`.
  Every spawn is `new CObjMOB` and every death a `SAFE_DELETE`. What gets reused is the global
  **object index**, so a stored reference must be `{index, Get_SpawnTIME()}` plus a zone check, never a
  pointer.
- **Level and name come from the row.** Server `CObjMOB::Get_LEVEL`/`Get_NAME`/`GetOri_DEF/RES/AVOID`/
  `Get_GiveEXP` read LIST_NPC on every call, and all of them are virtual. Max HP, ATK, HIT, attack speed
  and crit are cached at `Init`. `CObjSUMMON::SetCallerOBJ` (`cobjnpc.cpp:669-691`) already scales a
  monster per instance, so there is a precedent.
- **The client draws from its own table.** The client takes a monster's name, level and max HP from
  its own STB. `gsv_MOB_CHAR` carries only the row, the quest id and Z. Every sector re-announce builds
  a brand-new client object (`Add_MobCHAR` never dedupes). So the vendetta data has to ride inside every
  add packet, not arrive once.
- **Levels cost little hit chance; ATK has a cliff.** Measured exactly, each monster level above the
  player costs about 2 points of normal-attack hit chance. Skills land 100% up to +10. Danger grows
  with ATK roughly squared (`ATK*(ATK-DEF+250)`), and some class/monster pairs go from safe to one-shot
  between x1.25 and x1.5.
- **Kill EXP ignores max HP.** `CCal::Get_EXP` divides by max HP, so a monster with x10 HP pays the same
  EXP as a normal kill. Bigger is not worth more unless we make it so.
- **Real play on the alpha** (server logs, 28 Oct - 7 Nov):
  - one player online 55% of active minutes, two players 25%;
  - some zone held two or more players only 30% of the time;
  - game-server sessions have a median of 13 min, so a random moment is about 45 min from the next
    restart.

  Whoever kills a vendetta will nearly always be its victim or the victim's party. Restarts, not
  timers, decide how long a vendetta lives.
- **Deaths are not logged anywhere today**, so how often vendettas would appear is a guess, about
  0.5-3 a day at the current activity.

---

## 2. The design in one page

1. **Promotion.** A wild camp monster kills an eligible player (section 3). After a **3 s hold**, the
   monster gets:
   - a name, "Gorgo the Grim";
   - a title, "Slayer of Thomas";
   - +1 level;
   - more HP;
   - an always-on nameplate with the title line and a glow.

   It leaves its camp, which refills, and settles **where it killed**.
2. **Growth.** Each new distinct victim adds to its kill count, title, levels, HP, ATK and bounty, up to
   5 victims. Growth never applies mid-fight: a hunter dying in the fight does not heal or strengthen it.
3. **Bounty.** The bounty grows with each victim. When it dies:
   - everyone in the kill gets EXP for the longer fight (the normal EXP split);
   - victims who took part get their lost EXP back;
   - the hunters get a zuly bounty and extra drop rolls, scaled by the victim count.
4. **Lifetime.** It ends:
   - when killed;
   - **45 min after its last kill**;
   - at a restart (v1 keeps nothing).

   The victim is told the time limit.
5. **Telling the players.** The victim and their party get a whisper and a minimap marker at its
   location, and the zone gets a chat line. The monster shouts. Its death is announced to every victim.

---

## 3. Eligibility (one predicate, `CVendetta::IsEligible`)

**Zone.** Not:
- `is_pvp_zone()` or `is_clan_zone()`, which also removes the Desert of the Dead (29), Luna and Junon
  clan fields, and Forgotten Temple 56/57 (PvP state 1 although PvE: user decision);
- `CCerberusLair::IsLair`;
- `ZONE_FLAG_PK_ALLOWED`;
- an instance zone (future dynamic instances);
- on the config deny list.

Towns cannot be excluded by "has regen points": Zant, Junon Polis, Eucar, Xita, Muris, Spire Village
and Skaaj all have them. That is fine in v1 because the vendetta never travels (section 4).

**Killer.** Checked at hook time:
- `IsA(OBJ_MOB)` and `Get_HP() > 0`. Do_KillAI runs first and may have killed or morphed it.
- `!GetCallerUsrIDX()` and `!GetSummonedSkillIDX()`: not a player's summon. `CObjSUMMON` is `OBJ_MOB`
  too.
- `!m_IngSTATUS.IsSET(FLAG_ING_DEC_LIFE_TIME)`: not a summoned minion.
- `Get_TeamNO() == TEAMNO_MOB`: not the escort or the lair sleepers.
- `m_pRegenPOINT != NULL`, or already a vendetta. This drops controller, quest, AI-called, GM `/mon`
  and event spawns in one test.
- Walk speed > 0. 18 spawned rows are stationary, including the Flanae family.
- Mark (col 27) is not 11 (crown), 16 (skull) or 12 (D=Seed). Gold star 10 is excluded by default
  (section 12). Marks 13 and 15 are a Jrose pair and Rudolph Santa and never spawn at regen points.
- The row's AI never uses AIACT_09 (morph, 11 rows) or AIACT_23 (suicide, 53 rows; it fires on ally
  crowding, not just age). Compute this once at boot by walking the AI patterns (`CAI_FILE` already has
  `CollectCastSkills` as a model).
- Not within 100 m of another living vendetta.

**Victim.** Each must hold:
- not staff (`Get_RIGHT() >= 100`; the `IsGM` bitmask helper misses rights 100-255);
- level >= 10 (there is no death penalty below);
- level >= the monster's base level - 10, so nobody gets a nemesis they cannot hit, and low-level alts
  cannot feed it;
- account not counted for **any** vendetta in the last 60 min;
- IP not already counted on this vendetta;
- not already a victim of this vendetta.

**Caps.**
- 1 per zone, 6 server-wide. Reserve the server-wide slot under the mutex; it is cross-thread.
- 10 min between new promotions in a zone.
- A death to an existing vendetta always counts, if the victim qualifies.

**Audit (all 68 zones, 448 regen-spawned rows).**
- About 333 rows pass a predicate like this one, 307 of them at level 10 or above. Gold stars take
  about 25 off that.
- 96 of the passing rows never start a fight (passive STOP patterns). They only matter once roaming
  arrives.

---

## 4. Location: anchored where it killed (v1), roaming later (v2)

**Why no roaming in v1:**
- **The victim couldn't find it.** Monster names only draw when hovered or targeted, neither minimap
  draws monsters, and the victim revives elsewhere. A vendetta that walked off to another camp is one
  Grunter among twenty.
- **It is the riskiest piece by far.** Roaming needs:
  - A* legs inside a 128 m window, with a search budget the whole zone shares;
  - a scan while walking, because monsters only look for players while standing;
  - town detection, which the server has no data for;
  - clustering regen points into camps (Sikuku has 2,116 points 9 m apart);
  - a stuck detector.
- **The two anchor rules contradict each other.** The roaming review wants the anchor re-set every
  tick. The abuse review wants it at waypoints only. Each rule breaks the other's case.

**v1 rule:**
- **At promotion**, detach from the camp exactly once (`m_pRegenPOINT->SubLiveCNT(); m_pRegenPOINT =
  NULL`). The camp refills and `Run_AWAY` falls back to `m_PosBORN`. Then set `m_PosBORN` to the kill
  site.
- **At each counted kill**, re-anchor to that kill site, clamped to 150 m from the original camp. It
  settles wherever it kills, so it drifts a little, and it never ratchets into a town.
- **Never touch `m_PosBORN` at other times.** The AI's own idle wander and walk-back (422 of 448 rows)
  then behave exactly as for any monster.
- **C++ break-off at 50 m** from the anchor while target-less, using `Nav_PathTo`. Not
  `Nav_MoveOrStraight`, whose last fallback walks straight through walls. About 20 real combat rows
  have no leash of their own.
- **Grace after revive.** It does not start a fight with one of its own victims for 30 s after that
  victim revives (normal zones give no revive protection). It still fights back.
- **No retaliate hook.** 410 of 448 rows already retaliate when hit with no target.

**v2 roaming, if wanted, needs all of this** (recorded so nobody rediscovers it):
- **A camp graph:** regen points clustered single-linkage at about 40 m, each camp with a level band
  from both its basic and its tactics rows.
- **Explicit deny circles per zone.** Junon Polis and Eucar points all sit 85 m or more from any NPC,
  so an NPC radius does not find the city.
- **A geofence** over NPCs, the start position, *all* revive points (needs a new accessor; `m_pPosLIST`
  is private) and warp arrivals.
- **The anchor rule:** `m_PosBORN = m_PosCUR` only while on a validated leg.
- **A controller aggro scan while walking**, collect-then-act.
- **Pathing:** `Nav_PathTo` legs of 40-60 m. A `false` return can just mean the budget is spent this
  tick, so retry before re-picking.
- **Help calls:** a decision on AIACT_11 (295 rows).
- **Zone 26:** stay out of the Forest of Wisdom while the escort runs (it is `TEAMNO_USER`).
- **The client:** a moving marker or zone roster packet.

---

## 5. Growth curve (combat-sim tuned)

k = counted distinct victims, capped at 5. Always recomputed from the STB row, never compounded:

| knob | value | why |
|---|---|---|
| level | +1 per victim, cap **+3** | the median build keeps 83%+ of normal hits, the worst build 72.8% at level 180; +5 drops the worst to 68.7% |
| max HP | x(2 + 2.5k), cap **x12** (k1 x4.5, k2 x7, k3 x9.5, k4+ x12) | sets the fight length |
| ATK | x(1 + 0.04k), cap **x1.2** (never above 1.25) | the one-shot cliff starts at about x1.35 |
| DEF / RES / AVOID / HIT | x1.0 | DEF punishes low-ATK classes; AVOID duplicates the level gate |
| crit | promoted level x 0.6 | same rule as `Init` |
| move / attack speed | unchanged | the client takes attack speed from its table |

Sim results: 3 classes at levels 50/120/200, 2 plain monsters per level, 80 fights each. The sim plays
perfectly, so these are best-case.

| | solo, median time to kill | worst pair, solo win | duo / trio, median time to kill |
|---|---|---|---|
| normal kill | 2.2-3.9 s | 100% | -- |
| k1 | 14-25 s | 99%+ | 6-13 s / 4-8 s |
| k5 | 40-72 s | 72% (60% typical play) | 18-37 s / 12-24 s |

So k1 is a revenge the victim can win alone, about 5x a normal fight, and k5 is a group monster by
fight length. The danger stays out of the one-shot range. Before building M1, test one dangerous pair
in game: a Mage or Scout against Wolf Keeper 390 at level 120.

**Mid-fight rule.** The identity (name, title, kill count) appears after the 3 s hold. Stat growth
applies only once no player has damaged it for 30 s. HP keeps its *percentage* when max HP rises. So
"Mira finishes the beast that killed Thomas" still happens, and nobody can reset a nearly dead vendetta
by dying into it. Do **not** zero `m_SavedDAMAGED` on promotion: without a heal, that would take EXP
and drop credit from the victim's party.

**Clamp.** Promoted max HP is capped at `INT_MAX / 100`. `Get_PercentHP` and the classic gauge both
compute HP x 100 in an int. Eligible rows top out at 162,400 (Astarot), so this only matters for GM
promote on a skull.

---

## 6. Rewards and the bounty

The rule is that **dying can never pay**, while the victim can still win back what they lost.

| who | gets | how |
|---|---|---|
| everyone in the kill | EXP for the longer fight | Override `CObjMOB::Get_GiveEXP` to `NPC_GIVE_EXP x hpMul`. EXP per HP removed then equals grinding that monster, through the existing `Give_EXP`/party code. No new EXP path, and nothing a feeder couldn't earn by grinding. |
| each counted victim in the kill (alive, in the zone, hit it or in the killer's party within 50 m) | the EXP their death cost | Measure the loss around `Set_PenalEXP` in `classUSER::Dead`: `m_lEXP` before - after, plus `m_lPenalEXP` after - before. **Not** `m_iAppliedPenaltyEXP`: the sign bug (section 11) inflates it and `Cancel_PenalEXP` zeroes it. Otherwise the refund lapses and the victim is told. |
| hunters | zuly bounty | Pool = 20 x money_unit(base level) x Σw, Σw capped at 5. money_unit = (Lv+20)(Lv+60) x DROP_M / 3200, one money drop (`calculation.cpp:160`). Per victim: about 4.8k z at level 50, 15.7k at 120, 35.7k at 200, 45k at 230. The Wisdom escort pays 50k for a 15-min party event. |
| hunters | spoils | k extra `CCal::Get_DropITEM` rolls of the row's own table (they scale with level), made in ProcZone with `SetACTIVE()` **before** `Add_DIRECT` (root CLAUDE.md: activating afterwards makes the item invisible). Package boxes cannot scale with level: `FindUseItemPackage` is a fixed compile-time list. |

**Hunters and split.**
- Hunters are taken from a per-vendetta damage ledger, keyed by owner DBID, fed at the `Save_Damage`
  call site (`cobjchar.cpp:1647-1655`):
  - a summon's damage counts in full for its owner;
  - overkill is clamped to the HP actually removed;
  - a hunter needs 10% of the damage;
  - their party within 50 m also counts.
- Do not use `m_SavedDAMAGED`. Many rows get only 6-7 slots, it drops newcomers while all slots are
  fresh, counts summons at half, and `Give_EXP` overwrites it.
- Split: pool x min(1 + 0.1(n-1), 1.5) / n per payee, deduped by DBID. This mirrors party EXP. The
  draft's `B/sqrt(n)` paid out more the bigger the group.
- **Same zone only.** `Add_MoneyNSend`/`Add_ItemNSend` take no lock, so paying a user in another zone
  races that zone's thread. `Give_EXP` makes the same check (`cobjnpc.cpp:392`).
- Clear the vendetta state before paying, so a payout runs exactly once.

**Who may claim the zuly** (user decision, section 12). Default: each victim's share of the zuly is
paid to *strangers* only. Nobody sharing that victim's account, IP or party at death gets it. The
victim's own revenge pays the refund, the EXP and the spoils, so "a reward scaled to its kill count"
holds for everyone. Two figures:
- On the alpha the zuly will mostly sit unclaimed, because strangers rarely meet.
- With the 60-min account rule, letting the victim's circle claim it too caps feeding at one bounty per
  account per hour. At level 120 that is about 15.7k z/h, against about 36k z/h of grinding money.

**Staff.**
- Staff (rights >= 100) in the ledger, or as the killer, voids the bounty for everyone. `/kill_all` is
  level 100 and kills through the user-kill branch.
- GM-made vendettas pay nothing.
- All of this is overridden by `test_mode` (section 9).

**Do not build:** a revenge multiplier (it paid 1.5 x 0 solo and a jackpot in party wipes), EXP in
the bounty, or credit to the caller of a minion. `CObjMOB::Get_CALLER`'s hash check is always true for
monsters, so a reused index credits an unrelated monster.

---

## 7. What the player sees

| # | element | notes |
|---|---|---|
| 1 | **always-on nameplate** while in view | Only for vendettas, so players and other monsters draw exactly as today (the presentation rule in memory). Use a virtual `IsVendetta()` on client `CObjCHAR` for the `cobjchar.cpp:5802` condition. Skip `CTargetManager`'s re-registration for vendettas only; leave `CViewMSG::AddObjIDX` untouched. Draw it only for the object the server index currently maps to, so an orphaned duplicate (an old `Add_MobCHAR` defect) never shows a frozen "Gorgo". |
| 2 | **title line** above the name | In a colour outside the level ladder (bone-white or ember, user's pick). **The name keeps its level colour**, the player's only signal of the level gap. Keep the role mark: skull 16 means a five-player boss. Clamp the classic gauge width to 100. |
| 3 | **glow** on the model | `setVisibleGlowRecursive`; re-apply after a client `Change_CHAR`. |
| 4 | UI2 target frame | Kind line `<base monster> - Slayer of Thomas`, plus a short line `2 victims - 15,700 z`. A long single line wraps and grows the 210dp frame. A second line needs RML/RCSS under `3ddata/rmlui`, loaded loose, so publish them. |
| 5 | Monster Inspector | Live `Get_NAME`/`Get_LEVEL`, the stat grid labelled as base stats, a vendetta line. Today it reads the table. |
| 6 | **whisper** to the victim and party | `<SERVER>::` always shows. "Gorgo the Grim (Grunter Fighter) took your life and a name. It prowls where you fell, in Junon Polis, for about 45 minutes. Bring it down to win back the EXP you lost." |
| 7 | **minimap marker** at the anchor, for the victim and party | `CMinimapDLG::AddIndicatorCoordinates`, drawn by both minimaps. Use a reserved index range: it asserts on a duplicate, and quest scripts share the space. Remove-then-Add on re-anchor. A client-side expiry equal to the lifetime, so a restart leaves no ghost. |
| 8 | the monster shouts | `Send_gsv_SHOUT` uses the server `Get_NAME`, so the new name comes for free. |
| 9 | zone chat line | On promotion, a new victim, death or expiry. Whispers on its death go to every victim: "Mira's party has avenged you: Gorgo is dead." |

**Notices** use a new `GSV_VENDETTA_NOTICE`. The server sends a template id plus tokens; the client
substitutes `NPC_NAME(row)` and `ZONE_NAME(zone)` and appends a chat line with
`AppendChatMsg(..., CHAT_TYPE_SYSTEM, colour)`. This reaches both classic and UI2. Two reasons:
- The server's column-0 names differ from the client's names in 67 rows (e.g. 543 Sikuku Tracer vs
  Tracker).
- The `GSV_ANNOUNCE_CHAT` banner is a classic dialog that draws under UI2 windows, and its text goes
  through the cheat parser.

**Left out of v1:** the pop-up banner, and a sentence in the death window. The window's text is fixed
when it opens, which is before the 3 s hold ends.

**Names.** "Gorgo the Grim":
- **Given name:** from a curated pool of about 40, 4-7 ASCII letters, none matching a LIST_NPC word,
  never two living vendettas with the same name. Candidates: Gorgo, Brakka, Skarn, Vosk, Grimmok,
  Thrask, Ruk, Mardok, Sliv, Korra, Durn, Vexa, Ozrik, Harg, Tarsk, Bruul, Kesh, Morga, Zarn, Ulgar.
- **Epithet:** by role mark. Basic "the Grim", Guard "the Unbroken", Fighter "the Cruel", Warrior "the
  Butcher", Ranger "the Swift", Hunter "the Patient", Magician "the Hexer", stars "the Warlord".
- **Why the epithet is always there:** the name then always contains a space. Player names are
  `^[a-zA-Z0-9_-]{4,20}$`, and `Get_ClientCharOBJByName` (the chat exchange command) also matches
  monsters, so a bare "Gorgo" could be mistaken for a player.

**Title.**
- k1: "Slayer of Thomas".
- k2: "Slayer of Thomas and Mira", or "and one other" past 30 characters.
- k3 and up: "Slayer of Thomas and N others".

The first victim is always named.

---

## 8. Server architecture

New files `src/sho_gameserver/src/vendetta.{h,cpp}`, in `sho_gameserver.vcxproj` and its `.filters`.

**`CVendetta` singleton.**
- **Zone slots.** One slot per zone, preallocated in the constructor from the zone count, never resized.
  Zone numbers are bounds-checked, so a future instance zone above the row count is simply ignored.
- **Locking.** A slot is written only by its own zone thread. One mutex covers the server-wide slot
  count, GM requests and the status snapshot. Reading party members needs `CParty::Lock`.
- **`ProcZone(CZoneTHREAD*)`**, called after the escort's in `CZoneTHREAD::Execute`
  (`gs_threadzone.cpp:238-241`):
  - **Throttle.** Return at once when the slot is empty; tick every 500 ms. It runs inside the nav
    meter window, so avoid `ForEachObject`: resolve the few tracked `{idx, spawn}` keys with
    `Get_CharOBJ` plus `GetZONE() == pZone`.
  - **Apply held promotions** once 3 s have passed. Re-validate index, spawn time, zone, `HP > 0` and
    not `CMD_DIE`. Do not check the row, because `Change_CHAR` may have changed it. Merge several kills
    by one monster.
  - **Apply deferred growth** after 30 s without damage.
  - **Run the break-off** and the revive grace.
  - **Expire** after 45 min, never within 60 s of taking damage. Removal is
    `DetachTargeters()` + `Sub_DIRECT` + `delete`, no drop, followed by whispers.
  - **Pay** queued payouts.
  - **Carry out GM requests.**

**Hooks (each is a few lines).**

| where | what |
|---|---|
| `classUSER::Dead`, inside the once-guard, after `Set_PenalEXP` (`gs_user.cpp:919-925`) | Measure this death's EXP loss. If eligible, record a request **by value**: `{mob idx, spawn time, row, victim dbid, account, IP, name, level, party member dbids, measured loss}`. Never act here: this is inside the attacker's `Proc` in the object loop, and an area skill would hit later victims with the new stats. |
| `Apply_DAMAGE` user-kill branch, after `Give_EXP`, before `Do_DeadAI` (`cobjchar.cpp:1670-1672`) | If the target is a vendetta, copy the whole payout record (ledger, victims, kill count, row, staff flag) into the zone slot. The monster is deleted at the end of its death motion, possibly before ProcZone runs. |
| the `Save_Damage` call site (`cobjchar.cpp:1647-1655`) | Feed the DBID ledger when the target has `m_pVendetta`. |
| `CObjMOB::Dead` | Move the targeter-unlink loop (`cobjnpc.cpp:244-256`) into `CObjMOB::DetachTargeters()` and call it from both places. |
| `CObjMOB::Change_CHAR`, after the stat block (line ~612), before `GSV_CHANGE_NPC` is built | Re-apply the promotion so the packet carries promoted HP, then send `GSV_MOB_VENDETTA` right after. |
| `CObjMOB::Make_gsv_ADD_OBJECT` (`cobjnpc.cpp:144`) | Append the trailer after `Add_ADJ_STATUS`, never on `FLAG_ING_DEC_LIFE_TIME` objects. |

**`CObjMOB` changes** (server-only class, but see section 10 for the clean rebuild).
- **Members.** `tagVendetta* m_pVendetta = nullptr;` as an in-class initialiser. Move `~CObjMOB` out of
  the header into `cobjnpc.cpp`: deleting a forward-declared type triggers C4150 and leaks its strings.
  Hot getter state (bonus level, multipliers) is plain ints.
- **Getter overrides:** `Get_LEVEL` (base + bonus), `Get_NAME` (vendetta name), `Get_GiveEXP` (x hpMul).
  `Get_INT`/`Get_SENSE` follow `Get_LEVEL` automatically.
- **`Promote()`** must be a member, because `m_nCritical` is private. It recomputes from the row:
  - `m_iOriMaxHP` in 64-bit, clamped;
  - HP keeping its percentage;
  - `stats.attack_power`;
  - `m_nCritical`.

  Then it sends the live update.

**GM and player commands.** Append to the **end** of both `CommandId` and the `commands` vector in
`commands.h`; `REGISTER_COMMAND` pairs them by index.
- `/vendetta status|list|promote [victim]|addkill|expire`, level 100:
  - `promote` takes the GM's current target and runs the full victim path, with the GM as stand-in.
    It still refuses `OBJ_NPC`, summons, `DEC_LIFE_TIME` objects, non-`TEAMNO_MOB` monsters, type-999
    rows and marks 11/16.
  - There is no "demote" or "clear": `expire` removes the monster. A live demotion cannot lower the
    client's bar ("lower HP never silently moves the bar").
- `/bounty`, level 1: whispers the living vendettas.

---

## 9. Config, test mode, logging

**Config.**
- A `[vendetta]` section, read by `Vendetta::LoadSettings`, modelled on `Nav::LoadSettings`:
  `std::atomic<int>` fields, read at start and from `reload_game_config`.
- **Integers only.** The toml FFI exposes only `toml_get_int` and `toml_get_str`, so a value like 1.2
  is silently ignored. Write every multiplier as an integer percent: `hp_base_pct = 200`,
  `hp_per_kill_pct = 250`, `atk_per_kill_pct = 4`, `atk_cap_pct = 120`, `bounty_drops_per_victim = 20`...
- Free an `FfiString` only when `toml_get_str` returned true (memory: a missing key otherwise crashes
  the server at startup).
- Do not put these in `GameConfig`: reload copies that struct non-atomically while zone threads read it.

**`test_mode` (int, default 0).** Without it the feature cannot be tested in game. The developer is
staff, all local accounts share one IP, and the 60-min rule blocks a second try. `test_mode = 1`:
- staff count as victims and hunters;
- the IP rule and account cooldown are off;
- the timers run in seconds;
- every log line is tagged `TEST`.

Every voided bounty logs a dry-run payout table, so the formula can be read without paying anyone.
The non-GM sim accounts (`simtest@local`, `simsummon@local`) can then play victim and hunter on one PC.
Set `enabled = 0` during combat-sim validation sessions, so a sim character's death cannot promote its
target and skew the numbers.

**Logging, prefix `[vendetta]`, in `Exes/log/gameserver-<date>.log`.**
- One line per player death (zone, killer idx/row/spawn/mark/level, victim dbid/account/IP/level,
  verdict and reason).
- One line per promotion, counted kill, growth step, expiry and payout (each payee's dbid, zuly,
  refund, items).
- A minted-since-boot total in `/vendetta status`.
- A per-minute `zone N live k` line next to the nav stats.
- On the client, a `LOG_DEBUG` when a vendetta update is applied or dropped.

---

## 10. Wire format and the client

**Spawn-packet trailer.**
- **Struct:** `tagMOB_VENDETTA { WORD magic; BYTE version; BYTE kills; short level; int maxHP;
  DWORD bounty; }` followed by `name\0 title\0`, under 160 bytes in all.
- **Server:** appended in `CObjMOB::Make_gsv_ADD_OBJECT` after `Add_ADJ_STATUS`, built in a local
  buffer and added with one `AppendData`. All three add-packet senders use this one builder (spawn,
  sector entry, arriving users); clan halls never send monsters. `CObjNPC` has its own builder, so NPCs
  never carry it.
- **Client:** in `Recv_gsv_MOB_CHAR`, skip the trailer when `FLAG_ING_DEC_LIFE_TIME` is set. Compute
  `off = sizeof(gsv_MOB_CHAR) + 2 x popcount(the 5 adj flags)` yourself, because `Recv_tag_ADJ_STATUS`
  can return without advancing. Then require `m_nSize >= off + sizeof(tag)` and a matching magic, read
  the strings with the bounded three-argument `Packet_GetStringPtr`, and cap them at 16/40 characters.
- **Old clients ignore trailing bytes**: no `sizeof` check, and the FlatBuffer verifier rejects id
  0x0792.

**`GSV_MOB_VENDETTA` (0x07f2), the live update.**
- **Fields:** `{WORD obj, short row, BYTE state, int hp (-1 = none), tagMOB_VENDETTA}`.
- **Sent** only with `SendPacketToSectors`, never with the deferred `SendPacketToZONE`, and only from
  ProcZone for a live monster. Each socket's queue is FIFO and everything runs on the zone thread, so it
  can neither overtake an add packet nor reach a viewer that lacks the object.
- **Client:** apply only to an `OBJ_MOB` with `Get_CharNO() == row`, `HP > 0` and no lethal event
  pending. Call `Set_MaxHP` first, then `Reconcile_HP(hp)`, never `Set_HP` (same as
  `Recv_gsv_CHANGE_NPC`).

**`GSV_VENDETTA_NOTICE` (0x07f3).** Template id, tokens, row, zone, names, numbers and marker
coordinates (section 7).

Ids 0x07f2-0x07fe are free on both sides; 0x07ff is the client's `SOCKET_NETWORK_STATUS`.

**Client `CObjMOB`.**
- **Members:** vendetta name, title, level, kills, bounty. Keep them out of `CObjMOB::Create`, so a
  client `Change_CHAR` (which re-creates the model and resets max HP) does not wipe them.
- **Overrides:** `Get_NAME` and `Get_LEVEL`, falling back to the table when empty, because client
  `CObjNPC` inherits them. They reach the nameplate, the UI2 target frame, the damage meter and the
  hunt line.
- **Quest kill credit is by row**, so it is unaffected.

**Version skew is harmless.** Old clients log the unknown packet and carry on, so the server can ship
before testers patch.

**Rebuilds.** Both twins change layout: server `CObjMOB`, and client `CObjMOB`, which is also the base
of client `CObjNPC`. Delete `build/<config>` and rebuild client and `sho_gameserver` serially. The
`net_prototype.h` additions rebuild all three servers.

**Data.** Only optional v2 art: a vendetta mark in the empty `TARGETMARK` cell 17 (cells 17-35 are
blank; DDS art and a bake, no TSI or `UI_strID` step). It would need a virtual `GetTargetMarkIndex()`.
Any data change goes through a re-runnable `scripts/` tool, because `data/` is untracked.

---

## 11. Pre-existing problems found on the way (independent of vendetta)

1. **The death penalty can pay.** `Set_PenalEXP` has a sign error in its cap branch
   (`cobjavt.h:423-424`, copied on the client in `cuserdata.h:833-834`).
   `m_iAppliedPenaltyEXP -= (DIV02(need) - m_lPenalEXP)` subtracts a negative once the debt passes half
   a level. So at capped debt a death records an inflated penalty, and a Resurrection then refunds more
   EXP than the death took. It should be `-= (m_lPenalEXP - DIV02(need))`. Fix both copies as a
   separate, separately tested change. Vendetta makes deliberate deaths more common.
2. **Removing a live monster leaves its targeters linked.** `Sub_DIRECT` (`gs_threadzone.cpp:659-679`)
   never unlinks the users whose target node sits in the monster's `m_TargetLIST`; only
   `CObjMOB::Dead` does. Once the index is reused by another monster in the zone, the next retarget
   runs `DeleteNode` on the wrong list: a null dereference or list corruption. It is rare, because the
   index has to wrap. Existing exposure: the Cerberus sleeper swap and `RealProc`'s `UpdateSECTOR`
   failure path. `DetachTargeters()` fixes it for all of them.
3. **Wrong "pooled" comments** in `cai_lib.h:29-30` and `cobjnpc.cpp:72-74`. Objects are not pooled;
   only indices are reused.
4. **Stale skull docstring** in `scripts/fix-monster-marks.py:35`: it says ten players, the current rule
   is five (`rebalance-monster-roles.py`, 2026-10-16).
5. **No death logging anywhere**, and no client IP in any game-server log. M0 adds both.

---

## 12. Decisions for the user (recommended default first)

| # | decision | recommended | alternative |
|---|---|---|---|
| 1 | roaming in v1 | **no**: anchored where it killed, re-anchoring at each kill (section 4) | roam camp to camp in v1, the riskiest milestone, and it needs a moving marker to be readable |
| 2 | lifetime | **45 min after its last kill; a restart ends it**; the victim is told | persistence in v1 (a DB row plus respawn at boot) for "it waits for you tomorrow" |
| 3 | growth | **the sim curve in section 5** (k1 = a 15-25 s solo revenge, k5 = a group fight) | gentler (k1 HP x1.25, barely different from a normal kill) or harsher (HP x20 cap) |
| 4 | who may claim the zuly | **strangers only** for each victim's share; victims get the refund, EXP and spoils | anyone, including the victim's party, bounded by the 60-min account rule |
| 5 | gold stars (two-player content) | **excluded in v1** (k1 would be about 18x trend HP on 1.40 ATK, near the cliff) | eligible, the strongest "bring a friend" story; bronze/silver are eligible either way, kings and skulls never |
| 6 | Forgotten Temple 56/57 | **excluded** (they are PvP state 1) | allowed: they are quest-gated PvE |
| 7 | announcements | **whisper + chat line + monster shout**, no pop-up | also the `GSV_ANNOUNCE_CHAT` banner for 3+ victims (check it under UI2 first) |
| 8 | title colour | bone-white or ember (outside the level ladder) | -- |
| 9 | M0 log-only phase | **yes, 1-2 weeks of play** before building any presentation | go straight to M1 |
| 10 | death-penalty sign bug | **fix it now, separately** | leave it; vendetta never reads the field |

---

## 13. Milestones

Each one ends with a test checklist for the user and one commit after the in-game test.

**M0 -- log only (server, about a day of work, then 1-2 weeks of play).**
- The `classUSER::Dead` hook plus `IsEligible`, logging "would promote" verdicts with reasons. Nothing
  changes for players.
- This measures real frequency, where deaths cluster, and testers' rights and IPs. Then set caps,
  lifetime and the IP rule from data.
- Also port the scratch tuning tools into the repo: a `--vendetta k` mode for `scripts/combatsim`, and
  the AI eligibility audit as a `scripts/` tool over `rosefmt.aip`.

**M1 -- promotion and presentation (server + client; the class-layout change, clean rebuild).**
- **Server:** `CVendetta`, the `CObjMOB` members and getters, `Promote()` with the 3 s hold and growth
  deferral, anchoring and break-off, revive grace, expiry with `DetachTargeters`, the `Change_CHAR`
  re-apply.
- **Wire:** the trailer and `GSV_MOB_VENDETTA`.
- **Client:** nameplate, title, glow, target frame, Monster Inspector.
- **Tools:** `/vendetta`, config, `test_mode`, logs.
- **Test:**
  - a natural promotion by a non-GM death;
  - the title appearing only after the visible death;
  - the victim's party finishing the fight (bar behaviour);
  - a second victim (deferred growth);
  - a second client leaving and re-entering the sector, a zone change, a relog;
  - GM expire while a player targets it, then targeting another monster (no crash);
  - an old client connecting (a log line only);
  - the Monster Inspector and damage meter;
  - UI2 and classic both.

**M2 -- bounty and rewards.**
- **Server:** the DBID ledger, payout record by value, the `Get_GiveEXP` scale, the measured refund,
  zuly and extra drops, same-zone payees, the staff void and anti-feed rules (`test_mode`
  overrides). `/bounty`.
- **Client:** `GSV_VENDETTA_NOTICE` with client-side text, whispers, the minimap marker.
- **Test:**
  - a solo revenge (refund);
  - a party kill with one member warped away (forfeits);
  - a hunter dead at the kill (still paid zuly, no EXP);
  - a voided staff kill (dry-run log);
  - a server restart (no ghost marker).

**v2 backlog** (after M0 data and some v1 play):
- roaming (section 4's list);
- persistence (DB row, respawn at the anchor, a 24 h lifetime, a journal timer quest; a timer needs
  persistence or it creates stale-quest bugs);
- "it remembers you" taunts when a victim comes within 25 m;
- a revenge edge (victims deal +25% damage to it: server-only, no presentation change);
- a trophy cosmetic;
- an aura bone effect;
- the mark in cell 17;
- a UI2 bounty board;
- stricter stranger rules for a public server (revive protection for victims, daily claim limits,
  payee level gate).

**Docs when built:** this file becomes the brief. Lessons go into `doc/making-events.md`, plus
gameserver and client `CLAUDE.md` entries: `DetachTargeters`, the 3 s hold, the trailer offset rule,
integer-percent config.

---

## Appendix: where the numbers came from

The review's scratch tools sit in the session scratchpad (temporary, so port them in M0):
`work\hitcalc.py` (exact hit chance), `work\vsim.py` (a promoted-monster subclass of the combat sim),
`work\numbers\cand*.py` and `tables.txt` (curve runs), `work\econ.py` and `numbers\bosses.py` (EXP, zuly,
boss figures), and `work\vendetta_audit.py` with `analyze*.py` (the AI and regen audit over rosefmt).
