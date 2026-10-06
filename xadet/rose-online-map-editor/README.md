# ROSE Online Map Editor

A map editor made around 2007 for the game ROSE Online.

## Prerequisites

Good question, my memory isn't that great. My best guess would be:

- XNA 3.1
- irrklang

## Making a minimap

**Tools > Make minimap...** draws the open map from straight above and turns
it into a minimap in the style of the original game: parchment frame, painted
water, and every warp gate marked with the name of the zone it leads to. It
uses the map as it is in the editor, unsaved changes included.

1. Open the map, then **Tools > Make minimap...**.
2. **Generate.** The new minimap appears on the left, the zone's current one on
   the right (they line up exactly, so you can compare them).
3. Options, applied at once:
   - *Match the original minimap's colours*: keeps the zone's look, for
     example Luna's deep blue water. Off: the Junon look.
   - *Name the warp gates*, *Waves on the water*.
   - *Quality*: High is what the original game used; Draft is faster.
4. **Save PNG...** to touch it up in an image editor, or **Install** to write it
   as the zone's `MINIMAP.DDS`. A zone with no minimap gets one, and
   `LIST_ZONE.STB` is updated to point at it.
5. Pack your data again and restart the game client to see it.

Install keeps the file it replaces in `MinimapBackups\<map folder>\` next to
the editor; **Restore original** puts it back. Do not ship that folder with
your data.

Things to know:

- Areas the map has no terrain for are filled in from their surroundings
  (water if they border water).
- The frame is cut from Junon Polis' minimap
  (`3DDATA\MAPS\JUNON\JPT01\MINIMAP.DDS`); without it the minimap gets a
  plain brown border.
- Install is refused when the minimap or `LIST_ZONE.STB` exists only inside
  the packed archive (`data.idx`). Unpack them first.
