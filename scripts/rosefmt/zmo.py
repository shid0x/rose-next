"""ZMO motion clips: the header and the action-frame event trailer (read side).

Header: ``"ZMO0002\\0" | u32 fps | u32 frame_count | u32 channel_count``, then
per channel ``u32 type | u32 bone`` and the frame data. Writing a full clip is
``import-glb-weapon.py``'s ``write_zmo`` (node-rotation clips only); this module
reads the header and edits the trailer.

Trailer (``EZMO`` / ``3ZMO``): ``... body ... | u16 n | n x i16 event-per-frame
| [3ZMO: i32 interpolation interval] | u32 trailer_offset | "EZMO"``.
``truncated`` cuts a clip to its first N frames (frame records, events and the
header count together) -- for an imported clip with a dead tail; ``sliced`` takes any
frame range, e.g. to split a shot at its firing frame; ``joined`` plays two clips of
one skeleton back to back; ``with_trailer`` gives a clip without events an empty table. The events are what makes a clip *do*
something: a normal attack lands at frame 21 (melee) or fires at 22/23
(bow/gun); a skill release launches at 24/34 (or 26/25). **A bare-handed
monster on a 22/23 clip has nothing to fire** and used to deal damage with no
digit (``scripts/fix-zmo-attack-frames.py``). The server decrements the frame
count by one when it times a swing.
"""
import struct

from .binio import read_cstr, read_file

MAGIC = b"ZMO0002"
TRAILER_TAGS = (b"EZMO", b"3ZMO")


def read_header(blob):
    """(fps, frames, channels=[(type, bone)]) or None for a non-ZMO file."""
    b = read_file(blob)
    magic, o = read_cstr(b, 0)
    if magic != MAGIC:
        return None
    fps, frames, nchan = struct.unpack_from("<III", b, o)
    o += 12
    chans = [struct.unpack_from("<II", b, o + 8 * i) for i in range(nchan)]
    return fps, frames, chans


def events(blob):
    """(offset_of_first_event, [event per frame]) or (None, None) without a trailer."""
    b = read_file(blob)
    if len(b) < 8 or b[-4:] not in TRAILER_TAGS:
        return None, None
    off, = struct.unpack_from("<I", b, len(b) - 8)
    if not (0 <= off < len(b) - 2):
        return None, None
    n, = struct.unpack_from("<H", b, off)
    if off + 2 + 2 * n > len(b):
        return None, None
    return off + 2, list(struct.unpack_from("<%dh" % n, b, off + 2))


def event_set(blob):
    """The set of non-zero event ids a clip carries; empty without a trailer."""
    _, ev = events(blob)
    return {e for e in ev if e} if ev else set()


# bytes per frame per channel, by channel type (zz_motion_channel_frame_size)
FIELD_SIZE = {1 << 1: 12, 1 << 2: 16, 1 << 3: 12,                 # position, rotation, normal
              1 << 4: 4, 1 << 9: 4, 1 << 10: 4,                   # alpha, texture anim, scale
              1 << 5: 8, 1 << 6: 8, 1 << 7: 8, 1 << 8: 8}         # uv0-uv3


def frame_layout(blob):
    """(offset of the frame section, bytes per frame, header) -- the frame records
    follow the channel table, one fixed-size record per channel, repeated per frame."""
    b = read_file(blob)
    h = read_header(b)
    if h is None:
        raise ValueError("not a ZMO0002 clip")
    _, o = read_cstr(b, 0)
    start = o + 12 + 8 * len(h[2])
    stride = 0
    for ctype, _ in h[2]:
        if ctype not in FIELD_SIZE:
            raise ValueError("unknown channel type %d" % ctype)
        stride += FIELD_SIZE[ctype]
    return start, stride, h


def frame_values(blob, frame):
    """The floats of one frame, channel by channel: [(type, bone, (floats...)), ...]."""
    b = read_file(blob)
    start, stride, (_, frames, chans) = frame_layout(b)
    if not 0 <= frame < frames:
        raise ValueError("frame %d of %d" % (frame, frames))
    o, out = start + stride * frame, []
    for ctype, bone in chans:
        n = FIELD_SIZE[ctype] // 4
        out.append((ctype, bone, struct.unpack_from("<%df" % n, b, o)))
        o += FIELD_SIZE[ctype]
    return out


def truncated(blob, frames):
    """The clip cut to its first ``frames`` frames: frame count, frame records and the
    event trailer (one event per frame) all shortened, the trailer offset re-pointed.
    Refuses a clip with anything between its frame records and its trailer."""
    return sliced(blob, 0, frames)


def sliced(blob, start, end):
    """The clip's frames ``start`` .. ``end - 1`` as a clip of its own, events moved with
    their frames -- e.g. a shot split into its wind-up (0 .. fire) and its release (fire ..
    total), so a skill's action motion can begin on the firing frame. ``sliced(b, 0, n)``
    is ``truncated``; identity on every avatar clip we ship."""
    b = read_file(blob)
    first, stride, (fps, total, chans) = frame_layout(b)
    if not 0 <= start < end <= total:
        raise ValueError("cannot take frames %d..%d of a %d-frame clip" % (start, end, total))
    frames = end - start
    body_end = first + stride * total
    base, ev = events(b)
    if base is None:
        if len(b) != body_end:
            raise ValueError("clip has %d bytes after its frames and no trailer" % (len(b) - body_end))
        tail = b""
    else:
        if base - 2 != body_end:
            raise ValueError("data between the frames and the trailer -- refusing")
        if len(ev) != total:
            raise ValueError("trailer has %d events for %d frames" % (len(ev), total))
        tag = b[-4:]
        # 3ZMO keeps a u32 after the events -- the clip's interpolation interval,
        # read by CMotion::Load for extended version 3 -- so whatever sits between
        # the events and the offset field travels with them.
        extra = b[base + 2 * total:len(b) - 8]
        new_body_end = first + stride * frames
        tail = (struct.pack("<H", frames) + struct.pack("<%dh" % frames, *ev[start:end])
                + extra + struct.pack("<I", new_body_end) + tag)
    _, o = read_cstr(b, 0)
    head = bytearray(b[:first])
    struct.pack_into("<I", head, o + 4, frames)
    return bytes(head) + b[first + stride * start:first + stride * end] + tail


def joined(first_blob, second_blob):
    """Two clips of one skeleton played back to back as one clip: frames and events of
    ``first`` then ``second`` -- e.g. a held charge pose followed by the start of the
    release, so a skill's whole wind-up is its cast motion at natural speed. Both must
    have the same fps and channel table; the trailer's extra fields are ``first``'s."""
    a, b = read_file(first_blob), read_file(second_blob)
    fa, stride, (fps, na, chans) = frame_layout(a)
    fb, stride_b, (fps_b, nb, chans_b) = frame_layout(b)
    if (fps, chans, stride) != (fps_b, chans_b, stride_b):
        raise ValueError("clips differ in fps or channels -- cannot join")
    base_a, ev_a = events(a)
    base_b, ev_b = events(b)
    if (base_a is None) != (base_b is None):
        raise ValueError("one clip has an event trailer and the other none")
    for blob, first, total, base in ((a, fa, na, base_a), (b, fb, nb, base_b)):
        body_end = first + stride * total
        if (base - 2 if base is not None else len(blob)) != body_end:
            raise ValueError("data between the frames and the trailer -- refusing")
    frames = na + nb
    body = a[fa:fa + stride * na] + b[fb:fb + stride * nb]
    tail = b""
    if base_a is not None:
        extra = a[base_a + 2 * na:len(a) - 8]
        tail = (struct.pack("<H", frames) + struct.pack("<%dh" % frames, *(ev_a + ev_b))
                + extra + struct.pack("<I", fa + len(body)) + a[-4:])
    _, o = read_cstr(a, 0)
    head = bytearray(a[:fa])
    struct.pack_into("<I", head, o + 4, frames)
    return bytes(head) + body + tail


def with_trailer(blob):
    """The clip with an (empty) ``EZMO`` event trailer when it has none -- a monster's
    idle or casting clip often ships without, and ``with_events`` needs one to write in.
    A clip that has a trailer comes back as it is; one with anything after its frame
    records that is not a trailer is refused."""
    b = read_file(blob)
    if events(b)[0] is not None:
        return b
    first, stride, (_, frames, _) = frame_layout(b)
    body_end = first + stride * frames
    if len(b) != body_end:
        raise ValueError("clip has %d bytes after its frames and no trailer" % (len(b) - body_end))
    return b + struct.pack("<H", frames) + bytes(2) * frames + struct.pack("<I", body_end) + b"EZMO"


def with_events(blob, changes):
    """The clip's bytes with ``{frame: event}`` written into its trailer."""
    b = read_file(blob)
    base, ev = events(b)
    if base is None:
        raise ValueError("clip has no frame-event trailer")
    out = bytearray(b)
    for frame, event in changes.items():
        if not 0 <= frame < len(ev):
            raise ValueError("frame %d past the trailer (%d frames)" % (frame, len(ev)))
        struct.pack_into("<h", out, base + 2 * frame, event)
    return bytes(out)
