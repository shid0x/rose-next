# Monster Inspector — Custom UI Art Spec (designer handoff)

The Monster Inspector window is **custom-drawn** (not one of the XML dialogs):
the code blits sprites and draws live text/icons/3D on top. That means a custom
skin is easy to integrate — the artist provides bitmaps + a coordinate sheet,
and the code maps its layout constants onto them. No engine or format knowledge
needed on the design side beyond the constraints below.

## What the window must contain (functional zones)

The art can be any size/shape/style, but it must reserve space for these
zones. Current build values are given as reference — all of them are code
constants that will be remapped to the new design.

| Zone | Content drawn by the game (live) | Current rect (px, panel-relative) |
|---|---|---|
| Title bar | text "Monster Inspector", drag handle = whole panel | 560 × 26 top strip |
| Close button | click target (art can be a real button) | 20 × 18 at top-right |
| 3D preview pane | live rotating monster model, rendered *over* the art | 220 × 280 at (10, 36) |
| Name line | "<Monster>  (Lv. N)" text | x 240, y 36, w 310, h 20 |
| HP bar | fill + "cur / max" text | x 240, y 60, w 310, h 16 |
| Stats grid | 8 label+value pairs, 2 columns × 4 rows | x 240, y 84, row height 18 |
| Drops label | "Drops (N)" text | x 240, y 164 |
| Drop icon grid | up to 35 game icons, each **40 × 40** | origin (240, 184), 7 per row, 42 px pitch, max 5 rows |

Current overall size: 560 wide × 326–404 tall (height grows with drop rows).

## Deliverables checklist

1. **Panel background — one flat PNG at final pixel size (1:1), with alpha.**
   This is the single most important constraint: the art is blitted 1:1, so
   deliver at exactly the size it should appear on screen. No 9-slice system
   is available, and stretching bitmaps with baked gradients produces visible
   banding (already bitten us once).
   - Pick **one fixed panel size** covering the worst case (5 drop rows).
     Variable-height art is possible but only with a vertically *tileable/flat*
     middle section — fixed size is strongly recommended.
   - Keep the whole panel ≤ ~600 × 450 so it fits an 768-high screen.
2. **3D preview pane area = plain dark region in the background art.**
   The monster model renders directly on top of whatever is painted there
   (there is no render-to-texture); busy art will fight the model. Dark,
   low-contrast, subtle texture at most. Any pane size/aspect works (the
   camera auto-frames), roughly portrait proportions look best.
3. **Drop slot cells** (optional): if the design has per-slot frames, the
   inner area must fit the game's 40 × 40 icons; give the cell pitch
   (spacing) in the coordinate sheet.
4. **HP bar** (optional custom art): background piece at final size + a fill
   piece. The fill is cropped/stretched horizontally by current HP, so it must
   be **horizontally flat** (vertical gradient fine, no baked end caps).
5. **Close button** (optional): normal state required; hover/pressed states
   optional (not currently wired, can be added).
6. **No baked text for live values.** Monster name, level, HP numbers, stat
   values, drop count are drawn by the game font on top. Static *labels*
   ("Attack", "Defense", …) may be baked into the art if the designer prefers
   his own typography — but then they are fixed (no localization) and the
   coordinate sheet must leave the value areas clear. Game text can be any
   color; tell us the palette.
7. **Coordinate sheet** (the key deliverable besides the art): a mockup or
   table with panel-relative pixel rects for every zone in the table above —
   preview pane rect, name/HP/stat rows, drop grid origin + cell pitch + max
   columns/rows, close button rect.
8. **File formats**: flattened PNGs (sRGB, straight/non-premultiplied alpha —
   semi-transparency is fine, the panel alpha-blends over the world) plus the
   layered source (PSD/Figma) for future tweaks. We convert to the engine's
   DDS/TSI atlas format on our side; textures end up on power-of-two sheets
   (≤ 1024 × 1024), so individual pieces should stay within that.

## Out of scope for the artist (engine-drawn, unchanged)

- Drop-icon artwork (comes from the game's item icon atlas)
- Hover tooltip look (shared game-wide `CInfo` tooltip)
- The 3D monster model, its lighting and rotation
- Fonts (game fonts; color per-zone is configurable)

## Integration notes (our side)

- Art gets packed onto a DDS sheet + TSI sprite entries (same tool family as
  `scripts/add-item-icon.py`); `CMonsterInspectorPanel` then draws the new
  sprites 1:1 and its layout constants (`cmonsterinspectorpanel.cpp`, the `k*`
  constants at the top) are updated from the coordinate sheet.
- Gotchas already documented: `ID_BLACK_PANEL` gradient banding when
  stretched, 512-px source-rect clipping in `CTDrawImpl::Draw(width)` — both
  avoided by the "deliver at final size, draw 1:1" rule above.
