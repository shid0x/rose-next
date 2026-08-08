"""Restore the melee hit effect and impact sound on monsters' weapons.

Symptom: a monster hits you, damage numbers appear, but there is no impact
effect and no hit sound. Reported for "Cursed Ant Vagabond" (NPC 1572).

Cause: the mob-weapon block of our LIST_WEAPON.STB is damaged. For a melee
attacker, CObjCHAR::ActionInFighting case 21 reads

    WEAPON_DEFAULT_EFFECT (game col 39) -> the effect handed to Hitted()
    WEAPON_ATK_HIT_SOUND  (game col 42) -> row into LIST_HITSOUND.STB, column
                                           chosen by the target's material type

and case 31 reads WEAPON_ATK_START_SOUND (col 40) for the swing. Damage is
server-authored and unaffected, which is why the numbers still show while the
effect and sound are silently missing.

Some rows are blank; others are shifted one column right. Weapon 1121, the
Cursed Ant Vagabond's, is the latter: every reference has 602/52/-/25 across
cols 39-42, ours has -/602/-/- , so DEFAULT_EFFECT and ATK_HIT_SOUND resolve to
0 and the value 602 (an effect id) is fed to the sound player as a start sound.

This aligns cols 39-42 with the references for weapon rows that an NPC actually
equips, but only where **all three** reference dumps (ruff, QQ-iROSE, titanRose)
agree on the value -- rows where they disagree are left alone. Both directions
are applied: filling a blank, and clearing a stale value the references do not
have (otherwise a shifted row keeps feeding an effect id to the sound player).

Every value is validated against the table that actually consumes it before
anything is written -- effect ids against LIST_EFFECT.STB, start/fire sounds
against FILE_SOUND.STB, hit sounds against LIST_HITSOUND.STB -- because the
columns index three different tables and a plausible-looking number can be out
of range for its own.

Player weapon rows are untouched: only rows referenced by LIST_NPC col 5 or 6
are considered, and none of those carries an item type, price or STL key.

Idempotent, backs up to build/ (outside data/, which pack.rs would otherwise
bake into the .vfs), verifies after writing. Both client and server read this
table, so rebake the VFS and restart the servers.
"""
import argparse, os, shutil, struct, sys, time

OUR_DATA = "data"
WEAPON_STB = os.path.join("3DDATA", "STB", "LIST_WEAPON.STB")
NPC_STB = os.path.join("3DDATA", "STB", "LIST_NPC.STB")
EFFECT_STB = os.path.join("3DDATA", "STB", "LIST_EFFECT.STB")
SOUND_STB = os.path.join("3DDATA", "STB", "FILE_SOUND.STB")
HITSOUND_STB = os.path.join("3DDATA", "STB", "LIST_HITSOUND.STB")

REFERENCES = {
    "ruff":  r"C:\Users\Thomas\Desktop\Testclients\ruff\extracted data",
    "QQ":    r"C:\Users\Thomas\Desktop\Testclients\QQ-iROSE Online\QQiroseData",
    "titan": r"C:\Users\Thomas\Desktop\Testclients\titanRose\data",
}

# game column -> (label, which table validates it)
COLUMNS = {
    39: ("WEAPON_DEFAULT_EFFECT", "effect"),
    40: ("WEAPON_ATK_START_SOUND", "sound"),
    41: ("WEAPON_ATK_FIRE_SOUND", "sound"),
    42: ("WEAPON_ATK_HIT_SOUND", "hitsound"),
}
NPC_WEAPON_COLS = (5, 6)


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


def cell(data, r, c):
    return data[r][c].strip() if r < len(data) and c < len(data[r]) else b""


def norm(v):
    """'' and '0' both mean 'nothing' in these columns"""
    return b"" if v in (b"", b"0") else v


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.join(args.root, OUR_DATA)
    wpath = os.path.join(root, WEAPON_STB)
    if not os.path.isfile(wpath):
        raise SystemExit(f"not found: {wpath}")

    wraw, woff, wpn = stb_read(wpath)
    _, _, npc = stb_read(os.path.join(root, NPC_STB))
    sizes = {
        "effect": len(stb_read(os.path.join(root, EFFECT_STB))[2]),
        "sound": len(stb_read(os.path.join(root, SOUND_STB))[2]),
        "hitsound": len(stb_read(os.path.join(root, HITSOUND_STB))[2]),
    }

    refs = {}
    for name, base in REFERENCES.items():
        p = os.path.join(base, WEAPON_STB)
        if not os.path.isfile(p):
            raise SystemExit(f"reference not found: {p}")
        refs[name] = stb_read(p)[2]

    used = {}
    for i in range(len(npc)):
        for c in NPC_WEAPON_COLS:
            v = cell(npc, i, c)
            if v.isdigit() and int(v) > 0:
                used.setdefault(int(v), []).append(i)

    fixes, rejected = [], []
    for r in sorted(used):
        for col, (label, table) in COLUMNS.items():
            vals = {norm(cell(t, r, col)) for t in refs.values()}
            if len(vals) != 1:
                continue                       # references disagree
            want = vals.pop()
            have = norm(cell(wpn, r, col))
            if have == want:
                continue
            if want:                            # validate before trusting it
                if not want.isdigit():
                    rejected.append((r, label, want, "not numeric")); continue
                i = int(want)
                if i >= sizes[table]:
                    rejected.append((r, label, want,
                                     f"out of range for {table} table ({sizes[table]} rows)"))
                    continue
            fixes.append((r, col, label, have, want, used[r]))

    print(f"weapon rows equipped by an NPC: {len(used)}")
    print(f"cells to change (all three references agree): {len(fixes)}")
    for r, col, label, have, want, npcs in fixes:
        who = ", ".join(f"{i}:{npc[i][0].decode('latin-1')}" for i in npcs[:2])
        act = "clear" if not want else f"-> {want.decode()}"
        print(f"   wpn {r:5d} {label:22s} {have.decode() or '-':>5} {act:>10}   {who}")
    if rejected:
        print(f"\nskipped {len(rejected)} value(s) that do not resolve in our tables:")
        for r, label, want, why in rejected:
            print(f"   wpn {r} {label} = {want.decode()}: {why}")

    if not fixes:
        print("\nnothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(args.root, "build", f"mob-hit-effects-backup-{stamp}")
    os.makedirs(backup, exist_ok=True)
    shutil.copy2(wpath, os.path.join(backup, "LIST_WEAPON.STB"))

    for r, col, _, _, want, _ in fixes:
        wpn[r][col] = want
    stb_write(wpath, wraw, woff, wpn)

    _, _, verify = stb_read(wpath)
    for r, col, label, _, want, _ in fixes:
        if norm(cell(verify, r, col)) != want:
            raise SystemExit(f"VERIFY FAILED: wpn {r} {label}")
    print(f"\nwrote {len(fixes)} cell(s); backup at {backup}; verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
