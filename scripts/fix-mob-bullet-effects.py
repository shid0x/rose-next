"""Restore the missing WEAPON_BULLET_EFFECT values on ranged monsters' weapons.

Symptom: a ranged monster (e.g. NPC 1578 "Ikaness Leader" in map 64, Marsh of
Ghosts) plays its firing animation and its fire sound, but no projectile is ever
drawn -- and the hit itself never presents either.

Cause: the mob-weapon block of our LIST_WEAPON.STB is incomplete. For these rows
`WEAPON_BULLET_EFFECT` (game column 38) is empty where every reference iROSE data
dump has a value. Both sides of the game read that one cell:

  client  CObjCHAR::Get_BulletNO()                 -> WEAPON_BULLET_EFFECT(weapon)
          ActionBow case 22 / ActionGun case 23 only call g_pBltMGR->Add_BULLET()
          when it is non-zero, so no CBullet is created -> no projectile visual,
          and since Hitted() for a ranged attack is driven *only* by the bullet
          impact (bullet.cpp), the queued damage event is never consumed.
  server  CObjCHAR::UsesProjectileAttackPresentation()
          -> presentation_kind_for_normal_attack() picks MeleeHitFrame instead of
          ProjectileImpact, which nothing on a gun/bow motion consumes.

So restoring the cell fixes the visual and the damage presentation at once, on
both sides, with no code change.

The value is an index into LIST_EFFECT.STB, whose row supplies the bullet effect
file, the hit effect, move type and speed (see EFFECT_BULLET_* in
src/common/include/rose/io/stb.h). All referenced LIST_EFFECT rows and .eft files
already exist in our data -- only the weapon->effect link was missing.

Rows restored (value agreed by all three reference clients: QQ-iROSE, RoseZA test
client, titanRose):

  wpn 1115 -> 464   NPC 1583 Orpe, 1585 Orpe Lukete
  wpn 1119 -> 153   NPC 1584 Eudy, 1586 Eudy Lukete
  wpn 1120 -> 601   NPC 1581 Ikaness Engineer
  wpn 1123 -> 433   NPC 1578 Ikaness Leader, 1579 Ikaness Guard
  wpn 1126 -> 425   NPC 1456 Gangster Pang Little Jack, 1457 Gangster Pang Jack
  wpn 1134 -> 192   NPC 1458 Penguin Artillery
  wpn 1141 -> 463   NPC 1565/1566 Sikuku Gargoyle Shaman

Not restored (the references disagree, so the correct value is unclear): weapon
rows 759, 882, 1039, 1096, 1097, 1148.

Idempotent: a row that already holds the target value is left alone; a row that
holds a *different* non-zero value is reported and skipped rather than
overwritten. Makes a .bak backup. Use --dry-run to preview.

After running: restart the servers (STBs are cached at startup) and rebake/deploy
the client VFS data.

STB1 binary layout, for reference -- only the trailing data block is rewritten
here, so the header offset stays valid:
    "STB1" | u32 data_offset | u32 rows | u32 cols | ...header/name tables...
    at data_offset: (rows-1) * (cols-1) strings, each u16 length + raw bytes.
Note (rows-1)/(cols-1): STBDATA::load drops the header row and root column, so
data[r][c] read here is exactly what get_int32(r, c) returns in game.
"""
import argparse, io, os, shutil, struct, sys

STB_REL = os.path.join("data", "3DDATA", "STB", "LIST_WEAPON.STB")

BULLET_EFFECT_COL = 38  # WEAPON_BULLET_EFFECT -> g_TblWEAPON.get_int32(I, 5 + 33)

# weapon row -> LIST_EFFECT row
FIXES = {
    1115: 464,
    1119: 153,
    1120: 601,
    1123: 433,
    1126: 425,
    1134: 192,
    1141: 463,
}


def stb_read(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    f = io.BytesIO(raw)
    if f.read(4) != b"STB1":
        raise SystemExit(f"{path}: not an STB1 file")
    offset, rows, cols = struct.unpack("<III", f.read(12))
    f.seek(offset)

    def pstr():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)

    data = [[pstr() for _ in range(cols - 1)] for _ in range(rows - 1)]
    return raw, offset, data


def stb_write(path, raw, offset, data):
    out = io.BytesIO()
    out.write(raw[:offset])
    for row in data:
        for cell in row:
            out.write(struct.pack("<H", len(cell)))
            out.write(cell)
    with open(path, "wb") as fh:
        fh.write(out.getvalue())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    args = ap.parse_args()

    path = os.path.join(args.root, STB_REL)
    if not os.path.isfile(path):
        raise SystemExit(f"not found: {path} (run from the repo root or pass --root)")

    raw, offset, data = stb_read(path)
    print(f"{STB_REL}: {len(data)} rows x {len(data[0])} cols")

    changed = 0
    for row, effect in sorted(FIXES.items()):
        if row >= len(data):
            print(f"  wpn {row}: row out of range -- SKIPPED")
            continue
        cur = data[row][BULLET_EFFECT_COL].decode("latin-1")
        want = str(effect)
        if cur == want:
            print(f"  wpn {row}: already {want} -- ok")
            continue
        if cur not in ("", "0"):
            print(f"  wpn {row}: holds {cur!r}, not empty -- SKIPPED (refusing to overwrite)")
            continue
        print(f"  wpn {row}: {cur!r} -> {want}")
        data[row][BULLET_EFFECT_COL] = want.encode("latin-1")
        changed += 1

    if not changed:
        print("nothing to do")
        return
    if args.dry_run:
        print(f"dry run: {changed} cell(s) would change")
        return

    shutil.copyfile(path, path + ".bak")
    stb_write(path, raw, offset, data)

    _, _, verify = stb_read(path)
    for row, effect in sorted(FIXES.items()):
        got = verify[row][BULLET_EFFECT_COL].decode("latin-1")
        if got != str(effect):
            raise SystemExit(f"VERIFY FAILED: wpn {row} reads {got!r}, expected {effect}")
    print(f"wrote {changed} cell(s); backup at {os.path.basename(path)}.bak; verified")


if __name__ == "__main__":
    sys.exit(main())
