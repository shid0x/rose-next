"""Make WARP.STB destination names byte-match the .ZON entries they look up.

Symptom: walking into certain warp gates disconnects you ("server disconnected")
instead of teleporting. Affected in our data: Xita Refuge <-> Shady Jungle
(warps 126/127/128/129) and Sikuku Prison -> Forest of Wandering (135).

Cause: an encoding mismatch, not a missing destination. A warp row names an event
position in the destination zone; `classUSER::Recv_cli_TELEPORT_REQ` resolves it
through `CZoneLIST::Get_EventPOS`, which does `StrToHashKey(szPosName)` -- a hash
over the raw bytes. Our `WARP.STB` stores the Korean 시 ("o'clock") suffix as
UTF-8 (`ec 8b 9c`) while the `.ZON` files store it as CP949 (`bd c3`). Same text,
different bytes, so the hash misses, `Get_EventPOS` returns NULL and the server
treats it as a hacking attempt and drops the client.

Only names containing non-ASCII are affected, which is why every pure-ASCII
Eldeon warp (130-134) always worked and exactly the five with a 시 suffix failed.

This rewrites the STB cell to the bytes already present in the destination .ZON --
the .ZON is map data and the authority here, and it is the smaller, safer edit.
Rows whose destination genuinely does not exist in the .ZON are reported and left
alone: in our data those are warps 37, 43, 70 and 90-98, which have no gate object
anywhere, so no player can reach them (see scripts/restore-warp-gates.py).

Idempotent, backs up WARP.STB to build/ (outside data/, which pack.rs would bake
into the .vfs), verifies after writing. Server-side table: restart the servers.
"""
import argparse, os, shutil, struct, sys, time

OUR_DATA = "data"
WARP_STB = os.path.join("3DDATA", "STB", "WARP.STB")
ZONE_STB = os.path.join("3DDATA", "STB", "LIST_ZONE.STB")
LUMP_EVENT_OBJECT = 1
BS = chr(92)


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
    return raw, off, rows, cols, data


def stb_write(path, raw, off, data):
    out = [raw[:off]]
    for row in data:
        for c in row:
            out.append(struct.pack("<H", len(c)) + c)
    open(path, "wb").write(b"".join(out))


def zon_event_names(path):
    """LUMP_EVENT_OBJECT: i32 count, then count x (3 floats, u8 len + name)"""
    d = open(path, "rb").read()
    n, = struct.unpack_from("<i", d, 0)
    tab = [struct.unpack_from("<ii", d, 4 + 8 * i) for i in range(n)]
    for t, off in tab:
        if t != LUMP_EVENT_OBJECT:
            continue
        cnt, = struct.unpack_from("<i", d, off)
        o = off + 4
        names = []
        for _ in range(cnt):
            o += 12
            ln = d[o]; o += 1
            names.append(d[o:o + ln]); o += ln
        return names
    return []


def ascii_of(b):
    return bytes(c for c in b if 32 <= c < 127)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = os.path.join(args.root, OUR_DATA)
    wpath = os.path.join(root, WARP_STB)
    for p in (wpath, os.path.join(root, ZONE_STB)):
        if not os.path.isfile(p):
            raise SystemExit(f"not found: {p}")

    wraw, woff, wrows, wcols, warp = stb_read(wpath)
    _, _, _, _, zone = stb_read(os.path.join(root, ZONE_STB))

    cache = {}
    def names(zn):
        if zn not in cache:
            rel = zone[zn][1].decode("latin-1").replace(BS + BS, os.sep).replace(BS, os.sep) \
                if zn < len(zone) else ""
            p = os.path.join(root, rel)
            cache[zn] = zon_event_names(p) if rel and os.path.isfile(p) else None
        return cache[zn]

    fixes, missing, ok = [], [], 0
    for w in range(len(warp)):
        dest, pos = warp[w][1], warp[w][2]
        if not dest.isdigit() or int(dest) <= 0 or not pos:
            continue
        zn = int(dest)
        ns = names(zn)
        if ns is None:
            continue
        if pos in ns:
            ok += 1
            continue
        cand = [n for n in ns if ascii_of(n) == ascii_of(pos)]
        if len(cand) == 1:
            fixes.append((w, zn, pos, cand[0]))
        elif len(cand) > 1:
            print(f"   !! warp {w}: {len(cand)} candidates, skipping (ambiguous)")
        else:
            missing.append((w, zn, pos))

    print(f"warp rows resolving exactly      : {ok}")
    print(f"byte-mismatched (fixable)        : {len(fixes)}")
    print(f"destination absent from the .ZON : {len(missing)} (left alone; these have no gate object)")
    for w, zn, a, b in fixes:
        print(f"   warp {w:3d} -> zone {zn}")
        print(f"      was : {a!r}")
        print(f"      now : {b!r}")

    if not fixes:
        print("\nnothing to do")
        return 0
    if args.dry_run:
        print("\ndry run: nothing written")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = os.path.join(args.root, "build", f"warp-encoding-backup-{stamp}")
    os.makedirs(backup, exist_ok=True)
    shutil.copy2(wpath, os.path.join(backup, "WARP.STB"))

    for w, _, _, good in fixes:
        warp[w][2] = good
    stb_write(wpath, wraw, woff, warp)

    _, _, _, _, verify = stb_read(wpath)
    for w, zn, _, good in fixes:
        if verify[w][2] != good:
            raise SystemExit(f"VERIFY FAILED: warp {w}")
        if verify[w][2] not in names(zn):
            raise SystemExit(f"VERIFY FAILED: warp {w} still does not resolve in zone {zn}")
    still = [w for w in range(len(verify))
             if verify[w][1].isdigit() and int(verify[w][1]) > 0 and verify[w][2]
             and names(int(verify[w][1])) is not None
             and verify[w][2] not in names(int(verify[w][1]))
             and any(ascii_of(n) == ascii_of(verify[w][2]) for n in names(int(verify[w][1])))]
    if still:
        raise SystemExit(f"VERIFY FAILED: {len(still)} byte-mismatch(es) remain")
    print(f"\nfixed {len(fixes)} row(s); backup at {backup}; verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
