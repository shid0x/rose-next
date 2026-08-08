# CLAUDE.md — ROSE Online Map Editor (xadet)

## What This Is

The legacy **xadet** ROSE Online Map Editor, vendored into the Rose Next Classic
workspace at `xadet/rose-online-map-editor/`. It is a standalone C# desktop app
(circa 2007) used to author ROSE map/zone data — terrain, tiles, objects, NPCs,
monsters, spawns, warps, water, effects, sounds, and event triggers.

Treat it as an isolated subproject. It does **not** share build tooling, language,
or runtime with the rest of the workspace (which is C++/Rust). The only coupling is
the ROSE *data* it reads/writes from `../../data`.

- **UI:** WPF (`.NET Framework 3.5`)
- **Rendering:** XNA 3.1 (DirectX 9 era), hosted inside a WinForms panel in the WPF window
- **Audio:** irrKlang (`irrKlang.NET2.0.dll`)
- **Platform:** x86 only, `WinExe`, root namespace `Map_Editor`
- **License:** Apache 2.0

> **Vendored, not a submodule.** This tree is a copy of upstream
> [jackwakefield/rose-online-map-editor](https://github.com/jackwakefield/rose-online-map-editor)
> at commit `7f0462e`, committed straight into this repo (Apache 2.0, `LICENSE`
> kept). There is no nested `.git` and no upstream remote — our changes can't go
> back upstream, because they target this project's data layout rather than the
> retail-era layout the editor was written for. `git log -- xadet/` splits into
> exactly two commits: the pristine vendor, then everything we changed.

## Build, Deploy, Run

Build with **VS2019 MSBuild, Release, x86**. The `irrKlang.NET2.0.dll` reference is
stale, so point `ReferencePath` at the workspace `data` folder where the DLL lives.

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'Map Editor.sln' /p:Configuration=Release /p:Platform=x86 `
  /p:ReferencePath='C:\Users\Thomas\Desktop\Rose\rose-next-classic\data' /m:1 /v:minimal
```

**Editing source is not enough.** The user runs `../../data/Map Editor.exe`, so after
any change you must rebuild and copy the binaries into `data`:

```powershell
Copy-Item 'Map Editor\bin\x86\Release\Map Editor.exe' '..\..\data\Map Editor.exe' -Force
Copy-Item 'Map Editor\bin\x86\Release\Map Editor.pdb' '..\..\data\Map Editor.pdb' -Force
```

The editor's working directory is `../../data`. ROSE data paths come from the
STB/ZON/ZSC/IFO files and often use legacy mixed-case paths like `3Ddata`.

## Architecture

Entry flow:

1. `App.xaml.cs` — application entry. Installs three global exception handlers
   (WPF dispatcher, AppDomain, WinForms thread) that all funnel to `Output.WriteException`.
   Loads config via `ConfigurationManager`, then creates and shows the `Main` window.
2. `Main.xaml(.cs)` — the WPF main window. Hosts the XNA render surface in a WinForms
   `Panel` (`RenderPanelHost.Child`, exposed as `RenderPanel`). Wires up timers,
   menus, manipulation modes, and the right-side tool panels.
3. `Engine/Main.cs` — the XNA `Game`-style render/update loop driving everything below.

Globals live on `App`: `App.Form` (the WPF window) and `App.Engine` (the XNA engine).

### Engine manager layout (`Map Editor/Engine/`)

The engine is organized into single-responsibility *managers*:

- **`FileManager/`** — binary readers/writers for ROSE formats. This is the
  serialization heart of the editor.
  - `Data/` — `STB` (tables), `STL` (localized strings), `TileSet`, `LTB`
  - `Map/` — `ZON` (zone metadata), `IFO` (per-block objects/NPCs/events/water/collision/sounds), `HIM` (heightmap), `TIL` (tiles), `LIT` (lightmaps), `MOV` (movement/walkability)
  - `Models/` — `ZSC` (object defs), `ZMS` (meshes), `ZMO` (motions)
  - `Character/` — `CHR` (NPC/monster model defs)
  - `FileManager.cs` / `FileHandler.cs` — dispatch + low-level binary IO
- **`MapManager/`** — runtime, in-memory map objects grouped by domain:
  `Terrain/` (Heightmaps, Water), `Objects/` (Decoration, Construction, Animation,
  Collision, EventTriggers), `Characters/` (NPCs, Monsters), `Events/` (SpawnPoints,
  WarpGates), `Misc/` (Effects, Sounds, Sky). `MapManager.cs` orchestrates load/save.
- **`ToolManager/`** — interactive editing tools, one per editable object type,
  mirroring the MapManager domains. `Manipulation/` holds Translate / CursorTranslate
  gizmos; `ITool` is the common interface.
- **`RenderManager/`** — `ObjectManager`, `AnimationManager`, `TextureManager`, and
  debug `Primitives/` (Cone, Cylinder, Sphere, Radius).
- **`ShaderManager/`** — one wrapper class per HLSL effect (Height, HeightEditing,
  Object, NPC, Animation, TileRotation, SimpleColour, SimpleTexture). Compiled
  shaders ship as `.xnb` under `Content/`.
- **`CameraManager/`** — Orthographic + Perspective camera types.
- **`SpriteManager/`** — `FontManager`, `ToolTipManager`.
- **`UndoManager/`** — command pattern. Each editable type has a `Commands/<Type>/`
  folder with `Added` / `Removed` / `Positioned` / `ValueChanged` (and type-specific)
  commands implementing `ICommand`. Touching edit behavior usually means touching
  the matching undo command too.

### UI (`Map Editor/Forms/`)

- `Forms/Controls/*.xaml(.cs)` — the right-side editing panel for each tool
  (BrushTool, HeightTool, TileTool, WaterTool, DecorationTool, ConstructionTool,
  AnimationTool, CollisionTool, EventTriggerTool, NPCTool, MonsterTool,
  SpawnPointTool, WarpGateTool, EffectTool, SoundTool, plus `NPC/PreviewPanel`).
- `Forms/New`, `Forms/Open`, `Forms/Search`, `Forms/Tools/Options` — dialogs.

### Misc (`Map Editor/Misc/`)

- `ConfigurationManager.cs` — loads/validates editor config.
- `Output.cs` — logging. Every output line and all exceptions are mirrored to
  `../../data/Map Editor.log`.

### Editor-local data

- `ESTB/` — editor-only tileset helper STBs (`Table_Tileset_*`, `TileLookup_Type_*`,
  `EDITOR_System_Object.STB`) checked into this project. The editor resolves these
  from **both** `3Ddata\ESTB` (old layout) and root `ESTB` (current Rose Next layout)
  — keep both lookups working.

## ROSE Data File Reference

| Ext | Purpose |
|-----|---------|
| `STB` / `STL` | data tables / localized strings (e.g. `LIST_ZONE.STB`, `LIST_ZONE_S.STL`) |
| `ZON` | zone-level metadata, textures, spawn points |
| `IFO` | per-block objects, NPCs, monsters, events, water, collision, sounds |
| `HIM` / `TIL` | terrain heightmap / tile indices |
| `MOV` | per-tile walkability/movement |
| `LIT` | object/building lightmaps |
| `ZSC` / `ZMS` / `ZMO` | object definitions / meshes / optional motions |
| `CHR` | NPC/monster character model definitions |

## Debugging Workflow

The editor logs to `../../data/Map Editor.log`. When a bug is reported:

1. Read the tail of the log — it records the load *stage* and full stack traces.
2. Trust the logged stage/trace before guessing.
3. Patch source → rebuild Release x86 → copy `.exe` + `.pdb` into `../../data`.
4. Clear the log before asking the user to retest.

```powershell
Get-Content '..\..\data\Map Editor.log' -Tail 200
Clear-Content '..\..\data\Map Editor.log'
```

## Editing Guidelines

- **Keep changes narrow.** This is old UI/tooling code with many implicit
  assumptions; avoid broad refactors and formatting churn.
- **Be tolerant of optional assets.** Log missing optional models/textures/motions
  and continue; only abort a map load when a *required* file is missing. Never
  silently swallow new exceptions in load paths — log context + the full exception.
- **WPF threading.** Updates to WPF controls from background threads must go through
  the dispatcher.
- **Don't touch game data** to fix editor code unless the user explicitly asks.
  Editing `../../data` is a separate concern from editing this source tree.
- **A saved map reaches the two halves of the game differently.** The servers
  read `data/` directly (`server.toml` `data_dir`), so a save applies on their
  next restart; the client reads maps from `rose.vfs` (no loose `3ddata/maps` in
  the launch folder), so the same save needs a VFS re-bake + deploy before it is
  visible in game.
- **Saving is all-or-nothing and has no undo.** `Save_Click` rewrites the ZON,
  every IFO, every TIL, every HIM and all LITs of the loaded map, with no dirty
  tracking and no backup — and `data/` is git-untracked by design. Back up the
  zone folder before an editing session.

## Compatibility Fixes Already Made

- Tileset helper STBs resolve from both `3Ddata\ESTB` and `ESTB`.
- **A bad global table no longer kills startup.** `FileManager.Add` registers an empty
  placeholder for a missing or unreadable STB/STL/ZSC/CHR and continues, and `STB.Load`
  checks the `STB` signature up front so the log names the file instead of dying with a
  bare `EndOfStreamException`. This is what foreign data sets trip over — QQ-iROSE ships
  an *encrypted* `3Ddata\TERRAIN\TILES\ZONETYPEINFO.STB` (editor-only data; the game
  itself never reads that table), which used to abort `Initialize()` and leave every
  later dictionary lookup throwing `KeyNotFoundException`. With no zone-type table,
  `GetTileSetFile` falls through to `InferTileSetFile`, so maps still load and only the
  tile brush palette is empty. The default constructors of STB/STL/ZSC/CHR allocate
  their collections so a placeholder is safe to read.
- Out-of-range object references in an IFO are skipped with a log line rather than
  aborting the map load (`Object.Add` bounds-checks the IFO's object ID against the
  zone ZSC, and each part's model/texture ID against the ZSC's lists).
- Map loading wrapped in stage-specific exception logging with UI unfreeze on failure.
- `Output` mirrors every line and full exceptions to `Map Editor.log`.
- Heightmap fallback allocates `ShadowMapRaw` before filling a default shadow texture.
- Empty/missing optional ZSC motion paths are skipped instead of opening an empty `.ZMO`.
- Monster list remove guards against `SelectedIndex == -1`.
- Tactical monster removal uses `TacticalList.SelectedIndex` (not `BasicList`).
- Newly added monster row is auto-selected after adding to a spawn.

## Verification Checklist

**Map-load fixes:** open a Junon and a non-Junon map; confirm the log reaches
`Loading Completed`; review missing-optional-asset logs for acceptability.

**Monster/spawn UI:** select a spawn; add+delete a Basic monster; add+delete a
Tactical monster; try Delete/Remove with nothing selected (must not throw); test
undo/redo if undo commands were touched.

**Object/terrain:** open a map with decoration + construction; pan to force
rendering; confirm load finishes and the UI unfreezes after any failure.
