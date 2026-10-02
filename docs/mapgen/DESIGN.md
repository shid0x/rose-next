# Map generator — level-design guide

What makes a generated map read like ROSE, collected from measurements of
the retail maps and from the user's review of generated ones. Claude reads
this before writing a layout file. Each entry says what was seen, the
evidence, and what the generator does about it (or "open").

## Ground paint

- **Retail paints in large, coherent patches, especially on slopes.**
  Neighbouring tile corners share a brush 74-81% of the time on Junon
  grassland maps (JG01-04), and 79-86% on slopes of 45° and more. Phase 7
  maps measured 65-72% overall and 65-68% on steep ground, and put about
  twice retail's dark soil on cliffs (12-14% against 5-9%). On a cliff
  face each mismatch stretches into a tall stripe: the user saw "weird
  tiles used seemingly at random on mountains" (2026-10-02). The brush
  *mix* per slope band already matches retail; the patch *size* does not.
  Open: calibrate patch coherence against retail, steep ground first.

## Villages

- **Harbour villages stand on stilts over water** (user, 2026-10-02).
  Adventurer's Plain's village and Kenji Beach's houses are wooden
  platforms on stilts: the ramp end on land, the rest over the water. Their
  lifted paint shows it (seabed and sand under them). Placed on a flat dry
  pad, the stilts stand on grass. Open: place such a prefab on a shore,
  with its wet part over water and only its land part flattened.
- **Lifted villages on flattened pads are fine** on gentle ground; on
  jagged terrain the round flat pad stands out (phase 7, #3).

## Decoration

- **Flower density at x6 of the ground's default reads right for "a
  flower-dense region"** (user, 2026-10-02, map 2).

## Map shape

- **Generated maps are all square; ROSE's are not** (user, 2026-10-02).
  Retail bends the playable area: much of a zone can be blocked by cliffs
  and the walkable part follows an irregular outline. A generated map
  fills its whole rectangle, with a cliff ring along the border. Open:
  measure retail play-area shapes (how much of each zone is walkable, how
  irregular its outline is, missing chunks), then give layouts a shape.
