# Game data

This folder holds the runtime **game data files** the servers and asset
pipeline read (STB tables, scripts, 3D models, shaders, etc.).

> **The contents of this folder are intentionally NOT tracked in git.**
> Only this README and the empty top-level folders (via `.gitkeep`) are
> committed, so the expected structure is visible. Place your own game data
> files into these folders.

The server's `data_dir` (in `server.toml`) should point at this folder.

## Expected layout

```
data/
├── 3DDATA/      # 3D models, motions, effects, STB tables, maps, etc.
│   ├── AI/          AVATAR/      CONTROL/     CUTSCENE/
│   ├── EFFECT/      ELDEON/      ENVOBJ/      EVENT/
│   ├── HELP/        ITEM/        JUNON/       LUNAR/
│   ├── MAPS/        MOTION/      NPC/         PAT/
│   ├── QUESTDATA/   SPECIAL/     STB/         TERRAIN/
│   ├── TITLE/       WEAPON/
├── CAMERAS/     # Camera definition files
├── ETC/         # Misc data (TSI icon atlases, etc.)
├── RES/         # Resource files
├── SCRIPTS/     # Game scripts
├── SHADERS/     # Compiled shaders
└── clanmark/    # Clan mark images (clanmark_dir in server.toml)
```

## Where to get the data

You can supply your own ROSE data, or start from the original ROSE Next
assets. See the **Client → Assets** section of the top-level
[README](../README.md) for the asset download link and baking instructions.
