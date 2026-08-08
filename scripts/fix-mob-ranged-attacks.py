"""Fix the two monsters whose ranged attack produces nothing at all.

Both were found by cross-checking every NPC's attack motion against the weapon it
carries: a motion emitting action 22 (bow) or 23 (gun) needs WEAPON_BULLET_EFFECT
on that weapon, because Add_BULLET is what spawns the projectile *and* the bullet
impact is the only thing that calls Hitted(). Without it the monster swings, no
projectile appears, no hit frame fires, and the server-applied damage is never
presented -- HP just drifts down.

Unlike scripts/fix-mob-bullet-effects.py, the reference dumps do **not** agree
here, so both values below are deliberate choices rather than a restore:

1. Lunar Keeper (NPC 384/385/386, spawned in LP03/LP04). Gun motion, 13.5-15 m
   attack range -- but our NPC rows point at *player* weapon items:
       384 -> 122 "Moon Sword"   (a two-handed sword, 617750 zulies)
       385 -> 127 (an entirely empty row)
       386 -> 131 "Woodman Axe"
   ruff and titan point all three at 1079 "Candle Ghost Weapons"
   (mobwpn/mob_can) whose BULLET_EFFECT 439 is a ghost projectile. QQ-iROSE
   shares our broken mapping. Taking ruff/titan: it is the only variant where
   the attack does anything, and 1079's mesh and effect both exist in our data.
   This changes what the Keepers visibly hold, which is the point -- a Keeper
   carrying a player greatsword was never intended.

2. Sikuku Infiltrator (NPC 1721, EZ01). Bow motion with **no** melee action
   frame, so with no bullet effect its attacks do literally nothing. All three
   references agree weapon 1148 needs one but each picks a different projectile
   (ruff 164 twisting bow, QQ 162 fairy bow, titan 153 plain arrow). Chosen: 162.

Pre-flight validates the whole chain before writing -- the weapon row exists, its
ZSC object has a mesh present on disk, and the effect resolves through
LIST_EFFECT -> FILE_EFFECT to real .eft files -- and re-checks it afterwards.

Idempotent, backs up to build/ (outside data/, which pack.rs would bake into the
.vfs). Both client and server read these tables: rebake the VFS and restart the
servers.
"""
import argparse, os, shutil, struct, sys, time

OUR_DATA = "data"
NPC_STB = os.path.join("3DDATA", "STB", "LIST_NPC.STB")
WEAPON_STB = os.path.join("3DDATA", "STB", "LIST_WEAPON.STB")
EFFECT_STB = os.path.join("3DDATA", "STB", "LIST_EFFECT.STB")
FILE_EFFECT_STB = os.path.join("3DDATA", "STB", "FILE_EFFECT.STB")
WEAPON_ZSC = os.path.join("3DDATA", "WEAPON", "LIST_WEAPON.ZSC")

NPC_R_WEAPON = 5
BULLET_EFFECT = 38
EFFECT_BULLET_NORMAL = 11
EFFECT_HITTED_NORMAL = 9

# NPC row -> weapon row it should carry
NPC_WEAPON_FIXES = {384: b"1079", 385: b"1079", 386: b"1079"}
# weapon row -> bullet effect
WEAPON_BULLET_FIXES = {1148: b"162"}


def stb_read(path):
    raw = open(path, "rb").read()
    off, rows, cols = struct.unpack_from("<III", raw, 4)
    o = off
    data = []
    for _ in range(rows - 1):
        row = []
        for _ in range(cols - 1):
            n, = struct.unpack_from("<H", raw, o); o += 2
            row.append(raw[o:o + n]); o += n
        data.append(row)
    return raw, off, data


def stb_write(path, raw, off, data):
    out = [raw[:off]]
    for row in data:
        for c in row:
            out.append(struct.pack("<H", len(c)) + c)
    open(path, "wb").write(b"".join(out))


def zsc_meshes(path, obj):
    """mesh paths of one LIST_WEAPON.ZSC object, or None if absent"""
    d = open(path, "rb").read()
    o = 0

    def u16():
        nonlocal o
        v, = struct.unpack_from("<H", d, o); o += 2; return v

    def i32():
        nonlocal o
        v, = struct.unpack_from("<i", d, o); o += 4; return v

    def u8():
        nonlocal o
        v = d[o]; o += 1; return v

    def cstr():
        nonlocal o
        e = d.index(b"\0", o); s = d[o:e]; o = e + 1; return s

    def skip(n):
        nonlocal o
        o += n

    def props():
        """per-part property list: u8 tag, then u8 len + payload, until tag 0"""
        t = u8()
        while t:
            skip(u8())
            t = u8()

    meshes = [cstr() for _ in range(u16())]
    for _ in range(u16()):
        cstr(); skip(9 * 2 + 4 + 2 + 12)
    n_eft = u16()
    for _ in range(n_eft if n_eft > 0 else 0):
        cstr()
    n_obj = u16()
    for i in range(n_obj):
        i32(); i32(); i32()
        n_part = u16()
        if n_part == 0:
            if i == obj:
                return []
            continue
        parts = []
        for _ in range(n_part):
            m = u16(); u16(); props(); parts.append(m)
        for _ in range(u16()):
            u16(); u16(); props()
        skip(24)
        if i == obj:
            return [meshes[m] for m in parts if m < len(meshes)]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.join(args.root, OUR_DATA)
    npath = os.path.join(root, NPC_STB)
    wpath = os.path.join(root, WEAPON_STB)
    for p in (npath, wpath):
        if not os.path.isfile(p):
            raise SystemExit(f"not found: {p}")

    nraw, noff, npc = stb_read(npath)
    wraw, woff, wpn = stb_read(wpath)
    _, _, eff = stb_read(os.path.join(root, EFFECT_STB))
    _, _, fe = stb_read(os.path.join(root, FILE_EFFECT_STB))

    def check_effect(e, label):
        i = int(e)
        if i >= len(eff):
            raise SystemExit(f"{label}: effect {i} out of LIST_EFFECT ({len(eff)})")
        for col, what in ((EFFECT_BULLET_NORMAL, "bullet"), (EFFECT_HITTED_NORMAL, "hit")):
            v = eff[i][col].strip()
            if not v.isdigit() or int(v) == 0:
                continue
            fi = int(v)
            if fi >= len(fe):
                raise SystemExit(f"{label}: {what} file idx {fi} out of FILE_EFFECT")
            p = fe[fi][1].decode("latin-1")
            if p and not os.path.exists(os.path.join(root, p.replace(chr(92), os.sep))):
                raise SystemExit(f"{label}: {what} .eft missing: {p}")
        return True

    plan = []
    # --- NPC -> weapon repoints
    for n, want in NPC_WEAPON_FIXES.items():
        have = npc[n][NPC_R_WEAPON].strip()
        if have == want:
            continue
        w = int(want)
        if w >= len(wpn):
            raise SystemExit(f"NPC {n}: weapon row {w} does not exist")
        be = wpn[w][BULLET_EFFECT].strip()
        if not be.isdigit() or int(be) == 0:
            raise SystemExit(f"NPC {n}: weapon {w} has no BULLET_EFFECT")
        check_effect(be, f"weapon {w}")
        meshes = zsc_meshes(os.path.join(root, WEAPON_ZSC), w)
        if not meshes:
            raise SystemExit(f"NPC {n}: LIST_WEAPON.ZSC object {w} has no model")
        for m in meshes:
            mp = m.decode("latin-1").replace("\\", "/")
            if not os.path.exists(os.path.join(root, mp)):
                raise SystemExit(f"NPC {n}: weapon mesh missing: {mp}")
        plan.append(("npc", n, have, want,
                     f"{npc[n][0].decode('latin-1')}  weapon {have.decode() or '-'} -> {want.decode()} "
                     f"(bullet {be.decode()}, mesh {meshes[0].decode('latin-1')})"))

    # --- weapon bullet effects
    for w, want in WEAPON_BULLET_FIXES.items():
        have = wpn[w][BULLET_EFFECT].strip()
        if have == want:
            continue
        check_effect(want, f"weapon {w}")
        plan.append(("wpn", w, have, want,
                     f"weapon {w} BULLET_EFFECT {have.decode() or '-'} -> {want.decode()}"))

    print(f"changes: {len(plan)}")
    for kind, idx, have, want, desc in plan:
        print(f"   {desc}")
    if not plan:
        print("nothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(args.root, "build", f"mob-ranged-backup-{stamp}")
    os.makedirs(backup, exist_ok=True)
    shutil.copy2(npath, os.path.join(backup, "LIST_NPC.STB"))
    shutil.copy2(wpath, os.path.join(backup, "LIST_WEAPON.STB"))

    for kind, idx, _, want, _ in plan:
        if kind == "npc":
            npc[idx][NPC_R_WEAPON] = want
        else:
            wpn[idx][BULLET_EFFECT] = want
    if any(k == "npc" for k, *_ in plan):
        stb_write(npath, nraw, noff, npc)
    if any(k == "wpn" for k, *_ in plan):
        stb_write(wpath, wraw, woff, wpn)

    _, _, vn = stb_read(npath)
    _, _, vw = stb_read(wpath)
    for kind, idx, _, want, _ in plan:
        got = vn[idx][NPC_R_WEAPON].strip() if kind == "npc" else vw[idx][BULLET_EFFECT].strip()
        if got != want:
            raise SystemExit(f"VERIFY FAILED: {kind} {idx} reads {got!r}")
    # and the whole chain still resolves
    for n in NPC_WEAPON_FIXES:
        w = int(vn[n][NPC_R_WEAPON])
        check_effect(vw[w][BULLET_EFFECT].strip(), f"verify NPC {n}")
    for w in WEAPON_BULLET_FIXES:
        check_effect(vw[w][BULLET_EFFECT].strip(), f"verify weapon {w}")
    print(f"\nwrote {len(plan)} change(s); backup at {backup}; verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
