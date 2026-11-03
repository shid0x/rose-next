# ROSE Next Classic

A modernized [ROSE Online](https://en.wikipedia.org/wiki/ROSE_Online) private
server and client, built on the original iROSE C++ codebase. The gameplay is the
classic one; the foundation underneath it is not: PostgreSQL instead of MSSQL, a
Direct3D 9Ex client, a rebuilt HTML/CSS-style interface, server-authoritative
combat, a level cap of 240 with the balance work to match, and a dozen new
regions imported from other ROSE lineages.

The original ROSE Next project was discontinued and released so others could
continue it. This repository is one such continuation: an actively maintained
fork with bug fixes, new systems and new content on top of that release. It is
one of the most complete ROSE server/client codebases publicly available, and it
is non-commercial. Thanks to everyone who contributed to the original release.

Everything here is **32-bit (x86) Windows**: a C++/Rust build, Visual Studio
2019, PostgreSQL 12+.

---

## Highlights

### Client and rendering

- **Direct3D 9Ex** device with a plain D3D9 fallback. Alt-tab, lock screen, UAC
  and RDP no longer lose the device, so none of them reloads every texture any
  more. `D3DPOOL_MANAGED` is gone; read
  [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md) before touching rendering.
- **Borderless fullscreen** by default. The legacy exclusive device is still
  there behind an ini key for A/B tests.
- **24-bit depth buffer** (negotiated at device creation, was a hardcoded 16-bit
  one) and **transparency supersampling** on alpha-tested cut-outs (fences,
  leaves) on NVIDIA hardware. Both have ini toggles and log which path is live.
- **Smooth streaming.** Crossing a map-chunk boundary used to drop frames; chunk
  loads went from ~14 ms to 2–3 ms, resource creation is spread across frames,
  and a loading screen pre-warms the OS file cache for the zone. Every remaining
  hitch has a diagnostic that names its cause (see
  [doc/client-settings.md](doc/client-settings.md)).
- **Twice the view distance.** The client keeps a 5x5 square of map chunks
  loaded instead of 3x3, so the ground reaches 320–480 m and large cliffs and
  walls no longer pop in a few metres away. Terrain is drawn as one batch per
  chunk: the Far view went from ~5,600 terrain draw calls (7–9 ms a frame) to a
  few hundred (0.1–0.2 ms).
- **Map objects are culled from real mesh bounds**, not the (wrong in every ZSC)
  cached boxes, so walls no longer vanish when the camera turns. Coplanar
  duplicate placements that flickered in retail data are nudged apart by a tool.
- A **crash handler** writes minidumps, and `scripts/debug-client-crash.ps1`
  runs the client under cdb with symbols verified against the binaries.
- Missing assets and blank data rows **degrade instead of crashing**: a missing
  mesh logs once and the object renders without that part.

### UI2: the rebuilt interface

- Every in-game window is remade on **RmlUi 6.2 + FreeType**: HUD, bag, skills,
  a skill tree generated from the data instead of painted art, quests, shops,
  trade, storage, crafting, community, clan, options, chat, minimap and the
  conversation window. Windows are movable, the layout is saved, and the whole
  UI scales from 75 to 200 %. It costs no more frame time than the classic UI.
  The login and character screens stay classic.
- Turn it on with `[VIDEO] UI2=1` in `rose-next.ini` (the ready-made client
  below ships with it on), or **type `/ui2` in chat** to switch between UI2 and
  the classic interface live; the choice is saved. The Options window has the
  same checkbox.
- **Built to be restyled.** Each window is an `.rml` layout plus an `.rcss`
  stylesheet loaded loose from `3ddata/rmlui/`, with the shared look in
  `rose-theme.rcss`. Edit a stylesheet and **type `/uireload`** to re-read it
  without restarting. Design notes and traps:
  [doc/rmlui-evaluation.md](doc/rmlui-evaluation.md).
- Client-side extras, all packet-free: damage meter (`/dps`), monster inspector
  (a window skill showing stats, drops and a rotating model), item preview
  (alt+click any equipment icon to try it on), chat item links (shift+click),
  overhead `!`/`?` quest icons on NPCs, and summon control (ctrl+click).

### Gameplay and balance

- **Server-authoritative combat.** The server applies damage once and the client
  presents exactly what it is sent, in animation order, with reconciliation for
  every way an event can go missing. The rules are subtle and were expensive to
  get right: read the combat section of
  [src/client/CLAUDE.md](src/client/CLAUDE.md) before touching combat display.
  Flinch (hit stun) is removed on both sides.
- **Level cap 240**, with the experience curve, the level-based hit gate, monster
  HP/ATK/DEF/RES and reward tables re-fitted by idempotent, reversible balance
  passes in `scripts/` (see [doc/balance-analysis.md](doc/balance-analysis.md)).
- **Buffs and passives are percentage-based**, so they stay relevant past level
  100. Heals and HIT are deliberately still flat.
- Class passes for every job line, a damage meter to measure them with, and
  smaller fixes everywhere: items above id 1023 round-trip on the wire, skill
  casts resume the fight afterwards, heals sync HP to the party, Nagle is off
  on every socket, and a client can no longer warp itself anywhere through the
  relay packet.

### New regions

Imported from other ROSE lineages with their models, dialogs, AI, spawns, drops
and balance adapted to this server. Some are still being validated in game.

| Region | Zones | Origin |
| --- | --- | --- |
| **Karkia** — Abandoned Church, Desolate Cemetery, Spire Village, Burned Forest, Memories of Karkia, Infinite Prison, Foot of the Tower, Tower of Despair, Garden of Karkia | 86–88, 131, 133–136, 144 | Jrose |
| **Oro** — Muris, the Portal Room and Temple, the Wasteland, Oasis Shrine, Ruins Path, Golden Ring, Fossil Sanctuary, Colosseum | 14, 71–73, 78–81, 83, 85 | 667 test client |
| **Skaaj** — a cat-folk island town | 89 | Jrose |
| **SHIBUYA** and the **Live House SEVEN** — a level 60–80 zombie street with its own monster kit, boss and six quests | 126, 127 | Jrose (SECONDWALL collab) |
| **Cave of Ulverick** — a spider dungeon with three room bosses and a bone toll at the door | 34 | RoseZA |
| **Cerberus Lair** — an hourly five-player draw run by the server: whelps, a Warden, an ice wall and Cerberus | 49 | tsuki (map) + our own controller |
| **[God] Hebarn** — a talk-then-fight challenge boss at the Oasis Shrine | 80 | China ROSE |

Plus custom weapons imported from glTF (Meshy exports) with animated parts,
Jrose's back items and sub-weapons, socketed-gem glows restored, and dozens of
retail data repairs (missing models, blank projectile rows, dead skill books,
wrong attack frames, unlit lamps).

### Tooling

- **`rosefmt`** (`scripts/rosefmt/`): one Python reader and writer for every
  ROSE file format: the STB/STL tables, ZSC model tables, CHR, LTB, AIP monster
  AI, TSI atlases, QSD quests, `.CON` dialogs, the map files (ZON, HIM, TIL, MOV,
  IFO, LIT), ZMS meshes, DDS headers and the VFS index. Every codec reproduces
  every shipped file byte for byte (`scripts/rosefmt-roundtrip.py` checks all of
  them, and five other clients' dumps), and each module's docstring carries the
  format layout and its traps. Import it from any script; never write a parser
  twice. Design and inventory: [doc/data-codecs.md](doc/data-codecs.md).
- **Map generator and lightmap bakers** (`scripts/mapgen/`): a layout-driven
  zone generator; ground and object lightmap bakers fitted against retail;
  `scripts/relight-zone.py` applies them to any existing zone.
- **Map editor** (`xadet/`, C#/XNA): the 2007 editor, extended with headless
  screenshots, a minimap maker, a cross-zone object browser and placement tools.
- **Quest editor** (CLI + wizard), **NPC shop editor** and a GM item browser,
  and a **VFS browser** in [src/tools/](src/tools/).
- **130 Python scripts** in [scripts/](scripts/), built on `rosefmt`: region
  importers, balance passes, data audits and repairs, item/skill/icon importers,
  VFS verification. Each is idempotent with `--dry-run`, `--verify` and
  `--restore`, and its docstring is the record of why it exists (the game data
  is not in git, so the script is the committed history of every data change).

---

## Repository layout

```
Client (C++/D3D9Ex) ←→ LoginServer  ←→  WorldServer  ←→  GameServer
                              ↕              ↕               ↕
                        PostgreSQL DB   Game Data (STB/STL)  Game Data
```

| Path | What |
| --- | --- |
| `src/client/` | Game client — C++, Direct3D 9Ex, Win32 |
| `src/client/rmlui/` | UI2 — the RmlUi windows (assets in `data/3DDATA/rmlui/`) |
| `src/sho_loginserver/` `src/sho_worldserver/` `src/sho_gameserver/` | The three servers |
| `src/common/` `src/common-server/` | Shared C++: items, quests, calculations, IOCP sockets, SQL |
| `src/common-lib/` | Shared Rust: logger, config, FlatBuffers packets |
| `src/engine/` | The 3D engine (znzin) |
| `src/tgamectrl/` | The classic UI control framework |
| `src/pipeline/` | Rust asset packer (`pipeline.exe`) |
| `src/tools/` | VFS browser, NPC shop editor, quest editor (Rust) |
| `scripts/` | Build/dev scripts and the Python data tooling |
| `scripts/rosefmt/` | The file-format codecs, one module per format |
| `scripts/mapgen/` | The map generator and lightmap bakers |
| `xadet/` | The map editor (C#) |
| `database/` | PostgreSQL schema and migrations |
| `data/` | Game data (not tracked; see [data/README.md](data/README.md)) |
| `doc/` `docs/` | Design notes (see *Further reading*) |
| `website/` | The original ROSE Next account/download website (Django, Docker) |
| `thirdparty/` | C++ dependencies, each with a hand-written `.vcxproj` |

`CLAUDE.md` and `src/*/CLAUDE.md` are the engineering notes: the architecture
map and every "this looks wrong but is deliberate" explanation. The project is
developed with Claude Code and those files are its memory; they are the best
place to start reading before changing anything.

---

## Requirements

- **Visual Studio 2019** with the "Desktop development with C++" workload
  (toolset v142) and the **Windows 10 SDK** — the Direct3D 9 core headers come
  from the SDK, not the vendored DirectX SDK, because only the SDK copies
  declare the 9Ex interfaces.
- **Rust** with the `stable-i686-pc-windows-msvc` toolchain (everything is
  32-bit).
- **PostgreSQL 12+**.
- **PowerShell 7+** for the scripts.
- **Python 3** for the data tooling (Pillow for the icon scripts, `pymeshlab`
  only for mesh decimation).
- Optional: [`just`](https://github.com/casey/just) for the recipes below.

---

## Quickstart

```powershell
# 1. Build everything: thirdparty C++, Rust crates, then the Rose Next projects
just build release                 # or: scripts/build.ps1 -config release

# 2. Put the game files in data/ (see Client > Game data), then pack the client VFS
scripts/pack.ps1                   # data/  ->  Exes/data.idx + Exes/rose*.vfs

# 3. Create the database (see Server > Database), then an account
just create-account you@example.com mypassword        # player
just create-account gm@example.com  mypassword 2048   # GM

# 4. Start the servers, then the client from a folder holding the VFS and DLLs
just server-all release
rosenext.exe --server 127.0.0.1
```

The `just` recipes (`CONFIG` is `release` or `debug`, default `release`):

```
build [config]          Build code (thirdparty + Rust + C++)
build-assets [config]   Bake assets with the original pipeline (optional)
build-all [config]      Both
dev-setup               Link assets into dev/ for local running

client / client-auto    Start the client (auto-connect reads credentials from env)
server-all              Start login + world + game servers
loginserver / ls        worldserver / ws        gameserver / gs

create-account email password [access]   access 2048 = full GM rights
purge-cache             Drop the OS file cache (for cold-cache measurements)
```

---

## Build

Three phases, in this order: **thirdparty C++ → Rust crates → Rose Next C++**.
`scripts/build.ps1` runs all three; the manual steps are:

```powershell
# 1. Thirdparty C++ dependencies
MSBuild.exe thirdparty.sln -p:Configuration=release;Platform=x86

# 2. Rust crates (the 32-bit toolchain is set as an override in src/)
cd src
rustup toolchain install stable-i686-pc-windows-msvc
cargo build --release
cd ..

# 3. Rose Next C++ projects
MSBuild.exe rose-next.sln -p:Configuration=release;Platform=x86
```

Binaries land in `bin/release/`: `rosenext.exe`, `znzin.dll`, `sho_loginserver.exe`,
`sho_worldserver.exe`, `sho_gameserver.exe`, `pipeline.exe`.

> **Always build via the `.sln` files**, not individual `.vcxproj` files — the
> projects rely on solution-level properties that do not resolve standalone.
> MSBuild's diagnostics may be localised; filter for `error`/`erreur`
> accordingly. The sharp edges are collected in
> [doc/rose-next-build.md](doc/rose-next-build.md).

---

## Client

### Game data

The client reads its content from a packed **VFS**: `data.idx` plus `rose.vfs`
(and `rose_2.vfs`, `rose_3.vfs`… once the data passes 4.2 GB — ship every
`rose*.vfs` together). The game files that this project is developed and
balanced against are available here (the link is updated with each release):

```
https://mega.nz/file/MNcAGTra#btw__7pg4oGCVOguqSKKGH75wyip5Lb-F0yDXFWKAag
```

Place the raw files in [`data/`](data/) (layout in
[data/README.md](data/README.md)), then pack them:

```powershell
scripts/pack.ps1                    # data/  ->  Exes/data.idx + Exes/rose*.vfs
scripts/pack.ps1 -out path/to/dir   # write the VFS somewhere else
```

`pack.ps1` uses the freshly built `bin/release/pipeline.exe`, verifies the
archive afterwards, and refuses to bake while a `.bak` file sits under `data/`.
A full bake of the 2.5 GB tree takes about 15 seconds. `Exes/` is only a staging
folder; run the client from wherever you put the VFS.

> **Also required next to `rosenext.exe`:** `znzin.dll`, `triggervfs.dll`,
> `discord_game_sdk.dll` and `d3dx9_43.dll`. The build and `scripts/dist.ps1`
> place them; a hand-assembled folder that is missing one gives a client that
> exits at startup. `rosenext.exe` and `znzin.dll` must always be deployed as a
> **pair** from the same build.

The separate `assets/` baking pipeline (`build-assets.ps1`, `just dev-setup`)
is the original asset-authoring workflow; it is not needed for this setup.

### Running

```powershell
rosenext.exe --server <IP>

# Auto-connect (servers running, character exists)
rosenext.exe --server 127.0.0.1 --username user --password pass `
  --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character MyChar
```

The `client` project can also be launched and debugged from Visual Studio.

### Settings (`rose-next.ini`)

The client reads `rose-next.ini` from its **working directory**. Most keys are
written by the in-game Options window; these are the ones worth knowing by
hand. Every one of them logs which branch is live (`error.txt` for the renderer,
`client.log` for the rest) — **confirm there**, because a toggle that silently
does nothing looks exactly like one that works.

| Key | Effect |
| --- | --- |
| `[VIDEO] UI2` | `1` = the new interface, `0` = classic. Also set by `/ui2` and Options. |
| `[VIDEO] RMLUI` | `1` loads the RmlUi layer even with UI2 off, so `/ui2` can switch live. `UI2=1` implies it. |
| `[VIDEO] UI_SCALE` / `UI_LOCK` | UI2 scale (75–200) and panel lock. |
| `[VIDEO] FULLSCREEN` | `1` fullscreen, `0` windowed (the only resizable mode). |
| `[VIDEO] EXCLUSIVE_FULLSCREEN` | `0` (default) = borderless, `1` = legacy exclusive. Borderless ignores `[RESOLUTION]` and renders at the monitor's size. |
| `[VIDEO] VSYNC` | `1` (default, **recommended** for play). `0` uncaps the framerate; use it only while measuring. |
| `[VIDEO] MAX_FPS` | Frame cap independent of vsync (`0` = none, 20–1000), for the cases where vsync is off. |
| `[VIDEO] D3D9EX` / `DEPTH24` / `TRANSPARENCY_AA` | `0` forces plain D3D9 / a 16-bit depth buffer / no transparency supersampling. For A/B tests. |
| `[VIDEO] Camera` | View distance from Options: `0` Near, `1` Normal (default), `2` Far. The terrain fades out at 320 m on Normal, 450 m on Far (with the data from `scripts/set-camera-fog.py`). |
| `[VIDEO] TERRAIN_RADIUS` | Default `2` = a 5x5 square of 160 m map chunks kept loaded; `1` = the original 3x3. |
| `[VIDEO] TERRAIN_BATCH` | Default `1` = each chunk's terrain drawn as one batch (a few dozen draws instead of 256). `0` = the original one draw per 10 m tile. Read at startup. |
| `[VIDEO] TERRAIN_EVICT_BUDGET_US` | Default `2000`. Microseconds per frame for removing tiles that left the view; a camera swing used to remove them all in one frame. `0` = uncapped. |
| `[VIDEO] TERRAIN_INSERTS_PER_FRAME` | Default `24`. Distant tiles allowed to appear per frame. With `TERRAIN_BATCH=1` the ground is unaffected and this paces the objects on them. |
| `[VIDEO] TERRAIN_ZONLY` | Default `1`. `0` replaces the depth pre-pass of faded distant tiles with a front-to-back sort. Only matters with `TERRAIN_BATCH=0`. |
| `[VIDEO] LOAD_BUDGET_US` | Default `2000`. Microseconds per frame for pre-loading resources. |
| `[VIDEO] CACHE_WARM_MB` | Default `300`. Archive megabytes pulled into the file cache during each loading screen (3.7x faster texture reads measured). |
| `[VIDEO] FRAME_SPIKE_LOG_MS` / `STREAM_SPIKE_LOG_MS` | `0` (off). Log every frame over N ms total / N ms of resource loading, with the phase breakdown. The frame log also writes one `Frame dip:` line for a run of slow frames. |
| `[LOG] CRASHDUMP` | `normal` / `full` / `off` minidumps on a crash. |

Environment variables override the ini for one-off runs: `ROSE_NO_D3D9EX`,
`ROSE_NO_DEPTH24`, `ROSE_EXCLUSIVE_FULLSCREEN`, `ROSE_RMLUI`, `ROSE_UI2`,
`ROSE_LOG_LEVEL`, `ROSE_CRASHDUMP`.

How the streaming dials interact, what the spike logs mean and how to read a
hitch are in [doc/client-settings.md](doc/client-settings.md).

---

## Server

### Database

The schema lives in `database/migrations/` as ordered `up.sql` / `down.sql`
pairs. For a first-time setup, squash them into one script and import it:

```powershell
# 1. Install PostgreSQL 12+ and create a database, e.g. "rose-next"
scripts/squash-migrations.ps1          # 2. database/rose-next.sql
psql -d rose-next -f database/rose-next.sql   # 3.
```

### Data files

The servers read the game data (STB tables, quest scripts, AI, maps) from the
`data_dir` set in `server.toml`; by default a `data/` folder next to the
executable. The servers cache the tables at startup, so restart them after a
data change.

### Accounts

`just create-account` hashes the password and inserts the row; the third
argument is the access level (`2048` for full GM rights). To do it by hand,
insert into `account` with `email`, `password` (SHA256 + salt) and `salt`;
`scripts/generate-password.py` produces the hash.

### Configuration (`server.toml`)

Read from the executable's directory; a full example is
[doc/server.toml.example](doc/server.toml.example). Log levels run from `0`
(trace) to `5` (off).

```toml
[database]
connection_string = "postgres://postgres:postgres@localhost/rose-next"

[loginserver]
ip = "127.0.0.1"            # the address advertised to clients, not the bind address
port = 29000
server_port = 19000
password = "password"
log_level = 2
log_path = "C:\\rose-next\\server\\logs\\loginserver.log"
minimum_access_level = 1
max_users = 0

[worldserver]
ip = "127.0.0.1"
port = 29100
server_port = 19001
world_name = "1Rose Next"   # a leading number orders the world list
data_dir = "C:\\rose-next\\server\\data"
clanmark_dir = "C:\\rose-next\\server\\data\\clanmark"
log_level = 2
log_path = "C:\\rose-next\\server\\logs\\worldserver.log"

[gameserver]
ip = "127.0.0.1"
port = 29200
server_name = "Channel 1"
data_dir = "C:\\rose-next\\server\\data"
log_level = 2
log_path = "C:\\rose-next\\server\\logs\\gameserver.log"

# Optional gameplay tuning
[game]
base_move_speed = 200
base_attack_power = 70
base_attack_speed = 35
base_hit = 80
base_crit = 0
```

---

## Development notes

- `scripts/ab-build.ps1 stage <name>` / `use <name>` / `toggle` keeps two client
  builds side by side for in-game A/B tests (it swaps the exe and `znzin.dll`
  together).
- `scripts/debug-client-crash.ps1` runs the client under cdb and writes a full
  dump with all thread stacks on a crash; `client.log` flushes per record and
  survives a crash, `error.txt` (the engine log) is buffered and does not.
- `/dps`, `/uistats`, `/perfreset` and the GM debug HUD (`D`) are the in-game
  measurement tools.
- Data under `data/` is not in git, so **a script is the only committed record
  of a data change**: every balance pass, importer and repair carries its
  reasoning in its docstring and is reversible.

---

## Further reading

| Document | Covers |
| --- | --- |
| [CLAUDE.md](CLAUDE.md) | Architecture map, build facts, every subsystem's traps. Start here. |
| [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md) | The 9Ex port, pool rules, present/reset behaviour, rejected approaches. |
| [doc/client-settings.md](doc/client-settings.md) | Fullscreen modes, the frame cap, streaming dials and hitch diagnosis. |
| [doc/rmlui-evaluation.md](doc/rmlui-evaluation.md) | Why RmlUi, what the D3D9 backend implements, the authoring palette, the traps. |
| [doc/combat-display-fix.md](doc/combat-display-fix.md) | Server-authoritative damage presentation. |
| [doc/balance-analysis.md](doc/balance-analysis.md) | The combat formulas, the level gate, and the balance plan the passes implement. |
| [doc/making-monsters.md](doc/making-monsters.md) | Adding a monster: row, model, skills, animation events and AI, as a checklist; an attack players dodge. |
| [doc/making-events.md](doc/making-events.md) | A server-run event: the controller, monsters from code, an NPC ally, the dialog NPC, the quest wrapper, rewards, map placement. |
| [docs/mapgen/PLAN.md](docs/mapgen/PLAN.md), [FORMATS.md](docs/mapgen/FORMATS.md) | The map generator and the binary layout of every map format. |
| [doc/relighting-maps.md](doc/relighting-maps.md) | Baking lightmaps for an existing or edited zone. |
| [doc/zsc-bounding-boxes.md](doc/zsc-bounding-boxes.md) | Why every ZSC bounding box is wrong and how culling works around it. |
| [doc/cerberus-lair-brief.md](doc/cerberus-lair-brief.md) | Design of the server-run Cerberus Lair instance. |
| [doc/data-codecs.md](doc/data-codecs.md) | The `rosefmt` codec package: rules, inventory, how a codec is proven. |
| [doc/rose-next-build.md](doc/rose-next-build.md) | Build sharp edges, including the hand-written thirdparty projects. |

> These files carry a lot of "this looks wrong but is deliberate, here is why"
> context. If something in the codebase seems arbitrary, it is usually explained
> in one of them before it is worth changing.

---

## License and attribution

This project builds on the original iROSE codebase and the discontinued ROSE
Next release, and bundles the 2007 ROSE Online map editor under its own license
(`xadet/rose-online-map-editor/LICENSE`). Please retain the original credits
and respect the licensing of the upstream code if you redistribute or deploy
it.
