# CLAUDE.md — Engine (src/engine)

Rendering-device, culling, depth, lightmap and asset-loading rules. Loaded when a
file under src/engine/ is read; the root CLAUDE.md index points here.

### Rendering Device (Direct3D 9Ex)
The client runs on a **Direct3D 9Ex** device when available, falling back to plain D3D9. The payoff is that alt-tab, lock, UAC and RDP no longer lose the device — previously each cost a full invalidate → reset → reload-every-texture-from-VFS cycle that also threw on failure.

Consequences worth knowing before touching rendering:
- **`D3DPOOL_MANAGED` is illegal on a 9Ex device** and has been removed from everything we compile. New resources go in `D3DPOOL_DEFAULT` and must survive `invalidate_device_objects()` / `restore_device_objects()`. `D3DPOOL_SYSTEMMEM` is still legal — use it for anything that genuinely needs `LockRect`, since DEFAULT textures cannot be locked.
- A DEFAULT-pool buffer that is locked every frame needs `D3DUSAGE_DYNAMIC` + `D3DLOCK_DISCARD`; a plain DEFAULT buffer locked per-frame stalls the pipeline. Conversely `D3DLOCK_DISCARD` is illegal on a non-dynamic buffer.
- `S_PRESENT_OCCLUDED` and `S_PRESENT_MODE_CHANGED` are **success** codes — a plain `FAILED()` test misses them. Occlusion must throttle, never reset. An occlusion log line is *not* a black screen: a skipped present leaves the previous front buffer on screen, so the throttle costs a dropped frame, never a blank one.
- Under 9Ex `TestCooperativeLevel()` is deprecated and always returns `S_OK`; use `CheckDeviceState()`, and only after a present returns something unusual.
- **Fullscreen is borderless by default** — a windowed device covering the monitor, so nothing ever hands display-mode ownership back and forth. The legacy exclusive device is `[VIDEO] EXCLUSIVE_FULLSCREEN=1` (or `ROSE_EXCLUSIVE_FULLSCREEN=1`); it produced brief full-screen black flashes mid-game that borderless removes by construction. In borderless the backbuffer is **always** the monitor size — anything else is stretched by D3D and drifts UI hit-testing.
- **`_fill_fullscreen_mode_ex()`'s `D3DDISPLAYMODEEX` must mirror the present parameters exactly** (`Format` = `BackBufferFormat` even though that is `A8R8G8B8`, `RefreshRate` = `FullScreen_RefreshRateInHz` even when that is 0). The runtime cross-checks them and returns `D3DERR_INVALIDCALL` — and the fallback ladder then hands back a working Ex device via plain `CreateDevice`, so **only the log tells you**. Read `error.txt` after any device-creation change.
- Force the legacy path for A/B testing with `[VIDEO] D3D9EX=0` in `rose-next.ini` or `ROSE_NO_D3D9EX=1` in the environment — and confirm via the log, since a toggle that silently does nothing gives a false negative.
- The **depth-stencil format is negotiated at device creation**, not fixed — see "Depth Buffer Precision" below before touching format selection in `initialize()`.

`[VIDEO] VSYNC=0` uncaps the framerate — but vsync is **not** the only limiter. `zz_system::sleep()`, reached from `swapBuffers()`, enforces `max_framerate` (deadline pacing: `::Sleep()` plus a 1 ms spin; 500 fps and up count as no limit). With the default `[VIDEO] MAX_FPS=0` it does not bind, because `data/SCRIPTS/INIT.LUA:39` calls `setFramerateRange(15, 1000)`; `MAX_FPS=N` (honoured only with `VSYNC=0`, logged as `Framerate cap:` in client.log) re-calls it with N after the script. The `src/` default is **60**, so grepping `src/` alone gives the wrong answer — the same trap as `setMipmapLevel` and `setLazyBufferSize`. Note `sleep()` also runs `manager_update()`, the resource amortiser, so the client's `present` phase has never been GPU wait alone: measured, real Present is 0.2–0.4 ms and the rest was the amortiser plus a per-frame yield. Full design notes, the D3DX9 upgrade rationale, the rejected `FLIPEX` work and the remaining gaps are in [doc/d3d9ex-migration.md](../../doc/d3d9ex-migration.md).

### Missing Assets Must Degrade, Not Kill (Engine/VFS)

An asset referenced by the data but absent from the baked `.vfs` used to be **fatal anywhere in the game** — four independent defects sat on that one path, each masking the next. All are fixed; the contract now is *a missing file logs once and the object renders without that part*. Keep it that way:

- **Never pass a string-ish class through `ZZ_LOG`'s varargs.** `zz_slash_converter`'s first and only member is `char _str[ZZ_MAX_STRING]` and its `operator const char*()` applies at ordinary call sites but **not** through `...`, so `ZZ_LOG("%s", converter)` copies the buffer onto the stack and `%s` consumes the first four *characters* as a pointer. Always `.get()`. (`zz_string` survives only by luck — it stores a `char*` first.)
- `zz_vfs_pkg::open` must null-check `fp_` **before** using it. `VOpenFile` legitimately returns NULL for a file in neither a package nor on disk, and `zz_assertf` is compiled out in release.
- A failed load must be **recorded**, not retried. `zz_mesh::load` sets `load_permanently_failed` (and `set_path` only clears it when the path actually changes, since `loadMesh` calls `set_path` on every attempt). `CFileLIST::Get_DATA` sets `tagFileDATA::m_bLoadFailed` — that covers motions and materials, but **not meshes**: `loadMesh` always hands back the spawned node, so `CMeshLIST::Load_FILE` reports success even for a file that will never load. Without the engine-side flag a single missing mesh produced 2.26M retries and a 447 MB `error.txt`.
- `zz_manager` must **drop** a terminally failed node, not re-queue it. `zz_node::is_load_terminally_failed()` (overridden by `zz_mesh`) distinguishes "file is missing" from "not ready yet"; without it `update()` pops and re-pushes the node every frame for the life of the process, `update()` never hits its both-lines-empty early return, and since `push()` does a linear `find()` the cost is O(n²) per update in the number of missing meshes.
- `zz_manager::update`'s entrance loop also bounds failed re-inserts per update. The failure branch re-queues without decrementing `entrance_time_accumulated`, so an unloadable node otherwise spins forever *within a single update*. Note this only became a *hard* freeze once the retry throttling above was fixed — removing accidental throttling can expose a latent spin.

### Cull Bounds Come From Geometry, Not From The ZSC (Client/Engine)

Every ZSC object record caches a model-space AABB, and **it is wrong in every ZSC in
the game**: the exporter scaled only X into world units (cm) and left Y and Z in mesh
units, so the stored box is ~100x too thin and ~100x too short. Retail Junon has it too,
so it is not an import artefact. Muris' canyon walls registered as a 336 m x 4.0 m x
1.1 m ribbon on the ground instead of a 336 m x 396 m x 114 m wall; because fixed
objects live or die with their terrain patch, turning the camera pushed the ribbon out
of the frustum and `RemoveFromScene()` deleted the whole wall mid-screen. 222 of the
1388 map objects in `data/` under-cover by more than 20 m, the worst by 970 m (Junon
Polis' planet vessel).

It only ever mattered because we re-enabled patch culling: the original
`ViewCullingFunc` had its condition commented out around an unconditional `return 0`,
so every patch was "fully inside" every frame and the boxes were inert. *Re-enabling
dead code promotes its inputs from decorative to load-bearing* — the same shape as the
missing-asset retry storm below.

`CMAP_PATCH::MakeAABBFromObject` now asks the engine for the world AABB it already
derives from real mesh min/max (`getVisibleWorldMinMax` -> `CObjFIXED::GetWorldMinMax`),
keeping the cached box only as a fallback. This is safe at map-load time because
`loadMesh` reads the ZMS header min/max **eagerly**, independent of lazy geometry
loading, and `loadVisible` builds each part's OBB before the object is ever inserted
into the scene.

Things that will bite:

- **The client now imports a new engine export, so `rosenext.exe` and `znzin.dll` must
  be deployed as a pair.** A new exe against an old DLL will not start. `scripts/ab-build.ps1`
  exists for this constraint.
- **Mesh units depend on the ZMS version.** `load_mesh_minmax` applies `ZZ_XFORM_IN`
  (x0.01) for version < 7 only, so v7/v8 headers are metres and v6 headers are already
  cm. We ship 11 v6 meshes.
- `ViewCullingFunc` tests only **four** frustum planes (near, far, left, right — never
  top or bottom), so vertical FOV culls nothing and the commented-out z comparisons in
  `CompareSizePath2Obj` are not worth restoring on their own.
- A narrow residual remains: an object that fits inside its patch's 10 m footprint but
  towers over local terrain never gets `ExPatchEnable` and is still culled by a
  terrain-only z. See the doc for why the obvious one-line fix is insufficient.

`scripts/audit-zsc-bounds.py` is the verification tool: read-only by default, it
re-derives the correct box from ZMS headers plus part transforms, proves the axis-scale
signature per file, ranks objects whose geometry escapes their stored box, flags boxes
that are corruptly *over*sized, and reports missing mesh files. Run it after any
`import-*.py` that appends ZSC objects. Full record in
[doc/zsc-bounding-boxes.md](../../doc/zsc-bounding-boxes.md).

### Depth Buffer Precision (Engine)

The device used a **16-bit depth buffer** (`D3DFMT_D16`, hardcoded), and the avatar
camera's near plane is **1 m** against a far plane of 800 m (`LIST_CAMERA` columns 5
and 6, multiplied by 100 into cm by `ApplyCameraOption`, then by `ZZ_SCALE_IN` into
engine metres). Resolvable depth separation is `z^2*(f-n)/(n*f*2^bits)`, which at 16
bits is 3.8 cm at 50 m, 15 cm at 100 m and **61 cm at 200 m** — so any two surfaces
mounted closer than that collapsed into one depth bucket and z-fought. On screen it
read as black bands crawling across a distant object and resolving cleanly as you
walked in, because the loser of the fight is usually an unlit interior or backside.
It did not reproduce in the xadet map editor, which is XNA and defaults to Depth24.

`initialize()` now probes `D24S8 -> D24X8 -> D16` with `CheckDeviceFormat` +
`CheckDepthStencilMatch` and stores the winner in `depthstencil_format`. 24 bits is
256x finer, which pushes the onset past the far plane for any realistic mounting gap.

Things that will bite:

- **The near plane is the whole story; the far plane is nearly irrelevant.** With
  `f >> n` the expression collapses to `z^2/(n*2^bits)`. Pulling the far plane from
  800 m in to 250 m changes precision by 0.4%; raising the near plane from 1 m to 5 m
  improves it 5x. Reach for `n` or for bit depth, never for `f`.
- **Only the log tells you which format you got.** `r_d3d: depth-stencil format = N`
  — 75 is D24S8, 77 is D24X8, 80 is the old D16. A probe that silently falls back
  looks exactly like a fix that did not work. Same trap as the D3D9Ex toggle.
- `DEPTH_STENCIL_FORMAT` is now only the fallback seed. Every consumer reads the
  member `depthstencil_format`, including the offscreen z-surface in
  `restore_device_objects()` — that site used the macro directly and would otherwise
  have silently disagreed with the device.
- The FSAA check validates the sample type against the **backbuffer and the depth
  format**. Checking only the backbuffer was harmless while the depth format was a
  constant and is not once it varies: a mismatch fails device creation outright
  rather than degrading to no FSAA.
- Nothing in the engine touches the stencil buffer (no stencil render states, `Clear`
  never passes `D3DCLEAR_STENCIL`), so D24X8 is as good as D24S8. Both are probed
  because driver support for the two is not identical.
- Force the old buffer for A/B testing with `[VIDEO] DEPTH24=0` in `rose-next.ini` or
  `ROSE_NO_DEPTH24=1` in the environment — and confirm via the log.
- There is **no depth bias anywhere**. `zz_renderer_d3d::set_depthbias` is declared
  and defined but never called, and would not work if it were: D3D9's
  `D3DRS_DEPTHBIAS` takes a float bit-pattern, so its `int` parameter turns any small
  value into a denormal ~= 0. Do not reach for it to paper over a z-fight.

Which assets were affected: the failure needs two mesh parts of one object stacked
closer than the depth resolution. The recognisable family is shop signs, statues, and
the `road01`/`road01top` overlay pairs — that naming convention is the real tell, and
Junon Polis has the most of them.

A one-off scan for part pairs whose world AABBs overlap widely on two axes and by
under 25 cm on the third flagged 70 of the 14618 map objects. **Treat that as
corroboration for why only a few assets showed the artefact, not as a predictor**, and
do not rebuild it as a tool: an AABB overlap is not a surface separation (the boxes of
a concave part overlap where the surfaces do not), the thresholds were chosen to fit
the reported case rather than derived, and at 24 bits the separation that still fights
is under ~0.3 cm at any distance we draw — two orders of magnitude below what that
scan measured. If a similar artefact ever survives the format check in the log,
measure real mesh-surface separation; the AABB proxy will mislead you.

To confirm a suspected z-fight from data alone, with no rebuild:
`scripts/set-camera-near-plane.py` raises the near plane across all six `LIST_CAMERA`
rows (`--dry-run` / `--verify` / `--restore`). Precision scales with `n` but onset
*distance* only with `sqrt(n)`, so 1 -> 5 moves the artefact out ~2.2x rather than
removing it — a partial retreat is the confirming signal, not a weak result. The
follow camera's minimum distance is 1.0 m, so a 5 m near plane clips the avatar at
full zoom-in; that is expected, not a second bug. It writes no sidecar next to the
STB on purpose, since `pack.rs` would bake one into the `.vfs`.

### Object Lightmaps Are A Gutterless Atlas (Engine)

Each map-object *part* gets one cell of a shared lightmap texture — `OBJECT_128_0.DDS`
is a 4x4 grid of 128 px cells — and `SetLightMap` addresses it with a UV transform
applied in the vertex shader (`(uv + cell_xy) / grid_n`). There is **no padding between
cells**, so mipmapping, which knows nothing about the grid, would blend a part's
lighting with its neighbours' at low enough mip levels.

It cannot happen, and the reason is one line of Lua: `data/SCRIPTS/INIT.LUA:33` calls
`setMipmapLevel(3)`, which caps every texture at three mip levels. A 512 px atlas only
ever gets 512 -> 256 -> 128, and across every atlas we ship the smallest cell stays at
8x8 texels — bleeding needs cells under about 2. **Grepping `src/` alone says the
opposite**, because the DDS files do carry full mip chains down to 1x1 and the C++
defaults do not cap anything; this is the same trap as `setLazyBufferSize`.

So: treat `mipmap_level = -1` (load the file's full chain) as unsafe for lightmapped
map objects. `setDisplayQualityLevel` levels 3 and 4 set exactly that — level 5 escapes
it only by disabling lightmaps outright. If it ever did appear it would be a flat,
uniform brightness shift over a whole part with neighbouring parts of the same object
drifting out of agreement, never bands and never a sharp onset; it is also
self-limiting, since the mip level tracks screen size and a cell only falls under a
texel once the part is a couple of pixels across.

### Particle Vertex Buffers Are Rings (Engine, 2026-10-22)

The two shared particle vertex buffers (`zz_particle_emitter::m_vbParticles` / `_point`,
8,192 particles each) are filled through `LockParticleRing` / `UnlockParticleRing`: the next
free span with `D3DLOCK_NOOVERWRITE`, `D3DLOCK_DISCARD` only on wrap, draw from the returned
start vertex. **Never lock them directly with `DISCARD`.** Both particle render paths used to,
once per sequence per emitter (100+ a frame in a town): the particle pass cost ~3 ms for ~50
emitters and occasionally stalled to 7 ms -- the driver running out of buffer renames. As rings:
0.1-0.2 ms median, 0.5 ms worst, ~27 locks and 0-1 discards a frame. The frame spike log's
`rpass[...]` group times every scene pass (`zz_render_pass_slot`) and the particle locks
(`lock=` / `discard=`); start there for any render-phase spike.

**Engine frame statistics are off unless the client's spike log is on.** Every engine-side
timer and counter (render passes, particle locks, particle loads) checks `g_zz_frame_stats`,
which the client sets through `setFrameStatsEnabled` from `FrameProfiler::SetSpikeLogMs`
(i.e. `[VIDEO] FRAME_SPIKE_LOG_MS > 0`). Normal play reads no clocks for them. Gate any new
engine timer the same way -- ungated, its accumulators also never reset and wrap.

### Ground Overlay (Engine, 2026-10-18)

`::setGroundOverlay(xyz_cm, argb, count)` hands the engine translucent triangles to lay on
the terrain (the client's boss telegraphs). It **replaces** one buffer
(`zz_scene_octree::ground_overlay`), so the client sends it every frame and an empty one
when it has nothing; it never grows. `render_cull` draws it after the opaque objects and
before the transparent ones (`draw_ground_overlay` ->
`zz_renderer_d3d::draw_ground_triangles`): fixed function, vertex colours, z-tested, no
z-write, so rocks hide it and characters stand over it. **It used to be drawn right after
the terrain** (until 2026-10-29): with no z-write, every map object drawn later painted
over it, so a shape on a bridge deck vanished under the planks. It is `DrawPrimitiveUP` from
client memory, so nothing lives on the device and a reset has nothing to rebuild. Every
raw render, texture-stage and transform state it changes is read back and restored, so
the renderer's own flags (`state.use_alpha`, `use_fog`) stay true; only the shader
setters go through the renderer, so its cache sees NULL. There is no decal system: the
shape is geometry, sampled on the terrain by the client. Adding the buffer changed
`zz_scene_octree`'s layout (a member of `zz_system`, which the client's dev UI includes):
clean-rebuild `engine` and `client`, and ship `rosenext.exe` + `znzin.dll` together.

### Transparency Antialiasing (Engine, 2026-09-29)

MSAA only smooths polygon edges; an alpha-tested cut-out (fences, leaves, railings)
is decided once per pixel and shimmers at any MSAA level. The client now uses
NVIDIA's D3D9 **transparency supersampling**: `D3DRS_ADAPTIVETESS_Y = 'SSAA'`
while alpha test is on, detected with `CheckDeviceFormat(..., (D3DFORMAT)'SSAA')`.
The alpha test then runs per MSAA sample at the same threshold, so edges get the
MSAA level's smoothing (4x = 5 steps, 8x = 9) and nothing solid changes. The hook
is in `zz_renderer_d3d::enable_alpha_test`, which every alpha-test toggle goes
through. The UI2 options (Graphics > "See-through edges") switch it live (no restart);
with the classic interface it is ini-only: `[VIDEO] TRANSPARENCY_AA` (1 on, the default; 0 off; 2 = NVIDIA alpha-to-coverage,
ini-only, for comparison). `error.txt` logs `r_d3d: transparency AA ...` with the
driver's answer.

Things that will bite:

- **Not alpha-to-coverage, on purpose.** ROSE's ZSC alpha flags are unreliable
  (other ROSE devs hit it adding A2C): 96% of materials are alpha-tested, some
  textures carry a specular mask in alpha, some are blended for no reason. A2C
  turns every alpha value into partial coverage and dithers those; supersampling
  keeps the alpha test's own keep/drop decision, so it needs no exception list.
- **Never inside a sprite batch.** `init_sprite_state` enables alpha test for every
  2D batch, and supersampling reads glyph/icon textures at sub-pixel offsets:
  every piece of classic text blurred. The hook skips it while `_sprite_began`
  (which also covers the inspector/preview panes' mid-batch 3D) and
  `draw_sprite_ex` clears it. UI2 is unaffected: `RoseRmlRenderer::ApplyRenderState`
  disables alpha test for every pass.
- **NVIDIA only, needs MSAA.** AMD/Intel drivers answer no and nothing runs (AMD's
  own D3D9 hack is A2C only); DXVK/Proton do not implement the render state. The
  support probe runs whenever MSAA is on, even with the feature off, so the
  checkbox can switch it live; the mode (1 vs 2) applies on the next device.
- Cost is per-sample shading of alpha-tested 3D pixels only (fixed-function, cheap);
  not measured on low-end NVIDIA yet -- the alpha is the benchmark.
- **A pale outline on foliage that comes and goes with the camera is the transparent
  sort, not the texture** (Haian Coast's hanging vines, 2026-11-04). A texture-alpha
  material on a fogged object is alpha-*blended* with an alpha test of 32
  (`zz_material_colormap.cpp`, "do small alpha test for fogged objects") and drawn in
  the transparent pass, sorted far to near **per part**, each by its own centre
  (`compare_farther_than`). Two overlapping alpha parts of one model (a canopy and a
  vine card) swap order as the camera moves; when the front one draws first, its
  half-transparent edge blends with the sky already in the frame and writes depth, so
  the part behind it is cut out around it. Soft (gradient) alpha makes the edge wide;
  big overlapping canopies show it even with a hard cut-out (bilinear filtering makes
  a one-texel soft edge of any texture).
  **Two-pass alpha parts** (2026-11-04, `zz_renderer_d3d::render`, `[VIDEO]
  FOLIAGE_TWO_PASS`, default 1; `/alphasplit` flips it live for an A/B): such a draw is
  split into its **solid core** (alpha >= 224, depth written) and its **soft edge**
  (the material's own test, no depth write, `D3DCMP_LESS` so the core is not blended
  twice). Correctly sorted, the picture is the same; mis-sorted, the edge loses to
  what is behind it instead of cutting it out. 224 because every visible cut-out in our
  maps keeps its solid texels at 238+ (a scan of the 320; Haian's canopy is 0/239) --
  a texture whose solid level sat under it would stop writing depth altogether.
  Scope: `render_transparent` arms it per node (`zz_arm_foliage_split`) in single-pass
  rendering only. The alpha fog's distance fade rides the blend (`oD0.w` in the object
  shaders multiplies texture alpha), so in the fog band the core threshold is 224 times
  the fade at the node's farthest point (distance + 2 radii; the shader fades by view
  depth, never more), and deep in the fog (fade < 0.25) the node draws the old way. A
  first version split only nodes wholly nearer than the fog start, and Haian's 65 m
  canopies kept their outline beyond ~190 m on Far (fog 260-450 m; Mid 200-320, Near
  90-145, LIST_CAMERA cols 7/8); `render()`
  splits only a plain blended (`ZZ_BT_NORMAL`), alpha-tested (`>=`), depth-writing
  (`LESSEQUAL`) draw of a material not fading (object and material alpha 1), outside
  a sprite batch, **and whose texture is a cut-out, not a translucent surface**.
  **Translucent surfaces** (second round, same day; the test found the split wrong for
  them -- Marsh of Ghosts' crystals, Junon Polis's balloons, the Bone Warden ghost in the
  Desert of the Dead showed what drew after them): every texture is classified once at
  `download_texture`, from its file (`classify_texture_alpha`: the level nearest 128 px,
  DXT3/DXT5/A8R8G8B8/A4R4G4B4 decoded, DXT1/A1R5G5B5 binary): the share of visible texels
  (>= 32) that are half-transparent (< 224) **and more than 4 texels from any clear
  texel**. A cut-out's soft texels are a thin band beside clear ones; a surface's fill
  wide areas. Over 25% = a surface: never split, and `update_object_sort` draws every
  surface node **after** every cut-out (each group far to near) -- the standard order for
  transparency, so a ghost blends over the web behind it. Measured on our 353 alpha map
  textures: foliage 0-8.6%; every surface the test showed through 32% and up (crystals
  32-40%, balloons 38%, the ghost 50-58%; also lights, glass, the Lounge's cones, tables,
  Oro's `or_plantc`). **Junon's street-lamp globe (`STREETLIGHTB`, 23%) must stay split**:
  the lamp's lit "bulb" is the core of its glow effect inside the translucent globe, and a
  globe writing depth hides it (retail's look; a 15% cut-off brought it back). error.txt logs
  each surface once (`r_d3d: translucent surface ...`). Unreadable files stay unsplit in the
  first group. With `/alphasplit` off the order is the old single sort.
  It reads the state the material just applied from file-scope caches
  the setters (`enable_alpha_test` / `_blend`, `enable_zwrite`, `set_zfunc`) keep, so
  there is no class layout change. The 29 genuinely translucent map materials (lights,
  crystals, glass) need no exception: their texels mostly sit under 224, so they draw
  in the edge pass, without depth, as translucency should. Cost: a second draw call
  per split part; the debug HUD's `AlphaSplit: draws=` line counts them per frame.
  Validated in game 2026-11-04 (the user: "it fixes the issue"): at most 130 extra
  draws a frame on an Eldeon map, no change in frame rate on the RTX 5060. It also
  fixed a retail bug nobody had traced: **Junon Polis's street lamps (LIST_DECO_JPT 172)
  showed an unlit bulb** -- the bright core of the lamp's glow effect sits inside the
  head's translucent glass globe (`streetlight01t`, `STREETLIGHTB`, alpha ~200), and the
  globe wrote depth, so the effect drawn after it was hidden inside. (`streetlight01g` is
  the iron scrollwork, a plain cut-out.) Anything that "is sometimes missing behind glass, or
  shows through a solid surface, depending on the camera" is this sort; but a surface
  meant to be solid and stored half-transparent now shows through more often (Junon's
  tents: `set-texture-cutout.py`). The
  data mitigation for a very soft texture (`import-china-rose.py` `cutout_textures`)
  stays useful on its own.
- A full-mip-chain experiment for the same shimmer was reverted: it changed nothing
  visible (8x anisotropic already covered texture minification). Its patch and the
  one useful part -- pinning object lightmaps at 3 mip levels, which quality presets
  3/4 currently break -- are in `build/full-mips/`.
