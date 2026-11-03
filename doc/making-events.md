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

## 8. Deploying

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
