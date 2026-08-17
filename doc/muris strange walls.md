# Muris: walls that vanish depending on the camera

**Status:** diagnosed, not fixed. No code changed.

## Symptom

In Muris (`MAPS/ORO/TOWN`) one particular wall stops drawing depending on where the
camera is pointed. Always the same one, reproducible from the same angles.

## It is not the art, and not the lightmaps

Checked exhaustively against the much later 667 client, whose Muris is effectively
identical to ours:

* Lightmaps are **byte-identical** — all 21 `PlaneLightingMap.dds`, all 69
  `LIGHTMAP/*.dds`, all `.LIT` / `.DAT`.
* Every one of the 474 asset paths our Oro ZSCs reference resolves on disk, as do
  all 2,912 terrain tile references across every map.
* `LIST_CNST_ODT.ZSC` (Muris buildings) is byte-identical; `LIST_DECO_ODT.ZSC`
  differs by exactly one byte, a collision flag.
* Heightmaps match to float epsilon, and our terrain culling bounds are *computed*
  where the 667's are uninitialised `FLT_MAX` sentinels — ours is the better data.

## Cause

Placed objects hang off a terrain patch (`m_FixObjLIST`). When a patch is evicted its
objects leave the scene entirely, so a wall is visible only while its patch is.

Patch visibility is `CQuadPatchManager::ViewCullingFunc(&pPatch->m_aabb)`. That AABB:

* **x / y** — the patch's 1000-unit grid footprint, `io_terrain.cpp:1027-1031`.
* **z** — read straight from the `.HIM` `quad` block, `io_terrain.cpp:2249-2250`.
  That is **terrain height and nothing else**, and it is the only write to
  `m_aabb.z` in the whole client. (Line 1073 looks like a second one but is a read,
  passing the bounds to `loadTerrainBlockEx`.)

Nothing ever expands the box to include the objects standing on the patch. Measured
over Muris's 327 placements, **208 (64%) sit above their own patch's box**. Most
overhang trivially (median 11 units), but there is a cluster at **7,540-7,632** and
one at **9,779** — the `ORT01ISEKI01/02/03` ruins and `OROBJGAREKI01/02` rubble,
75-98 metres above a box whose top is 0-685.

## Which frustum plane actually does it

Worth knowing, because the obvious guess is wrong. `zz_viewfrustum` orders its planes
`np, fp, lp, rp, tp, bp` and `ViewCullingFunc` loops `for (i = 0; i < 4; i++)` — so it
tests **near, far, left, right and never top or bottom**. Vertical FOV culls nothing
here. The box's z reaches the test only through those four planes' *normals*, which
carry z components because the camera is pitched down.

The far plane is inert too: `CAMERA_MAX_RANGE` is 500 (`LIST_CAMERA.STB` col 4) x 100
= 50,000 units, while the loaded 3x3 map set spans only +/-24,000 from centre
(+/-34,000 to a corner). It sits outside the loaded terrain and cannot cull within it.

So the wall pops at the **left/right screen edges**, which is why it reads as
camera-dependent.

## Fixes, cheapest first

1. **Vertical margin on the patch box**, ideally behind `[VIDEO] PATCH_CULL_Z_MARGIN`
   defaulting to 0. Cost is confined to a 2-5 patch band at the left/right frustum
   edges (roughly `|n_z| * margin`), and those extra patches arrive through the
   existing `TERRAIN_INSERTS_PER_FRAME` throttle rather than as a spike. Measure it
   with `sub=` and `resPatch` on the Scene HUD line, margin 0 vs 8000, same spot.
2. **Proper fix:** expand each patch's `m_aabb.z` to cover the objects standing on it
   when the cell loads. Needs a sanity clamp — see below.

## Also found

`LIST_CNST_ODT.ZSC` object 7 (the cart group, 9 parts) carries a bounding box of
**+/-10,539,319 units, about 105 km**. It does not affect patch culling, since object
bboxes are not consulted there, but it is corrupt and would break anything that does
use per-object bounds — including fix 2 above if written naively.
