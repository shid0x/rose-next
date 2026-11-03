# Wisdom Tree escort: implementation brief

Status (2026-11-03): **validated in game and committed (0a84cd82).** Since then,
built and not yet tested:
- the Blightchief at 3/4 of the Grunter King's HP, 33,000;
- the real reward (section 5).

Second test: "it's better". The second round:
- **The keeper at the tree holds his bow**, as on the round (LIST_NPC 1132 col 5).
- **Speech bubbles last 3 s for everyone**, timed in milliseconds (`CHAT_DISPLAY_MS`):
  a player's bubble counted frames, about 1.5 s at today's frame rates. An NPC line too
  long to read in 3 s stays longer, about 50 ms a character, up to 10 s.
- **The two bosses got an attack to dodge**, drawn on the ground while it is cast:
  - **Withered Guardian**, Rootquake: a 9 m circle around it with a 40% slow;
    about 2 s to step out.
  - **Grunter Blightchief**, Blight Cleaver: the Grunter King's own axe skill as
    three 15 x 2 m lines, 30 degrees apart. Stand between them, or behind him.
    This is a new shape, the fan (LIST_SKILL col 84, `SKILL_FAN`).
  - Each casts its attack every 10 hits taken. Sim, typical level-55 builds: a hit
    takes 30-45% of a player's HP.
  - Importer docstring: "The two bosses' attacks".

The first test's feedback, and what changed:
- **The route looks strange.** The user will send better points; until then the
  route below stands.
- **He was slow between stops.** He runs now: LIST_NPC run speed 450, about 70% of
  a player's pace. He walked at 200.
- **Silence between stops.** Every leg has two lines, said at a third and at two
  thirds of the way (`Stop::walk`).
- **The bubble cut his lines.** The player bubble wraps at 28 characters, often
  mid-word, and draws two lines at most. NPC and monster speech now gets its own
  bubble (`CChatBox::DrawSpeech`):
  - word-wrapped to 300 px, up to 6 lines, sized to the text;
  - nine slices of the same texture;
  - shown 4.5-12 s depending on the line's length, where the player bubble counts
    frames.
  Player bubbles are unchanged.
- **No "!" over him.** The client's quest icons only count a dialog trigger that
  registers a quest. The run is now quest 5521, "The Great Tree's Rounds":
  - the start trigger registers it; `WisdomTree-Join` gives it to the party members
    on the roster; `WisdomTree-End` takes it from everyone at the end;
  - a sweep every 2 s takes it from anyone in the forest holding it outside a run;
  - its timer is 15 minutes, the run's limit;
  - `quest-editor con-triggers data 1132` lists it as an accept.
- **The zuly reward went unnoticed.** It is sent as `GSV_REWARD_MONEY` now, which
  the client prints as a quest reward (purple). The EXP still shows as the usual
  system line.
- **A bow animation with no bow.** He carries the Elf Bow (LIST_WEAPON 206) on the
  right-hand dummy, as the forest's Ranger Clowns do. His arrows fly, and he
  shoots from 10 m.

To test: copy `bin/release/sho_gameserver.exe` to `Exes/` and restart the servers
(they read LIST_NPC, LIST_NPC.CHR, FILE_AI, QP401.QSD and the IFO from `data/`). Then
bake the VFS: the client needs the same tables plus `EM26-002.con` and
`ulngtb_con.ltb`. The client exe is unchanged.
- Through the dialog: [Tree Keeper] Faren stands south of the great tree,
  `/tp 26 554 537`.
- As a GM: `/escort start` (you and your party, anywhere in the forest),
  `/escort status`, `/escort skip` (kill the current wave, or end the current
  walk where he stands), `/escort stop`.
- The server log lines start with `[escort]`.

To check in game:
- The town keeper hides while the run is out (`GSV_SET_NPC_SHOW`).
- The escort shows as friendly.
- His attack clip looks right: it is a town NPC's clip, never used in a fight
  before (see the importer's docstring).
- Waves come at him.
- Pulling a monster off him by hitting it works (40% per hit).
- The pace.

First escort quest: a party follows an NPC around the great tree in the Forest of
Wisdom, the NPC fights beside them, and monsters ambush it at some of its stops.
Decided with the user:

- **The live Forest of Wisdom** (zone 26, map `JG04`), not a private copy: no zone
  work; other players can see the run and help.
- **On demand**: a party starts a run by talking to the NPC; one run at a time.
- **It fails** if the NPC dies or the time limit runs out.
- **No aggro system.** Players pull a monster off the NPC through the ambushers' own
  AI files (a "when hit" pattern that switches to the attacker part of the time);
  the rest is design: the NPC has the HP to take a wave, and the challenge is
  killing waves fast.

## 1. The place

The tree is `intellitree` (LIST_DECO_JG object 151, scale 2.8) at **(5546, 5416) m**,
chunk `34_31`. It stands in a clearing ringed by trees and a cliff line, roughly
x 5450-5680, y 5300-5545. Nothing spawns inside it: the nearest monster camp edge
is 144 m from the route (Grunters and Smoulies, lv 42-46, south-west of the
clearing). The way in is the opening around (5390, 5310).

[Mountain Guide] Kay (NPC 1131, at (5669, 5105)) was the first idea for the NPC. He
is not usable: a cliff separates his spot from the clearing, and the only walkable
way between them is 580 m through two Grunter camps.

**The route** (world m; checked on `JG04.lnav`: every leg walkable, about 650 m
on the grid, about 500 m in straight lines):

| # | Spot | Point | Stop | Wave comes from |
|---|---|---|---|---|
| 0 | the keeper's post, south of the trunk | (5544, 5373) | start | |
| 1 | the east roots | (5638, 5395) | ambush 1 | (5660, 5388) |
| 2 | the north-east edge | (5625, 5490) | a line, no fight | |
| 3 | the north side | (5539, 5525) | ambush 2 | (5533, 5550) |
| 4 | the west side | (5490, 5450) | ambush 3 | (5467, 5463) |
| 5 | south-west, the pond | (5505, 5362) | a line, no fight | |
| 6 | back at the post | (5544, 5373) | the final wave, then success | (5546, 5346) |

The stops sit on cells clear 3 m around. The wave points are 25-30 m out, away
from the tree, with a straight walkable line to their stop. These are first
guesses from the walk grid, to be tuned in game. Several legs bend around trees,
which the server's path follower handles.

Waves (`WAVES` in the controller; "+1" is per roster member past the first):

1. 3 Blight Grunters (+1).
2. 2 Blight Smoulies (+1) and a Blight Grunter.
3. A Withered Guardian and 2 Blight Grunters (+1).
4. The Grunter Blightchief, 2 Blight Grunters (+1) and 2 Blight Smoulies.

## 2. From the player's side

- A new NPC, **[Tree Keeper] Faren** (a placeholder name; the Ranger Paul's look),
  stands at the post. Talking to him offers to help check on the tree. The party
  leader (or a solo player)
  starts the run; every party member in zone 26 within 50 m of the keeper joins
  the roster.
- If a run is already going, the dialog says so ("I am out on my rounds; come back
  later"): the keeper's event value is 1 while busy.
- The town keeper disappears and a fighting keeper, the escort, starts walking the
  route at walking speed. He:
  - stops to fight while anything attacks him or is close, then carries on;
  - waits at each stop for a few seconds and says a line about the tree;
  - waits when no roster member is within 20 m ("Stay close!").
- At the ambush stops, a wave spawns 20-30 m away and goes for the keeper. Wave
  size grows with the number of players on the roster.
- **Success**: the final wave at the post dies. The keeper thanks the party; every
  roster member within 50 m gets the reward; the town keeper reappears.
- **Failure**: the keeper dies, 15 minutes pass, or no roster member comes within
  40 m of him for 60 s. The wave monsters die without killers (no EXP or drops),
  the town keeper reappears.

## 3. The server (C++)

**The controller**, `src/sho_gameserver/src/wisdom_escort.*`, has the same shape as
`cerberus_lair.*`:

- `ProcZone` runs in zone 26's thread only and owns the run: the escort's
  movement, waves, success, failure, cleanup. A 250 ms tick.
- `OnStart` runs in the player's thread after the start trigger succeeds
  (`classUSER::Do_QuestTRIGGER`, next to the Cerberus hook). It only records a
  request under the mutex; the zone thread picks it up.
- GM `/escort start|status|stop|skip`, for testing without the dialog.

**The escort is an ordinary `CObjMOB` on the players' team** (`TEAMNO_USER`):
- Friendly to every player, hostile to every monster (`CAI_OBJ::Is_ALLIED`, teams
  multiply, <= 100 is allied).
- Monsters' "enemy near" checks find it like any character (`F_AICOND_02`).
- The client gets the team in the add packet and treats the escort as friendly,
  as it does summons.
- It fights through its own AI file.
- No new class: the controller walks it as `CObjSUMMON::Proc` walks a summon. When
  the escort has no target and is stopped or moving, the controller sends it to
  the current waypoint (`Nav_MoveOrStraight`, walking). Its AI file has no leash
  action, so nothing sends it home. Because there is no new class, there is no
  class-layout change and no clean rebuild.
- It is tracked by object index plus NPC row, checked every tick (indices are
  reused).

**Waves** come from `RegenCharacter(..., TEAMNO_MOB, true)`. On the next tick,
each new wave monster is ordered at the escort with `SetCMD_RUNnATTACK`, the
summon attack path. They are tracked the same way as the escort.

**The town keeper** is an IFO NPC (MOB lump of `34_31.IFO`). It is hidden with
`CObjNPC::VSet_SHOW(0)` for the run and shown again after. This is the retail
`GSV_SET_NPC_SHOW` path, which needs checking in game.

## 4. The data (one importer, `scripts/import-wisdom-escort.py`)

`data/` is not in git: the script is the record.

- LIST_NPC rows, each with a CHR entry and a name STL key:
  - the town keeper (dialog NPC);
  - the escort keeper: the same model, which needs walk, run, attack, hit and die
    motions; Kay's robber-skeleton family has them;
  - three ambusher rows cloned from the forest's Grunters, Smoulies and Guardian
    Tree, plus a final-wave leader.
- AI files:
  - the escort: attacks any non-allied character within 12 m, attacks whoever
    hits him, no leash, no wander;
  - the ambushers: attack the nearest enemy, and when hit switch to the attacker
    about 40% of the time (the "pull" from the design).
- The start trigger in a QSD: the keeper's event value 0, level >= 40.
- The keeper's dialog: the start option and a "busy" answer while the event value
  is 1 (`quest-editor`, as the Cerberus gatekeeper's toll dialog).
- The town keeper placed in `34_31.IFO`.

Ambush monsters are new rows on purpose. AI is per row, so changing the forest's own
Grunter AI would change every Grunter in the forest.

## 5. Open points

- The keeper's name, look and lines. His overhead lines are monster speech, so the
  client draws the bubble in red (`Recv_gsv_CHAT`); the lines that matter also go to
  the roster as whispers.
- The reward is the user's (2026-11-03): 50,000 zuly and the Insect Wings (Flame),
  LIST_BACK 1026. Both are given as quest rewards (purple lines); a full bag gets
  the wings at the player's feet. There is no EXP reward. It is once an hour per
  player, kept in memory (a restart forgets it), so the wings can be had every hour:
  open whether they should be once per character.
- A level cap: high-level players can help, but should they earn the reward?
- Later: an objective line in UI2 ("Stop 3/6"), and the keeper's HP shown with the
  party.
