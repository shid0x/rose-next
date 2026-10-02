"""Top-down preview PNG of a generated heightfield (phase 2).

Hillshade tinted by the walkability analysis:

* green: walkable and connected to the start;
* yellow: walkable but not reached from the start;
* red: too steep to climb (>= 54 degrees);
* magenta: TRAP, reachable but no way back (must never appear);
* white: chunk borders;
* blue dot: the start point.

North is up.
"""

import numpy as np
from PIL import Image

from .terrain import GRID_CM


def _tint_water(rgb, wet_px):
    """Blend the water colour over wet pixels (the client's sea is blue-green)."""
    rgb[wet_px] = rgb[wet_px] * 0.35 + np.array([40, 110, 150]) * 0.65


def render_tiles(field, lattice, colours, analysis, out_path, px_per_tile=8, exaggeration=4.0, wet=None):
    """Painted preview: corner brushes blended bilinearly across each 10 m
    tile (roughly how the tile textures blend), times a hillshade. Start in
    blue, chunk borders in white."""
    rows, cols = lattice.shape
    pal = np.zeros((max(colours) + 1, 3))
    for b, rgb in colours.items():
        pal[b] = rgb
    rgb = pal[lattice]                                             # (rows, cols, 3) at corners
    n = px_per_tile
    t = (np.arange(n) + 0.5) / n
    out = np.zeros(((rows - 1) * n, (cols - 1) * n, 3))
    for r in range(rows - 1):
        wy = t[:, None, None]
        for c in range(cols - 1):
            wx = t[None, :, None]
            out[r * n:(r + 1) * n, c * n:(c + 1) * n] = (
                rgb[r, c] * (1 - wx) * (1 - wy) + rgb[r, c + 1] * wx * (1 - wy)
                + rgb[r + 1, c] * (1 - wx) * wy + rgb[r + 1, c + 1] * wx * wy)
    gx = np.gradient(field, axis=1) / GRID_CM * exaggeration
    gy = np.gradient(field, axis=0) / GRID_CM * exaggeration
    light = np.array([-1.0, 1.0, 1.4])
    light /= np.linalg.norm(light)
    shade = np.clip(0.45 + 0.55 * (-gx * light[0] - gy * light[1] + light[2]) / np.sqrt(gx * gx + gy * gy + 1.0), 0.25, 1.1)
    # field has 4 vertices per tile; sample the shade at the preview's pixel centres
    vr = np.clip(((np.arange(out.shape[0]) + 0.5) / n * 4).astype(int), 0, field.shape[0] - 1)
    vc = np.clip(((np.arange(out.shape[1]) + 0.5) / n * 4).astype(int), 0, field.shape[1] - 1)
    out *= shade[vr][:, vc][..., None]
    if wet is not None:
        _tint_water(out, wet[vr][:, vc])
    out[::16 * n, :] = 255
    out[:, ::16 * n] = 255
    sr, sc = analysis["start_cell"]
    pr, pc = int(sr / 4 * n), int(sc / 4 * n)
    out[max(0, pr - 4):pr + 5, max(0, pc - 4):pc + 5] = (40, 90, 255)
    Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)[::-1]).save(out_path)
    return out_path


def render(field, analysis, out_path, scale=2, exaggeration=4.0, wet=None):
    gx = np.gradient(field, axis=1) / GRID_CM * exaggeration
    gy = np.gradient(field, axis=0) / GRID_CM * exaggeration
    # Light from the north-west, 45 degrees up; relief exaggerated for display only.
    light = np.array([-1.0, 1.0, 1.4])
    light /= np.linalg.norm(light)
    shade = (-gx * light[0] - gy * light[1] + light[2]) / np.sqrt(gx * gx + gy * gy + 1.0)
    shade = np.clip(0.35 + 0.65 * shade[:-1, :-1], 0.2, 1.0)

    colour = np.zeros(shade.shape + (3,))
    colour[...] = (235, 210, 90)                                   # walkable, not reached
    colour[analysis["home"]] = (110, 175, 85)
    colour[analysis["steep"]] = (200, 70, 60)
    colour[analysis["traps"]] = (230, 0, 230)
    rgb = colour * shade[..., None]
    if wet is not None:
        _tint_water(rgb, wet[:-1, :-1])

    rows, cols = shade.shape
    rgb[::64, :] = 255                                             # chunk borders (64 cells each)
    rgb[:, ::64] = 255
    sr, sc = analysis["start_cell"]
    rgb[max(0, sr - 2):sr + 3, max(0, sc - 2):sc + 3] = (40, 90, 255)

    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)[::-1])   # row 0 = south -> bottom
    if scale != 1:
        img = img.resize((cols * scale, rows * scale), Image.NEAREST)
    img.save(out_path)
    return out_path
