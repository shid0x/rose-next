"""Map generator: codecs for ROSE zone files (phase 0).

See docs/mapgen/PLAN.md for the project and docs/mapgen/FORMATS.md for the
formats. Every codec here is `parse_x(bytes) -> model` and
`build_x(model) -> bytes`; `scripts/mapgen-roundtrip.py` proves they are exact
on the whole of data/3DDATA/MAPS.
"""

from . import chunk, ifo, lit, zon
from .binio import FormatError

# extension -> (parse, build)
CODECS = {
    ".zon": (zon.parse, zon.build),
    ".ifo": (ifo.parse, ifo.build),
    ".him": (chunk.parse_him, chunk.build_him),
    ".til": (chunk.parse_til, chunk.build_til),
    ".mov": (chunk.parse_mov, chunk.build_mov),
    ".lit": (lit.parse_lit, lit.build_lit),
}
