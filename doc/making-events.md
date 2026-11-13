# Making a server-run event

How to build an event the game server drives: a timed instance, an escort, waves of
monsters, an NPC who fights with the players. These are the lessons from the two built
so far:
- **the Cerberus Lair**: an hourly draw into a dedicated zone. Code in
  `src/sho_gameserver/src/cerberus_lair.*`, data in `scripts/import-cerberus.py`, design
  in [cerberus-lair-brief.md](cerberus-lair-brief.md).
- **the Wisdom Tree escort**: a party escorts an NPC round the great tree in the live
  Forest of Wisdom. Code in `src/sho_gameserver/src/wisdom_escort.*`, data in
  `scripts/import-wisdom-escort.py`, design in
  [wisdom-tree-escort-brief.md](wisdom-tree-escort-brief.md).

Each answer below was found by reading code first; this file is so the next event
does not have to. For the monsters themselves (rows, clips, skills, AI, an attack to
dodge), see [making-monsters.md](making-monsters.md).

## 1. The controller (C++)

- **A singleton with a `ProcZone(CZoneTHREAD*)`**, called from `CZoneTHREAD::Execute`
  next to the others (`gs_threadzone.cpp`). It returns at once outside its zone and
  ticks on its own clock (250-500 ms).
- **The zone's thread owns the event.** Every object in a zone lives in that thread,
  and a player's packets run in the thread of the zone they stand in. Spawning, orders,
  removals and rewards all happen in `ProcZone`.
- **Collect, then act.** Never add or remove objects inside `ForEachObject`: collect what
  you need, then act on it.
- **Anything from another thread only records a request**, under the controller's
  mutex: a GM command typed in another zone, a quest trigger. The zone thread acts on
  it at its next tick.
- **Hooks:**
  - a dialog trigger, by name, in `classUSER::Do_QuestTRIGGER` (`gs_user.cpp`), after the
    trigger succeeded (its conditions are the check);
  - a GM command in `cheatcmd.cpp`, plus `common/include/rose/common/commands.h`. Only
    the server includes it: the client sends a slash command as chat, so it needs no
    change.
  - the `.cpp`/`.h` in `sho_gameserver.vcxproj` and its `.filters`.
- **Log lines with a prefix** (`[cerberus]`, `[escort]`): the log is
  `Exes/log/gameserver-<date>.log`.

## 2. Monsters from code

- **`RegenCharacter(x, y, range_cm, row, 1, team, true)`** spawns at once and returns
  the monster, but only the last one it made: spawn one at a time to keep each.
- **The returned monster is usable at once, but announced only on the next pass.**
  Its index is valid immediately (`Add_OBJECT`), but it joins the zone's lists then:
  give orders the next tick.
- **Track a monster by object index *and* NPC row**: indices are reused. Look it up with
  `g_pObjMGR->Get_CharOBJ(idx, true)`, which returns null when it is dead, and check
  `Get_CharNO()` and its zone.
- **Send it at a target**: `SetCMD_RUNnATTACK(targetIndex)`, the summon attack path.
  From then on its own AI decides.
- **Order a cast from code** (the Crypt Necromancer, `CCatacombs::ProcBoss`, 2026-11-10):
  `((CObjMOB*)p)->SetCMD_Skill2OBJ(targetIndex, skill, motion)`, what AI action 24
  calls. A controller's clock keeps a boss's pace the same for one player and for
  five, which an "every N hits taken" AI rule cannot. Rules:
  - order only on a target inside the skill's range (col 6; blank = the monster's
    swing reach), or it walks first and the clients abandon the late cast;
  - one at a time: skip while `Get_COMMAND()` is `CMD_SKILL2*` or `Get_ActiveSKILL()`
    is set. That is also how to chain two: order the second on the first tick the
    monster is free again;
  - a stunned or silenced monster drops the order and still returns true: check
    `m_IngSTATUS.IsIgnoreSTATUS()` first;
  - the order makes that character its target; action mode 2 (col 29) puts it back
    on the one it was fighting.
- **A mechanic that is not a cast: the floor layer** (the Karkia catacombs' Argentia,
  2026-11-12; `CCatacombs::ProcFloor`). `CZoneTHREAD::SendZoneWarn(shapes, n, warnMS,
  effect)` draws circles, rings, strips and cones on every client's floor for a time
  and plays a FILE_EFFECT row on each as it ends; the controller keeps the same shapes
  and, when the time is up, hits who stands in them with `boss->Hit_WithSKILL(player,
  row)`: a skill row kept for the purpose, computed like a cast's hit and sent as that
  skill's result, so the level gate, resistance and the damage meter work as for a
  cast and the player gets the row's hit effect and hit sound (cols 74-76; the sound
  plays only with an effect). The packet carries two FILE_SOUND rows too, one as the
  shapes appear and one as they fire: **a skill's casting-sound columns are read by
  nothing**, so without them a mechanic is silent (the user's first remark on Argentia).
  Rules learned:
  - run it **every frame** of the zone thread, not on the controller's half-second
    tick: a dodge window is two seconds;
  - give the boss something to do meanwhile: a self skill with no effect (a copy of a
    monster self buff with the status cleared) is only its clips and casting effects.
    Time the floor from the moment that cast starts (`m_bCastingSTART` and
    `Get_ActiveSKILL()`), since an order waits for the swing in progress;
  - a square is a strip as wide as it is long; a late arrival does not see a warning
    already running;
  - the client holds a skill result until its caster's action frame while the caster
    is doing *any* skill: for a monster it now presents at once when the result is
    for another skill than the one being done (`Recv_gsv_DAMAGE_OF_SKILL`);
  - **a monster with walk and run speed 0 does not chase** (`CObjCHAR::Nav_Chase`,
    `SetCMD_RUNnATTACK`): it takes only targets in its reach and lets go of one that
    leaves it. Before, every client ran it on the spot.
  Added with Ferdinando (2026-11-12, `scripts/catacomb_ferdinando.py`), a boss that
  walks and has six moves:
  - **more than two skills on one monster**: `MAX_MOB_ANI` is 24, and CHR slots 12-23
    are six more cast / release pairs (the row's cols 52 / 68 name the slots);
  - **cast speeds in multiples of 10 %**: the engine plays a clip at a whole number of
    frames a second, so 25 % of 30 fps runs at 7 on the client and 7.5 on the server
    and the floor fires off the animation;
  - **a move with a front is aimed at the boss's current target**: a self skill does
    not turn the model, and he already faces whoever he is hitting;
  - **the row's col 29 = 2** sends him back to the fight he was in when the move ends;
  - **a warning's effect is turned along its strip or cone** (the effect's +X is the
    heading; at a strip's middle, at a cone's point), so build line and fan effects
    along +X. An effect linked to the boss by a casting slot is drawn at his size;
    one played by a warning is in the world at its own;
  - **`CObjCHAR::Blink_POSITION(x, y)`** puts a monster somewhere else at once
    (`GSV_MOB_BLINK`): no walk, no stop, its command and clip untouched. The caller
    answers for the spot (open floor, same level);
  - **another client's effects may be held**: SlopRose's kit marks effects its client
    removes by itself (a mesh `repeat` 0, a particle sequence with `loops` 0). Ours
    never end, so give them a count before using one as a one-shot;
  - **player clips on a monster**: EVO-REFORGED's `AnimRetarget/retarget.py` (by bone
    name; 221 of our NPC skeletons carry the avatar's `b1_*` bones), run as a
    subprocess. Clear the source's frame events and write ours.
  Added with Gladiomurus (2026-11-13, `scripts/catacomb_gladiomurus.py`; the shared
  tools are `scripts/catacomb_kit.py`):
  - **a shape that stays**: a cue's `dwLingerMS` / `dwTickMS` keep its hazards after
    they fire. Nothing is drawn for it: the effect played as it fires must last as
    long (her sword walls: a mesh entry's `repeat` x its loop). **It is looked at every
    frame, and a strip also hits whoever crossed it**: checked twice a second, a wall
    3 m wide let every runner through (0.25 s to cross). Each player is hit at most
    once per tick, standing in it or going through. Since crossing is caught at any
    width, make the shape as wide as what is drawn (her walls: 1.2 m, the blades are
    0.9 m): at 3 m it hurt players who stood clear of the swords;
  - **the next move does not wait for a standing shape**: `FloorBusy` counts cues and
    hazards that have not fired, so a wall is still up during the move after it;
  - **cues that aim themselves when they come due**: `iThrustLength` (a strip from
    the boss at one player, the boss turned to it), `iDiveRadius` (a circle on the
    farthest player, kept as the boss's spot), `bAtSpot` (shapes centred there),
    `bBlinkToSpot` (a cue with no shape that puts the boss there);
  - **`Blink_POSITION(x, y, deg)` also turns the model** (the server keeps no facing):
    the same spot with a facing is a turn on the spot, which is how a self skill is
    pointed at someone other than the current target;
  - **a 3ds Max biped under its default names** ("Bip01 L Thigh") needs a map file
    for the retargeter (`BONE_MAP` in her script); the avatar's bones are `b1_*`;
  - **a monster's idle or casting clip may ship without an event table**:
    `rosefmt.zmo.with_trailer` gives it an empty one to write events in;
  - **a bigger effect mesh is a scaled copy**: an `.eft` mesh entry has no scale, so
    scale the ZMS's positions and its vertex ZMO's position channels (`great_sword`).
  Added with the Assassin (2026-11-13, `scripts/catacomb_assassin.py`):
  - **a soak: a shape to stand in.** A cue's `iSoakNeed` makes its circles soaks, sent
    with `ZONE_WARN_SOAK` so the client draws them green where a danger is red. As
    one lands, those in it take its row; with fewer in it than asked, everybody in
    the room takes `nFailSkill` and the room flashes. **Never ask for more players
    than are in the room** (`nAsked` is clamped to the living): the user tests alone,
    and a fight that cannot be done alone cannot be tested. The same for how many
    soaks appear at once;
  - **a shared blow** (`bSplit`): no failure, the row's damage x (those asked for /
    `iSoakNeed`) / (those in it), through `Hit_WithSKILL(target, row, fScale)`. Paired
    with a red ring over the rest of the room it teaches itself: the only place left
    to stand is the green one;
  - **a cue may make another cue** (a strike's echo): push it to a side list and
    append after the loop, the loop holds a reference into the vector;
  - **say what to do** (`Alert(players, text, ms, style)` -> `GSV_ZONE_ALERT`): a
    mechanic with one right place to be gets a line in the middle of the screen for as
    long as the players have, green for a soak. For the moves that are only "leave the
    red", the floor is enough: an alert on everything is noise.
- **Remove it**:
  - with no death and no drop: `pZone->Sub_DIRECT(p); delete p;` (the GM "npc DEL"
    pattern);
  - killed with no killer, so no EXP or drop: `p->Add_DAMAGE(p->Get_HP() + 1)`.
- **A regen point cannot be made to respawn** (`CRegenPOINT::Reset` only zeroes its
  count). An event spawns its own.
- **A town NPC row cannot be spawned.** `RegenCharacter` refuses type 900 and above, and
  dialog NPCs are 999. A fighting twin of a dialog NPC is a separate monster row.

## 3. An NPC who fights with the players

- **A monster row spawned on `TEAMNO_USER`.** `Is_ALLIED` multiplies the two teams, and
  100 or less is allied. So it is allied with every player (2 x 2) and NPC (2 x 1), and
  an enemy of every monster (2 x 100).
  - Monsters' "enemy near" condition (AICOND 02) finds it like any character.
  - The client gets the team in the add packet and shows it friendly, as it shows summons.
- **No new class.** The controller walks it the way `CObjSUMMON::Proc` follows an owner:
  - `Nav_MoveOrStraight(x, y, run, z)`;
  - re-issue the walk when it stops, throttled;
  - accept arrival a little short of the point.
  It needs an AI with no leash and no wander, so nothing sends it home.
- **There is no threat table.** A monster turns on whoever hits it only as its AI's
  "damaged" pattern says (a chance per hit), so design around it:
  - give the ally HP enough to take a wave;
  - make killing the waves the challenge;
  - give the attackers a "turn on the attacker" chance, so players can pull them.
- **It speaks with `Send_gsv_CHAT`**: a bubble over its head, which the client wraps and
  times. The server's `Say_MESSAGE` does nothing. Whisper the roster the lines that
  matter.
- **It cannot dodge.** It stands in every area attack.

## 4. The dialog NPC

- **Place it in the zone's IFO**, in the MOB lump, with its `.CON` name (`rosefmt.ifo`).
  The `.CON` needs a LIST_EVENT row naming it.
- **Write the dialog with `quest-editor con-toll`**:
  - the offer fires the trigger when its conditions pass, and a "lack" answer shows
    otherwise;
  - `--open-when N` shows a "closed" answer unless the NPC's event value is N, for
    "busy" or "not now".
- **Its event value 0** is `CObjNPC::Set_ObjVAR(0, v)`, which sends
  `GSV_SET_EVENT_STATUS` on a change. The trigger reads it with COND_011, and so does
  the dialog: the client's COND_011 always passes, so the dialog checks the value itself.
- **Hide it while it is busy**, `CObjNPC::VSet_SHOW(0)`, and show it again with
  `VSet_SHOW(1)`. Each sends only on a change.

## 5. The "!" over the NPC, and a timer in the journal

- **The client's quest icons only count a dialog trigger that registers a quest**
  (REWD_000 op 1; `src/client/event/CLAUDE.md`). A trigger that just starts an event
  shows nothing. So make the event a quest:
  - a LIST_QUEST row and its `LIST_QUEST_S.STL` key (name, description, start
    message, end message);
  - col 1 is a time limit in 10 s units (90 = 15 min), which the journal counts down;
  - check with `quest-editor con-triggers data <npc row>`.
- **The server can fire a trigger for a player**: `classUSER::Do_QuestTRIGGER(hash)`
  applies it, and `GSV_QUEST_REPLY` makes the client run the same trigger on its copy of
  the log. Use it to:
  - give the quest to party members who did not talk to the NPC;
  - take it from everyone at the end: COND_000, which also selects the quest, then
    REWD_000 op 0.

  It clears the player's last-talked NPC: save `m_iLastEventNpcIDX` and put it back.
- **Sweep stale quests**: anyone in the zone holding the quest outside a run (a start
  the event refused, a logout mid-run) gets the end trigger.

## 6. Rewards a player notices

- **Send them as quest rewards**: they print as purple lines, where a plain money
  update is a system line nobody notices.
  - `Add_MoneyNSend(n, GSV_REWARD_MONEY)` for zuly;
  - `Add_ItemNSend(item)` for an item: `GSV_REWARD_ITEM`, or the item at the player's
    feet when the bag is full.
  - EXP only has the usual system line.
- **Item numbers above 999 need `tagITEM::Init(type, no)`** with two unsigned
  arguments: the packed `type * 1000 + no` cannot hold them.

## 7. Placing things on the map

- **World cm = IFO position + 520000.** A chunk is 160 m. A point's chunk is
  `floor(x / 16000)`, `floor(y / 16000)`, and its IFO is named `<x>_<64 - y>.IFO`.
- **GM `/tp <zone> <x> <y>` takes 10 m units**: (5544, 5373) m is `/tp 26 554 537`.
- **The walk grid** (`<zone>/<FOLDER>.lnav`, `rosefmt.lnav`) has 1 m cells:
  - `simple` = walkable class, `complex` = has object floors;
  - search it for walkable legs between points and for clear spots;
  - its tile `heights` (2.5 m vertices, row 0 south) give the ground height for a placement.
- **Keep a route clear of wild camps**: list the zone's regen points (`rosefmt.ifo`,
  REGEN lump, range in metres). The escort route keeps 144 m and more from any.
- **IFO floats are 32-bit**: round a record you build through `struct` `f` before
  comparing it with one read back.

## 8. A price, a clock and a record (the Karkia catacombs)

From `src/sho_gameserver/src/catacombs.*` and `scripts/import-catacombs.py`:

- **A price with a rule the QSD cannot express is taken by the controller.** A QSD
  reward runs before any hook, so "once a day" could only refuse after the items were
  gone. The trigger keeps its conditions and takes nothing; the hook checks the rule,
  then takes the items across stacks with `classUSER::Sub_ItemNSend(slot, item)`, which
  also tells the client what the slot holds now. A trigger with conditions and no
  rewards still counts as succeeded, so the hook runs.
- **Check where the click came from.** A trigger name can be sent by a client from
  anywhere: the hook tests the player's zone and distance to the NPC.
- **A schedule is a function of the wall clock, not a latch.** Work the state out from
  the time at every tick. A server started mid-window then finds the right state, and
  GM test commands only add a skew to that clock, so they run the real code.
- **A record that must survive a restart is written at once**, not at the character's
  next save: `GS_CThreadSQL::Add_CatacombSave` queues it and wakes the SQL thread. It
  is read at login by its **own** query, so a database without the table still logs
  characters in. The controller keeps its own map by character id; no member was
  added to `classUSER`.
- **An IFO chunk may have no NPC lump at all** (the Desolate Cemetery had no NPC):
  add the region and its table entry (`rosefmt.container.Region`), then re-bake the
  zone's walk grid, since an NPC is a bake input.
- **Check a placement against the walk grid's `simple` cells**, not only the terrain
  height: a spot beside a building can lie inside its collision footprint.

- **A deadline players should see is a zone timer**: `pZone->SetZoneTimer(id, label,
  seconds)` (0 removes it). The clients count down by themselves and an arrival gets what
  is left, so set it once and again only when the end moves (compare with
  `ZoneTimerLeft(id)`). UI2 draws it; the classic interface needs an announcement too.

## 9. Deploying

- **Server-read data** (LIST_NPC, LIST_NPC.CHR, LIST_SKILL, FILE_AI, the AIs, QSDs,
  IFOs, clips) **needs a server restart**. The game server's exe is locked while it
  runs, so copy `bin/release/sho_gameserver.exe` (and its `.pdb`) to `Exes/` once it
  is stopped.
- **Client data needs a bake**: `scripts/pack.ps1`, then copy `data.idx` and every
  `rose*.vfs` to the launch folder.
  - `rosenext.exe` and `znzin.dll` go together.
  - `data.idx` can come out byte-identical when no file changed size, a cell edit
    from 733 to 550 for instance. Check the archive's content (`rosefmt.idx`, then
    read the file at its offset), not the index's hash.
