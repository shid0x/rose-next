"""The lump container shared by .ZON and .IFO.

    i32 count
    count x (i32 type, i32 offset)        # offsets are absolute
    ... lump bodies ...

Readers seek to each offset and read what that lump type means; nothing in
the format says where a lump ends. So the model keeps the physical layout:

* `entries` is the lump table in file order: (type, region index).
* `regions` are the distinct lump offsets in ascending order. A region runs
  to the next region's offset, or to end of file.
* Each region holds the parsed lump plus `tail`: bytes after what the parser
  consumed, up to the next region. Retail files carry editor junk there,
  mostly after the last lump (import-oro.py:503-508).
* `pre_gap`: bytes between the end of the lump table and the first region.

Serialising lays out header, pre_gap, then each region (parsed body + tail)
and recomputes every offset from that layout. A file built from scratch uses
the same path with empty tails, so the round-trip exercises the real writer.

A lump the parser rejects is kept as raw bytes, with the reason, so one
unknown layout never hides the rest of the file.
"""

from dataclasses import dataclass, field
from typing import Any, List, Optional, Tuple

from .binio import FormatError, Reader, Writer


@dataclass
class Region:
    lump_type: int
    body: Any                     # parsed model, or bytes when raw
    tail: bytes = b""
    raw_reason: Optional[str] = None   # set when body is the raw bytes

    @property
    def is_raw(self):
        return self.raw_reason is not None


@dataclass
class Container:
    entries: List[Tuple[int, int]] = field(default_factory=list)   # (type, region index)
    regions: List[Region] = field(default_factory=list)
    pre_gap: bytes = b""
    # Entries whose offset points into the header or past the end. Kept verbatim.
    odd_entries: List[Tuple[int, int, int]] = field(default_factory=list)  # (slot, type, offset)

    def lumps(self, lump_type):
        """Parsed bodies of every region of this type, in file order."""
        return [r.body for r in self.regions if r.lump_type == lump_type and not r.is_raw]

    def lump(self, lump_type):
        found = self.lumps(lump_type)
        return found[0] if found else None


def parse(data, parsers, writers):
    """parsers: {type: fn(Reader) -> body}; writers: {type: fn(Writer, body)}."""
    r = Reader(data)
    count = r.i32()
    if count < 0 or count > 64:
        raise FormatError("implausible lump count %d" % count)
    table = [(r.i32(), r.i32()) for _ in range(count)]
    header_end = r.pos

    c = Container()
    good = sorted({off for _, off in table if header_end <= off <= len(data)})
    index_of = {off: i for i, off in enumerate(good)}
    first_type = {}
    for t, off in table:
        if off in index_of and off not in first_type:
            first_type[off] = t

    for slot, (t, off) in enumerate(table):
        if off in index_of:
            c.entries.append((t, index_of[off]))
        else:
            c.entries.append((t, -1))
            c.odd_entries.append((slot, t, off))

    c.pre_gap = bytes(data[header_end:good[0]]) if good else bytes(data[header_end:])

    for i, off in enumerate(good):
        end = good[i + 1] if i + 1 < len(good) else len(data)
        t = first_type[off]
        sub = Reader(data, off, end)
        fn = parsers.get(t)
        if fn is None:
            c.regions.append(Region(t, bytes(data[off:end]), b"", "no parser for lump type %d" % t))
            continue
        try:
            body = fn(sub)
            # Prove the writer can reproduce what was consumed before trusting it.
            w = Writer()
            writers[t](w, body)
            consumed = bytes(data[off:sub.pos])
            if w.getvalue() != consumed:
                raise FormatError("re-serialised lump differs from the bytes it was parsed from")
            c.regions.append(Region(t, body, bytes(data[sub.pos:end])))
        except FormatError as e:
            c.regions.append(Region(t, bytes(data[off:end]), b"", str(e)))
    return c


def build(c, writers):
    """Serialise a container; offsets are recomputed from the layout."""
    count = len(c.entries)
    header_size = 4 + 8 * count

    bodies = []
    for reg in c.regions:
        if reg.is_raw:
            bodies.append(reg.body)
        else:
            w = Writer()
            writers[reg.lump_type](w, reg.body)
            bodies.append(w.getvalue() + reg.tail)

    offsets = []
    pos = header_size + len(c.pre_gap)
    for b in bodies:
        offsets.append(pos)
        pos += len(b)

    odd = {slot: off for slot, _, off in c.odd_entries}
    out = Writer()
    out.i32(count)
    for slot, (t, ri) in enumerate(c.entries):
        out.i32(t)
        out.i32(odd[slot] if ri < 0 else offsets[ri])
    out.raw(c.pre_gap)
    for b in bodies:
        out.raw(b)
    return out.getvalue()
