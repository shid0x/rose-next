"""Zero the out-of-range bone slots in skinned meshes (unused influences written as 0xFFFF).

Symptom: a client crash (ACCESS_VIOLATION in znzin!zz_mesh_tool::load_mesh_8,
`movzx ecx, word ptr [edi+eax*2]` with eax = 0xFFFF) the first time the Cerberus
Lair boss is drawn -- seen in the 2026-11-01 test session, 10 seconds before a
tester's client died in the same fight. Most of the time it does not crash at all,
which is why it took a group test to show up.

Cause: every skinned vertex carries four bone slots, and each slot indexes the
mesh's own bone table (`u16 bone_count + u16 bone each`, a handful of entries).
The loader resolves all four through that table, whatever their weight
(zz_mesh_tool.cpp, `mesh->bone_indices[usxyzw.*]`), with no bounds check. Classic
ROSE meshes fill an unused slot with 0 at weight 0; some exporters write 0xFFFF
instead. 0xFFFF reads 128 KB past a ten-byte vector -- usually a page that is
still mapped, so the loader reads garbage and carries on (the value only feeds a
max that is never used), and only crashes when that page happens to be free.
The shader would also receive 65535 as a blend index for that slot, harmless only
because its weight is 0.

Found by scanning all 8,417 packed .ZMS: one mesh, imported with the Cerberus
model from tsuki (scripts/import-cerberus.py copies NPC\\ANIMAL\\CERBERUS as is):

  3DDATA\\NPC\\ANIMAL\\CERBERUS\\HEAD01.ZMS  5 bones, 840 vertices, 7 vertices
      (79, 92, 93, 95, 136, 141, 143) with slots [65535, 1, 0, 0] and weights
      [0, 1, 0, 0]

Fix: a slot whose index is >= the bone count AND whose weight is 0 is set to 0,
which is what a classic mesh holds there; the vertex deforms exactly as before.
A bad slot with a non-zero weight is reported and left alone -- that mesh needs
its skin looked at, not a silent rebind. The engine now clamps such slots at load
as well (and logs the mesh once), so this script is about keeping the data honest
and the log quiet, not the only line of defence.

Scans every .ZMS under data/3DDATA (so it also covers whatever the next import
brings in); re-run it after importing a skinned model from another dump.
Idempotent; --dry-run previews. Originals go to build/zms-bone-slots/orig/ (never
a .bak under data/). Verified after writing by re-parsing each file. Re-bake the
client VFS afterwards; the server never reads meshes.

    python scripts/fix-zms-bone-slots.py --dry-run
    python scripts/fix-zms-bone-slots.py
"""
import argparse
import os
import shutil
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)

from rosefmt import zms  # noqa: E402

DATA_3D = os.path.join(REPO, "data", "3DDATA")
BACKUP_DIR = os.path.join(REPO, "build", "zms-bone-slots", "orig")


def scan(path):
    """(mesh, slots, weights, zero_mask, weighted_mask) or None for an unskinned mesh."""
    blob = open(path, "rb").read()
    m = zms.parse(blob)
    if m.bone_slots is None or m.weights is None:
        return None
    slots = np.asarray(m.bone_slots).reshape(-1, 4)
    weights = np.asarray(m.weights).reshape(-1, 4)
    out_of_range = slots >= len(m.bones)
    zero = out_of_range & (weights == 0.0)
    weighted = out_of_range & (weights != 0.0)
    return blob, m, slots, zero, weighted


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    a = ap.parse_args()

    paths = []
    for root, _, names in os.walk(DATA_3D):
        for nm in names:
            if nm.upper().endswith(".ZMS"):
                paths.append(os.path.join(root, nm))
    paths.sort()

    fixed, skipped, failed = [], [], []
    for p in paths:
        rel = os.path.relpath(p, REPO)
        try:
            r = scan(p)
        except Exception as e:  # a mesh the codec cannot read is not ours to judge here
            failed.append((rel, str(e)))
            continue
        if r is None:
            continue
        blob, m, slots, zero, weighted = r
        if weighted.any():
            verts = sorted(set(int(v) for v in np.where(weighted.any(axis=1))[0]))
            skipped.append((rel, len(m.bones), verts))
        if not zero.any():
            continue
        verts = sorted(set(int(v) for v in np.where(zero.any(axis=1))[0]))
        fixed.append((rel, len(m.bones), int(zero.sum()), verts))
        if a.dry_run:
            continue

        backup = os.path.join(BACKUP_DIR, os.path.relpath(p, os.path.join(REPO, "data")))
        if not os.path.exists(backup):  # keep the first original across re-runs
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copyfile(p, backup)

        new_slots = slots.copy()
        new_slots[zero] = 0
        m.bone_slots = new_slots.astype(np.asarray(m.bone_slots).dtype).reshape(np.asarray(m.bone_slots).shape)
        out = zms.build(m)
        if len(out) != len(blob):
            raise SystemExit("%s: rebuilt size %d != original %d -- not written" % (rel, len(out), len(blob)))
        with open(p, "wb") as fh:
            fh.write(out)

        check = scan(p)
        if check is None or check[3].any():
            raise SystemExit("%s: verify failed after writing (restore from %s)" % (rel, backup))

    print("scanned %d mesh(es) under %s" % (len(paths), os.path.relpath(DATA_3D, REPO)))
    for rel, nb, n, verts in fixed:
        print("  %s %s: %d unused slot(s) >= %d bones on vertices %s" % (
            "would fix" if a.dry_run else "fixed", rel, n, nb, verts))
    for rel, nb, verts in skipped:
        print("  SKIPPED %s: out-of-range slot WITH weight (%d bones) on vertices %s -- check the skin" % (
            rel, nb, verts[:20]))
    for rel, err in failed:
        print("  unreadable %s: %s" % (rel, err))
    if not fixed:
        print("nothing to fix")
    elif a.dry_run:
        print("dry run: nothing written")
    else:
        print("originals in %s; re-bake the client VFS" % os.path.relpath(BACKUP_DIR, REPO))


if __name__ == "__main__":
    main()
