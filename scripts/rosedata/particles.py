"""Particle-system helpers the data scripts share.

* ``rolling_loop(blob)`` -- a looping .ptl whose cloud blinks out all at once, made to
  roll: births spread over one lifetime, each particle faded in and out.

Why (Haian Coast's giant tree, 2026-11-04: "the loop ends a bit abruptly"). China Rose's
``_bandy_runa_01.ptl`` has three looping sequences (``loops`` 0), each with one lifetime
for every particle and an emit rate far above what its slots can take (1000/s for 20).
The whole cloud spawns in one frame. Emission then waits for a free slot
(``zz_particle_event_sequence::update``), so the cohort lives, dies and respawns
together for as long as the effect lives, and with alpha 1 from birth to death every
particle vanishes in the same frame and the cloud reappears somewhere else: the cut.

What ``rolling_loop`` does to each sequence, of lifetime L (particle time: particles age
4.8x real time, zz_particle.cpp -- the emit rate is per real second) and n slots:

  * Births spread over one lifetime: emit rate n / (L / 4.8), plus 2% so a freed slot
    is refilled the frame it frees (the surplus is banked). Every slot keeps its own
    phase from then on, so a few particles are always fading in and a few out and
    nothing changes all at once.
  * Alpha 0 at birth, 1 by FADE_IN * L, held, 0 again at L. The hold is a fade event
    too: the engine ramps each event towards the next *fade* event of its type,
    skipping plain ones.
  * The colour event's alpha at birth set to the same 0: events at one time run in
    no fixed order (std::sort), so either may land last.
  * n scaled by 1 / the mean alpha (0.75), so the cloud gives off the light it did.

The cost: a new emitter fills in over one lifetime instead of at once. A map object's
effect starts when its chunk loads, which is mostly well before the player gets there.
The engine updates an in-scene emitter on screen or off (zz_scene_octree::update_time),
so the phases do not fall back into step while the camera looks away.

Used by ``import-china-rose.py`` (``rolling_effects``).
"""
from rosefmt import ptl

PARTICLE_AGE_RATE = 4.8     # particle time per real second (ZZ_TICK_PER_SEC 4800 / 1000)
FADE_IN = 0.2               # alpha 0 -> 1 over the first 20% of the life
FADE_OUT = 0.7              # held at 1 until 70%, back to 0 at death
MEAN_ALPHA = FADE_IN / 2 + (FADE_OUT - FADE_IN) + (1 - FADE_OUT) / 2      # 0.75
RATE_MARGIN = 1.02


def rolling_loop(blob):
    """The .ptl's bytes with every sequence made to roll (see the module docstring).
    Refuses a sequence it would not leave as authored: a finite one, a lifetime range,
    or an alpha already doing more than "1 from birth"."""
    seqs, tail = ptl.parse(blob)
    for i, s in enumerate(seqs):
        life = s.life[0]
        if s.loops != 0 or s.life[0] != s.life[1] or life <= 0:
            raise ValueError("sequence %d: loops %d, life %s -- not a looping one-lifetime sequence"
                             % (i, s.loops, s.life))
        alpha = s.events_of(ptl.EVENT_ALPHA)
        colour = s.events_of(ptl.EVENT_COLOR)
        if any(e.time != [0.0, 0.0] or e.fade for e in alpha + colour) \
                or any(e.values != [1.0, 1.0] for e in alpha) \
                or any((e.values[3], e.values[7]) != (1.0, 1.0) for e in colour):
            raise ValueError("sequence %d: its alpha is not simply 1 from birth" % i)
        n = round(s.num_particles / MEAN_ALPHA)
        rate = n / (life / PARTICLE_AGE_RATE) * RATE_MARGIN
        s.num_particles = n
        s.emit_rate = [rate, rate]
        for e in colour:
            e.values[3] = e.values[7] = 0.0
        s.events = [e for e in s.events if e.type != ptl.EVENT_ALPHA]
        s.events[0:0] = [ptl.Event(ptl.EVENT_ALPHA, (0.0, 0.0), 0, (0.0, 0.0)),
                         ptl.Event(ptl.EVENT_ALPHA, (life * FADE_IN,) * 2, 1, (1.0, 1.0)),
                         ptl.Event(ptl.EVENT_ALPHA, (life * FADE_OUT,) * 2, 1, (1.0, 1.0)),
                         ptl.Event(ptl.EVENT_ALPHA, (life,) * 2, 1, (0.0, 0.0))]
    return ptl.build(seqs, tail)
