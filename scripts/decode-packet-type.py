#!/usr/bin/env python3
"""Decode a packet type id from the client's frame-spike log into its name.

The `pkt[n=.. worst=0x0796/50.3ms]` field in a `Frame spike:` line reports the
packet whose handler cost the most in that frame. Types are printed in hex so
they grep directly against src/common/net_prototype.h, which defines them as
`#define GSV_... 0x....`; this just does that lookup for you.

    python scripts/decode-packet-type.py 0x0796
    python scripts/decode-packet-type.py 0x0796 0x071b
    ... | python scripts/decode-packet-type.py --log client.log

With --log it reads a client log and annotates every Frame spike line's worst
packet in place, which is the usual way to read a capture.
"""
import argparse
import pathlib
import re
import sys

HEADER = pathlib.Path(__file__).resolve().parent.parent / "src" / "common" / "net_prototype.h"
DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+0x([0-9a-fA-F]+)")
WORST = re.compile(r"worst=0x([0-9a-fA-F]{4})")


# Client->server and server->client packets share the id space, so 0x0796 is
# both CLI_STOP and GSV_STOP. The log only ever reports *received* packets, so
# prefer the inbound-direction name and keep the rest as context.
INBOUND = ("GSV_", "SRV_", "LSV_", "WSV_")


def load_table():
    if not HEADER.exists():
        sys.exit(f"cannot find {HEADER}")
    table = {}
    for line in HEADER.read_text(encoding="utf-8", errors="replace").splitlines():
        m = DEFINE.match(line)
        if m:
            table.setdefault(int(m.group(2), 16), []).append(m.group(1))
    return table


def name_for(table, tid):
    names = table.get(tid)
    if not names:
        return "UNKNOWN"
    inbound = [n for n in names if n.startswith(INBOUND)]
    best = inbound[0] if inbound else names[0]
    others = [n for n in names if n != best]
    return best + (f" (also {', '.join(others)})" if others else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("types", nargs="*", help="type ids, e.g. 0x0796 or 1942")
    ap.add_argument("--log", help="annotate every Frame spike line in this client log")
    args = ap.parse_args()

    table = load_table()

    if args.log:
        for line in pathlib.Path(args.log).read_text(encoding="utf-8",
                                                     errors="replace").splitlines():
            if "Frame spike:" in line:
                m = WORST.search(line)
                if m:
                    tid = int(m.group(1), 16)
                    line += f"    <- worst packet: {name_for(table, tid)}"
            print(line)
        return

    if not args.types:
        ap.error("give some type ids, or --log")

    for t in args.types:
        tid = int(t, 16) if t.lower().startswith("0x") else int(t)
        print(f"0x{tid:04x} = {name_for(table, tid)}")


if __name__ == "__main__":
    main()
