# Making a new monster

A checklist and a list of traps, gathered while building the Cerberus Lair
(Cerberus, Hellhounds, Hellhound Whelps, the Warden of the Seal), Hebarn, the Cave
of Ulverick, the Oro/Karkia imports and the Wisdom Tree escort. Every trap here cost
a test round at least once. `scripts/import-cerberus.py` is the worked example: most
rules below have a named constant or function there. Section 6 is a recipe for a boss
attack players dodge.

Importing a monster from another dump? The order of work is in
[importing-monsters.md](importing-monsters.md); this file is the traps to check along the way.
Building the event around it (a controller, an ally NPC, a quest, rewards)? See
[making-events.md](making-events.md).

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
- **The right-hand weapon hangs on the skeleton's dummy `p_00`**, town NPCs included
  (a dialog NPC shows its col 5 weapon too). A clip that shoots on 22 wants a bow
  there: the forest's Ranger Clowns carry theirs in col 5, and so does the Tree Keeper
  (`import-wisdom-escort.py`). A weapon with a bullet (LIST_WEAPON col 38) makes the
  attack a projectile on both sides, landing on impact.

## 3. Animations and skills (the part that breaks quietly)

- **CHR slots are `MOB_ANI_*`** (`datatype.h`): 0 stop, 1 walk, 2 attack, 3 hit, 4 die,
  5 run, 6/7 the first cast and its release, 8/9 the second. **A town NPC's entry often
  fills 1-4 with idle clips** (a crying clip as its walk): read the slots
  (`rosefmt.chr`, `rosefmt.zmo.events`) before giving a town NPC's look a fight. A
  mesh can borrow another entry's motions when the skeleton is the same (the Tree
  Keeper: Ranger Paul's mesh, Kay's robber motions).
- **Which skill a monster casts, on which slots**: `combat-sim.py ai --mob <row>`
  prints its AI file as rules. The Grunter King's axe (Strike 2988) is on slots 8/9.
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
- **Every effect a skill attaches to a character must be one-shot.** Casting effects
  (cols 56-67), the hit effect (74) and the hit-dummy effects (77/80) are attached to the
  caster or target and removed only when the effect finishes (`CEffect::IsFinish`); a PTL
  sequence with `loops 0` never does, so it stays on the character for good. The
  Necromancer's first cut used `scavenger_dark01` (a monster aura) as a hit dummy and left
  a dark aura under the player's feet. Check every sequence's `loops` with `rosefmt.ptl`
  before picking an effect (`import-necromancer.py`'s docstring).
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
- **The server multiplies a skill's damage by the release clip's attack frames.** The
  Grunter King's axe clip swings twice (hits on 16 and 38), so as a release it hits
  twice as hard. To keep one hit, clear the second swing's events in the monster's own
  copy; the second swing still plays, as animation only.
- **The server lands a skill when its release clip starts**, not on the clip's hit
  frame: Do_SKILL sets the motion and runs Skill_START at once. A release whose hit
  comes late puts the damage on screen before the blow. Section 6 has the fix.

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
  abandons a remote cast that has not started within 3 s.
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
  nothing. Run `audit-ai-monster-refs.py` and `audit-ai-skill-refs.py` after writing AI,
  **with `--dry-run` first: without a flag they fix what they flag**. The skill audit
  compares an imported AI file (`kh_`, `kak_`, `ks_`, `or_`... prefixes) against its source
  dump and strips a cast whose skill is a different type there -- so a new row of ours at
  an id the dump has blank reads as a MISMATCH and its casts are deleted (the Quarantine
  Officers' Tornado, 2026-11-01). Add such rows to the audit's `DELIBERATE` before running it.
- Call-for-help (action 18) wakes only same-id monsters with no target, within the
  given metres.

## 6. An attack to dodge (a telegraphed cast)

A boss attack drawn on the ground while it winds up, which a player who moves in time
avoids. Worked examples:
- the Warden's Seal Burst and Cerberus's Hellfire Breath (`import-cerberus.py`);
- the Wisdom Tree escort's Rootquake and Blight Cleaver (`import-wisdom-escort.py`,
  `SKILLS`, `build_clips`, `boss_ai`).

**1. Pick the shape.** The client draws exactly what the server measures
(`CObjCHAR::Skill_DamageToAROUND`, `src/client/groundtelegraph.h`). Copy the row named
here, then overwrite:

| Shape | Type (col 5) | SKILL_SCOPE (col 8) | Copy | In use |
|---|---|---|---|---|
| circle round the caster | 17 | radius, cm | Seal Burst 7023 (40% slow) | Rootquake 7203 |
| strip ahead | 7 | length x 10000 + width, cm | Charge 3603 | Terrasaurus Charge |
| cone ahead | 7 | 100000000 + length x 10000 + degrees | Hellfire Breath 7022 | Cerberus |
| fan of strips | 7 | a strip, and col 84 = lines x 1000 + degrees apart | Charge 3603 | Blight Cleaver 7204 |

The radius counts from the caster's centre: add the body of a big model. A type-7
shape is aimed where the target stood when the cast began and stays there, so a player
who moves once it shows gets out. Fan lines overlap near the caster: 2 m lines 30
degrees apart leave gaps from about 6 m out.

**2. Draw it and time it.**
- Col 83 = 1 puts the shape on the ground for the whole cast clip.
- The cast clip at col 53's speed is the dodge window; about 2 s reads well (31 frames
  at 50%, 12 frames at 20%).
- Col 6, the range, is how near the target must be for the cast to start without a
  chase first.

**3. Make the blow land with the damage.** The server lands the skill when the release
clip starts (section 3), so the release must start at the impact. If the model's skill
clip is one long swing, split it at the impact frame (`rosefmt.zmo.sliced`): the raise
becomes the cast clip, played slowly, and the rest becomes the release. Examples:
Cerberus's breath, the Blightchief's axe. Write both as the monster's own clips and
repoint its CHR slots (`CLIP_SLOTS` in `import-wisdom-escort.py`). In the release:
- **24 on the impact** (the area's payload);
- **74 on the impact** for casting effect 2 (cols 62-64), something on the ground:
  Earthsplitter's fissure 3003, the Warden's `shockwave_01` 400;
- **44 on the cast clip's first frame** for casting effect 0 (cols 56-58);
- clear any second swing's events (section 3, attack frames multiply damage).

**4. Size the damage by measuring it.**
- `combat-sim.py new --job 121 --level 55 -o b.json`, then
  `combat-sim.py run b.json --mob <row>`, lists every cast's damage on a typical build.
- A hit that takes 30-45% of a typical player's HP, against 5-10% for a normal swing,
  reads as "should have moved" without killing.
- The simulator does not know the fan; it measures one strip.

**5. Cast it from the AI.**
- Every N hits the monster takes: monster variable 2, counted by every "damaged"
  event, the pull included, or the count skips hits.
- Gate it on the target being inside the shape (`c_target_within`).
- Optionally also cast it in "attack move" on a target running off (a chance and
  `c_target_at_least`).
- `boss_ai()` in `import-wisdom-escort.py` does all three. Then run
  `audit-ai-skill-refs.py --dry-run`.

**6. Test it with a character within 30 levels of the monster.**
- Above that, magic hits for 0 and the status is resisted (section 4).
- An ally NPC cannot dodge: an escort takes every area attack, so give it the HP.

## 7. Spawning

- **The client puts a new monster on the highest surface at its spot**
  (`GetHeightTop`); the server sends no height. Spawned under rock arches or a roof, it
  stands on the roof: heard, never seen. AI summons land where the summoner stands, so
  summon from code at an open-sky point instead (the Cerberus Hellhounds).
- **Leashing, wandering and fleeing walk only where the zone's walk grid reaches**
  (`.lnav`, `scripts/nav-bake.py`; the `.MOV` files are retired). A zone not baked yet
  has no check at all. Chasing ignores the grid (`src/sho_gameserver/CLAUDE.md`,
  "Walkability").
- Regen points are an escalation state machine, not a list: see CLAUDE.md, "Monster
  Spawning: A Regen Point's Cap Is Not A Cap". Never derive a population from the caps.
- A spawn point must be on walkable ground near the surface; check it against the map
  (the walk grid, `rosefmt.lnav`, and the heightfield) before placing it.
  [making-events.md](making-events.md) section 7 has the arithmetic.

## 8. Deploying and testing

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
