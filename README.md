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

## What's in here

This is a full client + server stack. Everything is **32-bit (x86) Windows**.

```
Client (C++/DX9)  ←→  LoginServer  ←→  WorldServer  ←→  GameServer
                              ↕              ↕               ↕
                        PostgreSQL DB   Game Data (STB/STL)  Game Data
```

| Component        | Location                | Tech                          |
| ---------------- | ----------------------- | ----------------------------- |
| Game client      | `src/client/`           | C++, DirectX 9, Win32         |
| Login server     | `src/sho_loginserver/`  | C++ (authentication)          |
| World server     | `src/sho_worldserver/`  | C++ (world/channel mgmt)      |
| Game server      | `src/sho_gameserver/`   | C++ (combat, zones, NPCs)     |
| Shared C++       | `src/common/`, `src/common-server/` | items, quests, sockets |
| Shared Rust      | `src/common-lib/`       | logger, config, FlatBuffers   |
| 3D engine        | `src/engine/`           | DirectX 9 rendering           |
| UI framework     | `src/tgamectrl/`        | custom GUI controls           |
| Asset pipeline   | `src/pipeline/`         | Rust asset baking tool        |
| Dev tools        | `src/tools/`            | VFS browser, NPC shop editor  |

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
  "Desktop development with C++" workload
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
https://mega.nz/file/NNNjgZhR#nalV3n7ZLRz44oBhiY8qyA-kn2llt5Rn3SMtsg8gxqU
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

## License & attribution

This project builds on the original iROSE codebase and the discontinued ROSE
Next release. Please retain the original credits and respect the licensing of the
upstream code if you redistribute or deploy it.
