"""LIST_NPC rows every balance pass keeps out of its level trend.

The monster passes -- rebalance-karkia.py, rebalance-oro-667.py,
rebalance-endgame-curve.py, rebalance-oro-bosses.py, rebalance-eldeon-outliers.py
(since 2026-10-04, when the Ulverick import moved its DEF fit) -- fit a stat-vs-level trend
on the live table's level 60-199 rows, and rebalance-exp-rewards.py fits its tier
medians the same way. Each recorded its result in a sidecar and verifies against
a re-fit. So a later import that lands monsters in that window moves the trend
and every one of those verifies drifts, although no recorded row changed: the
SHIBUYA Deaders (lv60-100, 2026-09-29) shifted all five, 35 to 83 rows each,
and hiding them brought every one back to 0 mismatches.

An import is measured against the trend; it does not move it. Add its ids here
rather than to each script. (Oro's own band 2100-2399 predates this module and is
still excluded inline where it always was -- moving it would re-fit the passes
that have always included it.)

Loaded by path, like every other shared script here:
    spec = importlib.util.spec_from_file_location(
        "balance_trend_exclude", os.path.join(HERE, "balance-trend-exclude.py"))
"""

# import-shibuya.py stage 4: the twelve Deaders (DEADERS).
SHIBUYA = frozenset({1833, 1947, 1948, 4060, 4061, 4062, 4063, 4064, 4065, 4066,
                     4068, 4069})

# import-ulverick.py stage 2: the Cave of Ulverick's minion and three bosses (MONSTERS).
ULVERICK = frozenset({531, 532, 533, 534})

# import-cerberus.py stage 2: the Hellhound Whelps and the Warden of the Seal on the way
# in, Cerberus awake and asleep, and the Hellhounds it calls.
CERBERUS = frozenset({2680, 2681, 2682, 2683, 2684})

# import-necromancer.py: tsuki's Necromancer at level 120, stats from the trend by hand.
NECROMANCER = frozenset({2414})

# import-shadow-soldier.py: Jrose's Shadow Soldier at level 190, stats from the trend by hand.
SHADOW_SOLDIER = frozenset({2297})

# import-core-guardian.py: Jrose's Core Guardian at level 200, stats from the trend by hand.
CORE_GUARDIAN = frozenset({2337})

# import-amphisbaena.py: the Amphisbaena and the four monsters its AI summons.
AMPHISBAENA = frozenset({2911, 2914, 2915, 2920, 2921})

# import-wisdom-escort.py: the Tree Keeper's escort twin and the four ambushers, copies
# of forest monsters; the Grunter Blightchief (lv60) is a Grunter King at 3/4 of its HP.
WISDOM_ESCORT = frozenset({225, 226, 227, 228, 229})

EXCLUDED = (SHIBUYA | ULVERICK | CERBERUS | NECROMANCER | SHADOW_SOLDIER | CORE_GUARDIAN
            | AMPHISBAENA | WISDOM_ESCORT)


def excluded(row):
    return row in EXCLUDED


# --- the monster role layer (rebalance-monster-roles.py, 2026-10-15) ------------------
# Role profiles by icon are applied last, as a layer on top of every pass above. Those
# passes fit and verify against the table *without* it: they load LIST_NPC through
# without_roles(), and refuse to write while the layer is applied (restore the roles,
# run the pass, apply the roles again).
import json as _json
import os as _os

ROLE_MANIFEST = _os.path.join(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))),
                              "build", "monster-roles", "manifest.json")


def roles_applied():
    return _os.path.isfile(ROLE_MANIFEST)


def without_roles(stb):
    """The table as the passes left it: every role cell still holding the value the
    layer wrote goes back to its recorded pre-role value (in place; returns stb)."""
    if not roles_applied():
        return stb
    with open(ROLE_MANIFEST, encoding="utf-8") as fh:
        cells = _json.load(fh)["cells"]
    for key, (old, new) in cells.items():
        r, c = (int(x) for x in key.split(","))
        if stb.get(r, c).strip() == new.encode("latin-1"):
            stb.set(r, c, old.encode("latin-1"))
    return stb
