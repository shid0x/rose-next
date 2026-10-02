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


def render(field, analysis, out_path, scale=2, exaggeration=4.0):
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
