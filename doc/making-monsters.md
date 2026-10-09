# Making a new monster

A checklist and a list of traps, gathered while building the Cerberus Lair
(Cerberus, Hellhounds, Hellhound Whelps, the Warden of the Seal), Hebarn, the Cave
of Ulverick and the Oro/Karkia imports. Every trap here cost a test round at least
once. `scripts/import-cerberus.py` is the worked example: most rules below have a
named constant or function there.

`data/` is not in git, so **the import script is the only record** of a monster.
Write the row, the model, the AI and the skills from a script, never by hand, and put
the reasoning in its docstring.

## 1. The row (LIST_NPC.STB)

- **Copy a template row of ours** with the right model family, then overwrite what
  you want (`extra_row()`). Clear what belongs to the template: target/event cols 25
  and 41, the shop tabs, give it its own STL key (`LIST_NPC_S.STL`) and the default
  PvP state. A row must also get a `LIST_NPC.CHR` entry (copy the template's).
- **Max HP is `level x HP column`** (col 8), not the column itself.
- **Size (col 4) is a percentage scale of the model** (800 = 8x). Two things do not
  follow it and must be scaled by hand:
  - **Col 42, the name/HP-bar height**, is absolute cm. The client takes the higher of
    it and the model's measured bind-pose height. Copied at another size it puts the
    label inside the body (Warden: the Lunar Keeper's 370 at twice its size) or high
    above it (Hellhound). Scale it by `size / template size`; floating models need it,
    since their bind pose sits below where they hover.
  - **Area skill radii.** A type 17 burst is measured from the caster's centre with no
    allowance for its body: Infernal Leap's 5.5 m missed everyone standing at an 8x
    Cerberus's heads. Size the radius for the model.
- **Col 27 is the mark beside the focused name**, used directly as a sprite index:
  1 basic, 2 guard, ... 8/9/10 bronze/silver/gold star, 11 crown (king), 16 skull
  (unique boss). Policy and table: `scripts/fix-monster-marks.py`.
- **Col 39 is a glow** around the whole model (`RRRGGGBBB`, decimal).
- **Monsters in levels 60-199 go into `scripts/balance-trend-exclude.py`**, or every
  balance pass that fits a trend over that window drifts.
- **EXP is only seeded**: `rebalance-exp-rewards.py` owns the column (its
  `--restore`, then a plain run, prices new rows without touching others).
- **Level and drops**: `Get_DropITEM` returns nothing once the killer is 10+ levels
  above the monster, and an item number above 999 cannot drop at all.

## 2. Weapons, hits and projectiles

- A monster with a **weapon row** (col 5) presents its hits through that row (LIST_WEAPON
  39 hit effect, 40/42 sounds, 38 the projectile). A blank weapon row is worse than
  none: no impact, no sound. Without a weapon, `NPC_HAND_HIT_EFFECT` (col 33) is the
  impact; 403 is the retail default.
- The **left hand (col 6) indexes `LIST_SUBWPN.ZSC`**, not the weapon list
  (`fix-npc-hand-models.py --verify`).
- Normal attack clips present on **frame 21 (melee) or 22/23 (bow/gun)**. A bare-handed
  monster on a 22/23 clip has no projectile to fire (`fix-zmo-attack-frames.py`).

## 3. Animations and skills (the part that breaks quietly)

- **A cast uses two CHR slots**: the AI's `nMotion` is the casting clip, `nMotion + 1`
  the release. Only the **release clip's event frames** make the skill land on screen:
  24/34 launch a projectile, 26 fires, 25 hits; a payload (area hit, status) drains on
  24/34/25/35/26/10/20/56/66. A release clip with none of them lands the damage with no
  animation. `audit-ai-skill-refs.py` warns about every such cast; read its output.
- **Casting effects (LIST_SKILL cols 56-67: three columns per effect, effect / dummy
  point / sound) play only on clip event frames 44, 64, 74, 84** (casting effects 0-3).
  Most monster clips carry none, so the effects you wrote never show, and a spell that
  hits nobody looks like nothing happened. The fix is the monster's own copies of the
  two clips with those events added (`SEAL_CLIPS` / `zmo_with_events()` in
  `import-cerberus.py`); the server ignores the events, it only counts attack frames.
  Inspect a clip's events with `audit-ai-skill-refs.zmo_events()`.
- **The hit effect (col 74) plays on targets that took a result**, so it too is
  invisible when nobody is hit.
- **Skill effect columns index FILE_EFFECT directly.**
- **Where a breath or bolt starts is a dummy point** on the skeleton (cols 57/72/75,
  999 = the model's root). If the model has no good point (Cerberus fired from its
  collar), add one to a copy of the skeleton that only this monster's CHR entries
  name (`SKEL_REL` + `p_mouth`).
- **Cast speed**: cols 53 (cast) and 69 (release) scale the clips for monster casts on
  both sides. A status shows 1-3 s after the server applies it (cast clip + release
  frame): size stun and slow durations for that.

## 4. Skill rows (LIST_SKILL.STB)

- Copy one of ours of the same **type** and overwrite (`leap_row()`, `seal_row()`).
  Type 17 hits around the caster. Type 6 is a projectile that damages its target
  only, but applies its status to everyone within `SKILL_SCOPE` of the target.
- **Never copy `SKILL_POWER` from another dump**: their scale one-shots players here.
  Our monster attack skills run 25-230; measure on a real character.
- **The status goes in col 11** (with success col 13, duration col 14). RoseZA/667
  author it in cols 88/90, which our table never reads. A slow also needs
  `AT_SPEED` (23) in col 21 and the percentage in col 23.
- **The level gate applies to monsters too**: a monster's magic hits a player for 0
  once the player is far enough above it (`lv + 30 - target lv`), and a harmful status
  is resisted on `success x (lv x 2 + INT + 20) / (RES x 0.6 + 5 + AVOID)`. **A
  level-240 test character takes 0 and resists almost everything from a level-150
  monster.** For type 17 the server then sends no hit packet at all, so nothing shows
  on the target. Judge damage and statuses with a character of the intended level.

## 5. The AI (.AIP)

Write it from code with the helpers in `scripts/import-hebarn.py` (`heb.pat`, `heb.ev`,
`c_*`, `a_*`), as `build_warden_ai()` does. Register it in `FILE_AI.STB` and in the
row's col 16.

- **Patterns run at different times.** "attack move" runs only while the monster
  **chases** (never while it stands and swings); "damaged" runs on a header-set share of
  the hits it takes. **A spawn by `RegenCharacter` (regen points, the Cerberus
  controller) never runs "created"**: put the idle search in "stop".
- **Events are first-match-wins.** A 100% event starves everything after it, and two
  events that can fire close together can stack: the Warden's random burst at 67% HP
  followed by its 66% burst mid-wind-up gave two shouts for one spell. Space related
  casts with a hit counter (monster variable + `SEAL_GAP`).
- **Distances in conditions and actions are metres** (the loader multiplies by 100).
- **Gate every cast on the target already being in range** (`c_target_within`). A
  cast ordered from further away makes the server chase first, and the client
  abandons a remote cast that has not started within 5 s.
- **A cast waits for the swing in progress**: expect ~1-2 s between the order (and
  any shout attached to it) and the cast animation starting.
- **AI speech (action 28) is server-driven** (2026-10-09): the overhead bubble is local
  chat the server sends; kind 1 also shouts to the zone, kind 2 announces. The client
  runs its own copy of the AI on every hit it shows, but its conditions past 26
  (monster variables included) are stubs that return true and its dice are not the
  server's. Never rely on the client's copy for anything a player sees.
- **Action 02 is the other speech action: client-only chatter.** The server ignores it;
  each client says the line from its own copy of the AI, so players see different
  lines at different moments (the Jelly Beans' "appears / hit / dies" lines). Use it
  for mood lines with no mechanic behind them: it costs the server nothing, and it is
  the only way a line fires when a regen spawn appears, since the server never runs
  "created" for those. A line that warns about a mechanic must be action 28.
- **Condition 17 ("select NPC N") is a server-wide lookup**: an AI copied from an
  instance can reach its controller NPC anywhere on the server.
- **Every monster or skill id an AI names must exist here.** A `Change_CHAR` (action 9)
  to a blank row crashed the client; a cast of a blank skill played a cast motion for
  nothing. Run `audit-ai-monster-refs.py` and `audit-ai-skill-refs.py` after writing AI.
- Call-for-help (action 18) wakes only same-id monsters with no target, within the
  given metres.

## 6. Spawning

- **The client puts a new monster on the highest surface at its spot**
  (`GetHeightTop`); the server sends no height. Spawned under rock arches or a roof, it
  stands on the roof: heard, never seen. AI summons land where the summoner stands, so
  summon from code at an open-sky point instead (the Cerberus Hellhounds).
- **A zone with no `.MOV` blocks every cell**, which silently kills leashing, wandering
  and fleeing while chasing still works.
- Regen points are an escalation state machine, not a list: see CLAUDE.md, "Monster
  Spawning: A Regen Point's Cap Is Not A Cap". Never derive a population from the caps.
- A spawn point must be on walkable ground near the surface; check it against the map
  (the `.MOV` grid and the heightfield, `scripts/mapgen`) before placing it.

## 7. Deploying and testing

- **Server-read data needs a server restart**: LIST_NPC.STB, **LIST_NPC.CHR** (the server
  loads it too), LIST_SKILL.STB, FILE_AI.STB, AI_s.STB, the `.AIP` files, ITEM_DROP.STB.
- **Client data needs a bake** (`scripts/pack.ps1`) and a client restart: STBs, STLs,
  CHR, `.ZMO`, `.ZMD`, effects, textures. A client started before the bake shows the
  old data (the "invisible breath" of the first Cerberus test).
- Verify with the import script's `--verify`, then the two AI audits.
- To read a fight: the client log's `CombatTrace` lines name every cast
  (`GSV_TARGET_SKILL` = ordered, `GSV_SKILL_START` = animation started, `skill start
  consumed by action` = release frame reached, `GSV_DAMAGE_OF_SKILL` = a result).
  The client log is `client-<date>.log` in the game folder; the server's is
  `Exes/log/gameserver-<date>.log`.
