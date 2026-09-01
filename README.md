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
- **Borderless fullscreen** is now the default, so nothing takes ownership of the
  display mode. The classic exclusive device is still there behind
  `[VIDEO] EXCLUSIVE_FULLSCREEN=1` — see *Client settings* below.
- Frame-pacing fix (`timeBeginPeriod(1)`); vsync is the only frame cap.
- **Map-chunk streaming**: walking across a chunk boundary used to drop frames.
  Chunk loading went from ~14 ms to ~2–3 ms (the VFS read path was doing one OS
  call *per number read*), and the display-time stall — a whole chunk's 256
  terrain tiles built and uploaded in a single frame — is now spread across
  frames. At 60 Hz the remaining dip is 1–2 fps. Tunable, see *Streaming and
  hitch tuning* below.
- Particle-budget work to cut hitches in busy zones.

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
https://mega.nz/file/4QN01ZoQ#yQRSxnOATpFXWXLV35D3cLlCDfJp57YIxIqamlsI_HU
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
| `[VIDEO] VSYNC=0` | Disable vsync. |
| `[VIDEO] MAX_FPS` | Default `0` (no cap). Caps the framerate via the engine's own limiter, independently of vsync. Clamped to 20–1000. **Worth setting even on fast hardware** — see below. |
| `[VIDEO] RMLUI=1` | Enable the RmlUi UI layer. `/dps` then opens the RmlUi damage meter instead of the legacy one. |
| `[VIDEO] FULLSCREEN` | `1` = fullscreen, `0` = windowed. Written by the in-game options screen. Windowed is the only resizable mode. |
| `[VIDEO] EXCLUSIVE_FULLSCREEN` | *Which kind* of fullscreen `FULLSCREEN=1` gives you: `0` (default) = **borderless**, `1` = legacy exclusive. See below. |
| `[RESOLUTION] WIDTH` / `HEIGHT` | Client size. Ignored in borderless, which always renders at the monitor's native size. |
| `[VIDEO] TERRAIN_INSERTS_PER_FRAME` | Default `24`. How many distant terrain tiles may appear per frame. **Lower = smaller hitch, slower horizon fill-in.** See below. |
| `[VIDEO] LOAD_BUDGET_US` | Default `2000`. Microseconds per frame the engine may spend pre-loading resources. `0` = legacy behaviour. See below. |
| `[VIDEO] MAP_PREFETCH` | Default `1`. Background thread that warms the OS file cache for map chunks before they are read. `0` disables it. |
| `[VIDEO] CACHE_WARM_MB` | Default `300`. Megabytes of the archive pulled into the OS file cache during each loading screen, on the same background thread. **Measured 3.7x faster in-game texture reads.** `0` disables it. See below. |
| `[VIDEO] STREAM_SPIKE_LOG_MS` | Default `0` (off). Diagnostic: log a `client.log` line for every frame spending ≥ this many ms loading resources. `4` is a good value when hunting a hitch. |
| `[VIDEO] FRAME_SPIKE_LOG_MS` | Default `0` (off). Diagnostic: log a `client.log` line for every frame whose **total** time is ≥ this many ms, with the phase breakdown. `20` is a good starting value at 60 fps. Catches the hitches `STREAM_SPIKE_LOG_MS` cannot see. |

Three environment variables override the INI, which is handy for one-off runs:

```powershell
$env:ROSE_NO_D3D9EX = "1"            # force plain D3D9
$env:ROSE_RMLUI = "1"                # enable the RmlUi layer
$env:ROSE_EXCLUSIVE_FULLSCREEN = "1" # legacy exclusive fullscreen instead of borderless
```

> Whichever way you set these, **confirm the result in the logs** — every one of
> these paths logs which branch is live (`error.txt` for the renderer and screen
> mode, `client.log` for the client-side ones). A toggle that silently does
> nothing is indistinguishable from one that works, and that has cost real
> debugging time here more than once.

#### Fullscreen: borderless vs exclusive

`FULLSCREEN=1` gives you a **borderless** window by default — a normal windowed
Direct3D device sized to fill the monitor. Nothing ever takes ownership of the
display mode, so alt-tabbing is instant and there is no mode switch to blank the
screen. Two consequences worth knowing:

- `[RESOLUTION] WIDTH`/`HEIGHT` are ignored; the render target always matches the
  monitor. A mismatched backbuffer would be stretched by D3D and would drift the
  UI hit-testing away from what is drawn.
- The window is not topmost, so overlays and other windows behave normally.

`EXCLUSIVE_FULLSCREEN=1` restores the classic exclusive device, which does own
the display mode and does honour `[RESOLUTION]`. It is kept mainly as an A/B
switch: exclusive fullscreen has been observed producing brief full-screen black
flashes mid-session (Windows minimises an exclusive-fullscreen window when the
app is deactivated, and each minimise/restore round trip is a display mode
switch). Borderless has no such mechanism. Details and the current state of that
investigation are in [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md).

Confirm which one you actually got from `error.txt`:

```
screen: mode=borderless size=1920x1080 (ini FULLSCREEN=1, EXCLUSIVE_FULLSCREEN=0)
```

#### Framerate: vsync, `MAX_FPS`, and a claim that did not survive testing

`[VIDEO] MAX_FPS` exists because vsync used to be the only way to limit the
framerate — the engine has its own limiter, but `data/SCRIPTS/INIT.LUA` asks for
a maximum of 1000, which disables it in practice. Use it if you want a cap
without vsync's added latency, or on a VRR display where the usual advice is to
cap a few Hz below refresh.

**It does not appear to affect streaming, contrary to what this section first
claimed.** The theory was that the resource amortiser loads ahead in wall-clock
time, so a higher framerate would leave it less room and force more loading onto
the render path. Measured on the same route, at the same chunk arrival
(`[mesh=7 tex=9]`), warm cache, same log threshold:

| config | frame budget | `flush` (work forced at render) |
| --- | --- | --- |
| uncapped, ~300 fps | 3.3 ms | 3.8 / 4.1 / 5.0 ms |
| capped, 144 fps | 6.9 ms | 4.0 ms |

No difference. The earlier claim rested on a 60 Hz run that appeared to show
`flush` of 0.0–1.5 ms, but **that run used `FRAME_SPIKE_LOG_MS=20` while the
others used 15** — and at 60 Hz a frame carrying 4 ms of flush totals only ~17 ms,
so it was never logged. The comparison was selection bias, not a result.

If you want to settle it, compare with **`STREAM_SPIKE_LOG_MS`** instead: it
triggers on flush time rather than total frame time, so it is independent of
framerate and of whether vsync pins every frame to the refresh interval. A
total-frame-time threshold cannot compare streaming across framerates, which is
the trap above.

The limiter is `::Sleep()`-based (`zz_system::sleep`), so the cap is approximate
and lands slightly *under* the target, and it can only ever add delay — it cannot
rescue a frame that is already late. Confirm it took effect in `client.log`:
`Framerate cap: 144 fps ([VIDEO] MAX_FPS)`.

#### Streaming and hitch tuning

The world is streamed in *chunks* (a 160 m square of terrain, 16×16 tiles). When
you cross a boundary the client loads the next chunk from disk and, shortly
after, has to build and upload its geometry and textures to the GPU. Both used to
stall the frame. The defaults below are the measured-good values; you should only
need to touch them if you are on unusual hardware or chasing a stutter.

**`TERRAIN_INSERTS_PER_FRAME`** (default `24`) is the one worth experimenting
with. It caps how many *distant* terrain tiles are allowed to appear in a single
frame — tiles near you are never delayed, so this can't leave a hole under your
feet. Previously all 256 tiles of an arriving chunk appeared at once, and the
frame that rendered them paid for all of it (12–46 ms).

- **Lower it** (12, 8) for a smaller hitch, at the cost of distant terrain
  filling in more gradually. If you can see terrain popping in at the horizon,
  you have gone too low.
- **`0`** disables the cap entirely, restoring the old behaviour.

What actually costs the time is the *textures* a batch of tiles pulls in (~3 ms
each) rather than the tiles themselves (~0.1 ms each), so this dial has more
effect than its name suggests.

**`LOAD_BUDGET_US`** (default `2000`) is how long per frame the engine may spend
loading resources *ahead* of needing them. The engine estimates load cost with a
rule from 2003 — one millisecond per kilobyte — which badly overestimates modern
hardware, so a 350 KB texture was assumed to need 350 ms and got postponed for
~70 frames, then loaded urgently at the worst possible moment. This paces that
work on actual measured time instead. `0` restores the old behaviour.

**`CACHE_WARM_MB`** (default `300`) is a different trade from the two above: it
spends time you are *already* waiting through. While a loading screen is up, a
background thread reads that zone's asset groups end to end — the zone's own map
directory first, then its terrain tiles, then the shared NPC and motion trees —
so the first in-game access to any of them costs no disk read. Map-chunk requests
always take priority, and the budget is per zone, not per session.

Measured on a cold cache, same five-zone route, warming on versus off:

| | in-game texture read | frames > 20 ms |
| --- | --- | --- |
| on | **0.135–0.142 ms** each | 6 |
| off | 0.503 ms each | 10 |

Two things worth knowing before you tune it. The gain is **entirely in the
streaming that happens while you walk around** — it does nothing for the zone
transition itself (0.475 vs 0.445 ms), because the warm starts at the same moment
that zone's own texture burst does and can never get ahead of it. And measuring
any of this needs a genuinely cold cache: a second run reads from RAM either way,
so an A/B without an eviction step reports "no difference" however well it works.
`just purge-cache` drops the standby list in about a second, which is the part of
a reboot that matters here.

**Diagnosing a hitch.** There are two spike logs, and they answer different
questions. Start with the frame one, because it tells you whether streaming is
even involved.

**`FRAME_SPIKE_LOG_MS`** triggers on *total frame time* and reports where the
milliseconds went. Set it to `20` (a doubled frame at 60 fps) and play; every
frame over the threshold writes one line to `client.log`:

```
Frame spike: 47.3 ms (avg 8.1) | netin=0.3 logic=2.1 scnupd=1.2 shadow=31.9
render=8.4 ui=1.1 present=1.9 oth=0.4 | logic[obj=1.4 terr=0.2 fx=0.3 uiupd=0.1]
| flush=30.2ms/258n [terrain=255 mesh=0 tex=3 mat=0 other=0] | suppressed=0
```

Read it in this order:

1. **Which phase is large?** That is the answer. `netin` = packet drain (a spawn
   burst lands here), `logic` = game logic (split further by the `logic[...]`
   group), `scnupd` = engine transforms/culling/animation, `shadow` =
   `beginScene()` and the shadow-map pass, `render` = draw submission, `present`
   = waiting for the GPU, `oth` = inside the frame but outside every bracket.
2. **Is `flush=` large?** Then it is the streaming path and the two dials above
   apply. If it is small while a phase is large, the hitch is *not* streaming and
   no amount of `TERRAIN_INSERTS_PER_FRAME` tuning will move it. Note `flush=` is
   a whole-frame total spread across `scnupd`, `shadow` and `render`, so it tells
   you streaming was involved without telling you which phase paid for it.
3. **Compare with `avg=`**, the recent frame-time mean. 40 ms against an 8 ms
   average is a hitch; 40 ms against a 35 ms average is just a slow scene, and
   the fix is throughput rather than spike-hunting.

`present` is expected to be large with `VSYNC=1` — that is where the frame cap is
paid — so run with `VSYNC=0` while hunting, otherwise every phase's real cost is
masked by the wait. `suppressed=N` reports spikes dropped by the 250 ms rate
limit, so a burst still tells you its true size. `flush=unsampled` means the
frame never reached the sample point (lost focus, or the scene did not begin) —
it does not mean flushing was free.

**`STREAM_SPIKE_LOG_MS`** is the narrower one: it triggers only on time spent
force-loading resources, so it is silent for any hitch streaming did not cause.
Set it to `4` and it writes:

```
Flush spike: 25.3 ms over 259 nodes [terrain=255 mesh=0 tex=4 ...] leadavg=1 ...
```

`terrain`/`mesh`/`tex` say *what* was loaded, and `leadavg` — how many frames the
work sat waiting before it was needed — says which knob applies: **~1 frame**
means it was needed immediately, so lower `TERRAIN_INSERTS_PER_FRAME`;
**hundreds of frames** means the pre-loader had time and wasted it, so raise
`LOAD_BUDGET_US`.

Turn both back off (`0`) afterwards — they are chatty. Zone-in and the first
frames after a warp are legitimately enormous, so expect a cluster there and
judge steady-state play instead.

The in-game debug HUD (`D` with GM rights) also carries `MapIO:`, `Flush:` and
`Time:` rows for live values, and `/perfreset` re-zeroes the peak counters, which
otherwise saturate during zone-in and then tell you nothing. The HUD's `Time:`
row is a 30-frame average with a `max` column — the frame spike log exists
because a single bad frame survives one window there and is then gone.

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
