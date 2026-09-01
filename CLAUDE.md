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
- Rust toolchain override: `stable-i686-pc-windows-msvc` in `src/`
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
- **RmlUi and FreeType are hand-written `.vcxproj` files in `thirdparty.sln`** (like every other dep here — the upstream projects are CMake-based and we do not use CMake). Both need `RMLUI_CUSTOM_RTTI` (client RTTI is off) and `RMLUI_FONT_ENGINE_FREETYPE` — without the latter RmlUi compiles its "no font engine" branch and `Rml::Initialise` fails at runtime. RmlUi also needs per-directory object output (`<ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>`): `Source/Core/Geometry.cpp` and `Source/Debugger/Geometry.cpp` share a basename and otherwise overwrite each other, failing the link with unresolved externals that look like missing sources.

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
├── tools/               # Standalone Rust dev tools (workspace members)
│   ├── vfs-browser/     # rose-vfs: TUI browser for data.idx / .vfs (ratatui + rfd)
│   ├── npc-shop-editor/ # egui editor for LIST_NPC/LIST_SELL shop tabs (COW on shared rows)
│   └── quest-editor/    # CLI + egui wizard: Hunt/Fetch quests + NPC dialog givers (see its PROGRESS.md)
├── rmlui/               # (in client/) RmlUi integration: D3D9 backend, system iface, panels
├── lib_util/            # C++ utility library
└── triggervfs/          # Virtual filesystem
data/                    # Game data files (STB tables, scripts, shaders)
database/                # PostgreSQL schema + migrations
thirdparty/              # C++ deps (lua, zlib, ogg/vorbis, imgui, flatbuffers, etc.)
scripts/                 # PowerShell build/dev scripts
 
```

## Database

- PostgreSQL 12+
- Connection configured in `server.toml` (see `doc/server.toml.example`)
- Migrations in `database/migrations/` — squash with `scripts/squash-migrations.ps1`
- Passwords: SHA256 + salt (generate with `scripts/generate-password.py`)

## Networking

- Custom packet-based protocol over TCP (IOCP on server side)
- FlatBuffers for some packet types (defined in `src/common-lib/packets/*.fbs`)
- C++ packet definitions in `src/common/include/rose/network/`
- Client has separate socket connections: Login → World → Game server

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

- Client looks for assets in `dev/game/`
- Servers look for data in `data/` (configurable in server.toml)
- A/B test two client builds in the real game: `scripts/ab-build.ps1 stage <name>` / `use <name>` / `toggle`. `rosenext.exe` imports `znzin.dll` by name, so the exe+dll must be swapped as a **pair** — never rename one half. See the header for the HUD-labelling and vsync caveats.
- Auto-connect: `rosenext.exe --server 127.0.0.1 --username user --password pass --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character CharName`

## Important Patterns

### Combat Damage Presentation
Combat damage is server-authoritative. The server calculates and applies HP once, then sends FlatBuffer `DamageEvent` / `CombatSwing` packets containing the final `damage_value`, `hp_after`, `presentation_kind`, and `lethal` checkpoint. Legacy `GSV_DAMAGE_OF_SKILL` packets also carry authoritative `m_iHP_AFTER`; the client must not synthesize combat skill checkpoints from visible HP. The client must not recalculate live combat damage.

The client queues `DamageEvent`s per defender in `CObjCHAR::m_CombatDamageQueue`. `CombatSwing` queues the event before starting the confirmed attack animation; `Hitted()` consumes exactly one matching event at the visual hit frame. Projectile damage is queued on receive and presented only on projectile impact. If hard control such as sleep/faint interrupts an attacker before its confirmed normal swing can spawn or reach its hit/projectile consumer, the client discards that exact queued event by `event_id`, folds the server-applied HP silently into the next real presentation, and immediately presents avatar death if the orphan was lethal. Direct HP stat packets (`UpdateStats.hp`, `GSV_SET_HPnMP`) update authoritative shadow HP; lower HP never silently moves the visible bar during combat.

**Mob-death item drops are announced by the sector broadcast, not by the damage packet.** The legacy `gsv_DAMAGE` / `gsv_DAMAGE_OF_SKILL` builders appended the drop inline as a `tag_DROPITEM` *and* called `CObjITEM::SetACTIVE()` as a side effect of building the packet. The FlatBuffer `DamageEvent` path carries no drop, so that activation has to be explicit — and it must happen **before** `CZoneTHREAD::Add_DIRECT`, because a field item is announced exactly once, at insertion: `AddObjectToSector` calls `Make_gsv_ADD_OBJECT`, which early-returns while `m_iRemainTIME <= 0`, and the entire nine-sector broadcast block is skipped. Activating afterwards produces an item that is live in the zone, holds a valid index, is pickable by position — and invisible to every client. Use `SetACTIVE()` rather than `bActive=true` on `InitItemOBJ`: only the former applies the longer party-ownership window. **A field item is also announced more than once, and the client must tolerate that** — at insertion, and again by the sector-visibility path when a player crosses into the sector it landed in (drop positions are randomised by +/-100 units, so a mob-death drop often lands one sector over). The server re-sends deliberately, calling `Update_OwnerObjIDX` first because ownership can change. `CObjectMANAGER::Add_GndITEM` does **not** dedupe: it allocates a fresh slot and `Set_EmptySlot` repoints `m_nServer2ClientOBJ` at the new object, orphaning the first — still drawn, unreachable by server index, so it can never be picked up or removed. The symptom is two identical items where one loots and one is permanent. `Recv_gsv_ADD_FIELDITEM` now refreshes the existing object instead; that is done in the handler rather than inside `Add_GndITEM` because the caller follows up with `InsertToScene()`, which calls `CreateAnimatable()` and re-links scene nodes and is not safe to run twice. Note `CObjectMANAGER::Set_ServerObjectIndex` has a guard for exactly this and it does not work: it indexes `m_nServer2ClientOBJ[client_idx]` where it means `[server_idx]`, and `Set_EmptySlot` writes the map directly without ever calling it — so the "Server replaced existing object" warning never fires. **There are two client routes that build a ground item, and both need that guard.** A skill kill announces the same drop twice: `Apply_DAMAGE` activates and inserts it (sector broadcast → `Recv_gsv_ADD_FIELDITEM`), and the very same `CObjITEM` is then passed to `Send_gsv_DAMAGE_OF_SKILL`, which appends it inline as a `tag_DROPITEM` → `PushFieldItemToList` → `CObjCHAR::DropFieldItemFromList`. The broadcast wins the race (sent at insertion; the list drains only when the death animation presents), so the second route is the one that orphans. Auto-attack kills never take it — `Send_gsv_DAMAGE2Sector` has **no callers**, so the FlatBuffer path carries no inline drop — which is why this reproduces on a skill/summon class (Dealer) and not on a Soldier auto-attacking, and why fixing only the handler looked like a complete fix. The other three drop sites (`cobjnpc.cpp`, two in `gs_user.cpp`) already pass `bActive=true` and were never affected, which is why player- and AI-dropped items worked while mob kills dropped nothing.

A queued event that can never be presented must be removed, or reconciliation dies. `has_pending_damage()` gates **both** HP reconciliation paths, so a single event stranded in a defender's queue disables HP convergence for that character for the rest of its life — the bar then moves only by each hit's own digit and the client/server drift accumulates unbounded. Draining on the attacker's `Dead()` covers only attackers that die; an attacker that leaves your sectors (`Del_Object`), a zone change (`Clear`), or a second swing queued before the first was presented all strand events too. `CObjCHAR::Proc()` runs a timed orphaned-damage sweep as the generic safety net; it resolves silently into shadow HP and deliberately skips lethal events, which have their own death-presenting paths. See client `CLAUDE.md`.

HP convergence is hidden from floating digits. `DamageEvent.damage_value` is the displayed hit number, while folded reconciliation/checkpoint drift only affects visible HP and death state. Local-avatar dead reconciliation becomes pending authoritative death: outgoing attacks show MISS/no damage until the next incoming monster hit presents death, except mutual-death cases where the avatar dies immediately after the monster's normal lethal presentation. When the avatar kills its only/last attacker mid-swing, the drain-on-death path presents the mutual death immediately on the kill (no incoming hit is coming). A `CObjCHAR::Proc()` backstop forces the death after ~1.5 s if the avatar is left pending-dead with no presentation (no-attacker kills like DoT/fall damage), so the player is never stranded alive-client / dead-server and frozen. Remote/non-avatar lethal melee events also have a ~1.5 s spectator fallback: if the killer animation never consumes the queued death on this client, the defender still runs `Dead()` instead of remaining visually alive. The fallback defers (up to a 6 s hard cap) while the killer's confirmed swing for that exact event is still live — slow cart/castle-gear swings put the hit frame past 1.5 s, and popping early killed one-shot targets mid-swing. See client `CLAUDE.md` for details.

### Rendering Device (Direct3D 9Ex)
The client runs on a **Direct3D 9Ex** device when available, falling back to plain D3D9. The payoff is that alt-tab, lock, UAC and RDP no longer lose the device — previously each cost a full invalidate → reset → reload-every-texture-from-VFS cycle that also threw on failure.

Consequences worth knowing before touching rendering:
- **`D3DPOOL_MANAGED` is illegal on a 9Ex device** and has been removed from everything we compile. New resources go in `D3DPOOL_DEFAULT` and must survive `invalidate_device_objects()` / `restore_device_objects()`. `D3DPOOL_SYSTEMMEM` is still legal — use it for anything that genuinely needs `LockRect`, since DEFAULT textures cannot be locked.
- A DEFAULT-pool buffer that is locked every frame needs `D3DUSAGE_DYNAMIC` + `D3DLOCK_DISCARD`; a plain DEFAULT buffer locked per-frame stalls the pipeline. Conversely `D3DLOCK_DISCARD` is illegal on a non-dynamic buffer.
- `S_PRESENT_OCCLUDED` and `S_PRESENT_MODE_CHANGED` are **success** codes — a plain `FAILED()` test misses them. Occlusion must throttle, never reset. An occlusion log line is *not* a black screen: a skipped present leaves the previous front buffer on screen, so the throttle costs a dropped frame, never a blank one.
- Under 9Ex `TestCooperativeLevel()` is deprecated and always returns `S_OK`; use `CheckDeviceState()`, and only after a present returns something unusual.
- **Fullscreen is borderless by default** — a windowed device covering the monitor, so nothing ever hands display-mode ownership back and forth. The legacy exclusive device is `[VIDEO] EXCLUSIVE_FULLSCREEN=1` (or `ROSE_EXCLUSIVE_FULLSCREEN=1`); it produced brief full-screen black flashes mid-game that borderless removes by construction. In borderless the backbuffer is **always** the monitor size — anything else is stretched by D3D and drifts UI hit-testing.
- **`_fill_fullscreen_mode_ex()`'s `D3DDISPLAYMODEEX` must mirror the present parameters exactly** (`Format` = `BackBufferFormat` even though that is `A8R8G8B8`, `RefreshRate` = `FullScreen_RefreshRateInHz` even when that is 0). The runtime cross-checks them and returns `D3DERR_INVALIDCALL` — and the fallback ladder then hands back a working Ex device via plain `CreateDevice`, so **only the log tells you**. Read `error.txt` after any device-creation change.
- Force the legacy path for A/B testing with `[VIDEO] D3D9EX=0` in `rose-next.ini` or `ROSE_NO_D3D9EX=1` in the environment — and confirm via the log, since a toggle that silently does nothing gives a false negative.
- The **depth-stencil format is negotiated at device creation**, not fixed — see "Depth Buffer Precision" below before touching format selection in `initialize()`.

`[VIDEO] VSYNC=0` uncaps the framerate — but vsync is **not** the only limiter, contrary to what this line used to say. `zz_system::sleep()`, reached from `swapBuffers()`, enforces `max_framerate` with `::Sleep()`. It does not bind in practice only because `data/SCRIPTS/INIT.LUA:39` calls `setFramerateRange(15, 1000)`; the `src/` default is **60**, so grepping `src/` alone gives the wrong answer — the same trap as `setMipmapLevel` and `setLazyBufferSize`. Note `sleep()` also runs `manager_update()`, the resource amortiser, so the client's `present` phase has never been GPU wait alone: measured, real Present is 0.2–0.4 ms and the rest was the amortiser plus a per-frame yield. Full design notes, the D3DX9 upgrade rationale, the rejected `FLIPEX` work and the remaining gaps are in [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md).

### Bone Particle Budget (Client/Engine)
Cosmetic character bone effects created by `CCharMODEL::CreateBoneEFFECT` are tracked separately by `CBoneEffectBudget` (`src/client/BoneEffectBudget.*`). This budget exists for passive bone-attached aura/loop effects only; skill, hit, projectile, terrain, weather, weapon, and normal world effects must not be registered there.

The manager budgets visible bone-effect groups by estimated particle capacity and emit rate, then applies particle-only tiers through `CEffect::SetParticleTier`: Full, Reduced, Minimal, Off. Mesh and sound components remain unchanged. Runtime caps and emit scaling are enforced in the engine particle emitter/sequence code, so degraded/off tiers are actually cheaper instead of only hidden. Current relaxed budgets are 480 runtime particles, 360 emit/sec, and 3 full duplicate groups per same NPC/effect signature. The debug HUD exposes this as the `BoneFx:` line.

Additive-compatible bone particles also opt into safe texture batching through `CEffect::SetParticleBatchRenderHint`, forwarded to `zz_particle_emitter`. Only `CreateBoneEFFECT` enables this hint. The engine batches hinted additive sequences by texture/render state in `zz_particle_emitter::RenderParticleListWithBatching`; incompatible particles and all non-bone effects use the old render path. The debug HUD exposes batching as the `PartBatch:` line.

### Server Zone Architecture
GameServer manages zones via `gs_threadzone` — each zone runs in its own thread. Players are tracked per-sector for efficient broadcasting (`send_packet_nearby()` checks 9 adjacent sectors).

### Summon Control (CTRL+Click)
Players can directly command their summons with CTRL+click: on terrain → all summons move there; on a hostile monster → all summons attack it. Non-CTRL clicks are unchanged, and CTRL+click is only hijacked when the player actually owns a summon. The client (`jcommandstate.cpp` `TrySummonControlClick`, hooked into both single- and double-click paths) sends the shared `CLI_SUMMON_CONTROL` packet; the server (`classUSER::Recv_cli_SUMMON_CONTROL` → `CZoneTHREAD::CommandSummons_MoveTo`/`_Attack`) is authoritative. `CObjSUMMON` holds a ~5 s manual-order window that suppresses its follow-the-owner loop and AIP auto-aggro, then resumes normal following. Move orders use `CObjSUMMON::PlayerOrderMoveTo` (tolerant of non-walkable cells) — **not** `SetCMD_MOVE2D`, whose `IsMovablePOS` gate silently drops clicks. See the client and gameserver `CLAUDE.md` files for details.

### Summon Info Panel (Client)
`CSummonInfoPanel` (`src/client/interface/csummoninfopanel.*`) is a draggable on-screen overlay showing the player's active summons (name, ATK/DEF/LV/RES, HP). It draws directly with sprites/fonts (no XML resource) and is visible only while the player owns a summon. Key gotcha: a summon's stats are **scaled at creation** by summon-skill level + owner level (server `CObjSUMMON::SetCallerOBJ`), so the panel shows the scaled values, not the raw NPC table. The client recomputes the same formulas once at summon time in `recvpacket.cpp` and caches them in `SummonMobInfo` — if you touch the server scaling, update the client copy too. The underlying summon gauge (`CObjUSER::m_SummonedMobList`) is decremented at packet receive on lethal FlatBuffer `CombatSwing`/`DamageEvent` **and** legacy `DMG_BIT_DEAD` damage packets; the server mirrors lethal packets to a summon's out-of-range owner so a summon dying off-screen can't leak a gauge entry. See client `CLAUDE.md` for details.

### Monster Inspector (Client-Only Window Skill)
The "Monster Inspector" skill (LIST_SKILL row appended by `scripts/add-monster-inspector-skill.py`, currently id 7001; learn via GM `/add skill 7001` — not `/set`, whose handler has no SKILL branch) opens `CMonsterInspectorPanel` showing a targeted monster's level, live HP, LIST_NPC stats, drop-list icons (from `ITEM_DROP.STB`: mob table + zone table, redirect groups expanded) and a rotating 3D model. It is a `SKILL_CREATE_WINDOW` (type 2) skill with `SKILL_POWER` 50 — the client opens the window locally and **never sends a packet**, so it cannot aggro; the server needed no changes. The 3D preview is a client-only puppet clone of the monster model glued in front of the camera behind a cut-out pane (no render-to-texture). See client `CLAUDE.md` "Monster Inspector" for details.

### NPC Overhead Quest Icons (Client-Only)
NPCs show a "!" when they offer an acceptable quest and a "?" when a quest can be turned in (nothing otherwise). Revives the dead retail pipeline (`CObjNPC::m_nQuestSignal` + `QUEST_EMOTICON_*` draw in `CNameBox::DrawNpcName`); our evaluator is fully client-side and covers **all** quests: trigger names are harvested from the NPC's `.CON` Lua string constants (readable even in compiled bytecode), classified by QSD reward composition (`REWD_000` op1 = accept; `COND_000` + advance-or-delete-with-grant = turn-in; delete-only = abandon option, ignored), and probed with the same `CheckQUEST(..., false)` call retail dialogs use. Quest-editor dialogs additionally evaluate their `CHK_*` Lua check functions in a throwaway state. Refresh: NPC spawn / quest packets / 2 s tick. Sprites installed by `scripts/add-quest-emoticons.py` (placeholders; `--icons` for real art). **Deploy gotcha:** `UI_strID.ID` loads via `fopen` from the loose `3ddata\control\xml\` folder — the VFS bake never covers it. The tool-side mirror of the classifier lives in `src/tools/quest-editor/src/classify.rs` (keep in sync with `cevent.cpp`); it powers the wizard's "this NPC already offers …" info, the `con-triggers` CLI, and an icon-compatibility check in the editor's verify step. See client `CLAUDE.md` "NPC Overhead Quest Icons".

### Damage Meter (Client-Only)
The `/dps` chat command (intercepted locally, never sent) toggles `CDamageMeterPanel` — a draggable DPS/damage overlay with Damage Done (self + party ranking), My Skills, and Damage Taken views. Data comes from `CDamageMeter` (`src/client/gamedata/`), fed by a read-only tap at the single chokepoint `CObjCHAR::PushCombatDamageEvent`; it observes the authoritative combat event stream and never mutates presentation state. Party members' damage is metered for free because combat events broadcast to the 9 surrounding sectors. Attribution rides on two `DamageEvent` wire fields that are **display metadata only** (presentation must never branch on them): `skill_id` (exact skill; legacy `GSV_DAMAGE_OF_SKILL` also carries it; `CombatSwing` = auto-attack; client heuristic as fallback) and `source_attacker_id` (credit target when ≠ attacker: DoT caster, summon owner — so DoTs credit their caster and party pets fold into their owner's row). Cart/castle-gear attacks fold to the rider via `IsPET()` (castle gear is `OBJ_CGEAR`, not `OBJ_CART` — an equality check silently drops gear damage). See client `CLAUDE.md` "Damage Meter".

### Chat Item Links (Client-Only)
Shift+click an item icon in any `CSlot` (inventory, bank, equip, stores) inserts `[Item Name]` into the chat input; on send it is substituted with the wire token `<il:XXXXXXXXXXXX>` (12 hex chars = the 6 packed bytes of `tagBaseITEM`). Receivers rebuild name/rarity-color/tooltip locally from STBs (nothing spoofable is transmitted), render the link inline in the chat log (`CTListBox` link segments), and hover shows the standard item tooltip. Overhead speech bubbles show the plain `[Name]`. **Zero server changes** — chat relays bytes verbatim; the client never sends a message whose `strlen` exceeds 129 bytes (the server's `IS_HACKING` chat guard). Codec in `src/client/interface/chatitemlink.*`; max 3 links/message. See client `CLAUDE.md` "Chat Item Links".

### Item Preview Panel (Client-Only)
Alt+left-click an equipment item (any inventory/bank/store `CSlot` icon, or a chat item link) opens `CItemPreviewPanel` — the player's character wearing that item in a rotating 3D pane, without equipping it or sending any packet. Previews are cumulative while the panel stays open (hat + chest + weapon together; closing resets). Ctrl+click was unavailable (already bound to wishlist registration on item icons). The puppet reuses `CJustModelAVT` (character-select model viewer), never inserted into the scene, rendered through the same avatar-selection viewport pipeline as the Monster Inspector. Shared pane helpers + the avatar-selection camera absolute-value mirror live in `src/client/interface/OverlayPanelUtil.*` (one mirror process-wide — both panels use it). See client `CLAUDE.md` "Item Preview Panel".

### RmlUi UI Layer (Client)
New/custom client panels can be authored in **HTML/CSS-like files** (`.rml` / `.rcss`) instead of
hard-coded C++ draw calls, via **RmlUi 6.2 + FreeType 2.13.3** (`thirdparty/RmlUi-6.2`,
`thirdparty/freetype-2.13.3`, built as x86 `/MT` static libs with `RMLUI_CUSTOM_RTTI`). The client
side is `src/client/rmlui/`; assets live loose in `3ddata/rmlui/`.

Off by default — enable with `[VIDEO] RMLUI=1` in `rose-next.ini` or `ROSE_RMLUI=1`. When enabled,
`/dps` opens the RmlUi damage meter instead of the legacy `CDamageMeterPanel`; both read the same
`CDamageMeter` core, so they A/B in place.

**Scope is new/custom panels only.** `tgamectrl`, the 56 retail XML dialogs, chat input and **IME**
are out — RmlUi has no IME composition handling, and the input boundary is the fiddliest part of the
integration. The purpose is quick, player-editable interfaces, **not** reproducing the original
TSI/atlas workflow: `.rml`/`.rcss` are loaded loose (never via the VFS) so players can edit them, and
texture loading resolves **disk first, VFS second** so a player's file overrides shipped art.

Linear gradients are implemented in the D3D9 backend without a shader, and `border-radius` needs no
renderer support, so skins need no image files at all. Radial/conic gradients, blurred `box-shadow`,
`filter` and `transform` are **not** implemented and will warn or do nothing. Full design notes,
phase history, the authoring palette and the traps are in
[doc/rmlui-evaluation.md](doc/rmlui-evaluation.md); client specifics are in the client `CLAUDE.md`.

### Cull Bounds Come From Geometry, Not From The ZSC (Client/Engine)

Every ZSC object record caches a model-space AABB, and **it is wrong in every ZSC in
the game**: the exporter scaled only X into world units (cm) and left Y and Z in mesh
units, so the stored box is ~100x too thin and ~100x too short. Retail Junon has it too,
so it is not an import artefact. Muris' canyon walls registered as a 336 m x 4.0 m x
1.1 m ribbon on the ground instead of a 336 m x 396 m x 114 m wall; because fixed
objects live or die with their terrain patch, turning the camera pushed the ribbon out
of the frustum and `RemoveFromScene()` deleted the whole wall mid-screen. 222 of the
1388 map objects in `data/` under-cover by more than 20 m, the worst by 970 m (Junon
Polis' planet vessel).

It only ever mattered because we re-enabled patch culling: the original
`ViewCullingFunc` had its condition commented out around an unconditional `return 0`,
so every patch was "fully inside" every frame and the boxes were inert. *Re-enabling
dead code promotes its inputs from decorative to load-bearing* — the same shape as the
missing-asset retry storm below.

`CMAP_PATCH::MakeAABBFromObject` now asks the engine for the world AABB it already
derives from real mesh min/max (`getVisibleWorldMinMax` -> `CObjFIXED::GetWorldMinMax`),
keeping the cached box only as a fallback. This is safe at map-load time because
`loadMesh` reads the ZMS header min/max **eagerly**, independent of lazy geometry
loading, and `loadVisible` builds each part's OBB before the object is ever inserted
into the scene.

Things that will bite:

- **The client now imports a new engine export, so `rosenext.exe` and `znzin.dll` must
  be deployed as a pair.** A new exe against an old DLL will not start. `scripts/ab-build.ps1`
  exists for this constraint.
- **Mesh units depend on the ZMS version.** `load_mesh_minmax` applies `ZZ_XFORM_IN`
  (x0.01) for version < 7 only, so v7/v8 headers are metres and v6 headers are already
  cm. We ship 22 v6 meshes.
- `ViewCullingFunc` tests only **four** frustum planes (near, far, left, right — never
  top or bottom), so vertical FOV culls nothing and the commented-out z comparisons in
  `CompareSizePath2Obj` are not worth restoring on their own.
- A narrow residual remains: an object that fits inside its patch's 10 m footprint but
  towers over local terrain never gets `ExPatchEnable` and is still culled by a
  terrain-only z. See the doc for why the obvious one-line fix is insufficient.

`scripts/audit-zsc-bounds.py` is the verification tool: read-only by default, it
re-derives the correct box from ZMS headers plus part transforms, proves the axis-scale
signature per file, ranks objects whose geometry escapes their stored box, flags boxes
that are corruptly *over*sized, and reports missing mesh files. Run it after any
`import-*.py` that appends ZSC objects. Full record in
[doc/zsc-bounding-boxes.md](doc/zsc-bounding-boxes.md).

### Depth Buffer Precision (Engine)

The device used a **16-bit depth buffer** (`D3DFMT_D16`, hardcoded), and the avatar
camera's near plane is **1 m** against a far plane of 800 m (`LIST_CAMERA` columns 5
and 6, multiplied by 100 into cm by `ApplyCameraOption`, then by `ZZ_SCALE_IN` into
engine metres). Resolvable depth separation is `z^2*(f-n)/(n*f*2^bits)`, which at 16
bits is 3.8 cm at 50 m, 15 cm at 100 m and **61 cm at 200 m** — so any two surfaces
mounted closer than that collapsed into one depth bucket and z-fought. On screen it
read as black bands crawling across a distant object and resolving cleanly as you
walked in, because the loser of the fight is usually an unlit interior or backside.
It did not reproduce in the xadet map editor, which is XNA and defaults to Depth24.

`initialize()` now probes `D24S8 -> D24X8 -> D16` with `CheckDeviceFormat` +
`CheckDepthStencilMatch` and stores the winner in `depthstencil_format`. 24 bits is
256x finer, which pushes the onset past the far plane for any realistic mounting gap.

Things that will bite:

- **The near plane is the whole story; the far plane is nearly irrelevant.** With
  `f >> n` the expression collapses to `z^2/(n*2^bits)`. Pulling the far plane from
  800 m in to 250 m changes precision by 0.4%; raising the near plane from 1 m to 5 m
  improves it 5x. Reach for `n` or for bit depth, never for `f`.
- **Only the log tells you which format you got.** `r_d3d: depth-stencil format = N`
  — 75 is D24S8, 77 is D24X8, 80 is the old D16. A probe that silently falls back
  looks exactly like a fix that did not work. Same trap as the D3D9Ex toggle.
- `DEPTH_STENCIL_FORMAT` is now only the fallback seed. Every consumer reads the
  member `depthstencil_format`, including the offscreen z-surface in
  `restore_device_objects()` — that site used the macro directly and would otherwise
  have silently disagreed with the device.
- The FSAA check validates the sample type against the **backbuffer and the depth
  format**. Checking only the backbuffer was harmless while the depth format was a
  constant and is not once it varies: a mismatch fails device creation outright
  rather than degrading to no FSAA.
- Nothing in the engine touches the stencil buffer (no stencil render states, `Clear`
  never passes `D3DCLEAR_STENCIL`), so D24X8 is as good as D24S8. Both are probed
  because driver support for the two is not identical.
- Force the old buffer for A/B testing with `[VIDEO] DEPTH24=0` in `rose-next.ini` or
  `ROSE_NO_DEPTH24=1` in the environment — and confirm via the log.
- There is **no depth bias anywhere**. `zz_renderer_d3d::set_depthbias` is declared
  and defined but never called, and would not work if it were: D3D9's
  `D3DRS_DEPTHBIAS` takes a float bit-pattern, so its `int` parameter turns any small
  value into a denormal ~= 0. Do not reach for it to paper over a z-fight.

Which assets were affected: the failure needs two mesh parts of one object stacked
closer than the depth resolution. The recognisable family is shop signs, statues, and
the `road01`/`road01top` overlay pairs — that naming convention is the real tell, and
Junon Polis has the most of them.

A one-off scan for part pairs whose world AABBs overlap widely on two axes and by
under 25 cm on the third flagged 70 of the 14618 map objects. **Treat that as
corroboration for why only a few assets showed the artefact, not as a predictor**, and
do not rebuild it as a tool: an AABB overlap is not a surface separation (the boxes of
a concave part overlap where the surfaces do not), the thresholds were chosen to fit
the reported case rather than derived, and at 24 bits the separation that still fights
is under ~0.3 cm at any distance we draw — two orders of magnitude below what that
scan measured. If a similar artefact ever survives the format check in the log,
measure real mesh-surface separation; the AABB proxy will mislead you.

To confirm a suspected z-fight from data alone, with no rebuild:
`scripts/set-camera-near-plane.py` raises the near plane across all six `LIST_CAMERA`
rows (`--dry-run` / `--verify` / `--restore`). Precision scales with `n` but onset
*distance* only with `sqrt(n)`, so 1 -> 5 moves the artefact out ~2.2x rather than
removing it — a partial retreat is the confirming signal, not a weak result. The
follow camera's minimum distance is 1.0 m, so a 5 m near plane clips the avatar at
full zoom-in; that is expected, not a second bug. It writes no sidecar next to the
STB on purpose, since `pack.rs` would bake one into the `.vfs`.

### Object Lightmaps Are A Gutterless Atlas (Engine)

Each map-object *part* gets one cell of a shared lightmap texture — `OBJECT_128_0.DDS`
is a 4x4 grid of 128 px cells — and `SetLightMap` addresses it with a UV transform
applied in the vertex shader (`(uv + cell_xy) / grid_n`). There is **no padding between
cells**, so mipmapping, which knows nothing about the grid, would blend a part's
lighting with its neighbours' at low enough mip levels.

It cannot happen, and the reason is one line of Lua: `data/SCRIPTS/INIT.LUA:33` calls
`setMipmapLevel(3)`, which caps every texture at three mip levels. A 512 px atlas only
ever gets 512 -> 256 -> 128, and across every atlas we ship the smallest cell stays at
8x8 texels — bleeding needs cells under about 2. **Grepping `src/` alone says the
opposite**, because the DDS files do carry full mip chains down to 1x1 and the C++
defaults do not cap anything; this is the same trap as `setLazyBufferSize`.

So: treat `mipmap_level = -1` (load the file's full chain) as unsafe for lightmapped
map objects. `setDisplayQualityLevel` levels 3 and 4 set exactly that — level 5 escapes
it only by disabling lightmaps outright. If it ever did appear it would be a flat,
uniform brightness shift over a whole part with neighbouring parts of the same object
drifting out of agreement, never bands and never a sharp onset; it is also
self-limiting, since the mip level tracks screen size and a cell only falls under a
texel once the part is a couple of pixels across.

### Missing Assets Must Degrade, Not Kill (Engine/VFS)

An asset referenced by the data but absent from the baked `.vfs` used to be **fatal anywhere in the game** — four independent defects sat on that one path, each masking the next. All are fixed; the contract now is *a missing file logs once and the object renders without that part*. Keep it that way:

- **Never pass a string-ish class through `ZZ_LOG`'s varargs.** `zz_slash_converter`'s first and only member is `char _str[ZZ_MAX_STRING]` and its `operator const char*()` applies at ordinary call sites but **not** through `...`, so `ZZ_LOG("%s", converter)` copies the buffer onto the stack and `%s` consumes the first four *characters* as a pointer. Always `.get()`. (`zz_string` survives only by luck — it stores a `char*` first.)
- `zz_vfs_pkg::open` must null-check `fp_` **before** using it. `VOpenFile` legitimately returns NULL for a file in neither a package nor on disk, and `zz_assertf` is compiled out in release.
- A failed load must be **recorded**, not retried. `zz_mesh::load` sets `load_permanently_failed` (and `set_path` only clears it when the path actually changes, since `loadMesh` calls `set_path` on every attempt). `CFileLIST::Get_DATA` sets `tagFileDATA::m_bLoadFailed` — that covers motions and materials, but **not meshes**: `loadMesh` always hands back the spawned node, so `CMeshLIST::Load_FILE` reports success even for a file that will never load. Without the engine-side flag a single missing mesh produced 2.26M retries and a 447 MB `error.txt`.
- `zz_manager` must **drop** a terminally failed node, not re-queue it. `zz_node::is_load_terminally_failed()` (overridden by `zz_mesh`) distinguishes "file is missing" from "not ready yet"; without it `update()` pops and re-pushes the node every frame for the life of the process, `update()` never hits its both-lines-empty early return, and since `push()` does a linear `find()` the cost is O(n²) per update in the number of missing meshes.
- `zz_manager::update`'s entrance loop also bounds failed re-inserts per update. The failure branch re-queues without decrementing `entrance_time_accumulated`, so an unloadable node otherwise spins forever *within a single update*. Note this only became a *hard* freeze once the retry throttling above was fixed — removing accidental throttling can expose a latent spin.

### Debugging a Client Crash or Freeze

The client has **no unhandled-exception filter and no minidump writer**, so a crash leaves `error.txt` ending with a clean `log: end.` and nothing else. Use `scripts/debug-client-crash.ps1` (servers up first): it hash-verifies `bin/<config>` PDBs against the deployed binaries, forces windowed mode, restores `rose-next.ini` afterwards, and writes `!analyze -v` + all thread stacks + a full `.dmp` on the access violation.

- cdb has **no working-directory switch** and the debuggee inherits the caller's, so it must launch from the game dir — otherwise the client can't find `rose.vfs` and exits early, looking exactly like "it didn't crash".
- For a **freeze, don't kill the process**: `cdb -pv -p <pid>` attaches non-invasively and works even with cdb already attached.
- The deployed `triggervfs.dll` does not match `bin/release`, so frames through it resolve to nonsense (`VGetVfsNames+0x…`) — disassemble the caller rather than trusting the symbol.
- **`client.log` survives a crash; `error.txt` does not.** The engine log is buffered, so a hard crash loses the whole session and the file still ends at the *previous* run's `log: end.` — which reads as "it never launched". The Rust-side `client.log` flushes per record, so read it first to see how far startup actually got.
- **A crash right after a class-layout change is a stale-build artifact until proven otherwise.** Adding a member to a widely-included header (`zz_node.h` is the base of every engine object) after an *interrupted* build leaves some objects compiled against the old layout. Kill stray `cl`/`link`/`mspdbsrv`, delete `build/<config>`, rebuild serially before debugging anything else.

### Terrain Streaming Performance

Chunk-display hitches are **resource creation at first render**, not chunk file I/O. The client `CLAUDE.md` has the full picture; the two things to know before touching it:

- **Measure lead time, not queue depth.** Frames between a resource being queued and being force-loaded is what tells you which fix applies. Terrain meshes measured **1 frame** (no amortiser can help — cap the inserts, `[VIDEO] TERRAIN_INSERTS_PER_FRAME`); textures measured **200-300 frames** (the amortiser had slack and wasted it — `[VIDEO] LOAD_BUDGET_US`). Applying either fix to the other problem does nothing.
- Diagnostics are opt-in: `[VIDEO] STREAM_SPIKE_LOG_MS` (0 = off) plus the `MapIO:`/`Flush:` debug-HUD rows and `/perfreset`.
- **`STREAM_SPIKE_LOG_MS` only fires on streaming time**, so a hitch from any other phase writes nothing and is indistinguishable from a smooth frame. `[VIDEO] FRAME_SPIKE_LOG_MS` (0 = off) triggers on *total* frame time and logs that frame's own phase split (`netin/logic/scnupd/shadow/render/ui/present/oth` + the logic sub-slots + the flush counters), which is what names a non-streaming hitch. Reach for it first; the streaming log narrows down what it finds. Run with `VSYNC=0` while hunting, or the vsync wait in `present` masks everything.

### Shared Data Types
`src/common/shared/` contains game data structures (items, quests, inventory, economy) used by both client and server. Changes here affect both sides.

### Item Encoding And Package Items
Item headers are shared wire data between client and server. `tagBaseITEM` uses a 5-bit item type plus an 11-bit item number so item IDs above 1023 (for example use-item boxes `10:1060`-`10:1062`) round-trip correctly. The created flag is server-only state on `tagITEM`; do not put it back into the packed 16-bit header or the client will decode high IDs as wrapped lower IDs (for example `10:1060` becoming `10:36`).

When creating items from explicit type/id pairs, use the type/id initializer instead of the legacy `type * 1000 + id` packed integer format. The latter cannot represent item numbers above 999. This matters for `/item`, package rewards, and any code path that spawns or grants modern high-numbered use items.

Use-item class `322` is a package box: the value in `LIST_USEITEM.STB` `ADD_DATA_VALUE` selects a server-side reward package. Unknown package IDs should fail without consuming the box so missing package mappings are visible and recoverable.

### Buffs And Passives Are Percentage-Based

`LIST_SKILL.STB` carries every ability effect in two columns: a **flat** value
(`SKILL_INCREASE_ABILITY_VALUE`, game col 22/25) and a **percentage**
(`SKILL_CHANGE_ABILITY_RATE`, col 23/26). Our buffs and passives now use the
percentage column — flat values were sized for a level-100 cap and decayed to
irrelevance by level 240 (Power Support's +70 ATK is 19% of a level-100
character and 7% of a level-240 one). Converted by
`scripts/convert-buffs-to-percent.py` (idempotent, sidecar,
`--dry-run`/`--verify`/`--restore`); its docstring is the record of what changed
and what was deliberately left flat.

Things that will bite:

- **A percentage must be taken off the *pre-buff* stat.** `Get_SkillAdjustVALUE`
  calls `Get_BaseAbilityValue`, not `Get_AbilityValue`/`Get_DefaultAbilityValue`.
  The current-value accessors already include the running buff, and
  `StatusEffects::IsEnableApplay` rejects a recast only when it is *weaker*, so a
  percentage off the current value compounds on every recast and settles at
  `rate/(1-rate)` — a declared +30% delivered +43%, +50% delivered +100%, and
  >=100% grew until the `(short)` cast overflowed. Keep the server
  (`CObjAVT::Get_BaseAbilityValue`) and client (`CObjUSER::Get_BaseAbilityValue`)
  lists in sync or the two sides disagree on a buff's magnitude.
- **The two columns follow opposite rules.** Passives are either/or
  (`InitPassiveSkill` / `Skill_LEARN` do `if (RATE) … else FLAT`, so a non-zero
  rate makes the flat value dead data); buffs are additive
  (`Get_SkillAdjustVALUE` sums both terms, so a leftover flat applies *as well*).
  Always zero the flat column when writing a rate.
- **Heals must stay flat.** `Get_SkillAdjustVALUE` resolves `AT_HP`/`AT_MP` to
  *current* HP/MP, so a percentage heal scales with what you have left — useless
  exactly when you need it. `AT_MAX_HP` is the one that means max HP.
- **HIT is deliberately still flat.** Accuracy is a cliff (see the level-gate
  section below), and a percentage hands *less* HIT to the low-CON builds already
  pinned at the 7% floor.
- Passives are re-derived from the table on load (`InitPassiveSkill` rebuilds
  from zero), so a data change needs only a server restart, no migration.
- `Get_BaseAbilityValue` is **virtual on `CObjCHAR`**, the base of every character
  object on both sides. Touching it is a vtable-layout change in a widely
  included header — clean-rebuild `client` and `sho_gameserver`, never
  incremental (see the class-layout warning under Common Pitfalls).

Full measurements and the rest of the balance plan are in
[doc/balance-analysis.md](doc/balance-analysis.md); `scripts/balance-sim.py`
re-derives every number in it against the live STBs.

### Monster Balance And The Level Gate

Combat math lives in `src/common/calculation.cpp` (server-only, behind `#ifdef __SERVER`). Two properties dominate how hard content feels, and neither is obvious from the numbers in `LIST_NPC.STB`:

- **The normal-attack gate is level-proportional.** `Get_SuccessRATE` discards an attack outright when `(player_lv + 10) - monster_lv * kLevelGateScale + rand(1..50)` is non-positive. Because the monster term is *scaled*, the required level surplus grows with absolute level — at the original 1.1 it was ~2-3 levels in Luna, 6-10 across Eldeon, and 20+ in Oro, where it exceeded our own 240 character cap (Gates of Muris wanted 243; the level-240 bosses wanted 254). It is now **1.05**. Anything that reads as "I can't hit this" is usually this gate, not accuracy — check the level difference before touching HIT/AVOID.
- **Skills deliberately use a gentler, non-proportional gate** (`Get_SkillDAMAGE`): weapon `lv + 20 - mlv`, magic `lv + 30 - mlv`. A level-140 raider lands 0% of auto-attacks on a level-173 monster but ~66% of skills. Casters therefore stay effective at a level deficit where auto-attacks stop connecting entirely — that asymmetry is intended, keep it.

Also worth knowing: **damage is proportional to `(ATK - DEF + 250)`**, so a monster whose DEF approaches the player's ATK sits on a damage floor — the fight gets long *and* reads as though the hits do nothing, and small gear changes swing the result wildly because the two terms nearly cancel. And `Get_DropITEM` returns false once `player_lv - monster_lv >= 10`, so lowering a monster's level to make it easier can silently kill its drops.

Five idempotent balance passes correct the data, each with a sidecar next to the STB and `--dry-run` / `--verify` / `--restore`. **Re-running them has an order dependency**: the DEF/RES pass looks up the trend at a monster's *current* level and consults the boss sidecar to identify bosses, so if a level or the boss set changes, restore and re-apply DEF/RES afterwards, then re-verify all five.

- `scripts/rebalance-oro-accuracy.py` — Oro monsters' HIT/AVOID onto our scale (evo-era imports dodged ~1.8x more than anything else here).
- `scripts/rebalance-endgame-curve.py --stat def|res` — caps level-200+ DEF/RES at the trend fitted from levels 60-199. Bosses get `BOSS_MULTIPLIER` (1.2).
- `scripts/rebalance-oro-bosses.py` — boss HP to 10x the HP trend and level to the 240 cap. Scope comes from the map REGEN lumps, not the HP column: quest NPCs sit at ~2,000,000 HP and must not be caught. `EXTRA_BOSS_ROWS` opts in non-Oro bosses by row (currently Luna's Behemoth King).
- `scripts/rebalance-eldeon-outliers.py` — two hand-picked monsters that escaped the passes above through scope gaps.

Their docstrings carry the reasoning and record what was deliberately left alone (Luna's Astarot King and Gem quartet, the big-HP piñata field monsters, unspawned duplicate rows with billion-HP columns). Since `data/` is gitignored, **the script is the only committed record of the change** — put new reasoning there, not just in a commit message.

### Data Repair Tooling

Our `data/` is a translated iROSE dump with gaps; the reference dumps in `C:\Users\Thomas\Desktop\Testclients\` (QQ-iROSE, RoseZA, titanRose) are intact, so diffing a single field across all three is a fast, high-confidence way to find and fix them. All three scripts below are idempotent, take `--dry-run`, and verify after writing. `data/` is gitignored, so **the script is the only committed record of the change** — put the reasoning in its docstring.

- `scripts/fix-mob-bullet-effects.py` — restores `WEAPON_BULLET_EFFECT` (`LIST_WEAPON.STB` game col 38). Empty there means a ranged monster fires **no projectile and lands no visible hit**: the client's `Get_BulletNO()` skips `Add_BULLET`, and the server's `UsesProjectileAttackPresentation()` picks `MeleeHitFrame`, which nothing on a bow/gun motion consumes.
- `scripts/add-zone-name.py` — appends entries to `LIST_ZONE_S.STL`. A blank name above the minimap is **not** a minimap bug: the name comes from the STL keyed by `ZONE_STRING_ID`, never from the STB name column. Documents the `ITST01` layout, incl. the 7-bit varint lengths and the fact that the client ignores the per-entry offset table and reads strings sequentially.
- `scripts/restore-warp-gates.py` — re-inserts `LUMP_TERRAIN_WARP` (type 10) objects into map `.IFO`s. `WARP.STB` and the destination `.ZON` event positions are usually fine; the missing piece is the trigger object the player walks into. Has a `--selftest` that proves the container rewrite is byte-identical before it touches anything.

Note `src/pipeline/src/pack.rs` walks the data tree filtering only *hidden* entries — no extension filter — so any `.bak` these scripts leave behind gets baked into the `.vfs`. Clean them before a bake.

### The .vfs Offset Limit (2 GB -> 4 GB)

**A `.vfs` archive cannot exceed 4 GB, and could not exceed 2 GB before 2026-08-29.** `FileEntry::lFileOffset` in triggervfs was a *signed 32-bit* `long`, so a file stored past byte 2,147,483,647 got a negative offset, the client seeked to garbage and read binary noise. It is now unsigned, which doubles the ceiling — the on-disk field is the same four bytes, only the interpretation changed.

Three things had to change together, and each failed differently:

- `FileEntry::lFileOffset` and `VFileHandle::lStartOff`/`lEndOff` are unsigned. `MapViewOfFile` already took the offset as a DWORD low + DWORD high(0) pair, so the engine's memory-mapped path needed nothing else.
- **`vfread` had a `(signed)` cast** on the end-of-file comparison that actively defeated the unsigned field, plus a `long` offset and a 32-bit `fseek`. The client opens `"r"` (plain fseek/fread) while the engine opens `"mr"` (memory-mapped) — **completely different code paths**, so verifying one proves nothing about the other. The mapped path worked as soon as the field was unsigned; the plain path silently returned **zeros** until `_fseeki64`.
- `vfseek`'s clamps must be **signed 64-bit**. In 32-bit unsigned, `lStartOff + offset` with a negative offset wraps and the underflow guard silently fails.

**Bake with `scripts/pack.ps1`** (`data/` -> `Exes/`). It now runs `verify-vfs.py` itself and fails the bake if the archive is unaddressable, and it prefers `bin/release/pipeline.exe` over the copy in `Exes/`. That default matters: `Exes/pipeline.exe` had gone **four months stale** (2026-04-06, against a rollover added 2026-08-17), so every bake ran a packer built before the size guard existed — which is the entire reason `rose.vfs` sailed past the split threshold with no `rose_2.vfs` while the code looked correct. Checking the timestamp of `bin/release/pipeline.exe` says nothing about what actually packed the archive.

Verify manually with `scripts/verify-vfs.py <game-dir>` after any bulk bake — it re-derives everything from the bytes on disk, so a packer bug cannot hide from it. `scripts/make-oversize-vfs.py` builds a sparse >2 GB archive with a deterministic payload and `vfs_buffer_tests.exe --bigtest <dir>` reads it back through the real triggervfs in **both** modes, checking content it re-derives from the file name — an independent oracle, not a comparison of two paths that share the bug.

The failure looks nothing like its cause. Whichever files happen to land past the boundary are simply the tail of the archive — when it first hit, that was `SCRIPTS\INIT.LUA`, so the client died at startup with a Lua `invalid control char near 'char(6)'` parse error followed by `assert: failed. zz_shader::check_system_shaders()`. Nothing pointed at the archive.

`pack.rs` now **rolls over to `rose_2.vfs`, `rose_3.vfs`, …** at 1.9 GB (the margin absorbs the largest single asset, since the check runs before writing) and hard-errors if an offset would still overflow. Both the `.idx` format (`VfsIndex::file_systems` is a list) and the runtime (`CVFS_Manager::m_vecVFS`, searched by `OpenFile`) already supported multiple archives — only the packer was hardcoded to one. **Ship every `rose*.vfs` alongside `data.idx`**, not just `rose.vfs`.

Diagnosing a suspected bad bake: parse the `.idx` FAT (`short len; char name[len]; long off,len,blk; BYTE deleted,compress,enc; DWORD version,crc`) and check for negative offsets — that is a two-minute script and it is unambiguous.

### Item Import Tooling
`scripts/import-weapon.py` imports a weapon from another ROSE data dump (e.g. an evo-era private server) as a new appended ID: STB row, `LIST_WEAPON.ZSC` model object (with mesh/material dedup), ground-drop model (`--copy-field-model`), STL name/desc key, and any missing mesh/texture files. Always start with `--dry-run`; it makes `.bak` backups and verifies after writing. `scripts/add-item-icon.py` adds a custom item icon from a PNG (any size, auto-downscaled to a 40×40 cell) to the `ITEM1.TSI` atlas and prints the new global icon index (`--weapon-row N` also patches the STB); requires Pillow. `scripts/add-skill-icon.py` is the same tool for **skill** icons (`SKILLICON.TSI`, extension sheets `skill04.dds`+, original indices 0–506, extensions from 507; `--skill-row N` patches `LIST_SKILL.STB` col 51). Note the two TSIs use different sprite-rect conventions (item `x..x+40`, skill `x..x+39`) — each script matches its atlas. Both docstrings document the underlying binary formats — read them before editing STB/ZSC/STL/TSI by hand.

**Putting a new skill in the skill tree needs an art edit**, not just an XML node: the boxes and connectors are painted into the per-category `DEALER_*.DDS`, and `CSkillTreeDlg` has no line-drawing code at all — the XML only positions a 40×40 icon, at coordinates that are **absolute inside the dialog**, not relative to the parent node. The xml loads loose via `fopen` (never the VFS) while the DDS loads from the VFS and ignores a loose copy, so the two halves deploy differently. Recipe, coordinate system and traps: [doc/skill-tree-art.md](doc/skill-tree-art.md).

Key facts: weapon visuals come from `LIST_WEAPON.ZSC` indexed by item number (1:1 with STB rows) — the STB "model file" text column is vestigial and never read by the game. Ground-drop visuals are a separate index (STB col 10) into `LIST_FieldITEM.ZSC`. Icon indices are global sprite positions in `ITEM1.TSI` (originally 50 sheets × 169 cells = 0–8449; extension sheets `icon51.dds`+ continue from 8450). After data edits, restart servers (they cache STBs at startup) and the client.

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
