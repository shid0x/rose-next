# ZSC object bounding boxes, and the walls that vanished in Muris

**Status:** fixed and validated in-game (2026-08-27). The client no longer reads the
stored box; `scripts/audit-zsc-bounds.py` keeps it honest.

This file supersedes the earlier note *"Muris: walls that vanish depending on the
camera"*, which diagnosed a real but secondary mechanism. What that investigation got
right, and the one thing it missed, are both recorded at the end.

## Symptom

In Muris (zone 71, `MAPS/ORO/TOWN`) a handful of large walls stop drawing depending on
where the camera points. Always the same ones, reproducible from the same angles, with
the wall still filling a large part of the screen when it disappears.

## Root cause: the ZSC bounding box has Y and Z in the wrong unit

Every ZSC object record ends with six floats — a model-space AABB in client units (cm),
read by `CMODEL::Load` into `m_BBMin` / `m_BBMax` ([io_model.h:195-201](../src/client/io_model.h#L195-L201)).
**Those six floats are wrong in every ZSC in the game.** Whatever tool wrote them
scaled only the X axis into world units and left Y and Z in raw mesh units. For a
one-part object with no part offset the relationship is exact:

```
stored.x == mesh.x * 100      <- correct (client cm)
stored.y == mesh.y            <- 100x too small
stored.z == mesh.z            <- 100x too small
```

`scripts/audit-zsc-bounds.py` re-derives this per file rather than assuming it. Every
map-object table reports the same signature — including untouched retail Junon and
Eldeon, so this is a retail-era exporter bug, not something an import did:

```
file                                X        Y        Z   samples
LIST_CNST_JPT.ZSC              1.0000   0.0100   0.0100   1
LIST_DECO_JPT.ZSC              1.0000   0.0100   0.0100   86
LIST_CNST_ODT.ZSC              1.0000   0.0100   0.0100   4
LIST_DECO_EJ.ZSC               1.0000   0.0100   0.0100   63
```

Blast radius across the 21 map-object ZSCs (1388 objects with parts): **222 objects
under-cover their real geometry by more than 20 m horizontally, 62 by more than 50 m.**
The worst object in the game is not in Oro at all — it is Junon Polis' planet vessel,
off by 970 m.

## Why a wrong box deleted a whole wall

Fixed objects hang off a terrain patch (`CMAP_PATCH::m_FixObjLIST`). When a patch is
evicted, `RemoveFromScene()` pulls every object on it out of the engine scene — so a
wall is visible only while its patch is.

A patch is 1000 units (10 m) square, and objects are owned by whichever patch contains
their **pivot** ([`CMAP::AddObject`](../src/client/io_terrain.cpp#L1528)), so almost
every building overhangs its own patch. That is what the "ExPatch" machinery is for:
`MakeAABBFromObject` unions each object's world AABB into `m_AABBMin`/`m_AABBMax`,
`CompareSizePath2Obj` flags a patch whose objects exceed its footprint, and
`SetExPatchTotal` promotes that patch up the quad tree until a node contains it —
or, for something bigger than a whole map cell, into the global list
`m_ExPatchList[85]`, re-tested every frame by
[`ViewCullingFunc2(&m_AABBMin, &m_AABBMax)`](../src/client/io_terrain.cpp#L698).

All of that works. It was being fed a broken box.

The wall under the floating jar in Muris — `31_31.IFO` construction entry #0,
`LIST_CNST_ODT.ZSC` object 14 (`ORT01CLIFF01L.zms` + `OROBJIWA01L.zms`), owned by patch
(7, 11) of map 31_33 — registered as:

```
              x 486554..520138   y 539781..540177   z     0..114     <- 336m x 4.0m x 1.1m
true geometry x 486554..520138   y 520159..559799   z     0..11389   <- 336m x 396m x 114m
```

A 4 m-wide ribbon lying on the ground at y ≈ 539,980, while the part of the wall you
actually look at sits 57 m south of it. Rotating the camera pushes that ribbon out of
the frustum long before the wall leaves the screen, the patch is dropped, and the whole
336 m wall disappears at once.

The tell is that the **pot standing on top of the wall** (`LIST_DECO_ODT` object 31 at
world 511148, 534197, 2800) belongs to patch (15, 6) — a different patch — so it stays
put. A jar floating in mid-air is this bug, unambiguously.

The seven Muris offenders, all `ORT01CLIFF*` canyon walls:

```
mapcell  cnst# zsc  mesh                  real size m   registered as
32_35    0     21   ORT01CLIFF03.zms      614x343x114   614x3.4x1.1
31_31    0     14   ORT01CLIFF01L.zms     336x396x114   336x4.0x1.1
33_31    1     16   ORT01CLIFF01R.zms     336x389x114   336x3.9x1.1
33_34    0     23   ORT01CLIFF0202R.zms   232x319x114   232x3.2x1.1
33_33    0     22   ORT01CLIFF0201R.zms   208x294x114   208x2.9x1.1
31_34    0     18   ORT01CLIFF0202L.zms   227x281x114   227x2.8x1.1
31_33    0     17   ORT01CLIFF0201L.zms   234x276x114   234x2.8x1.1
```

## Why it never showed in iROSE

The original client never culled a patch at all. `iRose Community/Sources/Client/IO_Terrain.cpp`:

```cpp
short CQuadPatchManager::ViewCullingFunc(aabbBox* box)
{
    ///if( g_GameDATA.m_bJustObjectLoadMode )
        return 0;
```

The `if` is commented out, leaving an unconditional `return 0` — "fully inside" for
every box, every frame. `CalculateQuadPatchCulling(0)` therefore always took case 0 at
the root and inserted all 256 patches of every loaded map. The broken boxes were inert
data. Re-enabling the real test (for the FPS reason documented at
[io_terrain.cpp:648](../src/client/io_terrain.cpp#L648)) made them load-bearing for the
first time.

**General lesson: re-enabling dead code promotes its inputs from decorative to
load-bearing.** The same shape as the missing-asset retry storm in CLAUDE.md, where
removing accidental throttling exposed a latent spin.

## The fix: derive bounds from the geometry we actually render

Rather than repair 20-year-old cached boxes and hope, the client now asks the engine
for the world AABB it already builds from real mesh min/max.

One new engine export, next to `setObbox`:

```cpp
ZZ_DLL int getVisibleWorldMinMax(HNODE hVisible, float fMin_Out[3], float fMax_Out[3]);
```

It calls `zz_visible::update_bvolume()` and returns `get_minmax()` scaled by
`ZZ_SCALE_OUT` (engine metres → client cm). `CObjFIXED::GetWorldMinMax()` wraps it for
the object's root node, and `MakeAABBFromObject` prefers it, keeping the old
`m_BBMin`/`TransformOBB2AABB` path as a fallback.

Why this is safe at map-load time, all four verified before writing it:

* **The bounds exist before the geometry does.** `loadMesh` reads the ZMS header
  min/max eagerly via `load_mesh_minmax` ([zz_interface.cpp:523](../src/engine/src/zz_interface.cpp#L523)),
  independent of lazy geometry loading.
* **Every part node has a real OBB immediately.** `loadVisible` sets `ZZ_BV_OBB`
  *before* `add_runit`, and `add_runit` calls `reset_bvolume()`.
* **The transform is current.** `invalidate_transform()` recurses downward, so
  `Add_GndCNST`'s `Scale()`/`Rotate()` on the root dirties every descendant; and
  `Create` → `Scale` → `Rotate` all complete before `CMAP::AddObject` runs.
* **Nothing is inserted into the scene yet.** `update_bvolume`'s internal
  `scene_refresh()` is a no-op while `inscene == false && _onode == NULL`.

The OBB→AABB conversion is a proper 8-corner transform
([zz_bvolume.cpp:216](../src/engine/src/zz_bvolume.cpp#L216)), so the result is tight,
not conservative. Querying the root node alone is complete: a scan of every ZSC found
**zero** `LIST_CNST_*` / `LIST_DECO_*` objects with more than one parentless part (all
1008 multi-root models are character equipment tables, built from `CCharPART` and never
routed through this path) — and `CObjFIXED::InsertToScene` only inserts the root
anyway, so the root subtree *is* the geometry we draw.

One coupled change came with it: the far-insert budget's near/far test in
[`patchmanager.cpp`](../src/client/terrain/patchmanager.cpp#L390) used to measure to
`m_AABBMin`, which sits at `+1e8` for object-less patches and, once bounds became
correct, would be dragged hundreds of metres away by a single large object. It now
measures to the patch's own terrain footprint (`m_aabb`), which is what it always
wanted.

**Deploy note:** the client imports a new engine export, so `rosenext.exe` and
`znzin.dll` must move as a **pair**. A new exe against an old DLL will not start. Use
`scripts/ab-build.ps1`, which exists for exactly this constraint.

## The audit tool

`scripts/audit-zsc-bounds.py` is read-only by default. It re-derives the correct box
from ZMS headers plus part transforms and reports:

* the axis scale signature per file (proves the diagnosis rather than assuming it);
* objects whose real geometry reaches outside the stored box, ranked — this list is a
  **prediction**: nothing on it should pop any more;
* objects whose stored box is far *larger* than the geometry, a separate corruption
  class (see below);
* ZSC parts whose mesh file is missing or unreadable.

`--fix` rewrites the six floats in place (size-preserving, `.bak`, `--restore`,
round-trip verified byte-identical). It is belt-and-braces: nothing in the workspace
reads the field any more. The client uses engine bounds; the xadet map editor parses it
into `ZSC.Object.BoundingBox`, never reads it back (it builds its own boxes via
`ObjectManager.CreateBox`), and has no ZSC save path, so it cannot re-break a repaired
file.

Worth running after any `import-*.py` that appends ZSC objects.

### Traps the script encodes

* **Mesh units depend on the ZMS version.** `load_mesh_minmax` applies `ZZ_XFORM_IN`
  (×0.01) for version < 7 only, so v7/v8 headers are metres and v6 headers are already
  cm. We ship 22 v6 meshes; treating them like v8 is a 100× error.
* **Part transforms chain through parents** — `CFixedPART::LoadVisible` sets a position
  *relative* to the parent. Using only the part's own transform happens to work for the
  cliff objects (identity parents) and is wrong in general.
* **A zero-part object has no effects block and no bounding box.** The record ends at
  the part count. Reading six floats there desynchronises the rest of the file.
* Only map-object tables (`LIST_CNST_*` / `LIST_DECO_*`) have a meaningful signature.
  Equipment tables hang off character bones and never reach patch culling.

## Also found: one genuinely corrupt box

`LIST_CNST_ODT.ZSC` object 7 (the Muris cart group, 9 parts, `ORT01CART.zms`) stores
±10,539,319 units — about 105 km, 105,393 m of slack past its real geometry. Harmless
now that nothing reads the field, but it would pin a patch resident forever for
anything that did. The audit reports it under "stored boxes far LARGER than the
geometry".

## What the earlier investigation got right, and the residual case

The superseded note blamed `pPatch->m_aabb.z`, which comes straight from the `.HIM`
quad block ([io_terrain.cpp:2249-2250](../src/client/io_terrain.cpp#L2249-L2250)) and
is **terrain height only** — nothing expands it to cover the objects standing on the
patch. Two of its findings are worth keeping:

* **Only four frustum planes are tested.** `zz_viewfrustum` orders planes
  `np, fp, lp, rp, tp, bp` and `ViewCullingFunc` loops `i < 4` — near, far, left, right,
  and **never top or bottom**. Vertical FOV culls nothing here; a box's z reaches the
  test only through those four planes' normals, which carry z components because the
  camera is pitched down. This is also why the commented-out z comparisons in
  `CompareSizePath2Obj` are not worth restoring on their own.
* **The far plane is inert.** `CAMERA_MAX_RANGE` is 500 (`LIST_CAMERA.STB` col 4) × 100
  = 50,000 units, while the loaded 3×3 map set spans only ±24,000 from centre. It sits
  outside the loaded terrain and cannot cull within it.

That mechanism is now mostly closed as a side effect: any object exceeding its patch's
10 m footprint — which is nearly all of them, and every object the old note measured —
gets `ExPatchEnable` and is tested against its own, now-correct, box instead of the
terrain box.

**The residual case is narrow but real:** an object that fits *inside* its patch's 10 m
XY footprint while standing far taller than the local terrain never gets
`ExPatchEnable`, so it is still culled by a terrain-only z. Think a tall thin pillar or
lamp post. Restoring the z comparisons in `CompareSizePath2Obj` is *not* sufficient on
its own, because the quad-tree nodes those patches would be promoted into carry
terrain-only z as well. The clean fix is the old note's option 2 — expand each patch's
and quad node's z at load to cover the objects standing on it — which was previously
unwritable because per-object bounds were garbage, and is now straightforward. Not
done; no reproduction case yet.
