#!/usr/bin/env python3
"""The crypt's toll: a Key of Remorse (2026-11-13).

The keepers of the Karkia catacombs used to ask five Black Hearts, a placeholder ("just
some Karkia drop as a temp thing", the user, 2026-11-09). The user, after playing it:
"import some material from Jrose, have it drop by monsters in Karkia and ask for it
instead of black heart, to feel more authentic".

  * **The item** is Jrose's LIST_NATURAL 1385, 悔恨の鍵 ("key of remorse"): a key, and
    named for what the two crypts are named for (Reminiscence, Sorrow). Jrose's client
    carries no drop tables for its Karkia (rows 854 / 831 of its ITEM_DROP are blank:
    its drops were the server's), so what it opened there is not known; here it opens
    the crypt. It lands on our LIST_NATURAL **150** (blank, under 1000, beside the
    Hearts it replaces) through `import-item.py --art-only --template-row 151`: every
    column is Black Hearts' (a material, type 429, the same price and weight), only the
    icon is Jrose's, copied into our atlas.
  * **The price is one**, once a day, where it was five Black Hearts: a key opens a door.
    src/sho_gameserver/src/catacombs.cpp takes it (TOLL_NO / TOLL_COUNT / TOLL_NAME: **the
    constants there and here must agree**); the keeper's dialog (import-catacombs.py,
    DIALOG) says so.
  * **Who drops it**: the Cemetery's monsters, in the slot Black Hearts had, and the
    Spire's (`add-karkia-drops.py`: MAT_CEMETERY, MAT_SPIRE; re-apply with `--rewrite`).
    One material slot in eight, so about as often as a key is wanted. Not the crypt's own
    monsters -- the crypt does not pay its own way in -- and not the flashback zones,
    which are Karkia before there was anything to regret.

Applied by `import-catacombs.py`; then `add-karkia-drops.py --rewrite`.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
import catacomb_pool as pool  # noqa: E402

NATURAL_STB_REL = r"3DDATA\STB\LIST_NATURAL.STB"
NATURAL_STL_REL = r"3DDATA\STB\LIST_NATURAL_S.STL"
KEY_ROW = 150                       # blank until now; catacombs.cpp TOLL_NO
KEY_SRC = 1385                      # Jrose LIST_NATURAL
KEY_TEMPLATE = 151                  # Black Hearts: the toll it replaces
KEY_NAME = "Key of Remorse"
KEY_DESC = "A key cut for a crypt door. The keepers of the Karkia catacombs take one as their toll."
KEY_COUNT = 1                       # catacombs.cpp TOLL_COUNT
ICON_BAK_REL = r"3DDATA\CONTROL\RES\ITEM1.TSI.bak-icons"
TRACKED = [NATURAL_STB_REL, NATURAL_STL_REL]


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def apply(ours, dry, src=pool.JROSE):
    n = oro.Stb(P(ours, NATURAL_STB_REL))
    cur = n.get(KEY_ROW, 0).decode("latin-1").strip()
    if cur == KEY_NAME:
        print(f"    {'LIST_NATURAL.STB':26s} row {KEY_ROW} {KEY_NAME} already in place")
        return
    if cur:
        raise SystemExit(f"LIST_NATURAL row {KEY_ROW} is {cur!r}: not free")
    cmd = [sys.executable, os.path.join(HERE, "import-item.py"), "--type", "natural", "--source", src,
           "--source-row", str(KEY_SRC), "--art-only", "--template-row", str(KEY_TEMPLATE), "--copy-icon",
           "--target-row", str(KEY_ROW), "--name", KEY_NAME, "--desc", KEY_DESC]
    if dry:
        cmd.append("--dry-run")
    print(f"    {'import-item.py':26s} natural {KEY_SRC} -> {KEY_ROW} {KEY_NAME}")
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in (res.stdout + res.stderr).strip().splitlines()[-4:]:
        print("        " + line)
    if res.returncode:
        raise SystemExit("import-item.py failed")
    # the icon copy leaves the atlas's backup beside it, and pack.ps1 refuses to bake over one
    bak = P(ours, ICON_BAK_REL)
    if os.path.isfile(bak):
        os.makedirs(os.path.join(ROOT, "build", "catacombs", "bak"), exist_ok=True)
        os.replace(bak, os.path.join(ROOT, "build", "catacombs", "bak", "ITEM1.TSI.bak-icons-key-of-remorse"))


def verify(ours, check):
    n = oro.Stb(P(ours, NATURAL_STB_REL))
    icon = n.get(KEY_ROW, 9).decode("latin-1").strip()
    check(n.get(KEY_ROW, 0).decode("latin-1").strip() == KEY_NAME and icon.isdigit() and int(icon) > 0
          and n.get(KEY_ROW, 4) == n.get(KEY_TEMPLATE, 4),
          f"LIST_NATURAL {KEY_ROW} {KEY_NAME} (a material, with an icon)")
    stl = oro.Stl(P(ours, NATURAL_STL_REL))
    check(stl.has("LNAT%d" % KEY_ROW), f"LIST_NATURAL_S has LNAT{KEY_ROW}")


if __name__ == "__main__":
    print(__doc__)
