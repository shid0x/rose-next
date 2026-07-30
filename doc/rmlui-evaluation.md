# RmlUi Evaluation — Modern UI Layer for Custom Client Panels

**Status:** evaluation only, nothing implemented.
**Question asked:** should the custom overlay panels (Monster Inspector, Damage Meter, Item Preview,
Summon Info) be moved off direct sprite drawing onto RmlUi (HTML/CSS-like UI)?

**Short answer:** yes, but **only as a parallel overlay layer for new/custom UI**, never as a
replacement for `tgamectrl` and never for chat input. The integration is real work — D3D9 has no
official RmlUi backend and the two 3D-preview panes fight the document model — but it is bounded,
and the client already contains a working precedent for a third-party UI renderer on this device
(the ImGui dev UI).

---

## 1. What we actually have today

One correction up front, because it changes the risk picture: **none of this is DirectDraw.** There
is no DirectDraw in the client at all. There are two separate UI stacks, plus the debug one:

| Stack | Used by | Size | Styling story |
|---|---|---|---|
| `tgamectrl` + XML dialogs | all retail UI (inventory, chat, skills, stores, …) | ~16k LOC lib, 59 dialog classes, 56 XML files, ~51k LOC in `src/client/interface/` | XML layout + TSI atlas sprites; rigid but data-driven |
| Direct sprite draw | Monster Inspector, Damage Meter, Item Preview, Summon Info | ~1.7k LOC across 4 panels + `OverlayPanelUtil` | **hard-coded C++ constants** — this is the pain |
| Dear ImGui 1.77 | dev windows, `_DEBUG` only | `interface/dev/` | immediate-mode |

The custom panels draw through the engine's D3DX sprite/font layer:
`::beginSprite(D3DXSPRITE_ALPHABLEND)` → `::drawSprite` / `::drawFont` → `::endSprite`, inside
`CGameStateMain::Render_GameMENU` ([cgamestatemain.cpp:258](src/client/system/cgamestatemain.cpp#L258)).
Backgrounds come from the game's own atlas (`ID_BLACK_PANEL` with a hand-cropped source rect to dodge
the baked gradient — see [OverlayPanelUtil.cpp:24-61](src/client/interface/OverlayPanelUtil.cpp#L24-L61)).

So the complaint is accurate but the cause is narrower than "DirectDraw": these four panels
deliberately bypassed *both* existing frameworks to avoid XML plumbing, and now pay for it in
hand-rolled layout, hand-rolled hit-testing, and hand-rolled text measurement.

**Concrete symptoms of the current approach** (all present in the code today):

- Layout constants are literal ints in anonymous namespaces — `kPanelW = 252`, `kRowH = 16`,
  `kBtnW = 18` ([CDamageMeterPanel.cpp:16-41](src/client/interface/CDamageMeterPanel.cpp#L16-L41)).
  Every visual tweak is a recompile.
- Input routing is a **manually ordered chain** in `ProcWndMsgInstant`
  ([cgamestatemain.cpp:1228-1271](src/client/system/cgamestatemain.cpp#L1228-L1271)): ItemPreview →
  MonsterInspector → DamageMeter → SummonPanel, replicated across `WM_LBUTTONDOWN` / `WM_LBUTTONUP` /
  `WM_MOUSEMOVE`, and the order must be kept in sync with draw order by hand. Every new panel adds
  three more hooks and one more ordering constraint.
- Drag is reimplemented per panel. Z-order is "whatever order `CUIMediator::Draw` calls them in".
- Text wrapping / mixed-color runs are hand-computed — the chat item-link listbox precomputes pixel
  extents at append time specifically because per-frame measurement was too expensive.
- No scrolling, no clipping, no focus model, no reusable widgets.

## 2. Is RmlUi technically viable here?

Yes. Here is every blocker found, with severity.

### 2.1 No official D3D9 backend — **medium, unavoidable**

RmlUi ships backends for OpenGL 2/3, Vulkan and DirectX 11/12; **D3D9 is not among them** (its
predecessor libRocket did have a D3D9 sample). We would write `Rml::RenderInterface` ourselves:
textured/untextured triangle lists, scissor rect, texture load/generate/release, and optionally
`SetTransform`.

This is genuinely small — roughly 400–600 lines — and we have a working template in-tree:
[thirdparty/imgui-1.77/examples/imgui_impl_dx9.cpp](thirdparty/imgui-1.77/examples/imgui_impl_dx9.cpp)
does exactly this shape (dynamic VB/IB, FVF, fixed-function texture stages) and runs on our 9Ex
device today. Note the RmlUi render-interface API changed shape in 6.0 (compiled geometry became
mandatory); pin a version before writing it.

### 2.2 `D3DPOOL_MANAGED` is illegal on 9Ex — **medium, well-understood**

Per [doc/d3d9ex-migration.md](doc/d3d9ex-migration.md), every GPU-visible resource must be
`D3DPOOL_DEFAULT` or `D3DPOOL_SYSTEMMEM`, and must survive `invalidate_device_objects()` /
`restore_device_objects()`.

**`D3DPOOL_DEFAULT` does not imply `D3DUSAGE_DYNAMIC`** — an earlier draft compressed this and was
too broad. The correct split, consistent with what [CLAUDE.md](CLAUDE.md) already states (*"a plain
DEFAULT buffer locked per-frame stalls the pipeline… conversely `D3DLOCK_DISCARD` is illegal on a
non-dynamic buffer"*):

| Resource | Pool | Usage | Recreate strategy |
|---|---|---|---|
| Streaming UI vertex/index buffer | DEFAULT | `DYNAMIC` + `DISCARD`/`NOOVERWRITE` | recreate empty |
| Static UI texture | DEFAULT | *none* | reload from source |
| Generated font atlas | DEFAULT | *none* | regenerate via RmlUi |
| 3D preview render target (if used) | DEFAULT | `RENDERTARGET` | recreate + matching depth-stencil |

The ImGui backend is the in-tree precedent for the first row. The extra work for RmlUi is that its
texture handles persist across frames, so the render interface must participate in the engine's
invalidate/restore cycle:

```cpp
void OnBeforeDeviceRebuild() { Rml::ReleaseTextures(); renderer.ReleaseDeviceObjects(); }
void OnAfterDeviceRebuild()  { renderer.CreateDeviceObjects(); /* textures reload lazily */ }
```

`Rml::ReleaseTextures()` forces all in-use textures to reload. **Do not assume compiled geometry
handles recreate themselves** — that depends on our own ownership model, so test it explicitly.

**This is not theoretical.** 9Ex removed the *alt-tab* reset, but a windowed frame drag still
rebuilds the device (`CApplication::ApplyWindowedClientResize` on `WM_EXITSIZEMOVE`). The
restore path is Spike 1 work, not deferred.

### 2.3 Fonts — **low; not actually a fork in the road**

Vendor **FreeType**. An earlier draft framed this as a genuine choice; it isn't.

`Rml::FontEngineInterface` is not a "draw this string" hook — an implementation must resolve faces by
family/weight/size, return metrics, measure UTF-8 strings, prepare font effects, and **generate
textured geometry** for strings. `ID3DXFont::DrawText` does not fit that contract in any small way,
and the existing fonts are a fixed preloaded enum ([game.h:13-29](src/client/game.h#L13-L29)) that
cannot scale arbitrarily.

FreeType gives arbitrary sizes, correct measurement, dynamic glyph atlases, weights, fallback fonts,
and a path to CJK coverage. Vendor it as a static `thirdparty/` lib with a hand-written `.vcxproj`
like every other dep here, and move on.

### 2.4 String encoding — **medium, and messier than it looks**

The client is `CharacterSet=MultiByte`; **RmlUi is UTF-8 everywhere** (`Rml::String` is
`std::string`). Conversion itself is routine. The problem is deciding *what to convert from*.

There is already a localisation layer — [localizing.cpp](src/client/util/localizing.cpp) — with
`UTF8ToMBCS`, charset→codepage mapping, and `GetCurrentCodePage()`. But look at the source of truth:

```
GetCurrentCodePage() → GetCodePageFromLang(GetCurrentLanguageID()) → GetKeyboardLayout(0)
```

**The codepage is derived from the player's keyboard layout, not from the data files.** The same
STB/STL bytes therefore decode differently depending on the user's OS locale. Today this mostly hides
because decoding and rendering are wrong together; feed it into RmlUi — which demands one true UTF-8
answer — and a French player and a Korean player get different results from identical data. There is
a standing comment at [cgamestatemain.cpp:290](src/client/system/cgamestatemain.cpp#L290) that
`drawFontf` handles English only in this build.

So the task is **not** "add a conversion helper". It is: establish the actual encoding of the data
tables as a fixed fact, independent of runtime locale. Worth doing regardless of RmlUi. Then
centralise:

```cpp
Rml::String RoseToUtf8(std::string_view src, UINT src_codepage);
std::string Utf8ToRose(std::string_view src, UINT dst_codepage);
```

Do **not** default to `CP_ACP`. Test with real game strings: English, accented French, Korean item
name, Japanese NPC name, Traditional Chinese description. Low complexity, high embarrassment
potential.

### 2.5 RTTI is disabled — **low, one build flag**

`<RuntimeTypeInfo>false</RuntimeTypeInfo>` in the client project. RmlUi provides a custom-RTTI build
option (`RMLUI_CUSTOM_RTTI`) for exactly this; set it and verify it still holds in whichever version
gets vendored. Enabling RTTI project-wide instead would be a much larger blast radius — don't.

### 2.6 Build integration — **low**

C++17 / v142 / x86 / `/MT` static runtime. RmlUi needs C++14+, so no problem, but it is CMake-based
while every dep here is a hand-written `.vcxproj` checked into `thirdparty/`. Follow the local
convention: generate once, commit a `.vcxproj`, match `/MT` and x86. Do not introduce CMake into the
build for one library.

### 2.7 The 3D preview panes — **the one item that could reveal an incompatibility**

> **Revised after dev review.** An earlier draft of this section claimed RmlUi has no concept of a
> 3D hole and proposed a two-pass "render, query rect, render again" workaround. That is true at the
> RCSS level but **wrong at the C++ level**, and the correction materially lowers the risk.

Monster Inspector and Item Preview do not draw a picture of a model — they punch a hole in the sprite
stream and run a **live engine render** through it:

```
::flushSprite()            // push already-issued panel sprites underneath
::setAvatarViewPort(pane)  // pane-scoped viewport, z-clear, own ortho camera
::RenderSelectedAvatar(h)  // engine animates + renders the puppet
::setDefaultViewPort()
```

RmlUi's intended extension point for this is a **custom element overriding `Element::OnRender()`**,
which runs after the element's own background/border/decorators and *before* its children — almost
exactly the ordering needed:

```
RmlUi pane background  →  OnRender(): 3D model  →  RmlUi children (frame lip, overlay)
```

So the element is authored normally and the document is never split:

```html
<rose-model-preview id="monster-preview">
    <div class="preview-overlay"></div>
</rose-model-preview>
```

**The real risk is therefore state discipline in our own backend, not RmlUi's architecture** — does
it submit geometry immediately or batch it; does it leave scissor/shader/blend/texture-stage state
active; does it survive the avatar renderer changing viewport and engine state; can it resume
afterwards. Since we write the backend, all of that is under our control. **Keep the first backend
synchronous and simple**; batching is a later optimisation.

Two gaps to design for explicitly:

- **Clipping vs. viewport.** `setAvatarViewPort` sets a D3D *viewport* and does its own z-clear — it
  will ignore RmlUi's scissor region. A preview element inside a scrolling container would render
  outside its clip. Intersect the pane rect with the active scissor region before setting the
  viewport, and skip the render entirely when fully clipped. This is an acceptance test, not an
  assumption.
- **Two preview elements open at once** must not disturb each other — same class of bug as the
  shared avatar-selection camera mirror in `OverlayPanelUtil` (see §Phase 3).

**Fallback if interleaving proves unreliable:** render the model to a `D3DUSAGE_RENDERTARGET` texture
before the UI pass and hand it to RmlUi as an ordinary texture. Architecturally cleaner — clipping,
transforms and overlays then work like any other image — at the cost of retargeting the avatar
renderer and one more DEFAULT-pool resource to recreate. Gotcha: `SetRenderTarget` needs a
**matching depth-stencil surface**, or the avatar pipeline's z-clear silently fails and the model
renders with broken depth.

Two viable paths, not one desperate workaround. Everything else about those panels (stat grids, drop
icons, tooltips, drag) maps to RCSS cleanly.

### 2.8 What must NOT be migrated

- **Chat input / IME.** `tgamectrl` carries a full IME stack — `imeview.cpp`,
  `tcandidatewindow*.cpp`, `csplithangul.cpp`. RmlUi has no built-in IME composition handling.
  Migrating chat input means rebuilding all of that. Hard no.
- **The 56 XML retail dialogs / 59 dialog classes.** ~51k LOC. Not migratable at any sane cost, and
  it would collide with the alternate-UI-skin work already in flight.

## 3. What RmlUi actually buys

**Buys, directly against today's pain:**

- RCSS styling with **runtime document reload** — iterate on panel visuals without a rebuild. For a
  private server where UI look is a differentiator, this is the headline win.
- One `Rml::Context` owns hit-testing, z-order, hover/active/focus. The four hand-ordered
  `ProcWndMsgInstant` hooks collapse into a single `Context::ProcessMouse*` call placed before the
  legacy chain.
- Free scrolling, clipping, wrapping, ellipsis, mixed-color runs, drag via CSS.
- **Data bindings** (model handles) fit the Damage Meter's row list almost perfectly — bind the
  snapshot, let RmlUi diff.
- A modding surface: `.rml`/`.rcss` as loose data files means skinners never touch C++.

**Does not buy:**

- Nothing for the retail UI, which is where the other 51k LOC lives.
- No perf win. It is a new draw pass on top of the existing sprite pass; budget it and measure it.

## 4. The honest alternative

The complaint is "styling *four* panels is difficult". If the horizon really is those four panels and
no more, a much cheaper option exists: extract a small retained box-model + style-struct layer over
the existing `OverlayPanelUtil` calls, loading style from a data file. Roughly 1–2k LOC, zero new
dependencies, zero UTF-8/RTTI/FreeType/backend risk, and it solves the hard-coded-constants problem
outright — though not scrolling, focus, or wrapping.

**Decision rule:**

- Goal is *these four panels, tidier* → build the small layer. Don't take the dependency.
- Goal is *a platform for many more custom panels, with CSS-editable skins for modders* → RmlUi,
  scoped strictly to new/custom UI.

The roadmap below assumes the second, since that is what the question implies.

## 5. Phased roadmap

Every phase ships behind an INI toggle and leaves the existing panels working. No phase is a
point of no return until Phase 5.

> **Revised after dev review.** The original plan opened with "port the Damage Meter". The better
> structure is **two tightly scoped spikes**: prove the ordinary 2D UI first, then separately prove
> the 3D pane. Five of the six hard items are bounded integration work; only the 3D render ordering
> could reveal an architectural incompatibility, and it should not be entangled with the other five.
> **Do not port Monster Inspector until Spike 2 passes.**

### Spike 1 — Ordinary 2D UI on a DX9Ex backend (~4–5 days)

Proves §2.1–2.6 without involving the monster renderer.

- Vendor RmlUi at a **pinned version** (decides the backend API — 6.x's render manager and mandatory
  compiled geometry differ materially from 5.x); build x86 `/MT` with `RMLUI_CUSTOM_RTTI`; commit a
  `.vcxproj`. Vendor FreeType the same way.
- Write the D3D9 `Rml::RenderInterface` — **synchronous and simple**, no batching.
- Implement only the mandatory surface: textured/untextured triangles, alpha blend, rectangular
  scissor, textures. Stub the optional features.

**Deliberately out of scope for the first backend** (these require optional renderer features and
would balloon it): `filter`/`backdrop-filter`, blurred `box-shadow`, `border-radius` combined with
clipping, `linear-gradient`, `transform: rotate`.

**Test document** exercising: 9-slice panel, text at three sizes, hover/pressed button, animated HP
bar, scrolling box, clipped item grid.

**Test matrix:** alt-tab · window resize · window drag · device rebuild · repeated open/close ·
multiple resolutions · Korean/Japanese text.

**Exit criteria:**
1. No device-loss regression, no black screen (remember the occlusion latch — `swap_buffers()` is
   what *sets* the occluded flag).
2. Windowed frame drag → device rebuild → textures and geometry come back.
3. No renderer-state leakage into the engine (world render unchanged with the overlay up — save and
   restore around the pass, as `dev_ui_render` does for `D3DRS_FILLMODE`).
4. Frame cost on the debug HUD, in the style of the existing `BoneFx:` / `PartBatch:` lines.

**Kill criteria:** if state leakage or device-restore proves messy at this scale, stop and build the
small layer from §4 instead.

#### Spike 1 — build-out status (2026-07-29)

**Done and verified by build; runtime criteria still open.**

Vendored at pinned versions with hand-written `.vcxproj` files following the local convention
(`thirdparty/rmlui.vcxproj`, `thirdparty/freetype.vcxproj`):

- **RmlUi 6.2** — chosen over 5.1 because flexbox landed in 6.0 (directly serves the CSS-authoring
  goal) and because 6.x's mandatory *compiled geometry* maps better onto D3D9 than 5.x's immediate
  mode: one VB/IB per handle, one draw per `RenderGeometry`, which also removes the "does the backend
  defer geometry?" question the dev review raised. Lua/Lottie/SVG plugins excluded.
- **FreeType 2.13.3** — amalgamated module list mirroring FreeType's own vc2010 project.

Both compile clean as x86 `/MT` `v142` static libs with `RMLUI_CUSTOM_RTTI` and RTTI off. **§2.5 and
§2.6 (build friction, RTTI) are confirmed low-risk — they cost nothing.**

New client code, all inert unless `[VIDEO] RMLUI=1` or `ROSE_RMLUI=1`:

| File | Role |
|---|---|
| `src/client/rmlui/RoseRmlRenderer.*` | the 8 mandatory `RenderInterface` calls, synchronous |
| `src/client/rmlui/RoseRmlSystem.*` | clock (QPC, *not* `timeGetTime`), logging, clipboard |
| `src/client/rmlui/RoseRmlUi.*` | context ownership, device lifetime, input bridge |
| `data/3ddata/rmlui/spike.rml` + `.rcss` | the test document from the plan above |

Hooks: `CGame::GameLoop` (init/shutdown), `CGameState::render_dev_ui` (the shared in-scene overlay
point every state already calls), `CGameState::ProcWndMsgInstant` (input, first refusal,
consume-only-if-handled), `CApplication::ApplyWindowedClientResize` (device rebuild, either side of
`resetScreen()`), and an `RmlUi: draws=` debug HUD line.

**Five concrete gotchas found while building it** — each would have cost time later:

1. **Premultiplied alpha.** RmlUi 6's `Vertex::colour` is `ColourbPremultiplied`, so the blend state
   must be `SRCBLEND=ONE` / `DESTBLEND=INVSRCALPHA`. The conventional `SRCALPHA` pair double-applies
   alpha — every glyph edge and faded panel comes out too dark. Textures loaded via D3DX arrive as
   *straight* alpha and must be premultiplied on load.
2. **Channel order.** RmlUi vertex colours and `GenerateTexture` pixels are RGBA; D3D9 wants BGRA.
   Silent red/blue swap otherwise.
3. **DEFAULT-pool textures cannot be locked.** Both texture paths go through a `D3DPOOL_SYSTEMMEM`
   staging texture + `UpdateTexture`.
4. **RmlUi does not re-request compiled geometry after a device reset.** The renderer therefore keeps
   CPU-side copies of every vertex/index buffer and refills them itself in `CreateDeviceObjects()`.
   `Rml::ReleaseTextures()` covers only the textures. This is exactly the ownership question the dev
   review flagged — confirmed, and handled.
5. **Object-filename collision.** `Source/Core/Geometry.cpp` and `Source/Debugger/Geometry.cpp` both
   emit `Geometry.obj`; without `<ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>` one
   overwrites the other and the link fails with unresolved externals that look like missing sources.

Also confirmed from the pool discussion in §2.2: compiled geometry is **write-once**, so its DEFAULT
buffers correctly use *no* `D3DUSAGE_DYNAMIC` — the four-way split holds in practice.

**Still open — these need a running client and a human looking at the screen:** the whole Spike 1
test matrix (alt-tab, resize, drag, device rebuild, repeated open/close, resolutions, CJK text) and
exit criteria 1–3. Nothing here is validated visually yet.

**Deploy note:** the spike loads `3ddata/rmlui/*` through RmlUi's default file interface, i.e. plain
`fopen` relative to the launch dir — the VFS bake does **not** cover it, exactly like `UI_strID.ID`.
Ship those files loose next to the client, or the document silently fails to load (the log says
which).

### Spike 2 — Standalone 3D model element (~3–4 days)

Only after Spike 1. A custom element and nothing else — no panel port:

```html
<div class="preview-test">
    <rose-model-preview><div class="glass-overlay"></div></rose-model-preview>
</div>
```

**Acceptance conditions:** background renders behind the model · overlay renders in front · model
stays inside the element rect · moving the element moves the viewport · **clipping works** (see the
scissor/viewport conflict in §2.7) · UI after the element still renders · no state leaks · device
rebuild survives · two preview elements do not break each other.

If this fails, fall back to the render-to-texture path (§2.7) before abandoning anything. Failure
here does **not** invalidate RmlUi for every other panel — the legacy panels simply stay on the old
rendering path.

### Phase 1 — Platform integration (~4–5 days)

Runs alongside/after Spike 1; hardens the spike into something shippable.

- `Rml::SystemInterface`: clock off `g_GameDATA.GetGameTime()`, clipboard, cursor.
- Input bridge: **one** entry point in `ProcWndMsgInstant`, placed *before* the existing panel chain,
  consuming the message only when RmlUi's context reports it handled it.
- FreeType vendored + font faces registered.
- Centralized codepage↔UTF-8 conversion helpers; document the rule.
- Texture loading routed through `triggervfs` so RCSS can reference existing game DDS/TSI assets.
- Device invalidate/restore wired into the engine's existing hooks.
- **Decide VFS vs loose for `.rml`/`.rcss` now.** Precedent: `UI_strID.ID` loads via plain `fopen`
  from the loose `3ddata\control\xml\` folder and the VFS bake never covers it — that mismatch cost a
  debugging round on the quest-icon work. Pick one, write it down, make the deploy script enforce it.

### Phase 2 — Port one panel: Damage Meter (~3–5 days)

The right first target: pure 2D, no 3D pane, self-contained, already has its style constants isolated
in one anonymous namespace, and its row list maps onto RmlUi data bindings.

- Ship behind `[UI] RMLUI_DAMAGE_METER=0/1` so old and new coexist for A/B.
- Keep `CDamageMeter` (the data core) completely untouched — it stays a read-only observer of the
  combat event stream. Only the view layer changes.
- Validates: layout, hover, drag, scroll, live data binding, input consumption, frame cost.

**Go/no-go gate.** If Phase 2 lands clean and the iteration speed on styling is visibly better, continue.
If it feels like fighting the library, stop with one panel migrated and no further commitment.

#### Phase 2 — status (2026-07-29): done and validated in-game

`src/client/rmlui/RoseRmlDamageMeter.*` + `data/3ddata/rmlui/damagemeter.rml`/`.rcss`.
`CDamageMeter` untouched — the RmlUi view is a second consumer of `BuildSnapshot()` alongside the
legacy panel, and `[VIDEO] RMLUI` picks which one `/dps` opens, so they A/B in place.

Validated: live data, view cycling, reset, close, hover states, drag across the screen, and game
input unaffected with the panel open.

**What moved out of C++.** Rows are generated by `data-for` from a bound data model, so nothing on
the C++ side creates or positions an element — compare `CDamageMeterPanel::Draw()`, which computes
every row offset, button position and right-aligned column by hand. `<handle move_target>` replaced
the `WM_LBUTTONDOWN`/`WM_MOUSEMOVE` drag code in `ProcWndMsgInstant`. Header buttons bind by name, so
the markup decides which element does what — no hit-test rectangles, no ordering constraint against
the legacy panel chain. `:hover`/`:active` are CSS instead of manual mouse-position tests.

**Fonts:** the client's UI font is Verdana (`CStringManager::GetFontNameByCharSet` is hardcoded to
it), loaded from the system font directory so the panel matches the rest of the HUD. Bold must be
loaded as a *separate face* or `font-weight: bold` silently does nothing.

**Input arbitration turned out to be the fiddly part — more than rendering was.** Two rounds of
real bugs, both worth remembering:

1. **`<body>` must span the viewport.** `ElementHandle` clamps a drag to the move target's
   containing block ([ElementHandle.cpp:306](thirdparty/RmlUi-6.2/Source/Core/ElementHandle.cpp#L306));
   for an absolutely-positioned panel that is `<body>`, and a body collapsed to its content leaves
   the panel draggable only a few pixels near the origin.
2. **Therefore do not decide input consumption from `Context::GetHoverElement()`.** A viewport-sized
   body is a screen-wide hit target whenever a document is open, so hover-based consumption swallows
   every click, attack and move order in the game. Filtering by tag name did not work either.
   **Test the cursor against panel geometry instead:** every top-level child of every visible
   document is solid UI, everything else is transparent. The contract is ours to keep — a panel is a
   direct child of `<body>` — and nested content needs no check because it lies inside its panel's
   box by construction.

Two corollaries of that boundary, each a real bug when missed: a panel drag continues *after* the
cursor leaves the panel, so a left-press must latch a dragging flag until release or the handle never
lets go; and `WM_MOUSEWHEEL` carries **screen** coordinates unlike the other mouse messages, so it
needs `ScreenToClient` before hit-testing or camera zoom breaks depending on where the window sits.

**Implication for scope:** the RmlUi ↔ legacy input boundary is where the remaining surprises live.
That argues for keeping the number of RmlUi panels small and their geometry simple, and is an
independent reason not to migrate the retail dialogs.

**Still on CSS colours, not game atlas art** — no `ID_BLACK_PANEL` background, no `UI00_GUAGE_RED`
bars. Needs texture loading routed through the VFS (the Phase 1 item). Panel position is also not
persisted, same as the legacy panel.

### Phase 3 — 3D-pane panels (~1–2 weeks)

**Gated on Spike 2 passing.** Do not start otherwise.

- Land the `OnRender` solution once in a shared helper — the RmlUi-side mirror of `OverlayPanelUtil`. Note the
  existing warning there: the avatar-selection camera mirror must stay a *single* process-wide
  instance, or two open panels mis-frame each other every frame. Same constraint applies.
- Then port Monster Inspector and Item Preview. Summon Info is trivial and rides along.
- Tooltips: decide whether they stay on `CToolTipMgr` (drawn at the end of `g_itMGR.Update()`, after
  `g_UIMed.Draw()`) or move into RmlUi. Mixing the two will produce z-order surprises — pick one per
  panel and be consistent.

### Phase 4 — Modding surface (~2–3 days)

- Runtime document/RCSS reload bound to a debug command.
- Ship `.rml`/`.rcss` per the Phase 1 deploy decision; update `scripts/dist.ps1`.
- Document the panel-authoring flow for skinners.

### Phase 5 — New UI is RmlUi-first (ongoing, optional)

Only after 2–4 are stable. New custom panels get built in RmlUi by default.

**A caution on scope, raised because dev review drifted here.** It is tempting to say "worst case we
still get inventory, options, quest windows, dialogs and tooltips". Inventory is *not* an ordinary
panel — it is `CSlot` + `CDragNDropMgr` + `CToolTipMgr` + `it_mgr` + the item-icon system, carrying
live features (shift+click chat links, alt+click item preview, ctrl+click wishlist). That list is
most of the ~51k LOC in `interface/` plus 56 XML dialogs. Reading it as near-term scope misestimates
the work by an order of magnitude.

**Explicitly out of scope, permanently, unless separately re-evaluated:** tgamectrl retail dialogs,
chat input, and **IME** — `tgamectrl` carries a full Hangul input stack (`imeview`,
`tcandidatewindow*`, `csplithangul`) and RmlUi has no IME composition handling, so any RmlUi text
field is a regression for CJK players.

The honest good outcome is: **RmlUi for new custom panels, legacy stack untouched.**

## 6. Risk register

| Risk | Severity | Mitigation |
|---|---|---|
| 3D pane render ordering | Medium *(was High — `OnRender` exists, §2.7)* | Spike 2, standalone; RTT fallback if it fails |
| Scissor ignored by 3D viewport | Medium | Intersect pane rect with active scissor; Spike 2 acceptance test |
| Device restore on windowed resize | Medium | Spike 1 exit criterion #2; `Rml::ReleaseTextures()` + lazy reload |
| Renderer state leakage into engine | Medium | Spike 1 exit criterion #3; save/restore around the pass |
| Data-table encoding is locale-derived | Medium | Fix the encoding-of-record question (§2.4) before wiring text |
| Scope creep into tgamectrl / inventory / IME | Medium | Written scope limit; §2.8 and Phase 5 caution are the boundary |
| Deploy mismatch (VFS vs loose) | Low | Decided in Phase 1, enforced by dist script |
| Input double-handling with legacy chain | Low | Single entry point, consume-only-if-handled |
| RTTI / `/MT` / x86 build friction | Low | Settled in Spike 1 |

## 6b. Alternatives to RmlUi (if the goal is specifically "author menus in CSS")

Worth separating what "CSS" actually means here, because the three parts have very different costs:

1. **A cascade / stylesheet** — restyle without recompiling. *This is the actual pain today.*
2. **A layout engine** — flexbox/auto-sizing instead of hard-coded pixel offsets.
3. **A declarative document tree** — markup instead of C++ draw calls.

Note the retail dialogs already have (3) and nothing else: `data/3DDATA/CONTROL/XML/*.xml` is parsed
through MSXML DOM in [tcontrolmgr.cpp](src/tgamectrl/src/tcontrolmgr.cpp) into absolute
`OFFSETX`/`OFFSETY`/`WIDTH`/`HEIGHT` with `GID` atlas references. No cascade, no layout. So "add CSS"
is really "add 1 and 2".

### Option comparison for a 32-bit D3D9 client

| Option | CSS fidelity | Integration cost | Verdict |
|---|---|---|---|
| **RmlUi** | RCSS: CSS 2.1 subset + flexbox (6.0+) | D3D9 backend + FreeType + UTF-8 + RTTI flag | Front-runner if you want a real UI *toolkit* |
| **litehtml** | Real HTML + CSS 2.1 & parts of CSS3 | ~15 callbacks against **existing** sprite/font code | Best if you want CSS but keep your renderer |
| **Yoga + own mini-CSS** | Whatever subset you write | No new rendering risk at all | Lowest risk, highest maintenance |
| **Sciter** | Real CSS + JS, very small runtime | Closed-source binary, bitmap blit per frame | Licensing/opacity cost in an OSS-ish project |
| **Ultralight** | Full WebKit CSS3 | **x86 support must be verified first** | Likely blocked; heavyweight (JS engine) |
| **CEF** | Full Chromium | Multi-process, 150 MB+, 32-bit builds rare | No |
| **NoesisGUI** | XAML, *not* CSS | Commercial + still needs a D3D9 renderer | Wrong tool for the stated goal |
| **libRocket** | RmlUi's ancestor | Unmaintained since ~2015 | Don't adopt — but see below |

### litehtml — the option most worth a look

[litehtml](https://github.com/litehtml/litehtml) (MIT) is a *rendering* engine, not a toolkit: it
parses HTML + CSS, does the cascade and the layout, and then calls **you** to draw. You implement a
`document_container` — roughly: `draw_text`, `draw_background`, `draw_borders`, `get_text_width`,
`load_image`, `get_image_size`, `set_clip`, plus a few metrics callbacks.

Why it fits this codebase unusually well:

- **No new renderer.** Those callbacks map almost one-to-one onto `OverlayPanel::DrawTextAt` /
  `DrawPanelBg` / `::drawSprite` / `::drawFont` / `getFontTextExtent` — code that already works on
  the 9Ex device.
- **No FreeType.** You supply text drawing and measurement with the existing `ID3DXFont` handles, so
  fonts keep looking exactly like the rest of the game.
- **The 3D-pane problem largely evaporates** (§2.7). You still own the draw loop, so you can flush,
  punch the viewport, render the puppet, and continue — the current `flushSprite` →
  `setAvatarViewPort` → `RenderSelectedAvatar` sequence keeps working, triggered by a marker element's
  computed rect.
- **No device-restore work**, because you never create D3D resources RmlUi-style — you reuse the
  engine's.

What you *don't* get, and would build yourself: focus handling, form controls, scrolling containers,
animations. You do get `:hover` and hit-testing (`on_mouse_over` / `on_lbutton_down`). Also worth
noting it's document-oriented — content changes trigger reflow, so cache the snapshot rather than
rebuilding the DOM every frame (the Damage Meter's existing 500 ms `kSnapshotRefreshMs` is already
the right shape for this).

### Yoga + a small CSS parser

If even litehtml feels like too much surface: [Yoga](https://github.com/facebook/yoga) (MIT, C++, no
dependencies) is just a flexbox layout engine — no rendering, no parsing. Pair it with a few hundred
lines of CSS parsing (type/`.class`/`#id` selectors, specificity-ordered cascade) and the existing
`expat` dep (already vendored) or MSXML for the document, and you have items 1–3 with **zero** new
rendering risk.

Realistically 2–3k LOC for a usable subset. That is not obviously cheaper than integrating RmlUi in
wall-clock terms — its advantage is *risk*, not effort: no unknowns, no deploy burden, nothing that
can break the device. The cost is that you own it forever.

### One practical tip regardless of choice

libRocket — RmlUi's unmaintained ancestor — shipped an actual **Direct3D 9 sample renderer**, which
is precisely the thing RmlUi lacks (§2.1). Don't adopt libRocket, but read that sample as the
reference implementation when writing the RmlUi D3D9 render interface. It will save a day.

### Choosing between them

- Menus are **mostly-static styled panels with buttons/hover** → **litehtml**. Best CSS-per-risk
  ratio here, and it keeps every piece of infrastructure that currently works.
- You want a **real interactive UI system** (focus, widgets, scrolling, data binding, animations) and
  expect many panels → **RmlUi**, per the roadmap above.
- You want **zero new dependencies** and full control → **Yoga + mini-CSS**.

## 7. Recommendation

Run **Phase 0**. It is ~3 days and it answers the only questions that actually matter (device
restore, state isolation, frame cost) with running code rather than argument. Decide on Phase 1+
from that result.

Adopt RmlUi as an **additive overlay layer for custom panels**. Do not frame it as replacing the
client's UI — that framing leads to a multi-month rewrite that collides with existing work and buys
nothing for the 51k LOC of retail interface code.

If after Phase 0 the appetite is smaller than "a platform for many panels", take the §4 alternative
and skip the dependency entirely. That is a legitimate outcome, not a failure.
