# ROSE Next Classic

ROSE Next Classic is a modernized [ROSE Online](https://en.wikipedia.org/wiki/ROSE_Online)
private server and client, built on the original iROSE C++ codebase. It keeps
the core gameplay intact while modernizing the technical foundation — most
notably porting the old code from MSSQL to **PostgreSQL**.

The original ROSE Next project was discontinued and released so others could
continue it. This repository is one such continuation: an ongoing, actively
maintained fork with bug fixes and enhancements on top of the original release.

There are significant improvements over the original iROSE code. While some bugs
remain, this is one of the most complete ROSE server/client codebases publicly
available. Thanks to everyone who contributed to the original release.

---

## What's been modernized

Beyond the MSSQL → PostgreSQL port, the notable changes from the original code:

**Rendering & platform**
- **Direct3D 9Ex** device (with plain D3D9 fallback). Alt-tab, lock, UAC and RDP
  no longer lose the device, so they no longer trigger a full reload of every
  texture. Consequence for contributors: `D3DPOOL_MANAGED` is illegal and gone —
  see [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md) before touching
  rendering.
- **D3DX9 June 2010 redistributable** — `d3dx9_43.dll` must ship next to the
  client. Without it, text silently fails to render.
- Frame-pacing fix (`timeBeginPeriod(1)`); vsync is the only frame cap.
- Terrain streaming and particle-budget work to cut hitches in busy zones.

**Combat**
- Combat damage is **server-authoritative**. The client presents what the server
  sends and never recalculates live damage. If you touch combat display, read the
  Combat Presentation section of [`src/client/CLAUDE.md`](src/client/CLAUDE.md)
  first — the ordering rules there are subtle and were expensive to get right.

**UI**
- **RmlUi 6.2 + FreeType** — new panels can be authored in HTML/CSS-like files
  (`.rml` / `.rcss`) instead of C++ draw calls. Off by default; see *Client
  settings* below. Scope is deliberately limited to new/custom panels: the retail
  dialogs, chat input and IME stay on the original `tgamectrl` framework.
  Details in [doc/rmlui-evaluation.md](doc/rmlui-evaluation.md).
- Client-side additions: damage meter (`/dps`), monster inspector, item preview
  (alt+click), chat item links (shift+click), overhead NPC quest icons, summon
  control (ctrl+click).

**Tooling**
- Quest editor, NPC shop editor and a VFS browser in [`src/tools/`](src/tools/).
- Python asset scripts in [`scripts/`](scripts/) for importing weapons and adding
  item/skill icons.

---

## What's in here

This is a full client + server stack. Everything is **32-bit (x86) Windows**.

```
Client (C++/D3D9Ex) ←→ LoginServer  ←→  WorldServer  ←→  GameServer
                              ↕              ↕               ↕
                        PostgreSQL DB   Game Data (STB/STL)  Game Data
```

| Component        | Location                | Tech                          |
| ---------------- | ----------------------- | ----------------------------- |
| Game client      | `src/client/`           | C++, Direct3D 9Ex, Win32      |
| Login server     | `src/sho_loginserver/`  | C++ (authentication)          |
| World server     | `src/sho_worldserver/`  | C++ (world/channel mgmt)      |
| Game server      | `src/sho_gameserver/`   | C++ (combat, zones, NPCs)     |
| Shared C++       | `src/common/`, `src/common-server/` | items, quests, sockets |
| Shared Rust      | `src/common-lib/`       | logger, config, FlatBuffers   |
| 3D engine        | `src/engine/`           | Direct3D 9Ex rendering        |
| UI framework     | `src/tgamectrl/`        | custom GUI controls (retail dialogs) |
| UI (new panels)  | `src/client/rmlui/`     | RmlUi — HTML/CSS-like authoring |
| Asset pipeline   | `src/pipeline/`         | Rust asset baking tool        |
| Dev tools        | `src/tools/`            | VFS browser, NPC shop editor, quest editor |

> The build mixes **Rust** (i686-pc-windows-msvc) and **C++** (Visual Studio
> 2019, x86). The Rust crates must be built before the C++ projects.

---

## Quickstart

If you have [`just`](https://github.com/casey/just) installed, the whole flow is:

```powershell
# 1. Build the code (Rust + thirdparty C++ + Rose Next C++)
just build release

# 2. Put your game files in data/, then pack them into the client VFS
#    (data.idx + rose.vfs) -- see Client > Assets below
scripts/pack.ps1

# 3. Create the database (see Database below), then create an account
just create-account you@example.com mypassword

# 4. Start the servers, then run the client
just server-all release
#    Run rosenext.exe with the data.idx + rose.vfs next to it, e.g.:
rosenext.exe --server 127.0.0.1
```

Available `just` recipes:

```
build config=CONFIG         # Build code projects (Rust + C++)
build-all config=CONFIG     # Build all code and assets
build-assets config=CONFIG  # Build game assets
dev-setup                   # Link assets into dev/ for local running

client config=CONFIG        # Start the client
client-auto config=CONFIG   # Start the client and auto-connect using .env
clr / cld                   # Aliases: auto client in release / debug

server-all config=CONFIG    # Start login + world + game servers
loginserver / ls            # Start the login server
worldserver / ws            # Start the world server
gameserver  / gs            # Start the game server

create-account email password access  # Create a game account (access defaults to 0)
```

`CONFIG` is `release` or `debug` and defaults to `release`.

---

## Requirements

- **Rust** with the `stable-i686-pc-windows-msvc` toolchain (the project is 32-bit)
- **Visual Studio 2019** Community or better, with the
  "Desktop development with C++" workload (toolset **v142**)
- **Windows 10 SDK** — the Direct3D 9 core headers come from here, not from the
  vendored DirectX SDK. The vendored copies were deleted precisely so the SDK
  ones (which declare the 9Ex interfaces) win.
- **PostgreSQL 12+**
- **Python 3+** (for scripts)
- **PowerShell 7.0+** (Core)
- **Clang-format** (for code formatting)
- *(Optional but recommended)* [`just`](https://github.com/casey/just)

---

## Build

The project builds in three phases, in this order: **Rust crates → thirdparty
C++ → Rose Next C++**. The `just`/`scripts/build.ps1` flow handles all of this
for you, but the manual steps are below for reference.

### Recommended

```powershell
just build release          # or: scripts/build.ps1 -config release
```

### Manual

**1. Rust** (must use the 32-bit toolchain):

```powershell
cd src/
rustup toolchain install stable-i686-pc-windows-msvc
rustup override set stable-i686-pc-windows-msvc   # sets the override for src/
cargo build --release
```

**2. Thirdparty C++ dependencies:**

```powershell
MSBuild.exe thirdparty.sln -p:Configuration=release;Platform=x86
```

**3. Rose Next C++ projects:**

```powershell
MSBuild.exe rose-next.sln -p:Configuration=release;Platform=x86
```

Output binaries land in `bin/release/` (`rosenext.exe`, `sho_loginserver.exe`,
`sho_worldserver.exe`, `sho_gameserver.exe`, `pipeline.exe`).

> **Always build via the `.sln` files, not individual `.vcxproj` files** — the
> projects rely on solution-level properties that don't resolve standalone.
>
> If you want both debug and release, build each configuration.

---

## Client

### Assets

The client reads its game content from a packed **VFS** (`data.idx` +
`rose.vfs`). You supply the raw game files yourself, or download the original
ROSE Next assets from:

```
https://mega.nz/file/gI1y1AKS#lSIxa7G4sYFL_w9YWYfrMoYgtFWeKpTA1g5GquJbb7Y
```

Place the raw game files into the [`data/`](data/) folder (see
[`data/README.md`](data/README.md) for the expected layout), then pack them into
a client VFS with:

```powershell
scripts/pack.ps1                    # data/  ->  Exes/data.idx + Exes/rose.vfs
scripts/pack.ps1 -out path/to/dir   # write the VFS somewhere else
```

`pack.ps1` packs everything in **`data/`** into a `data.idx` + `rose.vfs` pair
using the `pipeline.exe` tool (built with the rest of the project). The output
defaults to the `Exes/` folder, but you can send it anywhere with `-out` (and
override the source with `-in`).

To run the client, put the resulting `data.idx` and `rose.vfs` **next to
`rosenext.exe`**, wherever you choose to run it from. (`Exes/` is just a
convenient staging folder; there's nothing special about it.)

> **Also required next to `rosenext.exe`:** `znzin.dll`, `triggervfs.dll`,
> `discord_game_sdk.dll` and **`d3dx9_43.dll`**. The build and packaging scripts
> place these for you; if you assemble a run folder by hand and the client exits
> immediately at startup, a missing DLL is the usual cause.

> The separate `assets/` baking pipeline (`build-assets.ps1`, `assets/bake.manifest`)
> is the original asset-authoring workflow and is **not** required for this
> setup — `pack.ps1` packs the `data/` folder directly into the client VFS.

### Running

Run `rosenext.exe` from any folder that contains the packed `data.idx` +
`rose.vfs` (see Assets above):

```powershell
rosenext.exe --server 127.0.0.1
```

The `client` project can also be launched and debugged directly from Visual
Studio. (There is also a `just client` recipe wired to the original `dev/`
asset-baking workflow, but for this setup running the built `rosenext.exe`
alongside a packed VFS is the simpler path.)

### Client settings (`rose-next.ini`)

The client reads `rose-next.ini` from its **working directory** (not necessarily
the exe's folder — if you launch via a shortcut, check its "Start in"). Most of
it is written by the in-game options screen, but a few keys are only settable by
hand and are useful when developing or diagnosing:

| Key | Effect |
| --- | --- |
| `[VIDEO] D3D9EX=0` | Force the legacy D3D9 path instead of 9Ex. For A/B testing. |
| `[VIDEO] VSYNC=0` | Uncap the framerate. Vsync is the **only** frame cap — there is no software limiter. |
| `[VIDEO] RMLUI=1` | Enable the RmlUi UI layer. `/dps` then opens the RmlUi damage meter instead of the legacy one. |
| `[VIDEO] FULLSCREEN=0` | Windowed mode. Note the window is only resizable in windowed mode. |
| `[RESOLUTION] WIDTH` / `HEIGHT` | Client size. |

Two environment variables override the INI, which is handy for one-off runs:

```powershell
$env:ROSE_NO_D3D9EX = "1"   # force plain D3D9
$env:ROSE_RMLUI = "1"       # enable the RmlUi layer
```

> Whichever way you set these, **confirm the result in `client.log`** — both
> paths log which one is live. A toggle that silently does nothing is
> indistinguishable from one that works, and that has cost real debugging time
> here more than once.

### Connecting to a server

```powershell
rosenext.exe --server <IP>
```

**Auto-connect** (requires all servers running and an existing character):

```powershell
rosenext.exe --server 127.0.0.1 --username user --password pass `
  --auto-connect-server 1 --auto-connect-channel 1 --auto-connect-character MyChar
```

---

## Server

### Database

ROSE Next uses **PostgreSQL 12+**. The schema lives in `database/migrations/` as
ordered `up.sql` / `down.sql` migration pairs. For a first-time setup, squash the
migrations into a single script and import it:

```powershell
# 1. Install PostgreSQL 12+ and create a database, e.g. "rose-next"
# 2. Squash all migrations into one script
scripts/squash-migrations.ps1
# 3. Import it
psql -f database/rose-next.sql
```

Then point `server.toml` at your database (see Configuration below).

### Data files

The servers need access to the game data files (STB tables, scripts, etc.). By
default they look in a `data/` folder next to the executable; this is controlled
by `data_dir` in `server.toml`.

> **Note:** The contents of `data/` are not tracked in git — this is where you
> place the game data files for your server.

### Creating an account

The easiest way is the `just` recipe, which hashes the password and inserts the
row for you:

```powershell
just create-account you@example.com mypassword       # normal account
just create-account gm@example.com mypassword 2048   # GM account (RIGHT_MASTER)
```

The third argument is the access level (defaults to `0`). Use `2048` for full GM
rights.

Alternatively, insert into the `accounts` table manually — required fields are
`email`, `password` (SHA256 + salt), and `salt`. `scripts/generate-password.py`
converts a plaintext password to the hashed form.

---

## Development

`just dev-setup` runs `scripts/dev-setup.ps1`, which creates a `dev/` directory
with `dev/client` and `dev/server` subdirectories that symlink to the asset
files. Use these as the working directories when running the client/server.

> **Note:** dev-setup assumes assets have already been baked at least once (see
> `scripts/build-assets.ps1`).

After setup, update `dev/server/server.toml` with your database settings.

---

## Configuration

The servers read their settings from a `server.toml` file in the same directory
as the executable. A full example lives in `doc/server.toml.example`.

Log levels range from `0` (most verbose) to `5` (off):

```
0 - Trace   1 - Debug   2 - Info   3 - Warn   4 - Error   5 - Off
```

Example `server.toml`:

```toml
[database]
connection_string = "postgres://postgres:postgres@localhost/rose-next"

[loginserver]
ip = "127.0.0.1"
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
world_name = "1Rose Next"   # Prepend a number to control world-list ordering
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

# Optional gameplay balance tuning
[game]
base_move_speed = 200
base_attack_power = 70
base_attack_speed = 35
base_hit = 80
base_crit = 0
```

---

## Further reading

Start with [`CLAUDE.md`](CLAUDE.md) — it is the architectural overview and the
map of which subsystem lives where. Per-area notes are in
[`src/client/CLAUDE.md`](src/client/CLAUDE.md) and
[`src/sho_gameserver/CLAUDE.md`](src/sho_gameserver/CLAUDE.md).

Design notes for the larger pieces of work:

| Document | Covers |
| --- | --- |
| [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md) | The 9Ex port, pool rules, present/reset behaviour, rejected approaches. **Read before touching rendering.** |
| [doc/rmlui-evaluation.md](doc/rmlui-evaluation.md) | Why RmlUi, what the D3D9 backend does and does not implement, the authoring palette, and the traps. |
| [doc/combat-display-fix.md](doc/combat-display-fix.md) | Server-authoritative damage presentation. |
| [doc/rose-next-build.md](doc/rose-next-build.md) | Build sharp edges, including the hand-written thirdparty projects. |
| [doc/monster-inspector-ui-spec.md](doc/monster-inspector-ui-spec.md) | The monster inspector panel. |
| [doc/npc-weapon-models.md](doc/npc-weapon-models.md) | NPC weapon model data. |

> These files carry a lot of "this looks wrong but is deliberate, here is why"
> context. If something in the codebase seems arbitrary, it is usually explained
> in one of them before it is worth changing.

---

## License & attribution

This project builds on the original iROSE codebase and the discontinued ROSE
Next release. Please retain the original credits and respect the licensing of the
upstream code if you redistribute or deploy it.
