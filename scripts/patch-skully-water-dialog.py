"""Teach Skully's dialog that "In Need of Water" now wants two Waterskins.

`scripts/simplify-oro-water-quest.py` drops the requirement from three to two in
`QP401.QSD`, but that is only half the gate. Skully's conversation decides which
menu branch to show from its own Lua, and that Lua is *compiled bytecode* inside
`EM74-003.CON`. Probing it under the shipped `lua4.exe` with stubbed bindings:

    TA_4413_empty   true when Waterskins == 0
    TA_4413_1       true when Waterskins <= 1
    TA_4413_2       true when Waterskins <= 2
    TA_4413_3       true when Waterskins == 3      <-- the hand-in branch

So with the QSD at two the server would accept the hand-in, and the client would
never offer it: `TA_4413_3` wants exactly three. The quest icon still shows a "?"
because that evaluator runs the QSD path, which is what makes it look like a
server bug when it is a dialog one.

The blob cannot be recompiled (Lua 4.0.1 rejects multi-chunk buffers), so this
uses the QEX1 appendix -- extra Lua *source* after the Lua tail that
`cevent.cpp` runs into the same `lua_State` **after** the main blob, in both the
dialog path (`Start`) and the quest-icon path. A global defined there therefore
replaces the compiled one. Only `TA_4413_3` is redefined; `TA_4413_1` and `_2`
sit inside a submenu only reachable at zero Waterskins, so they still behave
exactly as they did.

Format, from `cevent.cpp` and `src/tools/quest-editor/src/convo.rs`:

    [script_off]  i32 lua_len; XOR'd lua
    [after]       "QEX1"; i32 appendix_len; XOR'd Lua source

The XOR key is a single repeating byte, `len` when odd else the **total file
size** -- so appending re-keys the main blob too, and both regions are
re-encoded here against the new size. `--verify` decodes the result back and
checks the main blob is byte-identical to the original and the appendix is the
source written.

Requires the QEX1-aware client, which we ship. `data/` is gitignored, so this
script is the committed record. Idempotent -- a second run replaces its own
chunk rather than stacking another.
"""
import argparse, os, shutil, struct, sys

CON_REL = os.path.join("data", "3DDATA", "EVENT", "EM74-003.CON")
FILE_HEADER_LEN = 524
MAGIC = b"QEX1"
BEGIN = "-- QE:BEGIN water-two-cacti"
END = "-- QE:END water-two-cacti"

WATERSKIN = 13180          # questitem:180, type*1000 + no
NEEDED = 2

APPENDIX = """%s
-- "In Need of Water" now asks for %d Waterskins, not 3 -- see
-- scripts/simplify-oro-water-quest.py. The compiled TA_4413_3 in this file's
-- Lua blob tests for exactly 3, which would leave the hand-in permanently
-- hidden. The client runs this appendix after that blob, so this wins.
--
-- 4413 / 4422 / 4423 are interchangeable variants of the same quest; whichever
-- one the player holds is the one whose quest inventory carries the Waterskins.
function TA_4413_3()
  local q = 4413
  if QF_findQuest(4413) == -1 then
    if QF_findQuest(4422) ~= -1 then
      q = 4422
    elseif QF_findQuest(4423) ~= -1 then
      q = 4423
    else
      return 0
    end
  end
  if QF_getQuestItemQuantity(q, %d) >= %d then
    return 1
  end
  return 0
end
%s
""" % (BEGIN, NEEDED, WATERSKIN, NEEDED, END)


def xor_key(v1, v2):
    return (v1 & 0xFF) if (v1 & 1) else (v2 & 0xFF)


def xor(buf, key):
    return bytes(b ^ key for b in buf)


def split(path):
    """-> (head_bytes, decoded_lua, decoded_appendix_or_None)"""
    b = open(path, "rb").read()
    script_off, = struct.unpack_from("<I", b, FILE_HEADER_LEN - 4)
    lua_len, = struct.unpack_from("<i", b, script_off)
    lua_start = script_off + 4
    lua = xor(b[lua_start:lua_start + lua_len], xor_key(lua_len, len(b)))
    end = lua_start + lua_len
    app = None
    if b[end:end + 4] == MAGIC:
        alen, = struct.unpack_from("<i", b, end + 4)
        app = xor(b[end + 8:end + 8 + alen], xor_key(alen, len(b)))
    return b[:script_off], lua, app


def build(head, lua, app_src):
    app = app_src.encode("latin-1")
    total = len(head) + 4 + len(lua) + 8 + len(app)
    out = bytearray(head)
    out += struct.pack("<i", len(lua))
    out += xor(lua, xor_key(len(lua), total))
    out += MAGIC
    out += struct.pack("<i", len(app))
    out += xor(app, xor_key(len(app), total))
    if len(out) != total:
        raise SystemExit("size bookkeeping is wrong: %d vs %d" % (len(out), total))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    path = os.path.join(args.root, CON_REL)
    if not os.path.isfile(path):
        raise SystemExit("not found: %s" % path)

    if args.restore:
        bak = path + ".bak"
        if not os.path.isfile(bak):
            raise SystemExit("no backup at %s" % bak)
        shutil.copyfile(bak, path)
        os.remove(bak)
        print("restored %s from .bak" % os.path.basename(path))
        return

    head, lua, app = split(path)

    if args.verify:
        if not app:
            print("VERIFY FAILED: no QEX1 appendix present")
            sys.exit(1)
        txt = app.decode("latin-1")
        if BEGIN not in txt or "TA_4413_3" not in txt:
            print("VERIFY FAILED: appendix present but does not redefine TA_4413_3")
            sys.exit(1)
        print("verify OK -- %s carries a %d-byte appendix redefining TA_4413_3"
              % (os.path.basename(path), len(app)))
        return

    if app and app.decode("latin-1") == APPENDIX:
        print("already patched, nothing to do")
        return
    if app:
        print("replacing an existing %d-byte appendix" % len(app))

    out = build(head, lua, APPENDIX)
    before = os.path.getsize(path)
    print("%s: %d -> %d bytes (+%d appendix)"
          % (os.path.basename(path), before, len(out), len(APPENDIX)))
    if args.dry_run:
        print("(dry run, nothing written)")
        return

    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(out)

    # decode the result back: the main blob must be untouched and the appendix
    # must be exactly what we wrote, or the re-key went wrong
    head2, lua2, app2 = split(path)
    if head2 != head:
        raise SystemExit("VERIFY FAILED: header/conversation section changed")
    if lua2 != lua:
        raise SystemExit("VERIFY FAILED: main Lua blob does not decode back")
    if app2 is None or app2.decode("latin-1") != APPENDIX:
        raise SystemExit("VERIFY FAILED: appendix does not decode back")
    print("    written; main blob and appendix both decode back cleanly")


if __name__ == "__main__":
    main()
