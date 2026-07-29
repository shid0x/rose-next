# D3D9 → D3D9Ex Migration Roadmap

Living document. **Update the status tables as work lands.** Written so a fresh session can
resume without re-deriving the audit.

## Goal and honest scope

Three separate commits, deliberately ordered by risk:

| Commit | Content | Status |
|---|---|---|
| **1** | Remove `D3DPOOL_MANAGED`; run entirely on `D3DPOOL_DEFAULT` under **ordinary D3D9** | **done — builds clean, validated in-game** |
| 2 | `Direct3DCreate9Ex` / `CreateDeviceEx` / `PresentEx` / `CheckDeviceState` / `ResetEx` | **done — builds clean, validated in-game** |
| 3 | `D3DSWAPEFFECT_FLIPEX` | **not pursued — see below** |

> **Validated on 2026-07-28:** rendering correct, text correct (after the D3DX9 upgrade
> below), clean log, and **alt-tab no longer triggers a device reset** — the headline
> benefit of the whole exercise.
>
> **Not yet exercised**, and worth covering before trusting this in anger: resolution
> change, fullscreen ↔ windowed toggle, MSAA on/off, lock screen, and an extended spell
> minimised (the `S_PRESENT_OCCLUDED` throttle is the newest and least-tested code path).
> `D3DERR_DEVICEREMOVED` remains untested and unhandled beyond a clean abort.
>
> To force the legacy D3D9 path for A/B testing, use **either**:
> - `rose-next.ini` → `[VIDEO]` → `D3D9EX=0`
> - environment → `ROSE_NO_D3D9EX=1`
>
> The log confirms which path is live (`CreateDeviceEx() ok. running D3D9Ex.`), and the
> resource-stats header prints `(D3D9Ex)` or `(D3D9)`. **Always confirm via the log** — a
> toggle that silently does nothing produces a false negative.

### Resolved — text did not render under D3D9Ex (D3DX9 upgrade)

Symptom: no chat, item descriptions, or player names. Everything else rendered; input worked.

**Confirmed** by A/B: forcing `D3D9EX=0` brought text back.

Cause: **D3DX9 was statically linked from the vendored pre-2005 SDK**
(`thirdparty/directx9/lib/d3dx9.lib` is 5.68 MB — a static lib, not an ~80 KB import lib;
`znzin.dll` imports `d3d9.dll` but no `d3dx9_XX.dll`). That D3DX predates D3D9Ex, and
`ID3DXFont` caches glyphs in a **`D3DPOOL_MANAGED`** texture, which a 9Ex device rejects.

Supporting evidence:
- No error dialog appeared, yet `zz_font_d3d.cpp` pops one if `D3DXCreateFontIndirect`
  fails — so creation succeeded and *drawing* is what fails, matching a lazily-created
  glyph cache.
- `ID3DXSprite` still works (UI renders). Sprite uses dynamic vertex buffers, no MANAGED
  texture; font uses a MANAGED texture. Font breaks, sprite does not.
- Commit 1 already moved every resource *we* compile off MANAGED, so the only MANAGED
  allocations left in the process are inside prebuilt third-party D3DX code.

Scope was narrower than it first appeared: our D3DX use is ~95% **math**
(`D3DXVECTOR3`/`D3DXMATRIX`/`D3DXQUATERNION`), which is header-only and allocation-free.
Every other device-touching D3DX call takes an explicit pool *from us*
(`D3DXCreateTextureFromFileInMemoryEx`, `D3DXCreateCubeTexture`). `ID3DXFont` was the only
component allocating a pool internally — so there was no second landmine behind it.

**Fix applied: upgraded D3DX9 from the 2005-era static lib to the June 2010 redist.**

- Source: the **`Microsoft.DXSDK.D3DX` NuGet package** (9.29.952.8, 9.4 MB) — same bits as
  the 572 MB DXSDK installer without the installer. Grab it from
  `https://www.nuget.org/api/v2/package/Microsoft.DXSDK.D3DX/9.29.952.8` (it is a zip).
- `thirdparty/directx9/include/d3dx9*.h` replaced (11 files, 1:1 with the old set).
- `thirdparty/directx9/lib/d3dx9.lib` replaced: **5.68 MB static lib → 86 KB import lib**.
- `thirdparty/directx9/bin/x86/d3dx9_43.dll` added (new, tracked).
- **Zero API churn** — the solution built clean with no source changes.

**New runtime dependency:** `d3dx9_43.dll` must now ship with the client. Verify with
`dumpbin /dependents bin/release/znzin.dll`, which should list `d3dx9_43.dll` alongside
`d3d9.dll`. It is copied automatically by `scripts/post-build.ps1` and bundled by
`scripts/dist.ps1`. **A deploy that forgets it will fail to start**, so any hand-rolled
copy to the run directory needs it too.

Options that were considered and not taken, kept for the record:
- **B** — replace `ID3DXFont` with an in-house GDI glyph rasteriser (SYSTEMMEM surface →
  DEFAULT texture). Would drop the D3DX runtime dependency entirely and leave D3DX as a
  header-only math library. Rejected as much more work with a high-visibility regression
  surface (CJK charsets, kerning) for a benefit the upgrade already delivers.
- **C** — stop at commit 1 and shelve 9Ex.

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

### Commit 3 was rejected — do not re-open without a new reason

**Decided 2026-07-29.** `FLIPEX` was only ever motivated by "windowed mode performs worse
than fullscreen". That symptom had nothing to do with the presentation model: it was the
vsync branch asymmetry (windowed hardcoded `D3DPRESENT_INTERVAL_IMMEDIATE`, so the frame
rate ran unbounded) plus the window-sizing clamp that left the backbuffer stretched into a
short client area. Both are fixed, and windowed performance was confirmed good in game.

So the cost is concrete — drop MSAA, rework the destination-window override (blockers
below, both still true) — and the benefit is now speculative against a problem that no
longer exists. That is the wrong trade.

The one thing that would justify revisiting: wanting **borderless fullscreen** as a
feature. That is genuinely flip-model territory. Drive it from wanting the feature, not
from the performance question, which is closed.

Lesson worth keeping: the sophisticated architectural explanation was wrong and two
mundane bugs accounted for everything. Measure the boring causes first.

### FLIPEX blockers (still true, if it is ever revisited)

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
than fullscreen, and the frame rate ran unbounded there. Both predate this work and are
*not* caused by the pool migration.

Two of the three windowed complaints turned out to have nothing to do with presentation
and are now fixed independently (see git history, both validated in game):

- **Unbounded frame rate** — there is no software frame limiter in the engine at all;
  vsync is the only cap, and the windowed branch hardcoded
  `D3DPRESENT_INTERVAL_IMMEDIATE` while fullscreen honoured `state.use_vsync`. The
  interval is now decided once for both modes. Suspecting this was independent of
  presentation was correct — do not credit `FLIPEX` for it.
- **UI hit-testing drifted in windowed mode** — Windows clamps a `WS_THICKFRAME` window
  to `SM_C*MAXTRACK` and the client had no `WM_GETMINMAXINFO` handler, so a client area
  the size of the desktop silently came back short, D3D stretched the backbuffer to fit,
  and mouse coordinates drifted further off the lower down the screen you clicked.

What remains genuinely open is windowed **performance**, which is still consistent with
BitBlt-model presentation under DWM — what `FLIPEX` addresses, with the
`Present(..., hwnd, ...)` override adding its own cost. That keeps some payoff on the
table for commit 3 but does not remove the MSAA conflict above. Evidence to weigh, not a
plan — and re-measure now that the frame rate is actually capped, since the earlier
"windowed feels worse" impression was formed while it was running unbounded.

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

### Bundled SDK headers lacked 9Ex — resolved in commit 2

The vendored `thirdparty/directx9/include/d3d9.h` was pre-Vista DX9.0c: no `IDirect3D9Ex`,
`Direct3DCreate9Ex`, or `D3DDISPLAYMODEEX`.

**How it was fixed:** `d3d9.h`, `d3d9types.h` and `d3d9caps.h` were *deleted* from the
vendored SDK so the Windows 10 SDK supplies them (`Include\<ver>\shared\`, which has full
9Ex). The rest of the vendored SDK stays, because **D3DX9 is not in the Windows SDK** and
the engine depends on it heavily (`D3DXCreateTextureFromFileInMemoryEx`, `ID3DXSprite`,
`D3DXFilterCubeTexture`, …).

Why deletion rather than reordering include paths: MSVC searches every `/I` path *before*
the system include dirs, so the vendored copy always won regardless of ordering. Removing
it is the only way to let the SDK header through. `d3dx9.h` uses a *quoted*
`#include "d3d9.h"`, which searches its own directory first and then falls through to the
`/I` list — so it now picks up the SDK header cleanly. The build requires a Windows 10 SDK,
which it already did (`WindowsTargetPlatformVersion` is `10.0`).

**Do not hand-declare the COM interfaces** (vtable-order / ABI risk for no gain).

**No linker change was needed.** The vendored `d3d9.lib` predates `Direct3DCreate9Ex`, so
that entry point is resolved with `GetProcAddress` on `d3d9.dll` instead of being imported.
That also provides the graceful fallback to plain D3D9 at no extra cost.

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

## Commit 2 — D3D9Ex device and status semantics

### Design

`IDirect3D9Ex`/`IDirect3DDevice9Ex` derive from the plain interfaces, so `d3d` and
`d3d_device` keep pointing at the same objects and **every existing call site is
unchanged**. Two extra pointers, `d3d_ex` / `d3d_device_ex`, are non-NULL only when the Ex
device exists and are used purely for the Ex-only entry points. They are **borrowed
aliases** — nulled before the real pointer is released, never released themselves.
`is_d3d9ex()` is the single predicate for "are we on 9Ex".

Creation order: `Direct3DCreate9Ex` (dynamically resolved) → `CreateDeviceEx` → on failure,
fall through to the inherited `CreateDevice` → `Direct3DCreate9`. Because a device created
from an Ex factory is Ex-capable even via plain `CreateDevice`, the success path
`QueryInterface`s for `IDirect3DDevice9Ex` and adopts the Ex paths if it is one. Without
that, `TestCooperativeLevel()` would return `S_OK` forever and the legacy reset path would
strand the renderer in a lost state that never clears.

### Status handling

`translate_present_result()` maps a `PresentEx`/`CheckDeviceState` HRESULT onto an intent.
The subtlety worth remembering: `S_PRESENT_OCCLUDED` and `S_PRESENT_MODE_CHANGED` are
**success** codes, so a plain `FAILED()` test misses them entirely.

| Status | Meaning | Action |
|---|---|---|
| `D3D_OK` | fine | carry on |
| `S_PRESENT_OCCLUDED` | minimised / fully obscured | throttle (`Sleep`), **never** reset |
| `S_PRESENT_MODE_CHANGED` | display mode changed | rebuild swapchain |
| `D3DERR_DEVICEHUNG` | driver reset / TDR | `ResetEx` |
| `D3DERR_DEVICEREMOVED` | adapter gone | unrecoverable in place — see below |

`CheckDeviceState()` is consulted **only after a present returns something unusual**, per
Microsoft's guidance, not polled every frame.

`reset_device()` and `wait_device_lost()` branch on `is_d3d9ex()`: the Ex path uses
`CheckDeviceState()` + `ResetEx()`, the legacy path keeps its original
`TestCooperativeLevel()` + `Reset()` logic verbatim so commit 1's validated behaviour is
untouched.

### Deliberately conservative

`ResetEx` still runs the **full** invalidate → reset → restore cycle. 9Ex can preserve more
across a reset than legacy D3D9 does, so this leaves performance on the table — but it is
correct either way, and it keeps commit 2 to one behavioural change at a time. Revisit only
once the Ex path is proven stable.

### Known limitations

- **`D3DERR_DEVICEREMOVED` throws.** Recovering means rebuilding the `IDirect3D9Ex` object
  and every device resource from scratch, which this renderer cannot currently do in place.
  It is logged clearly first. Rare in practice (driver upgrade, adapter change, some
  remoting transitions), but it is a real hole — a genuine fix is its own piece of work.
- **`FLIPEX` is not enabled**, so presentation is unchanged from commit 1. Windowed-mode
  performance is therefore expected to be the same as before; see the commit 3 notes.

### Test matrix for commit 2

Run each twice — once normally, once with `ROSE_NO_D3D9EX=1` — and compare:

- [ ] Confirm the log says `running D3D9Ex` (and `(D3D9Ex)` in resource stats)
- [ ] Fullscreen alt-tab, repeatedly — under 9Ex this should **not** trigger a reset at all
- [ ] Minimise / restore (exercises `S_PRESENT_OCCLUDED`; watch for a CPU spin while hidden)
- [ ] Lock screen (Win+L) and return
- [ ] Resolution change, and fullscreen ↔ windowed toggle
- [ ] MSAA on and off
- [ ] Load a new map immediately after alt-tab
- [ ] Extended idle while minimised, then restore
- [ ] Clean shutdown from both fullscreen and windowed

## Outcome

The migration is complete. Five commits on `d3d9ex-pool-migration`:

| Commit | What |
|---|---|
| `cc87722a` | Commit 1 — retire `D3DPOOL_MANAGED` |
| `23108aed` | Upgrade D3DX9 to the June 2010 redistributable (`d3dx9_43`) |
| `4e9745f2` | Commit 2 — Ex device, present and reset semantics |
| `699aab78` | Fix uncapped frame rate in windowed mode |
| `dff55520` | Fix UI hit-testing drift in windowed mode |

Delivered: no device loss on alt-tab (the goal), the driver's system-memory shadow copy of
every managed resource gone from a 32-bit address space, a modern D3DX, and two
long-standing windowed-mode bugs fixed along the way.

### Still open, in rough priority order

- **`D3DERR_DEVICEREMOVED` is logged and then throws.** Recovering means rebuilding the
  `IDirect3D9Ex` object and every device resource, which the renderer cannot do in place.
  Rare (driver upgrade, adapter change, some remoting transitions) but a real hole.
- **No `WM_SIZE` handler.** Dragging the window's resize frame leaves the engine unaware of
  the new client size, reproducing the same stretch-and-drift that `dff55520` fixed for
  mode switches. Needs a device reset per resize (or on `WM_EXITSIZEMOVE`).
- **Untested paths from the commit 2 matrix:** resolution change, MSAA on/off, lock screen,
  and an extended spell minimised (the `S_PRESENT_OCCLUDED` throttle remains the
  least-exercised new code). Fullscreen ↔ windowed toggling has since been well covered.

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
