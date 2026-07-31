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

- C++ formatting: clang-format (run `python scripts/format_code.py`)
- `.clang-format-ignore` for exclusions
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
- Auto-connect: `rosenext.exe --server 127.0.0.1 --username user --password pass --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character CharName`

## Important Patterns

### Combat Damage Presentation
Combat damage is server-authoritative. The server calculates and applies HP once, then sends FlatBuffer `DamageEvent` / `CombatSwing` packets containing the final `damage_value`, `hp_after`, `presentation_kind`, and `lethal` checkpoint. Legacy `GSV_DAMAGE_OF_SKILL` packets also carry authoritative `m_iHP_AFTER`; the client must not synthesize combat skill checkpoints from visible HP. The client must not recalculate live combat damage.

The client queues `DamageEvent`s per defender in `CObjCHAR::m_CombatDamageQueue`. `CombatSwing` queues the event before starting the confirmed attack animation; `Hitted()` consumes exactly one matching event at the visual hit frame. Projectile damage is queued on receive and presented only on projectile impact. If hard control such as sleep/faint interrupts an attacker before its confirmed normal swing can spawn or reach its hit/projectile consumer, the client discards that exact queued event by `event_id`, folds the server-applied HP silently into the next real presentation, and immediately presents avatar death if the orphan was lethal. Direct HP stat packets (`UpdateStats.hp`, `GSV_SET_HPnMP`) update authoritative shadow HP; lower HP never silently moves the visible bar during combat.

HP convergence is hidden from floating digits. `DamageEvent.damage_value` is the displayed hit number, while folded reconciliation/checkpoint drift only affects visible HP and death state. Local-avatar dead reconciliation becomes pending authoritative death: outgoing attacks show MISS/no damage until the next incoming monster hit presents death, except mutual-death cases where the avatar dies immediately after the monster's normal lethal presentation. When the avatar kills its only/last attacker mid-swing, the drain-on-death path presents the mutual death immediately on the kill (no incoming hit is coming). A `CObjCHAR::Proc()` backstop forces the death after ~1.5 s if the avatar is left pending-dead with no presentation (no-attacker kills like DoT/fall damage), so the player is never stranded alive-client / dead-server and frozen. Remote/non-avatar lethal melee events also have a ~1.5 s spectator fallback: if the killer animation never consumes the queued death on this client, the defender still runs `Dead()` instead of remaining visually alive. The fallback defers (up to a 6 s hard cap) while the killer's confirmed swing for that exact event is still live — slow cart/castle-gear swings put the hit frame past 1.5 s, and popping early killed one-shot targets mid-swing. See client `CLAUDE.md` for details.

### Rendering Device (Direct3D 9Ex)
The client runs on a **Direct3D 9Ex** device when available, falling back to plain D3D9. The payoff is that alt-tab, lock, UAC and RDP no longer lose the device — previously each cost a full invalidate → reset → reload-every-texture-from-VFS cycle that also threw on failure.

Consequences worth knowing before touching rendering:
- **`D3DPOOL_MANAGED` is illegal on a 9Ex device** and has been removed from everything we compile. New resources go in `D3DPOOL_DEFAULT` and must survive `invalidate_device_objects()` / `restore_device_objects()`. `D3DPOOL_SYSTEMMEM` is still legal — use it for anything that genuinely needs `LockRect`, since DEFAULT textures cannot be locked.
- A DEFAULT-pool buffer that is locked every frame needs `D3DUSAGE_DYNAMIC` + `D3DLOCK_DISCARD`; a plain DEFAULT buffer locked per-frame stalls the pipeline. Conversely `D3DLOCK_DISCARD` is illegal on a non-dynamic buffer.
- `S_PRESENT_OCCLUDED` and `S_PRESENT_MODE_CHANGED` are **success** codes — a plain `FAILED()` test misses them. Occlusion must throttle, never reset.
- Under 9Ex `TestCooperativeLevel()` is deprecated and always returns `S_OK`; use `CheckDeviceState()`, and only after a present returns something unusual.
- Force the legacy path for A/B testing with `[VIDEO] D3D9EX=0` in `rose-next.ini` or `ROSE_NO_D3D9EX=1` in the environment — and confirm via the log, since a toggle that silently does nothing gives a false negative.

Vsync is the **only** frame cap in the engine (there is no software limiter); `[VIDEO] VSYNC=0` uncaps. Full design notes, the D3DX9 upgrade rationale, the rejected `FLIPEX` work and the remaining gaps are in [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md).

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

### Shared Data Types
`src/common/shared/` contains game data structures (items, quests, inventory, economy) used by both client and server. Changes here affect both sides.

### Item Encoding And Package Items
Item headers are shared wire data between client and server. `tagBaseITEM` uses a 5-bit item type plus an 11-bit item number so item IDs above 1023 (for example use-item boxes `10:1060`-`10:1062`) round-trip correctly. The created flag is server-only state on `tagITEM`; do not put it back into the packed 16-bit header or the client will decode high IDs as wrapped lower IDs (for example `10:1060` becoming `10:36`).

When creating items from explicit type/id pairs, use the type/id initializer instead of the legacy `type * 1000 + id` packed integer format. The latter cannot represent item numbers above 999. This matters for `/item`, package rewards, and any code path that spawns or grants modern high-numbered use items.

Use-item class `322` is a package box: the value in `LIST_USEITEM.STB` `ADD_DATA_VALUE` selects a server-side reward package. Unknown package IDs should fail without consuming the box so missing package mappings are visible and recoverable.

### Item Import Tooling
`scripts/import-weapon.py` imports a weapon from another ROSE data dump (e.g. an evo-era private server) as a new appended ID: STB row, `LIST_WEAPON.ZSC` model object (with mesh/material dedup), ground-drop model (`--copy-field-model`), STL name/desc key, and any missing mesh/texture files. Always start with `--dry-run`; it makes `.bak` backups and verifies after writing. `scripts/add-item-icon.py` adds a custom item icon from a PNG (any size, auto-downscaled to a 40×40 cell) to the `ITEM1.TSI` atlas and prints the new global icon index (`--weapon-row N` also patches the STB); requires Pillow. `scripts/add-skill-icon.py` is the same tool for **skill** icons (`SKILLICON.TSI`, extension sheets `skill04.dds`+, original indices 0–506, extensions from 507; `--skill-row N` patches `LIST_SKILL.STB` col 51). Note the two TSIs use different sprite-rect conventions (item `x..x+40`, skill `x..x+39`) — each script matches its atlas. Both docstrings document the underlying binary formats — read them before editing STB/ZSC/STL/TSI by hand.

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
