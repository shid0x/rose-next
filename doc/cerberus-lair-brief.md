# Cerberus Lair: implementation brief

Status (2026-10-08): **validated in game** -- the draw, the fight and the eviction
(first test: three runs, one and two players), then the second round (no exit gate,
GM `open`/`draw` no longer latched, the loot): a two-player run, good loot, evicted
after the grace minute, gatekeeper back to "closed". Proof of concept: one lair, one
run at a time.

The schedule runs on the game server machine's **local** clock (`localtime_s`):
registration :00-:10, the draw at :10. A GM `open` counts as that hour's window.

Testing without waiting an hour: `/cerberus open` (registration for 2 minutes),
sign at the gatekeeper, `/cerberus draw`, `/cerberus status`, `/cerberus reset`.
The server log lines are prefixed `[cerberus]`.

## 1. The feature, from the player's side

- The **Arumic temple** in Arumic Valley (LP02, DECO 37 `arumic01-03`, the only
  placement, at (6261, 4819) m) gets a gatekeeper at its door, around (6235, 4845).
- **Every hour, on the hour**, the gatekeeper opens registration for **10 minutes**
  and says so to the whole server.
- Any player of **level 140 or more** who carries the fee can register through the
  gatekeeper's dialog. Logging out drops the registration.
- **At :10 the draw**: up to **5** registrants are picked at random. Fewer than 5
  registered is fine: everyone who registered goes, from 1 to 5. Players can be
  **anywhere** in the world at the draw. The fee is taken at the draw, from the
  players drawn only; a drawn player who can no longer pay is skipped.
- The drawn players are **put in a party together** (a drawn player who is in
  another party leaves it first; a lone player gets no party) and teleported to
  the lair's entrance.
- **The way in** (2026-10-09): two packs of three **Hellhound Whelps** (2680,
  light trash that calls its litter when hit) in the pines south of the entrance and on the ice south of the river, then the
  **Warden of the Seal** (2681, a lv152 mini-boss, ~74k HP) before the crater rim.
  Its one big spell, **Seal Burst** (7023), is an 8 m burst with a 40% slow behind
  a slow wind-up and a zone shout, cast at 66% and 33% of its HP and now and then.
- **Cerberus appears only when the Warden dies**, asleep in the crater. Coming
  close, or hitting it, wakes it.
- **Death inside revives inside**, at the lair's entrance (`restore`).
- **Cerberus dies**: a zone announcement, about 60 s to pick up the loot, then
  everyone is teleported back to the temple door. A run that lasts too long
  (20 min) ends the same way, without the kill.
- **Nobody gets out** before the end: there is no gate, a save-point revive becomes
  the lair's own, and warp scrolls are refused (the scroll is kept). Logging out
  leaves the fight; logging back in during the run puts you back inside.
- **Loot**, about five items a kill (details in the importer's docstring, "Loot"):
  every kill drops one unique weapon (the 13 named lv155 weapons, Icicle,
  Falchion-Firangi), one lv145-155 wing or back shield, a gem [5] and a gem [6];
  the table roll adds a lv160-170 weapon (45%), a gem [6]/[7] (24%), lv165 armour
  (15%) or a lv150-170 wing (14%). Nearly all of it had no source in the game.

## 2. The map and the monsters (data only)

Imported by `scripts/import-cerberus.py` from the tsuki dump.

- **Zone 49**, tsuki's own number, free here. 3 chunks (30_30 to 32_30), a
  480 x 160 m strip: the entrance and a frozen river to the west, a crater ringed by
  rock to the east. Every object it places is byte-identical to our Luna tables,
  every tile is ours; it ships `.MOV`, lightmaps and a minimap.
- **2682 Cerberus**, level 155, the awake boss. **2683 Cerberus (asleep)**: the same
  model on `CERBERUS_SLEEPING.ZMO`, no movement, and an AI whose only job is to
  turn into 2682 (AI action 9, `CObjMOB::Change_CHAR`, which swaps model, HP and AI,
  since the AI is looked up from the current row every tick).
- The model reuses our wolf skeleton (`WOLF3\RUNA_WOLF3_BONE.ZMD`) and our
  `runa_wolf1` motions, both byte-identical; only `NPC\ANIMAL\CERBERUS` (4 meshes,
  3 textures) and the sleeping clip are new.
- **No regen point.** tsuki's one point in the crater is emptied; the controller
  spawns every monster of a run itself: the whelps and the Warden when the run
  starts (`RUN_SPAWNS`), the sleeper at that position (`CRATER_X/Y`) once the
  Warden is dead. Between runs the lair is empty. A regen point cannot be made to respawn on demand
  (`CRegenPOINT::Reset` only zeroes its count and still waits a full interval).
- **No way out.** tsuki's `cerberus_warp` event object is dead (no QSD has it) and
  is removed. The first import put a warp gate there (WARP row 105); the first test
  asked for none, so it is gone and stage 1 blanks the row. The temple-door event
  point in LP02.ZON stays: the server evicts everyone there.
- **No way out on the server side either** (`CCerberusLair::IsLair`):
  `Recv_cli_REVIVE_REQ` turns a save-point revive into the lair's own (Luna players
  are saved on the same planet, so it would leave), and the warp-scroll path
  (`SKILL_TYPE_18` in `Recv_cli_USE_ITEM`) refuses in the lair before the scroll is
  consumed.
- No AI exists for Cerberus in any dump (QQ-iROSE's `CERBERUS.AIP` is our
  `CLAN_BOSS6.AIP`, the Astarot King, with two ids changed). Ours is written here.
- **The kit** (importer docstring, "The fight"): melee, Infernal Leap (skill 7021,
  the jump with fire effects), Hellfire Breath (skill 7022: two fireballs from the
  snout and a burn, at range), and two Hellhounds (2684) called at 66% and again
  at 33% HP. A fiery glow (LIST_NPC col 39) on Cerberus and the hounds.
- **The controller spawns the hounds, at the crater** (`HOUND_CALL_PCT`), not
  Cerberus's AI: a new monster stands on the highest surface at its spot on the
  client (`CObjMOB::Create` -> `GetHeightTop`, the server sends no height), and
  hounds summoned where Cerberus fought, under the rock arches, landed on the roof.

## 3. The controller (C++, game server)

A small state machine in the lair zone's thread (`CZoneTHREAD`), on the server's
local clock.

| State | When | What happens |
|---|---|---|
| IDLE | between runs | gatekeeper event value 0 |
| OPEN | :00-:10 | announcement; event value 1; the register trigger adds players to an in-memory list |
| DRAW | :10 | random pick of up to 5 online registrants who can pay; fee taken; party formed; teleport |
| RUN | until the kill or 20 min | spawns the whelps and the Warden, then Cerberus once the Warden dies; logins into the lair from anyone not in the run are sent out |
| GRACE | kill + 60 s | announcement; loot time |
| EVICT, RESET | then | everyone in the lair to the temple door; kill all monsters, delete ground items, reset the regen point; IDLE |

An hour whose :00-:10 window falls entirely inside a run is skipped; a run that ends
before :10 opens registration for what is left of the window.

- **Registration** is a QSD trigger (`Cerberus-Register`): the conditions are the
  gatekeeper's event value = 1, level >= 140, and the fee in the bag. The trigger
  itself takes nothing; the server recognises it by name after it succeeds and adds
  the player to the list.
- **The dialog** is `quest-editor con-toll ... --open-when 1`: a third answer,
  "closed", shows while the gatekeeper's event value is not 1. COND_011 always
  passes on the client, so the dialog reads the value itself
  (`QF_getNpcQuestZeroVal(QF_getEventOwner(E))`).
- **Threads.** The lair's thread owns the run: the draw, arrivals, evictions, the
  reset. The fee is charged on **arrival**, in the lair's thread, since by then it
  is the player's own; a player who can no longer pay is sent back out. The
  gatekeeper's zone (LP02) only mirrors the state into the NPC's event value 0.
  Party changes at the draw cross threads, as the retail party accept flow already
  does.
- **The party** is created at the draw and raised to party level 5
  (`CParty::Raise_PartyLEV`): a new party holds `level / 5 + 4` members, so 4 at
  level 1.
- **Death inside** revives at the lair's `restore` point; choosing the save point
  leaves the run. A run with nobody left inside ends after the arrival minute.
- **Teleports** go through `Send_gsv_RELAY_REQ`, the safe deferred form.
- **Security fix that comes with it**: `Recv_cli_RELAY_REPLY` refuses a
  destination other than the last one the server sent (`m_bRelayPENDING`,
  `m_nRelayZONE`, `m_RelayPOS` on `classUSER`). Before, a modified client could warp
  into the lair, or anywhere. A refused reply is logged and ignored, not a
  disconnect, since a stale reply can cross a newer request.
- **GM commands** `/cerberus status|open|draw|reset` (`open` only while idle, `draw` only while open: refused otherwise, never remembered -- a latched `open` used to fire when the next run ended), since nobody can wait an hour
  to test.

## 4. Milestones

1. Map, monsters, model (importer stages 1-2). **Done**; the exit gate added first was removed after the first test.
2. The gatekeeper (NPC 4149 `[Arumic Seal Keeper] Yelena`, model of the Arumic
   Prophet 1173, LIST_EVENT 140, `EM53-001.con`), the register trigger, the dialog
   (stage 3). **Done.**
3. The controller and the relay check, with the GM commands. **Built.**
4. The loot (stage 4 + the death drops in stage 2). **Done, validated.**
5. The boss kit (glow, Infernal Leap, Hellfire Breath, Hellhounds) and balance.
   **Done, validated.**
6. The way in: whelps, the Warden of the Seal (Seal Burst, loot table 967:
   lv145-155 armour and gems [4]/[5]), Cerberus gated on the Warden. **Built
   2026-10-09, untested.**

## 5. Things that will bite

- **Restart the servers and rebake** after every data stage; LIST_NPC.CHR is read
  by the server too.
- **New monster rows in levels 60-199 go into `balance-trend-exclude.py`**, and
  every balance pass is re-verified.
- **Drop-table ids are zone ids**: zone 49's row must be filled too.
- `Proc_TELEPORT` must only be called from the player's own zone thread; the
  controller lives in the lair's thread and the players are elsewhere at the draw,
  so every teleport is a relay request.
- **The constants live twice**: zone 49, LP02 53, NPC 4149, rows 2682/2683, the
  landing event, the trigger name, the fee and the crater position are in both
  `import-cerberus.py` and `cerberus_lair.cpp`. The importer refuses to run if
  tsuki's crater moved.
- tsuki's 30_30 lightmap index lit a record the client drops (a stray rock 3.2 m
  past the chunk edge): its entry is removed by the importer. The rock was never
  visible in tsuki either.
