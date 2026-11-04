"""Make half-transparent map textures hard cut-outs (alpha 0 or 255 at every mip level).

    python scripts/set-texture-cutout.py --dry-run     what would change
    python scripts/set-texture-cutout.py               write it
    python scripts/set-texture-cutout.py --verify      check every file holds the cut-out
    python scripts/set-texture-cutout.py --restore     put the originals back

Why: a map material with texture alpha is alpha-blended, and since the engine's two-pass
alpha parts (src/engine/CLAUDE.md, "Transparency Antialiasing", 2026-11-04) its texels
under alpha 224 draw without depth. A surface meant to be solid but stored
half-transparent is then see-through, and whatever draws after it shows over it -- it
did before too, by draw order, only less often. The fix belongs to the texture: the
cut-out (`rosedata.texture.cutout_dds`) at the material's own alpha reference, every
level cut at the threshold that keeps level 0's coverage. Colour untouched.

**Junon's market tents, 2026-11-04** (the user, after the two-pass test: "the tents are
somewhat transparent, the wooden base shows through when the camera turns"). Two
colours of one model -- `PORTBURDEN.DDS` (green, LIST_DECO_JPT 226 `portburden` /
`portburdent`) and `PORTBURDEN2.DDS` (red and yellow, LIST_DECO_JPT 228 `portburden02` /
`portburden02t`) -- each an atlas of cloth (canopy and drapes) at a flat alpha ~204, 80%
opaque, and the wooden poles and ropes at 255. Retail's choice: 668, Jrose, Rose Brazil
and tsuki ship the same two files' alpha. The tables using
them: LIST_DECO_JG, LIST_DECO_JPT and four Karkia decoration tables, so every tent is
fixed at once. Materials: alpha 1, alpha test 1, reference 128.

Left alone on purpose: the other textures the 2026-11-04 scan found mostly
half-transparent (lights, crystals, glass, water, the Starlight Lounge's light cones):
they are meant to be see-through, and the two-pass draw renders them correctly.

A row is applied only if the file in data/ is the one reviewed (`ORIGINAL_SHA`), or is
already the cut-out of that original. Originals, hash-checked, in build/texture-cutout/.
Afterwards re-pack the client data (`scripts/pack.ps1`); servers do not read textures.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rosedata import texture  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
UNDO = os.path.join(ROOT, "build", "texture-cutout")

# data-relative path: (alpha reference, sha256 of the reviewed original, what it is)
CHANGES = {
    "3DDATA/JUNON/ETC/PORTBURDEN/PORTBURDEN.DDS": (
        128, "8bcb5bc21a83c54dbadd1914c696777c6ad3ad82e69d0102a54063989e775693",
        "Junon market tent, green: cloth at alpha ~204"),
    "3DDATA/JUNON/ETC/PORTBURDEN02/PORTBURDEN2.DDS": (
        128, "d98355e9b29913e69a7e0e67cbe77b356d7cc8f559d3e795993a2bf6dd04ff6e",
        "Junon market tent, red and yellow: cloth at alpha ~204"),
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    a = ap.parse_args()
    os.makedirs(UNDO, exist_ok=True)
    manifest_path = os.path.join(UNDO, "manifest.json")
    manifest = json.load(open(manifest_path)) if os.path.exists(manifest_path) else {}
    bad = 0

    for rel, (ref, orig_sha, what) in CHANGES.items():
        path = os.path.join(DATA, rel)
        orig = os.path.join(UNDO, rel.replace("/", "__"))
        have_orig = os.path.isfile(orig) and sha(orig) == orig_sha
        if a.restore:
            if not have_orig:
                raise SystemExit("%s: no intact original in %s" % (rel, UNDO))
            shutil.copyfile(orig, path)
            print("%s: restored the original" % rel)
            continue
        source = orig if have_orig else path
        if sha(source) != orig_sha:
            raise SystemExit("%s: not the reviewed original (sha %s) and no backup of it; "
                             "review the texture before cutting it" % (rel, sha(source)[:12]))
        want = texture.cutout_dds(source, ref)
        current = open(path, "rb").read()
        state = "already a cut-out" if current == want else "to cut"
        print("  %-48s %-46s %s" % (rel, what, state))
        if a.verify:
            bad += current != want
            continue
        if a.dry_run or current == want:
            continue
        if not have_orig:
            shutil.copyfile(path, orig)
            manifest[rel] = {"orig_sha": orig_sha}
            json.dump(manifest, open(manifest_path, "w"), indent=1)
        with open(path, "wb") as fh:
            fh.write(want)
        print("  %-48s written (original in %s)" % (rel, UNDO))

    if a.verify:
        print("verify " + ("OK" if not bad else "FAILED (%d)" % bad))
        return 1 if bad else 0
    if a.dry_run:
        print("dry run: nothing written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
