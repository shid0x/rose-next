# 3D Data Edits

This file summarizes the manual 3D data changes made to fix water wave rendering issues.

## Breezy Hills

Zone:

- `LIST_ZONE.STB` row `23`
- Display name: `Breezy Hills`
- Map folder: `data/3DDATA/MAPS/JUNON/JG02`

Issue:

- Animated wave morphs on the river/shore caused visible animation glitches when moving the camera with right click or edge pan.
- The animation appeared to fast-forward or reverse depending on camera pitch.

Change:

- Removed the problematic `mulgyul01`, `mulgyul02`, and `mulgyul03` animated morph entries from the JG02 IFO files.
- Restored only the required waterfall/pool water sheet morphs:
  - `water02_JG02_01`
  - `water02_JG02_02`
  - `water02_JG02_03`
  - `water02_JG02_04`

Final JG02 animated morph state:

- Total animated morph entries: `4`
- Remaining morph ids: `20`, `21`, `22`, `23`
- Remaining morphs are the `water02_JG02_*` waterfall/pool sheets.
- No `mulgyul*` wave morphs remain.

## Valley of Luxem Tower

Zone:

- `LIST_ZONE.STB` row `21`
- Display name: `Valley of Luxem Tower`
- Map folder: `data/3DDATA/MAPS/JUNON/JD01`

Issue:

- The map had the same camera-related wave animation glitch as Breezy Hills.

Change:

- Removed all animated morph entries from the JD01 IFO files.
- JD01 only contained `mulgyul01`, `mulgyul02`, and `mulgyul03` animated wave morphs, so there were no `water02_*` waterfall/pool sheet morphs to preserve.

Final JD01 animated morph state:

- Total animated morph entries: `0`

## Shared Water Texture Restoration

During investigation, the shared Junon ocean textures were temporarily flattened:

- `data/3DDATA/JUNON/WATER/OCEAN01_01.DDS`
- through
- `data/3DDATA/JUNON/WATER/OCEAN01_25.DDS`

Those files are shared by Junon water rendering and should not remain flattened.

They were restored from:

- `C:/Users/Thomas/Desktop/rust rose/GameFiles/3DDATA/JUNON/WATER`

Final state:

- The `OCEAN01_01.DDS` through `OCEAN01_25.DDS` files are restored as the original varied 25-frame water texture set.

## Packing

After the data edits, the VFS was rebuilt with:

```powershell
scripts/pack.ps1 -out Exes
```

Generated files:

- `Exes/data.idx`
- `Exes/rose.vfs`

The client must be restarted after packing so it reloads the updated VFS.
