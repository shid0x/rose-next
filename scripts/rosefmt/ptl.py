"""PTL particle systems (``3DDATA/EFFECT/PARTICLES/*.ptl``), referenced by ``.eft`` effects.

Layout (``zz_particle_emitter::Load`` -> ``zz_particle_event_sequence::Load`` ->
``zz_particle_event_*::Load``)::

    u32 sequence_count
    per sequence:
        i32 name_len | name                      (quotes included: '"star01"')
        f32 life[min, max] | f32 emit_rate[min, max] | i32 loops
        f32 spawn_dir[min xyz, max xyz] | f32 emit_radius[6] | f32 gravity[6]
        i32 texture_len | texture
        u32 num_particles, align, update_coord, tex_width, tex_height,
            implement, dest_blend, src_blend, blend_op
        u32 event_count
        per event: u32 type | f32 time[min, max] | u8 fade | f32 payload[PAYLOAD[type]]

Things that matter when editing one:

* ``update_coord`` decides whether a particle follows its emitter.
  ``COORD_WORLD`` (0): once emitted it stays in world space -- an effect that
  moves (a bullet) leaves it behind where it was born. ``COORD_LOCAL_WORLD`` (1):
  its position rides the emitter, its rotation does not. ``COORD_LOCAL`` (2):
  both ride. A bullet's travelling look must be 1 or 2.
* A velocity event moves the particle *relative to its frame*: in world space
  that is a straight line in the direction the emitter faced at emission, so a
  world particle with a velocity is a self-propelled shot that ignores where the
  homing bullet goes.
* ``loops`` 0 is "forever"; ``loops * num_particles`` caps the total emitted.
* Emission waits for a free slot, so the births' spread is kept for as long as
  the effect lives: a sequence that fills every slot in one frame (emit 1000/s,
  one lifetime for all) dies and respawns as one cohort forever, and with no
  alpha fade the whole cloud blinks out at once. ``rosedata.particles.rolling_loop``
  staggers the births and fades each particle in and out.
* Alpha/colour fades: an event with ``fade`` 1 is the *target* the previous event
  of its type ramps towards, and each event links to the next **fade** event of
  its type, skipping plain ones -- so a hold between two ramps must be a fade
  event too. Events at the same time run in no fixed order (unstable sort).
* Blend modes are D3DBLEND values; 2/2 (ONE/ONE) is additive and ignores the
  vertex alpha, so an alpha-0 particle there is still visible through its colour.
* Sizes and gravity are cm (the engine scales them by ``ZZ_SCALE_IN``).

Round-trips byte for byte on every ``.ptl`` we ship (``rosefmt-roundtrip.py``).
"""
import struct

from .binio import FormatError, read_file

COORD_WORLD = 0
COORD_LOCAL_WORLD = 1
COORD_LOCAL = 2

EVENT_SIZE = 1
EVENT_TIMER = 2
EVENT_RED = 3
EVENT_GREEN = 4
EVENT_BLUE = 5
EVENT_ALPHA = 6
EVENT_COLOR = 7
EVENT_VELOCITY_X = 8
EVENT_VELOCITY_Y = 9
EVENT_VELOCITY_Z = 10
EVENT_VELOCITY = 11
EVENT_TEXTURE = 12
EVENT_ROTATION = 13

# floats after the common (time min, time max, fade) header, per event type
PAYLOAD = {EVENT_SIZE: 4, EVENT_TIMER: 2, EVENT_RED: 2, EVENT_GREEN: 2, EVENT_BLUE: 2,
           EVENT_ALPHA: 2, EVENT_COLOR: 8, EVENT_VELOCITY_X: 2, EVENT_VELOCITY_Y: 2,
           EVENT_VELOCITY_Z: 2, EVENT_VELOCITY: 6, EVENT_TEXTURE: 2, EVENT_ROTATION: 2}

_HEADER_U32 = ("num_particles", "align", "update_coord", "tex_width", "tex_height",
               "implement", "dest_blend", "src_blend", "blend_op")


class Event:
    __slots__ = ("type", "time", "fade", "values")

    def __init__(self, type, time, fade, values):
        self.type, self.time, self.fade, self.values = type, list(time), fade, list(values)


class Sequence:
    """One emitter sequence. Floats are kept as Python floats (exact for f32)."""

    def __init__(self, **kw):
        self.name = kw["name"]
        self.life = list(kw["life"])
        self.emit_rate = list(kw["emit_rate"])
        self.loops = kw["loops"]
        self.spawn_dir = list(kw["spawn_dir"])
        self.emit_radius = list(kw["emit_radius"])
        self.gravity = list(kw["gravity"])
        self.texture = kw["texture"]
        for k in _HEADER_U32:
            setattr(self, k, kw[k])
        self.events = list(kw["events"])

    def events_of(self, type):
        return [e for e in self.events if e.type == type]


def parse(b):
    """(sequences, tail) -- ``tail`` is whatever follows the last sequence."""
    b = read_file(b)
    o = 0

    def take(fmt):
        nonlocal o
        v = struct.unpack_from(fmt, b, o)
        o += struct.calcsize(fmt)
        return v

    def lstr():
        nonlocal o
        n, = take("<i")
        if n < 0 or o + n > len(b):
            raise FormatError("ptl: bad string length %d at %d" % (n, o - 4))
        s = b[o:o + n]
        o += n
        return s

    try:
        count, = take("<I")
        seqs = []
        for _ in range(count):
            kw = {"name": lstr()}
            kw["life"] = take("<2f")
            kw["emit_rate"] = take("<2f")
            kw["loops"], = take("<i")
            kw["spawn_dir"] = take("<6f")
            kw["emit_radius"] = take("<6f")
            kw["gravity"] = take("<6f")
            kw["texture"] = lstr()
            kw.update(zip(_HEADER_U32, take("<9I")))
            nev, = take("<I")
            events = []
            for _ in range(nev):
                t, = take("<I")
                if t not in PAYLOAD:
                    raise FormatError("ptl: unknown event type %d at %d" % (t, o - 4))
                tmin, tmax, fade = take("<2fB")
                events.append(Event(t, (tmin, tmax), fade, take("<%df" % PAYLOAD[t])))
            kw["events"] = events
            seqs.append(Sequence(**kw))
    except struct.error as ex:
        raise FormatError("ptl: truncated (%s)" % ex)
    return seqs, b[o:]


def build(seqs, tail=b""):
    out = [struct.pack("<I", len(seqs))]
    for s in seqs:
        out.append(struct.pack("<i", len(s.name)) + s.name)
        out.append(struct.pack("<2f2fi", *s.life, *s.emit_rate, s.loops))
        out.append(struct.pack("<18f", *s.spawn_dir, *s.emit_radius, *s.gravity))
        out.append(struct.pack("<i", len(s.texture)) + s.texture)
        out.append(struct.pack("<9I", *(getattr(s, k) for k in _HEADER_U32)))
        out.append(struct.pack("<I", len(s.events)))
        for e in s.events:
            if len(e.values) != PAYLOAD[e.type]:
                raise FormatError("ptl: event type %d needs %d values, has %d"
                                  % (e.type, PAYLOAD[e.type], len(e.values)))
            out.append(struct.pack("<I2fB", e.type, *e.time, e.fade))
            out.append(struct.pack("<%df" % len(e.values), *e.values))
    out.append(tail)
    return b"".join(out)
