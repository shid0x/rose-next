# CLAUDE.md — Rose Next Classic

## Project Overview

Rose Next Classic is a modernized ROSE Online private server + client built on the original iROSE C++ codebase. The server uses PostgreSQL (replacing MSSQL). The client is a Direct3D 9Ex (with plain D3D9 fallback) Win32 application. Everything is **32-bit x86 Windows**.

## Architecture

```
Client (C++/D3D9Ex) ←→  LoginServer (C++)  ←→  WorldServer (C++)  ←→  GameServer (C++)
                                    ↕                ↕                    ↕
                              PostgreSQL DB     Game Data (STB/STL)   Game Data
```

- **Client:** `src/client/` — Direct3D 9Ex (plain D3D9 fallback), Win32, packet-based networking
- **Servers:** `src/sho_loginserver/`, `src/sho_worldserver/`, `src/sho_gameserver/`
- **Shared C++:** `src/common/` (calculations, items, quests), `src/common-server/` (IOCP sockets, SQL threads)
- **Shared Rust:** `src/common-lib/` — FFI library (logger, config parsing, FlatBuffers codegen)
- **Engine:** `src/engine/` — 3D rendering, terrain, effects
- **UI framework:** `src/tgamectrl/` — custom GUI controls
- **Pipeline:** `src/pipeline/` (Rust) — asset baking tool

## Build System

Mixed Rust (i686-pc-windows-msvc) + C++ (VS2019, x86). Three-phase build:

```powershell
# Full build (recommended)
just build release    # or: scripts/build.ps1 -config release

# Manual steps:
# 1. Build thirdparty C++ deps
MSBuild.exe thirdparty.sln -p:Configuration=release;Platform=x86
# 2. Build Rust crates (MUST be i686)
cd src/ && cargo +stable-i686-pc-windows-msvc build --release
# 3. Build Rose Next C++ projects
MSBuild.exe rose-next.sln -p:Configuration=release;Platform=x86
```

### Key build facts
- Rust toolchain `stable-i686-pc-windows-msvc`: `scripts/build.ps1` passes it explicitly, but `common.vcxproj`'s pre-build `cargo build` relies on a local `rustup override set stable-i686-pc-windows-msvc` in `src/` (not checked in — set it on a fresh clone)
- Cargo target dir: `bin/` (set in `src/.cargo/config`)
- Output binaries: `bin/release/` — rosenext.exe, sho_gameserver.exe, sho_loginserver.exe, sho_worldserver.exe, pipeline.exe
- Thirdparty output: `bin/release/thirdparty/`
- `ntdll.lib` is a required linker dependency for client and all servers
- Build assets: `just build-assets release` or `scripts/build-assets.ps1`
- **`common` and `lib_util` used to compile with `/Od` in the *release* configuration** — an explicit `<Optimization>Disabled</Optimization>` in their release `ItemDefinitionGroup`, not an inherited default. Both are now `MaxSpeed`. **Check what a project actually compiles before estimating the blast radius.** `common.vcxproj` builds only 8 of the 21 `.cpp` files under `src/common/` — `io/stb.cpp`, `io/reader.cpp`, `log.cpp`, `network/network_util.cpp`, `util.cpp`, `uuid.cpp`, `sha256.cpp`, `goddess_effect.cpp`. `lib_util` adds `CStr`, `CRandom`, the socket layer, `PacketHEADER`, threads and hashing. `calculation.cpp` and the shared item/quest code are **not** in either — they are listed directly in `client.vcxproj` and `sho_gameserver.vcxproj`, so combat math was always optimized. The fix is real but it is the STB/string/socket layer, not the game logic. Measured on `item_drop.stb` (438 KB, 188,751 cells): parse **34 ms → 2 ms, 17x**, which took the character-select stall's table loading with it.
  - **A missing `<Optimization>` element does *not* mean `/Od`.** `triggervfs`, `tgamectrl` and `common-server` have no such element and inherit `/O2` from the MSBuild release defaults — they are fine. Only an *explicit* `Disabled` is a bug, so grepping for absent properties produces false positives. Confirm with `MSBuild ... -t:<project> -v:detailed` and read the actual `CL.exe` command line.
  - `sho_gameserver` is deliberately `MinSpace` (`/O1`), which is optimized, just for size rather than speed.
  - **MSVC's diagnostics here are localised (French).** Filtering build output for `": error"` silently matches nothing — the string is `": erreur"`. A build that failed can look like a build that passed.
- **D3D9 core headers come from the Windows 10 SDK**, not the vendored DX SDK — `d3d9.h`/`d3d9types.h`/`d3d9caps.h` were deleted from `thirdparty/directx9/include/` so the SDK copies (which have the 9Ex interfaces) win. MSVC searches every `/I` path before the system include dirs, so the vendored copies could not be beaten by ordering. The rest of the vendored SDK stays, because D3DX9 is not in the Windows SDK.
- **`d3dx9_43.dll` must ship with the client** — D3DX9 is linked against the June 2010 redistributable rather than the old static lib. `scripts/post-build.ps1` copies it into `bin/<config>` and `scripts/dist.ps1` bundles it; a hand-rolled deploy that forgets it produces a client that will not start.
- **RmlUi and FreeType are hand-written `.vcxproj` files in `thirdparty.sln`** (like every other dep here — the upstream projects are CMake-based and we do not use CMake). RmlUi needs `RMLUI_CUSTOM_RTTI` (client RTTI is off; the client defines it too, with `RMLUI_STATIC_LIB`, so the headers agree) and `RMLUI_FONT_ENGINE_FREETYPE` — without the latter RmlUi compiles its "no font engine" branch and `Rml::Initialise` fails at runtime. RmlUi also needs per-directory object output (`<ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>`): `Source/Core/Geometry.cpp` and `Source/Debugger/Geometry.cpp` share a basename and otherwise overwrite each other, failing the link with unresolved externals that look like missing sources.

## Project Structure

```
src/
├── client/              # Game client (D3D9Ex, Win32)
│   ├── network/         # Packet send/recv
│   ├── interface/       # UI dialogs
│   ├── gameproc/        # Game state processing
│   └── ai_lib/          # Client-side AI
├── sho_gameserver/      # Main game server (combat, zones, NPCs)
│   └── src/
│       ├── ai_lib/      # Server AI
│       ├── srv_common/  # Server combat/skill logic
│       └── common/      # Shared with worldserver
├── sho_loginserver/     # Authentication server
├── sho_worldserver/     # World/channel management
├── common/              # Shared C++ (client + server)
│   ├── shared/          # Items, quests, inventory, economy
│   └── include/rose/    # Shared headers (network/, common/, io/, util/)
├── common-server/       # Shared server (IOCP sockets, SQL)
├── common-lib/          # Rust FFI lib (logger, config, flatbuffers)
├── engine/              # 3D engine (D3D9Ex rendering)
├── tgamectrl/           # UI control framework
├── pipeline/            # Rust asset pipeline tool
├── launcher/            # ChronicRose.exe (players' launcher + patcher) and rose-patch.exe
│                       # (publish/serve the client as signed FastCDC chunks); see its README.md.
│                       # PRIVATE: gitignored, never pushed; its own cargo workspace (excluded
│                       # from src/Cargo.toml), built by scripts/build.ps1 only when present
├── tools/               # Standalone Rust dev tools (workspace members)
│   ├── vfs-browser/     # rose-vfs: TUI browser for data.idx / .vfs (ratatui + rfd)
│   ├── npc-shop-editor/ # egui editor for LIST_NPC/LIST_SELL shop tabs (COW on shared rows)
│   │                   # also builds gm-item-browser.exe: read-only tester catalog, /item + /mon clipboard;
│   │                   # flags broken LIST_NPC rows (server refusal, CHR/ZSC/mesh/texture/motion);
│   │                   # opens foreign dumps too: text codec picked per file (src/text.rs, roselib
│   │                   # is EUC-KR-only + rejects legacy I_NUM STLs; GBK vs Korean by lead byte),
│   │                   # China Rose's per-sex weapon ZSCs, CJK fonts from Windows; --check [--previews]
│   ├── quest-editor/    # CLI + egui wizard: Hunt/Fetch quests + NPC dialog givers (see its PROGRESS.md)
│   └── dev-studio/      # Python/PyQt6 (not a cargo member): Rose Dev Studio, GUI editor over
│                       # scripts/rosefmt -- STB + STL so far, plus the combat simulator
│                       # tab (Tools menu); `just studio`; see its README.md
├── rmlui/               # (in client/) RmlUi integration: D3D9 backend, system iface, panels
├── lib_util/            # C++ utility library
└── triggervfs/          # Virtual filesystem
data/                    # Game data files (STB tables, scripts, shaders)
database/                # PostgreSQL schema + migrations
thirdparty/              # C++ deps (lua, zlib, ogg/vorbis, imgui, flatbuffers, etc.)
scripts/                 # build/dev scripts, data import/repair/audit tools (Python), and
                         # scripts/rosefmt/: every file codec; scripts/mapgen/: the map generator (see scripts/CLAUDE.md)
 
```

## Database

- PostgreSQL 12+
- Connection configured in `server.toml` (see `doc/server.toml.example`)
- Migrations in `database/migrations/` — squash with `scripts/squash-migrations.ps1`
- Passwords: SHA256 + salt (generate with `scripts/generate-password.py`)

## Networking

- Custom packet-based protocol over TCP (IOCP on server side)
- FlatBuffers for some packet types (defined in `src/common-lib/packets/*.fbs`)
- C++ packet definitions in `src/common/net_prototype.h` (legacy ids and structs; server-to-server in `srv_packet.h`); the FlatBuffers packet wrapper is in `src/common/include/rose/network/packet.h`
- Client has separate socket connections: Login → World → Game server
- Nagle is disabled (`TCP_NODELAY`) on every accepted server socket (`CAcceptTHREAD`) and every client/server outgoing socket (`CRawSOCKET::Socket`): game traffic is small packets, and Nagle held each one behind the previous one's ACK (up to ~200 ms), bunching combat packets.

## Code Conventions

- C++ formatting: **do not run clang-format over the tree.** `.clang-format` /
  `.clang-format-ignore` are inherited from the original team and the checked-in code no
  longer matches what current clang-format produces (VS2019's 12.0.0 rewrites ~4x more
  lines than a typical change touches, burying real diffs in churn). Match the style of
  the surrounding code by hand instead. The `scripts/format_code.py` driver was removed
  for this reason; the config files are kept only so editors have something to read.
- Rust: standard `cargo fmt`
- Prefix conventions: `C` for classes (CItem, CObjCHAR), `m_` for members, `g_` for globals
- Server packet handlers: `Recv_cli_*` / `Send_gsv_*` naming
- Client packet handlers: `Recv_gsv_*` / `Send_cli_*` naming

## Dev Environment

```powershell
just dev-setup              # Creates dev/ symlinks for assets
just client release         # Run client
just server-all release     # Run all servers
just loginserver release    # Run individual server
```

- Client looks for assets in `dev/client/<config>/`
- Servers look for data in `data/` (configurable in server.toml)
- A/B test two client builds in the real game: `scripts/ab-build.ps1 stage <name>` / `use <name>` / `toggle`. `rosenext.exe` imports `znzin.dll` by name, so the exe+dll must be swapped as a **pair** — never rename one half. See the header for the HUD-labelling and vsync caveats.
- Auto-connect: `rosenext.exe --server 127.0.0.1 --username user --password pass --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character CharName`
- Players update through the launcher: `just patch-publish ["news"]` publishes `ROSEProject` as the
  next version, `just patch-serve` serves it (TCP 29080). Settings, the **secret signing key** and
  the store live in `Desktop\Rose\patch-server\` (outside the repo). `src/launcher/README.md` has
  the design and its traps (an exe named "*patch*" needs its `asInvoker` manifest or Windows
  demands elevation).

## Where The Detail Lives

Subsystem notes sit next to the code and load when a file in that folder is read;
zone histories are in `doc/zones/`. Each line's rule holds everywhere -- open the
file before working in that area.

- **Engine** -- `src/engine/CLAUDE.md`: D3D9Ex device (no `D3DPOOL_MANAGED`; the frame
  cap also lives in `zz_system::sleep` and INIT.LUA), cull bounds (ZSC boxes are wrong;
  use mesh bounds), depth precision (`r_d3d: depth-stencil format` in error.txt),
  lightmap atlas (keep `setMipmapLevel(3)`), transparency AA and two-pass alpha parts
  (the pale outline round mis-sorted foliage, `[VIDEO] FOLIAGE_TWO_PASS`), and "a missing asset logs
  once and degrades -- never fatal, never retried".
- **UI2 / RmlUi** -- `src/client/rmlui/CLAUDE.md`: every UI2 panel and window, traps
  and performance rules (`dp` not `px`; no nested auto-sized flex; a replaced classic
  dialog is never shown or hidden, and classic code checks it via `IT_MGR::IsDlgOpened`).
- **Client** -- `src/client/CLAUDE.md`: combat presentation, summon control, model nodes,
  dummy indices, device and screen modes, logging; its index points to `terrain/` (zone warp
  and terrain streaming), `interface/` (CNameBox, summon info, inspector, chat links, item
  preview, damage meter) and `event/` (NPC quest icons).
- **Game server** -- `src/sho_gameserver/CLAUDE.md`: combat flow, AI guards, summons,
  walkability (the walk grid; .MOV is retired), relay warps (`Recv_cli_RELAY_REPLY` honours only the destination
  the server sent), party size.
- **Game data scripts** -- `scripts/CLAUDE.md`: codecs (never write a new parser --
  `scripts/rosefmt`), importers, audits and repair tools, baking (`scripts/pack.ps1`;
  backups in `build/`, never a `.bak` under `data/`; the script docstring is the only
  record), the .vfs size limit, item import (`--art-only --template-row`), monster
  spawns (never sum `limitCNT`), missing NPC/skill rows and AI audits (run
  `audit-spawned-npcs.py` after any import), buffs (percent off the pre-buff stat),
  monster balance, the combat simulator (`scripts/combat-sim.py`), the weapon identity
  pass (server.toml's ATK/HIT/ASPD bonuses are 0 since 2026-10-15; the weapons carry them),
  and lessons from the zone imports. "I can't hit this monster" is
  usually the level gate in `Get_SuccessRATE` (`src/common/calculation.cpp`), not
  HIT/AVOID. Before adding a monster, read `doc/making-monsters.md` (section 6: an attack
  players dodge); to import one from another dump, follow `doc/importing-monsters.md`
  (start from `import-shadow-soldier.py`). To build a server-run event (an instance, an
  escort, an NPC who fights with players, a quest wrapper, rewards), read
  `doc/making-events.md`.
- **Rose Dev Studio** -- `src/tools/dev-studio/CLAUDE.md` (STL text is UTF-8; STB
  cells decode per cell).
- **Importing a map** -- `doc/importing-maps.md`: survey, rows, map folder, repairs, the
  walk grid (`nav-bake.py`), checks, test; China Rose's traps (its WARP/REGEN/morph ids
  name its own tables, a 128 px minimap, filler chunks) and `scripts/import-china-rose.py`.
- **Zones** -- `doc/zones/`: `oro.md` (never re-run `import-oro-667.py --stage 3` to
  change spawns -- it rewrites the Oro monster rows and undoes the balance, drop and
  mark passes; use `fix-oro-spawn-density.py`), `skaaj.md`, `shibuya.md` (`--stage 3`
  rebuilds the band's `.CON`s and drops their quest options, so always follow it with
  `--stage 7`), `ulverick.md`, `hebarn.md`, `cerberus-lair.md` (constants live in both
  the importer and `cerberus_lair.cpp`).

## Important Patterns

### Combat Damage Presentation
Combat damage is server-authoritative. The server calculates and applies HP once, then sends FlatBuffer `DamageEvent` / `CombatSwing` packets containing the final `damage_value`, `hp_after`, `presentation_kind`, and `lethal` checkpoint. Legacy `GSV_DAMAGE_OF_SKILL` packets also carry authoritative `m_iHP_AFTER`; the client must not synthesize combat skill checkpoints from visible HP. The client must not recalculate live combat damage.

The client queues `DamageEvent`s per defender in `CObjCHAR::m_CombatDamageQueue`. `CombatSwing` queues the event before starting the confirmed attack animation; `Hitted()` consumes exactly one matching event at the visual hit frame. Projectile damage is queued on receive and presented only on projectile impact. If hard control such as sleep/faint interrupts an attacker before its confirmed normal swing can spawn or reach its hit/projectile consumer, the client discards that exact queued event by `event_id`, folds the server-applied HP silently into the next real presentation, and immediately presents avatar death if the orphan was lethal. Direct HP stat packets (`UpdateStats.hp`, `GSV_SET_HPnMP`) update authoritative shadow HP; lower HP never silently moves the visible bar during combat.

**Mob-death item drops are announced by the sector broadcast, not by the damage packet.** The legacy `gsv_DAMAGE` / `gsv_DAMAGE_OF_SKILL` builders appended the drop inline as a `tag_DROPITEM` *and* called `CObjITEM::SetACTIVE()` as a side effect of building the packet. The FlatBuffer `DamageEvent` path carries no drop, so that activation has to be explicit — and it must happen **before** `CZoneTHREAD::Add_DIRECT`, because a field item is announced exactly once, at insertion: `AddObjectToSector` calls `Make_gsv_ADD_OBJECT`, which early-returns while `m_iRemainTIME <= 0`, and the entire nine-sector broadcast block is skipped. Activating afterwards produces an item that is live in the zone, holds a valid index, is pickable by position — and invisible to every client. Use `SetACTIVE()` rather than `bActive=true` on `InitItemOBJ`: only the former applies the longer party-ownership window. **A field item is also announced more than once, and the client must tolerate that** — at insertion, and again by the sector-visibility path when a player crosses into the sector it landed in (drop positions are randomised by +/-100 units, so a mob-death drop often lands one sector over). The server re-sends deliberately, calling `Update_OwnerObjIDX` first because ownership can change. `CObjectMANAGER::Add_GndITEM` does **not** dedupe: it allocates a fresh slot and `Set_EmptySlot` repoints `m_nServer2ClientOBJ` at the new object, orphaning the first — still drawn, unreachable by server index, so it can never be picked up or removed. The symptom is two identical items where one loots and one is permanent. `Recv_gsv_ADD_FIELDITEM` now refreshes the existing object instead; that is done in the handler rather than inside `Add_GndITEM` because the caller follows up with `InsertToScene()`, which calls `CreateAnimatable()` and re-links scene nodes and is not safe to run twice. `CObjectMANAGER::Set_ServerObjectIndex` had a guard for exactly this that never worked (it read `m_nServer2ClientOBJ[client_idx]` where it meant `[server_idx]`). That is fixed, and `Set_EmptySlot`, which writes the map directly without calling it, now warns by itself ("Server index N re-announced while client object M still holds it; old object orphaned"). It is a warning, not a dedupe, so both ground-item routes still need their own guard. **There are two client routes that build a ground item, and both need that guard.** A skill kill announces the same drop twice: `Apply_DAMAGE` activates and inserts it (sector broadcast → `Recv_gsv_ADD_FIELDITEM`), and the very same `CObjITEM` is then passed to `Send_gsv_DAMAGE_OF_SKILL`, which appends it inline as a `tag_DROPITEM` → `PushFieldItemToList` → `CObjCHAR::DropFieldItemFromList`. The broadcast wins the race (sent at insertion; the list drains only when the death animation presents), so the second route is the one that orphans. Auto-attack kills never take it — `Send_gsv_DAMAGE2Sector` has **no callers**, so the FlatBuffer path carries no inline drop — which is why this reproduces on a skill/summon class (Dealer) and not on a Soldier auto-attacking, and why fixing only the handler looked like a complete fix. The other three drop sites (`cobjnpc.cpp`, two in `gs_user.cpp`) already pass `bActive=true` and were never affected, which is why player- and AI-dropped items worked while mob kills dropped nothing.

A queued event that can never be presented must be removed, or reconciliation dies. `has_pending_damage()` gates **both** HP reconciliation paths, so a single event stranded in a defender's queue disables HP convergence for that character for the rest of its life — the bar then moves only by each hit's own digit and the client/server drift accumulates unbounded. Draining on the attacker's `Dead()` covers only attackers that die; an attacker that leaves your sectors (`Del_Object`), a zone change (`Clear`), or a second swing queued before the first was presented all strand events too. `CObjCHAR::Proc()` runs a timed orphaned-damage sweep as the generic safety net; it resolves silently into shadow HP and deliberately skips lethal events, which have their own death-presenting paths. See client `CLAUDE.md`.

HP convergence is hidden from floating digits. `DamageEvent.damage_value` is the displayed hit number, while folded reconciliation/checkpoint drift only affects visible HP and death state. Local-avatar dead reconciliation becomes pending authoritative death: outgoing attacks show MISS/no damage until the next incoming monster hit presents death, except mutual-death cases where the avatar dies immediately after the monster's normal lethal presentation. When the avatar kills its only/last attacker mid-swing, the drain-on-death path presents the mutual death immediately on the kill (no incoming hit is coming). A `CObjCHAR::Proc()` backstop forces the death after ~1.5 s if the avatar is left pending-dead with no presentation (no-attacker kills like DoT/fall damage), so the player is never stranded alive-client / dead-server and frozen. Remote/non-avatar lethal melee events also have a ~1.5 s spectator fallback: if the killer animation never consumes the queued death on this client, the defender still runs `Dead()` instead of remaining visually alive. The fallback defers (up to a 6 s hard cap) while the killer's confirmed swing for that exact event is still live — slow cart/castle-gear swings put the hit frame past 1.5 s, and popping early killed one-shot targets mid-swing. See client `CLAUDE.md` for details.

### Bone Particle Budget (Client/Engine)
Cosmetic character bone effects created by `CCharMODEL::CreateBoneEFFECT`, and the cosmetic effects an equipment model carries on its own ZSC dummy points (`CObjCHAR::New_ModelDummyEFFECT`: backs, wings, a custom weapon's moving pieces), are tracked separately by `CBoneEffectBudget` (`src/client/BoneEffectBudget.*`). This budget exists for passive bone-attached aura/loop effects only; skill, hit, projectile, terrain, weather, item-table weapon effects and normal world effects must not be registered there.

The manager turns off-screen bone effects Off and keeps every on-screen one at Full up to an 8,000-particle ceiling; past it, the farthest degrade through the particle-only tiers (`CEffect::SetParticleTier`: Full, Reduced, Minimal, Off). Mesh and sound components remain unchanged. Runtime caps and emit scaling are enforced in the engine particle emitter/sequence code, so degraded/off tiers are actually cheaper instead of only hidden. The old crowd limits (480 particles, 360 emit/sec, 3 full copies per aura) were dropped on 2026-10-22: with batching, 30 Frozen Thorns at full tier (~2,000 particles) measured 184 FPS with 0.3 ms `present` on the mid-range target (RTX 5060), which is the hardware bar -- low-end machines are not targeted. `[VIDEO] BONE_FX_BUDGET=0` lifts the ceiling for testing. The debug HUD exposes this as the `BoneFx:` line.

Additive-compatible bone particles also opt into safe texture batching through `CEffect::SetParticleBatchRenderHint`, forwarded to `zz_particle_emitter`. Only `CreateBoneEFFECT` and `New_ModelDummyEFFECT` enable this hint. The engine batches hinted additive sequences by texture/render state in `zz_particle_emitter::RenderParticleListWithBatching`; incompatible particles and all non-bone effects use the old render path. The debug HUD exposes batching as the `PartBatch:` line.

### Server Zone Architecture
GameServer manages zones via `gs_threadzone` — each zone runs in its own thread. Players are tracked per-sector for efficient broadcasting (`send_packet_nearby()` checks 9 adjacent sectors).

### Summon Control (CTRL+Click)
Players can directly command their summons with CTRL+click: on terrain → all summons move there; on a hostile monster → all summons attack it. Non-CTRL clicks are unchanged, and CTRL+click is only hijacked when the player actually owns a summon. The client (`jcommandstate.cpp` `TrySummonControlClick`, hooked into both single- and double-click paths) sends the shared `CLI_SUMMON_CONTROL` packet; the server (`classUSER::Recv_cli_SUMMON_CONTROL` → `CZoneTHREAD::CommandSummons_MoveTo`/`_Attack`) is authoritative. `CObjSUMMON` holds a ~5 s manual-order window that suppresses its follow-the-owner loop and AIP auto-aggro, then resumes normal following. Move orders use `CObjSUMMON::PlayerOrderMoveTo` (tolerant of non-walkable cells) — **not** `SetCMD_MOVE2D`, whose `IsMovablePOS` gate silently drops clicks. See the client and gameserver `CLAUDE.md` files for details.

### Summon Info Panel (Client)
`CSummonInfoPanel` (`src/client/interface/csummoninfopanel.*`) is a draggable on-screen overlay showing the player's active summons (name, ATK/DEF/LV/RES, HP). It draws directly with sprites/fonts (no XML resource) and is visible only while the player owns a summon. Key gotcha: a summon's stats are **scaled at creation** by summon-skill level + owner level (server `CObjSUMMON::SetCallerOBJ`), so the panel shows the scaled values, not the raw NPC table. The client recomputes the same formulas once at summon time in `recvpacket.cpp` and caches them in `SummonMobInfo` — if you touch the server scaling, update the client copy too. The underlying summon gauge (`CObjUSER::m_SummonedMobList`) is decremented at packet receive on lethal FlatBuffer `CombatSwing`/`DamageEvent` **and** legacy `DMG_BIT_DEAD` damage packets; the server mirrors lethal packets to a summon's out-of-range owner so a summon dying off-screen can't leak a gauge entry. Under UI2 the panel is `RoseRmlSummons`, which also lists each summon's abilities, read from its AI file's cast-skill actions (`src/client/rmlui/CLAUDE.md` "Summon cards"). See `src/client/interface/CLAUDE.md` "Summon Info Panel" for details.

### Monster Inspector (Client-Only Window Skill)
The "Monster Inspector" skill (LIST_SKILL row appended by `scripts/add-monster-inspector-skill.py`, currently id 7001; learn via GM `/add skill 7001` — not `/set`, whose handler has no SKILL branch) opens `CMonsterInspectorPanel` showing a targeted monster's level, live HP, LIST_NPC stats, drop-list icons (from `ITEM_DROP.STB`: mob table + zone table, redirect groups expanded) and a rotating 3D model. It is a `SKILL_CREATE_WINDOW` (type 2) skill with `SKILL_POWER` 50 — the client opens the window locally and **never sends a packet**, so it cannot aggro; the server needed no changes. The 3D preview is a client-only puppet clone of the monster model glued in front of the camera behind a cut-out pane (no render-to-texture). See `src/client/interface/CLAUDE.md` "Monster Inspector" for details.

### NPC Overhead Quest Icons (Client-Only)
NPCs show a "!" when they offer an acceptable quest and a "?" when a quest can be turned in (nothing otherwise). Revives the dead retail pipeline (`CObjNPC::m_nQuestSignal` + `QUEST_EMOTICON_*` draw in `CNameBox::DrawNpcName`); our evaluator is fully client-side and covers **all** quests: trigger names are harvested from the NPC's `.CON` Lua string constants (readable even in compiled bytecode), classified by QSD reward composition (`REWD_000` op1 = accept; `COND_000` + advance-or-delete-with-grant = turn-in; delete-only = abandon option, ignored), and probed with the same `CheckQUEST(..., false)` call retail dialogs use. Quest-editor dialogs additionally evaluate their `CHK_*` Lua check functions in a throwaway state. A passing QSD trigger is only a *candidate*: the icon also requires a headless walk of the dialog (inert probe Lua state, real gates) to reach a node that names the trigger, because dialogs gate on things the QSD never sees — a check that always returns 0 (Spero), an NPC event value for daily timed quests (Cornell, open 19:50-23:57 server time). Refresh: NPC spawn / quest packets / 2 s tick. Sprites installed by `scripts/add-quest-emoticons.py` (placeholders; `--icons` for real art). **Deploy gotcha:** `UI_strID.ID` loads via `fopen` from the loose `3ddata\control\xml\` folder — the VFS bake never covers it. The tool-side mirror of the classifier lives in `src/tools/quest-editor/src/classify.rs` (keep in sync with `cevent.cpp`); it powers the wizard's "this NPC already offers …" info, the `con-triggers` CLI, and an icon-compatibility check in the editor's verify step. See `src/client/event/CLAUDE.md` "NPC Overhead Quest Icons".

### Damage Meter (Client-Only)
The `/dps` chat command (intercepted locally, never sent) toggles `CDamageMeterPanel` (the RmlUi meter instead while RmlUi/UI2 is on; `/dps reset` clears the data) — a draggable DPS/damage overlay with Damage Done (self + party ranking), My Skills, and Damage Taken views. Data comes from `CDamageMeter` (`src/client/gamedata/`), fed by a read-only tap at the single chokepoint `CObjCHAR::PushCombatDamageEvent`; it observes the authoritative combat event stream and never mutates presentation state. Party members' damage is metered for free because combat events broadcast to the 9 surrounding sectors. Attribution rides on two `DamageEvent` wire fields that are **display metadata only** (presentation must never branch on them): `skill_id` (exact skill; legacy `GSV_DAMAGE_OF_SKILL` also carries it; `CombatSwing` = auto-attack; client heuristic as fallback) and `source_attacker_id` (credit target when ≠ attacker: DoT caster, summon owner — so DoTs credit their caster and party pets fold into their owner's row). Cart/castle-gear attacks fold to the rider via `IsPET()` (castle gear is `OBJ_CGEAR`, not `OBJ_CART` — an equality check silently drops gear damage). See `src/client/interface/CLAUDE.md` "Damage Meter".

### Chat Item Links (Client-Only)
Shift+click an item icon in any `CSlot` (inventory, bank, equip, stores) inserts `[Item Name]` into the chat input; on send it is substituted with the wire token `<il:XXXXXXXXXXXX>` (12 hex chars = the 6 packed bytes of `tagBaseITEM`). Receivers rebuild name/rarity-color/tooltip locally from STBs (nothing spoofable is transmitted), render the link inline in the chat log (`CTListBox` link segments), and hover shows the standard item tooltip. Overhead speech bubbles show the plain `[Name]`. **Zero server changes** — chat relays bytes verbatim; the client never sends a message whose `strlen` exceeds 129 bytes (the server's `IS_HACKING` chat guard). Codec in `src/client/interface/chatitemlink.*`; max 3 links/message. See `src/client/interface/CLAUDE.md` "Chat Item Links".

### Item Preview Panel (Client-Only)
Alt+left-click an equipment item (any inventory/bank/store `CSlot` icon, or a chat item link) opens `CItemPreviewPanel` — the player's character wearing that item in a rotating 3D pane, without equipping it or sending any packet. Previews are cumulative while the panel stays open (hat + chest + weapon together; closing resets). Ctrl+click was unavailable (already bound to wishlist registration on item icons). The puppet reuses `CJustModelAVT` (character-select model viewer), never inserted into the scene, rendered through the same avatar-selection viewport pipeline as the Monster Inspector. Shared pane helpers + the avatar-selection camera absolute-value mirror live in `src/client/interface/OverlayPanelUtil.*` (one mirror process-wide — both panels use it). See `src/client/interface/CLAUDE.md` "Item Preview Panel".

### Debugging a Client Crash or Freeze

The client installs `CrashHandler` (`src/client/CrashHandler.cpp`, from `winmain.cpp`): an unhandled exception writes `crash-<stamp>.txt` + `crash-<stamp>.dmp` in the game folder, and a vectored handler covers heap-corruption fail-fasts. Anything that runs no handler (see the fail-fast bullet below) leaves only `error.txt` ending with a clean `log: end.`. For those, and to catch a crash live, use `scripts/debug-client-crash.ps1` (servers up first): it hash-verifies `bin/<config>` PDBs against the deployed binaries, forces windowed mode, restores `rose-next.ini` afterwards, and writes `!analyze -v` + all thread stacks + a full `.dmp` on the access violation.

- cdb has **no working-directory switch** and the debuggee inherits the caller's, so it must launch from the game dir — otherwise the client can't find `rose.vfs` and exits early, looking exactly like "it didn't crash".
- **No crash dump despite `CrashHandler` means a fail-fast**: a CRT invalid
  parameter (`_stricmp(NULL)`, a bad `printf` argument...) or a `/GS` cookie ends
  in `int 29h`, which runs no exception filter and no VEH. `client.log` just stops.
  The debug script still catches it (`c0000409`, look for `_invalid_parameter` on
  the stack) -- reach for it straight away rather than reading code.
- **`0xE06D7363` with a 0-byte `.dmp` is out of address space**, not a missing handler:
  `std::bad_alloc`, and `MiniDumpWriteDump` itself had no memory left (three such crashes on
  two testers' machines, 2026-11-01). Resolve the `.txt`'s stack offsets with `ln` in
  `cdb -z bin\release\znzin.dll` / `rosenext.exe`. The client is `/LARGEADDRESSAWARE` since
  then (4 GB on 64-bit Windows); the `.txt` now carries a `memory :` line and the thrown type,
  `client.log` a `Memory (...)` line per zone and per minute, and the debug HUD a `Mem:` row.
  Task Manager's "Memory" column is the working set and says nothing about this.
- **A climbing `Memory (periodic ...)` line is a leak until shown otherwise -- find it with UMDH,
  not by reading code.** Admin, once: `gflags.exe /i rosenext.exe +ust` (Debuggers\x86); start the
  client; `umdh -p:<pid> -f:a.txt`, wait two minutes, `umdh -p:<pid> -f:b.txt`, then
  `umdh a.txt b.txt -f:diff.txt` with `_NT_SYMBOL_PATH=bin\release`. The diff ranks allocation
  stacks by growth. Each snapshot freezes the game ~15 s. Turn it off after (`-ust`): every
  allocation records a stack. It found the UI2 skill bar leaking ~13 MB/min (2026-11-02:
  `CHotIconSlot::GetHotIcon` returns a new icon the caller owns) and `STBDATA::get_cstr`
  copying every string it returned.
- For a **freeze, don't kill the process**: `cdb -pv -p <pid>` attaches non-invasively and works even with cdb already attached.
- Check `triggervfs.dll` in the game folder matches `bin/release` before trusting its frames: a stale copy resolves to nonsense (`VGetVfsNames+0x…`). It was stale until 2026-10-22 (redeployed with the archive-first `FileExists`); if it drifts again, disassemble the caller rather than trusting the symbol.
- **`client.log` survives a crash; `error.txt` does not.** The engine log is buffered, so a hard crash loses the whole session and the file still ends at the *previous* run's `log: end.` — which reads as "it never launched". The Rust-side `client.log` flushes per record, so read it first to see how far startup actually got.
- **A crash right after a class-layout change is a stale-build artifact until proven otherwise.** Adding a member to a widely-included header (`zz_node.h` is the base of every engine object) after an *interrupted* build leaves some objects compiled against the old layout. Kill stray `cl`/`link`/`mspdbsrv`, delete `build/<config>`, rebuild serially before debugging anything else.

### Shared Data Types
`src/common/shared/` contains game data structures (items, quests, inventory, economy) used by both client and server. Changes here affect both sides.

### Item Encoding And Package Items
Item headers are shared wire data between client and server. `tagBaseITEM` uses a 5-bit item type plus an 11-bit item number so item IDs above 1023 (for example use-item boxes `10:1060`-`10:1062`) round-trip correctly. The created flag is server-only state on `tagITEM`; do not put it back into the packed 16-bit header or the client will decode high IDs as wrapped lower IDs (for example `10:1060` becoming `10:36`).

When creating items from explicit type/id pairs, use the type/id initializer instead of the legacy `type * 1000 + id` packed integer format. The latter cannot represent item numbers above 999. This matters for `/item`, package rewards, and any code path that spawns or grants modern high-numbered use items.

Use-item class `322` is a package box: the value in `LIST_USEITEM.STB` `ADD_DATA_VALUE` selects a server-side reward package. Unknown package IDs should fail without consuming the box so missing package mappings are visible and recoverable.

### Dynamic Zone Objects (2026-10-10)

Map decorations the server puts up and takes down mid-game: `GSV_ZONE_OBJECTS`
(0x07f1, `common/net_prototype.h`), one packet per group with its state and its
placements (`tagZONE_OBJECT`: object id, kind 0 = the zone's `LIST_DECO` object / 1 =
the map's invisible collision panel, world cm, rotation about Z, scale per axis).
`CZoneTHREAD::SetZoneObjects` stores the group and broadcasts; `Send_gsv_JOIN_ZONE`
sends the standing groups to every arrival; nothing per tick. The client
(`src/client/zoneobjects.*`, `CZoneObjects`) builds them with `Add_GndTREE` /
`Add_CollisionBox` + `InsertToScene` -- in the scene with the ZSC's collision level,
so the avatar's body collision stops against them like any map wall -- fades a group
down over 2 s (`setVisibilityRecursive`, still blocking) and deletes it;
`CObjectMANAGER::Clear` forgets them on a zone change. **The server enforces nothing**
(the walk grid is baked from map objects, and these walls are added at run time), so
a client-only wall is as real as every existing one; monsters do not collide with
them (tree cylinders only). First use: the Cerberus Lair's ice wall; meant for the
Karkia catacombs maze. Walls through a map's open ground must be proven: the lair's
"rock ring" was a walkable ridge (`scripts/cerberus-wall.py`'s flood fill).

### NPC Dialog Quest Options (.CON QEX1 Appendix)
NPC conversations are `.CON` files (`data/3DDATA/EVENT/`) whose logic is compiled Lua 4 **bytecode** — new functions can't be merged into the blob (Lua 4.0.1 rejects multi-chunk buffers). Our extension: an optional appendix after the Lua tail (`b"QEX1"; i32 len; XOR'd Lua source`) that the client (`cevent.cpp`, `QEX_APPENDIX_MAGIC`) executes into the same `lua_State` via a second `Do_Buffer`. The quest editor uses it to append quest options to an NPC's existing dialog without replacing it (`quest-editor con-append`, or the wizard's append radio). Codec + gotchas (main-blob XOR key depends on file size; menu-0 append ordering) live in `src/tools/quest-editor/src/convo.rs` and the tool's `PROGRESS.md`. `.CON` files are client-only — the server never reads them; quest authority stays in QSD triggers. Appended `.CON`s require the QEX1-aware client: ship client + data together.

## Common Pitfalls

- **Always build via the .sln, not individual .vcxproj files.** Projects depend on `$(SolutionDir)` and `$(GeneratedDirCommon)` (→ `build/gen/common/`) from `rose-next.props`. These don't resolve when building a vcxproj standalone. Use: `MSBuild.exe rose-next.sln -p:Configuration=release;Platform=x86 -t:sho_gameserver`
- The `common.vcxproj` pre-build step runs `cargo build`, so building via the solution handles Rust automatically
- Always build Rust with i686 toolchain — the entire project is 32-bit x86
- FlatBuffers schemas in `src/common-lib/packets/` must be compiled with `flatc` (handled by build.rs)
- `src/common-lib/build.rs` uses `PROFILE` env var (not `DEBUG`) to find flatc path
- Server projects link against Rust staticlib output — build Rust before C++
- Thirdparty must be built before rose-next.sln
- A new member or virtual in a class with client/server twins (`CObjAI`, `CObjCHAR`,
  `CObjAVT` ... — separate headers under `src/client/` and `src/sho_gameserver/src/`,
  usually changed together) is a class-layout change: clean-rebuild every project whose
  header changed (`client`, `sho_gameserver`, or both when both twins change), never
  incrementally (see "Debugging a Client Crash or Freeze").
