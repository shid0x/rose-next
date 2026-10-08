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

CERBERUS = frozenset({2682, 2683})

EXCLUDED = SHIBUYA | ULVERICK | CERBERUS


def excluded(row):
    return row in EXCLUDED
