# znzin Engine — Performance Investigation

Investigation of `src/engine/` (the "znzin" renderer/scene graph inherited from iROSE), 2026-08-05/06.
Started from the suspicion that the engine issues a lot of unnecessary draw calls.

**Headline result: it does issue some, and they are not what costs frames.** Draw calls measure
~1.07 µs; all rendering together is 6.5 ms of a 26.7 ms crowded frame; the client renders normal play
at **163 fps / 6.1 ms**. The scaling cost in crowds is `CObjectMANAGER::ProcOBJECT()` at **45% of the
frame**, which no rendering change touches.

Four renderer optimizations were written, measured, and **mostly not merged** — they removed 64-80%
of D3D API traffic and moved the frame rate by ~0.1 ms, i.e. nothing. What merged is the
instrumentation that found the real answer, plus one change that removes plainly redundant work.

---

## What is on master

| | |
|---|---|
| **Frame profiler + `Time:`/`Logic:` HUD** | `src/client/system/FrameProfiler.*`. Wall-clock ms per frame phase. This is the tool that located the bottleneck. |
| **Bone matrix batching** | `zz_model::render` uploads a mesh's bone block in one `SetVertexShaderConstantF` instead of one call per bone. Merged as redundancy removal, **not** as a performance fix — it was measured at zero frame-rate effect. |

## What was written and deliberately not merged

Kept on branch `znzin-optimizations` for reference. All were verified correct and tested in-game;
they were dropped because they buy no measured performance and carry non-zero risk, and the project
priority is stability and visual fidelity.

| item | what it did | measured | why dropped |
|---|---|---|---|
| Renderer state cache | filtered redundant `SetRenderState`/`SetTextureStageState`/`SetSamplerState`/`SetTexture` | **74%** of state traffic removed | 0.4% frame time. Adds a permanent rule: every future direct device write must call `invalidate_state_cache()`. Largest subtle-breakage surface of the four. |
| Glow pass early-out | `render_glow` re-walks the whole scene whenever any object glows; skipped non-glowing units before their matrix uploads | one refined item cost **765** constant uploads/frame for **0** extra draws → cut to 92 | Restructures `zz_visible::render_runit`, which runs for every object in every pass. Safe by design and by testing, but not by construction. |
| Vertex shader constant shadow | filtered identical constant uploads | **6%** filter rate in the crowd case it was built for | Hypothesis (meshes of one model sharing a `bone_indices` ordering) was wrong. Adds a memcmp per upload for ~nothing. |
| Engine API-call counters (`Draw:` HUD line) | per-frame `SetRenderState`/`SetTexture`/etc. counts | — | Entangled with the state cache (`filt` is meaningless without filtering). Also: counting API calls is what sent this investigation down the wrong path for four rounds. |

---

## The measurements

All vsync-off (`[VIDEO] VSYNC=0`), Junon, ~400 spawned monsters for the crowd cases.

### Where the time goes

| phase (ms) | empty town (163 fps) | 400 mobs, camera away (44 fps) | 400 mobs, viewed (37 fps) | Δ |
|---|---|---|---|---|
| **total** | **6.1** | 22.3 | **26.7** | **+20.6** |
| **logic** | **0.4** | 12.5 | **13.0** | **+12.6 (61%)** |
| scnupd | 0.5 | 2.8 | 4.0 | +3.5 |
| shadow | 0.2 | 0.9 | 2.4 | +2.2 |
| render | 2.1 | 3.1 | 4.1 | +2.0 |
| ui | 1.2 | 1.3 | 1.4 | +0.2 |
| present | 1.7 | 1.7 | 1.7 | **0.0** |

- **Client logic is 61% of the cost of a crowd**, and it is *not* frustum-gated — looking away moved
  it 13.0 → 12.5 ms.
- **Draw calls are cheap: ~1.07 µs** (`render` = 4.1 ms for 3847 draws).
- **Nothing is GPU-bound.** `present` is flat at 1.7 ms in all three states.

### The bottleneck: `ProcOBJECT`

`Logic: 12.8ms = obj=12.4 terr=0.2 fx=0.0 uiupd=0.0 rest=0.2`

`g_pObjMGR->ProcOBJECT()` is **12.4 ms — 97% of the logic phase and 45% of the whole frame**, at
~31 µs per object per frame with 396 monsters.

`ProcOBJECT` ([object.cpp:939](../src/client/object.cpp#L939)) is a thin walk of `m_CharLIST` calling
`Proc()` on each entry, with **no distance or visibility gate** — every character is fully processed
whether or not it is on screen. The cost is inside `CObjCHAR::Proc()`
([cobjchar.cpp:3501](../src/client/cobjchar.cpp#L3501)): object vibration, `m_EndurancePack.Update()`,
the pending-death and stale-lethal combat backstops, `::inViewfrustum()`, `::getPosition()`,
`ProcMotionFrame()`, `ProcQueuedCommand()`, `ProcTimeOutEffectedSkill()`, and the `ProcCMD_*` state
machine (with a crowd chasing the player, `ProcCMD_MOVE` for every one of them).

**Which of those costs the 31 µs is not attributed. Sub-profile it; do not guess.**

### Does it matter?

Probably not at realistic populations. 400 objects in one courtyard is a constructed case; at ~50 the
same cost is ~1.5 ms. Normal play is 163 fps. Treat crowd performance as a known ceiling rather than
a live problem, unless sieges or clan wars actually reach those counts.

---

## Reading the `Time:` / `Logic:` HUD lines

```
Time:  6.1ms (max 7.0) logic=0.4 scnupd=0.5 shadow=0.2 render=2.1 ui=1.2 present=1.7 oth=0.0
Logic: 0.4ms = obj=0.3 terr=0.0 fx=0.0 uiupd=0.0 rest=0.1
```

| phase | covers |
|---|---|
| `logic` | client game logic: object Proc, AI, network, UI update, terrain streaming |
| `scnupd` | engine scene update: transforms, frustum cull, skeletal animation |
| `shadow` | `beginScene()` — which also runs the entire shadow map pass |
| `render` | `renderScene()` — 3D draw submission |
| `ui` | `Render_GameMENU()` + dev overlays |
| `present` | `endScene()` + `swapBuffers()` |
| `oth` | total minus the above; large means a phase is unbracketed |

Averaged over 30 frames; `max` is the worst single frame in the window. `logic` and `scnupd` are
mutually exclusive, so the phases sum. The `Logic:` line is a breakdown *inside* `logic`, not an
addition to it.

**Interpretation.** `render` high ⇒ CPU-bound submitting draws. `present` high ⇒ GPU or vsync bound —
D3D9 buffers commands, so waiting for the GPU surfaces at Present and fewer draws will not help.
`scnupd` high ⇒ animation and culling. `logic` high ⇒ nothing in this document helps.

**Measure with vsync off.** With it on, presentation quantizes to 60/30/20 and a steady "38 fps" is a
mix of 60 Hz and 30 Hz frames, not a frame time. That hid the first three results.

---

## Open findings (bugs, not optimizations)

Real defects found during the investigation, none of them merged. Ranked by whether they can cause
wrong behaviour rather than by speed.

**1. Morpher double-draw** — [zz_scene_octree.cpp:884-894](../src/engine/src/zz_scene_octree.cpp#L884-L894).
Every sibling branch in `update_distance` `continue`s; the `zz_morpher` branch does not, so morphers
land in `particle_nodes` **and** `visible_nodes`/`delayed_nodes` and are rendered twice. Present since
the initial import — original iROSE code, so retail had it too.

`zz_morpher` backs three things, and the fix's risk differs per case: vertex-animated character parts
(`CCharPART::Load_ZMODEL` with an `m_pMeshAniFILE`, opaque → removing the duplicate is byte-identical),
`CMeshEffect` ([io_effect.cpp:110](../src/client/io_effect.cpp#L110), usually additive → effects get
dimmer, and content was authored against the double blend), and `CObjMorph`.

Do not just add `continue`. The better experiment is *removing* morphers from the particle branch so
they stay in the depth-sorted `delayed_nodes` path — `particle_nodes` renders last, unsorted, after
ocean. Try both and compare a skill effect over water.

**2. Direct `SetTexture` bypasses of the texture-handle cache.** `zz_renderer_d3d` binds textures
straight on the device without touching `_cached.texture[]` in `draw_trail` (runs in normal gameplay),
`draw_sprite`, `draw_shadowmap_viewport`, and the debug draw helpers. Some *other* direct-bind sites
do call `_cached.invalidate_texture(...)`, so the omission looks accidental. Consequence: a later
`set_texture(handle, stage)` can cache-hit on a handle the device no longer has bound and skip a
`SetTexture` that was needed. Fix: add `_cached.invalidate_texture(stage)` next to each direct bind.

**3. Latent landmines** — none bite today.
- `zz_material_state::copy_to` ([zz_material.cpp:55-58](../src/engine/src/zz_material.cpp#L55-L58))
  does `memcpy(this, &dest, ...)`, i.e. it is a second `copy_from`. Declared, defined, never called.
- `set_pixel_shader` caches on the pre-override key: it stores `last_shader_index` *before* the
  `ZZ_RW_SHADOWMAP` branch swaps in the shadow shader, so a later scene-pass call with the same
  original index would cache-hit and leave the shadow-map pixel shader bound. Unreachable while pixel
  shaders are disabled; breaks the moment anyone sets `usePixelShader(1)`. The same branch passes
  `state.current_pass` where `get_pshader` expects a *shader format*.
- `set_light` reads the camera matrix into the `modelview_matrix` member, so `get_modelview_matrix()`
  afterwards returns the camera view rather than the per-object modelview. Not uploaded, so shaders
  stay correct; only the CPU-side getter lies.
- Unguarded `get_bvolume()` deref in the `zz_animatable` branch of `render_shadowmap_objects`
  ([zz_scene_octree.cpp:543-547](../src/engine/src/zz_scene_octree.cpp#L543-L547)). Unreachable —
  nothing calls `setShadowOnOff`.
- `try`/`catch(...)` around `DrawIndexedPrimitive` cannot do what its comment claims: the engine
  builds with the MSBuild default `/EHsc`, which does not catch SEH.

**4. Terrain draw count.** Terrain is ~57-78% of all draws, and the z-only prepass
(`use_zonly_terrain`, on at quality 0-2) draws every blended block twice. Deliberate — it removes
alpha ordering artifacts — but it is the largest single lever on draw count if that ever matters.
Test with a controlled visual A/B around terrain edges and water.

---

## Context: what the engine actually runs

Verified, and load-bearing for anything above.

- **Vertex shaders on, pixel shaders off.** `INIT.LUA` calls `usePixelShader(0)`, and `setShaderFormat`
  only creates a pixel shader when `use_pixel_shader` is set, so every `pshader_handle` is
  `ZZ_HANDLE_NULL` and the **fixed-function pixel pipeline is live**. That is why the ~13
  `SetTextureStageState` calls per draw are real work, not dead code. Do not "optimize them away".
- **Multipass is off.** Forced `false` on any device with `MaxSimultaneousTextures > 3`, i.e. every
  GPU made this century. The double `render_opaque()`/`render_transparent()` in that branch never runs.
- **Glow is on** (`useGlow(1)`), **z-only terrain prepass is on** at quality 0-2.
- **Item grade glow is the glow pass.** `CObjAVT::CreateGradeEffect` sets `ZZ_GLOW_TEXTURE` on the
  existing render unit; it does *not* duplicate the model. One refined item in view arms a full second
  traversal of the scene.
- **Shadows work.** `zz_model`'s constructor sets both `cast_shadow` and `shadow_onoff`, and the
  `get_shadow_onoff()` gate on the `zz_animatable` branch of `render_shadowmap_objects` is exactly
  what stops model *parts* being drawn twice into the shadow map. Don't "fix" that gate.
- **Bone limit is 22.** The skin shaders are `vs.1.1` and declare `c30-95 = bone matrix (max 22)`;
  `30 + 22*3 = 96` is exactly the `c0-c95` file `vs_1_1` guarantees. `ZZ_VSC_MAX_BATCHED_BONES`
  must never exceed this — a batched upload past c95 fails as one call and loses every bone for that
  mesh.

### Checked and found fine

So nobody re-audits them: octree frustum culling and the n-vertex plane test; the `first_render`
static in `zz_terrain_block` (legitimate — `loadTerrainBlockEx` bakes the patch origin into the mesh,
so every block shares one identity transform); `gather_visible` vs `render_children` (no double
render — `zz_visible::render`'s default is `recursive = false`); the shadow-map pass;
`zz_material::_set_texture`'s odd grow loop (lands on exactly `index + 1`).

---

## Method note

The investigation predicted the bottleneck four times — state calls, the glow pass, bone uploads,
then draw calls — and was wrong every time. The common cause: the **first** instrumentation counted
**API calls**, because that was the easy thing to count. Every subsequent decision then optimized the
quantity that was visible instead of the quantity that was slow. Four rounds of correct, verified,
measured work moved the frame rate by ~0.1 ms.

The phase timer should have been built first. Count calls only after timing says calls are the
problem.
