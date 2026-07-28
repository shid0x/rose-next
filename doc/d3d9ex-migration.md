# D3D9 → D3D9Ex Migration Roadmap

Living document. **Update the status tables as work lands.** Written so a fresh session can
resume without re-deriving the audit.

## Goal and honest scope

Three separate commits, deliberately ordered by risk:

| Commit | Content | Status |
|---|---|---|
| **1** | Remove `D3DPOOL_MANAGED`; run entirely on `D3DPOOL_DEFAULT` under **ordinary D3D9** | **done — builds clean, validated in-game** |
| 2 | `Direct3DCreate9Ex` / `CreateDeviceEx` / `PresentEx` / `CheckDeviceState` / `ResetEx` | not started |
| 3 | `D3DSWAPEFFECT_FLIPEX` — *investigate only, may be rejected* | not started |

### Expected behaviour change (inherent to commit 1)

Under MANAGED, textures survived a device reset for free. Now **every reset destroys and
reloads every texture from the VFS**, so alt-tab recovery under plain D3D9 gets *slower*,
not faster. That regression is the price of admission and is exactly what commit 2 removes
(a 9Ex device does not lose its resources to alt-tab at all). Judge commit 1 on
correctness and memory, not on reset latency.

**Commit 1 is valuable on its own** and ships on plain D3D9. It drops the driver's
system-memory shadow copy of every managed resource (relevant: the client is 32-bit, 2 GB
user address space) and hardens the existing invalidate/restore path. Do not treat it as
merely a stepping stone.

### What we actually get (do not oversell)

- **Real:** no device loss on alt-tab / lock / UAC / RDP under 9Ex. Today that path does a
  full VFS texture reload and `throw`s on failure ([`zz_renderer_d3d.cpp:3063`](../src/engine/src/zz_renderer_d3d.cpp#L3063)).
- **Real, but measure it:** 32-bit address-space relief. Instrumented in Stage C.
- **Probably unavailable:** flip-model presentation. See "FLIPEX blockers" below.
- **Not a thing:** raw FPS gains. 9Ex is not a performance patch.

### FLIPEX blockers (found in our code, decides commit 3)

`D3DSWAPEFFECT_FLIPEX` conflicts with two live features:

1. **MSAA is a real user setting.** `state.fsaa_type` is validated via
   `CheckDeviceMultiSampleType` ([`:603`](../src/engine/src/zz_renderer_d3d.cpp#L603)) and applied at
   [`:635`](../src/engine/src/zz_renderer_d3d.cpp#L635) / [`:674`](../src/engine/src/zz_renderer_d3d.cpp#L674).
   Multisampling requires `D3DSWAPEFFECT_DISCARD`.
2. **`swap_buffers` overrides the destination window** —
   `Present(NULL, NULL, hwnd, NULL)` at [`:3099`](../src/engine/src/zz_renderer_d3d.cpp#L3099).
   FLIPEX forbids the override.

Commit 3 therefore means "drop MSAA and rework presentation", not "set an enum".

**Observation from commit 1 testing (2026-07-28):** windowed mode performs measurably worse
than fullscreen, and the FPS limiter does not hold correctly in windowed mode. Both predate
this work and are *not* caused by the pool migration. They are, however, the classic
symptoms of BitBlt-model presentation under DWM — which is exactly what `FLIPEX` addresses,
and the `Present(..., hwnd, ...)` destination-window override adds its own windowed-mode
cost. This raises the potential payoff of commit 3, but does not remove the MSAA conflict
above. Treat as evidence to weigh, not as a plan. Worth confirming the FPS-limiter bug is
independent of presentation before assuming flip-model would fix it.

---

## Why this is tractable (audit findings)

The D3D surface area is unusually well-funnelled:

- One `Direct3DCreate9` — [`:161`](../src/engine/src/zz_renderer_d3d.cpp#L161)
- One `CreateDevice` funnel — `_create_device()` [`:493`](../src/engine/src/zz_renderer_d3d.cpp#L493)
- Lost/reset logic in three functions: `wait_device_lost()` [`:2994`](../src/engine/src/zz_renderer_d3d.cpp#L2994),
  `reset_device()` [`:3011`](../src/engine/src/zz_renderer_d3d.cpp#L3011), `swap_buffers()` [`:3097`](../src/engine/src/zz_renderer_d3d.cpp#L3097)

**The decisive finding:** for both textures and meshes, `init_device_objects()` and
`restore_device_objects()` are *identical code* differing only by pool filter
([`zz_texture.cpp:184-242`](../src/engine/src/zz_texture.cpp#L184-L242),
[`zz_mesh.cpp:131-194`](../src/engine/src/zz_mesh.cpp#L131-L194)).

- Textures rebuild via `load_real()` → reloads from VFS, retains no decoded CPU copy.
- Meshes rebuild via `update_vertex_buffer()` with **no** file reload → vertex/index data
  lives in RAM permanently, so static buffers can already restore themselves.

So for the bulk of resources the migration really is a pool flip. Pool is already a
first-class concept: `zz_device_resource::zz_resource_pool`
([`zz_device_resource.h:26-32`](../src/engine/include/zz_device_resource.h#L26-L32)),
values mirror `D3DPOOL`, and `ZZ_POOL_SYSTEMMEM` already exists (still legal under 9Ex).

**Internal renderer resources are already reset-cycle-aware.** `invalidate_device_objects()`
([`:1398`](../src/engine/src/zz_renderer_d3d.cpp#L1398)) already `SAFE_RELEASE`s the managed
ones (`normalization_cubemap`, `sprite_vertexbuffer_*`, `boundingbox_*`,
`shadowmap_overlay_texture`), and they are either recreated in `restore_device_objects()`
or lazily on next use. Flipping them to DEFAULT is behaviourally a no-op.

### Bundled SDK headers lack 9Ex (commit 2 blocker)

`thirdparty/directx9/include/d3d9.h` has no `IDirect3D9Ex`, `Direct3DCreate9Ex`, or
`D3DDISPLAYMODEEX` — pre-Vista DX9.0c. Commit 2 must move to current Windows SDK headers.
**Do not hand-declare the COM interfaces** (vtable-order / ABI risk for no gain).

---

## Commit 1 — work plan

### Stage A — lock-site prerequisites

`D3DPOOL_DEFAULT` textures cannot be `LockRect`ed. Exactly two sites do this. These must be
fixed *before* any pool flip.

| # | Site | Problem | Fix | Status |
|---|---|---|---|---|
| A1 | `create_normalization_cubemap` | MANAGED cubemap filled procedurally via `LockRect` | Fill a `SYSTEMMEM` staging cubemap, `UpdateTexture` into a DEFAULT one | done |
| A2 | `getSpriteTextureColor` [`zz_interface.cpp:7713`](../src/engine/src/zz_interface.cpp#L7713) | `LockRect(D3DLOCK_READONLY)` pixel read-back | **Dead code — zero callers repo-wide.** Guard the lock, fail gracefully | done |

Bulk texture upload is *not* affected: it goes through
`D3DXCreateTextureFromFileInMemoryEx` with pool as a parameter
([`:3789-3798`](../src/engine/src/zz_renderer_d3d.cpp#L3789-L3798)); D3DX stages internally.

### Stage C — instrumentation — **done**

`zz_renderer_d3d::log_resource_stats(const char * phase)` logs:

- cumulative creations per pool for textures / vertex buffers / index buffers
- live counts (`get_num_running()`) for each pool container
- `reset_cycles`, `tex_restore_fail`, `buf_restore_fail`
- committed and reserved address space, walked with `VirtualQuery`
  (kernel32 — deliberately avoids adding a psapi link dependency to the build)

It is called automatically as `"before-reset"` and `"after-reset"` inside `reset_device()`.

**How to read it:** the `MANAGED=` columns must stay **0**. Any non-zero value means a
creation path was missed and commit 2 will fail on it. The committed-bytes delta between a
pre-migration and post-migration run is the real answer to the address-space question —
report that number rather than assuming the full texture footprint was recovered.

### Stage B — pool migration, by category

Convert in this order, testing between each.

| # | Category | Sites | Status |
|---|---|---|---|
| B1 | Internal renderer resources | shadowmap overlay texture, 3 sprite VBs, bounding box VB+IB | done |
| B2 | Textures | `zz_texture.cpp` global default + explicit callers `zz_material.cpp`, `zz_interface.cpp` (`loadTexture`), `zz_sfx.cpp` (ocean) | done |
| B3 | Static vertex/index buffers | `create_vertex_buffer` / `create_index_buffer` static branches + `zz_mesh::set_pool` | done |
| B4 | sfx tile VBs | 4 sites in `zz_screen_sfx::make_tiles1..4` | done |

B2's explicit callers all route through `zz_texture::set_property(..., pool, ...)`, so they
inherit the standard invalidate/restore machinery.

### Findings during implementation (things a plain enum flip would have broken)

1. **Per-draw-locked buffers needed `D3DUSAGE_DYNAMIC`, not just a pool change.** The three
   sprite VBs and the bounding-box VB are `Lock`ed and refilled on *every draw* with flags
   `0`. That is tolerable on MANAGED (the lock hits the system-memory copy) but stalls the
   pipeline on a static DEFAULT buffer. They are now `D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY`
   in `D3DPOOL_DEFAULT`, locked with `D3DLOCK_DISCARD`.
2. **The bounding-box index buffer needed the opposite change.** It was locked with
   `D3DLOCK_DISCARD` — illegal on a non-dynamic buffer. It is written once immediately
   after creation, so it stays static DEFAULT and now locks with flags `0`.
3. **`zz_screen_sfx` was never wired into the reset cycle at all.** It is not a `zz_node`,
   so `zz_system`'s device-object walk never reached it — yet it already owned a
   `D3DPOOL_DEFAULT` render target (`screen_texture`), which would make `Reset()` fail
   outright while a screen effect was playing. `zz_system::invalidate_device_objects()` now
   calls `screen_sfx.clear()`, which releases every resource it owns and resets
   `texture_setup_onoff` so a mid-play effect rebuilds lazily. **This also fixes a latent
   pre-existing bug**, independent of the migration.
4. **Static mesh buffers needed no upload changes.** `update_vertex_buffer` /
   `update_index_buffer` already branch dynamic → `D3DLOCK_DISCARD`, static → flags `0`,
   which is correct for DEFAULT.
5. **Bulk texture upload needed no staging.** It goes through
   `D3DXCreateTextureFromFileInMemoryEx` with pool as a parameter; D3DX stages internally.
   Only the two Stage A lock sites were affected.
6. `zz_pool` exposes `get_num_running()` / `get_num_total()` — there is no `size()`.
   (The `.size()` calls that appear in the file are inside commented-out `ZZ_LOG` lines and
   have never compiled.)
7. `getSpriteTextureColor` was also falling off the end of a non-void function without
   returning. Fixed while guarding it.
8. **Both Stage A sites turned out to be dead code**, which is why A carries no runtime
   risk. `getSpriteTextureColor` has no callers repo-wide, and
   `create_normalization_cubemap`'s only call site is commented out (its consumer
   `set_normalization_cubemap_texture` just returns false on the NULL pointer). They were
   still worth converting: leaving illegal-under-9Ex patterns in the tree means commit 2
   trips over them the moment anything revives that code.

### Test matrix

In-game testing passed on 2026-07-28 and commit 1 was landed on that basis. The individual
boxes below are left unticked because the exact coverage was not recorded — re-run them as
a regression suite before commit 2, since 9Ex changes this path substantially.

Stress the **legacy** reset path — under plain D3D9 every DEFAULT resource must be released
before `Reset`, so this shakes out missing registrations and retained COM refs:

- [ ] Fullscreen alt-tab, repeatedly
- [ ] Lock screen (Win+L) and return
- [ ] Minimize / restore
- [ ] Resolution change
- [ ] Fullscreen ↔ windowed toggle
- [ ] Rapid successive device resets
- [ ] Load a new map immediately after restore
- [ ] Background VFS texture loading *during* device loss
- [ ] Shutdown immediately after a restore
- [ ] MSAA on and off (both `fsaa_type` paths)

---

## Gotchas

- **Build via the .sln, never a bare .vcxproj** (`$(SolutionDir)` / `$(GeneratedDirCommon)`).
  `MSBuild.exe rose-next.sln -p:Configuration=release;Platform=x86`
- Losing MANAGED means losing the runtime's VRAM residency management —
  `D3DERR_OUTOFVIDEOMEMORY` becomes ours to handle. Theoretical on modern GPUs for a
  2004-era working set, but the failure paths should log rather than `throw`.
- `Project 137/` is an old experimental Evolution-era tree. It contains a near-identical
  `zz_renderer_d3d.cpp` and will pollute greps. **Archaeology only — never port from it.**
- `zz_texture` has a `texture_locked` flag (`lock_texture()`) meaning "pinned in the cache",
  **not** a D3D lock. Unrelated to `LockRect`.
