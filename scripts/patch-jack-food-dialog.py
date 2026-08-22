"""Teach Bighand Jack's dialog the new "In Need of Food" material count.

Same shape as the other two Oro dialog patches: the QSD is only half the gate,
because the conversation picks its branch from its own compiled Lua inside
`EM74-002.CON`. Probed under the shipped `lua4.exe` with stubbed bindings, the
retail checks are:

    TA_4414_mid   true while the quest is held and not yet complete
    TA_4414_end   12/12/12 proofs AND Asper Meat >= 5 AND Asper Skin >= 5

`scripts/simplify-oro-food-quest.py` drops the materials to two. Left alone the
server would accept two and Jack would keep asking for five -- the same trap the
Waterskins hit, and the one that makes the overhead "?" light over an NPC with
no matching option.

Only those two are redefined. The rest of the TA_4414_* family (`_desert`,
`_hooded`, `_crowned`, `_177_178`, and so on) live inside the in-progress
submenu, reachable only when `TA_4414_mid` is true, and describe which
extermination proofs are still outstanding -- proof counts are unchanged, and
their wording stays correct at any material count.

The appendix is executed by `cevent.cpp` into the same `lua_State` after the
main blob, in both the dialog path and the quest-icon path, so these definitions
replace the compiled ones.

Requires the QEX1-aware client, which we ship. `data/` is gitignored, so this
script is the committed record. Idempotent -- a second run replaces its own
chunk rather than stacking another. `--verify` decodes the result back and
checks the main blob is byte-identical to the original.
"""
import argparse, os, shutil, struct, sys

CON_REL = os.path.join("data", "3DDATA", "EVENT", "EM74-002.CON")
FILE_HEADER_LEN = 524
MAGIC = b"QEX1"
BEGIN = "-- QE:BEGIN food-two-materials"
END = "-- QE:END food-two-materials"

MEAT, SKIN = 13175, 13176          # questitem:175 / :176
PROOFS = (13177, 13178, 13179)     # desert / hooded / crowned extermination
QUEST = 4414
NEED_MAT = 2
NEED_PROOF = 12

_PROOF_TEMPLATE = (
    "  if QF_getQuestItemQuantity(%d, %d) < %d then\n"
    "    return 0\n"
    "  end"
)
_proof_checks = "\n".join(
    _PROOF_TEMPLATE % (QUEST, p, NEED_PROOF) for p in PROOFS)

APPENDIX = """%s
-- "In Need of Food" now asks for %d Asper Meat and %d Asper Skin, not 5 each --
-- see scripts/simplify-oro-food-quest.py. The compiled checks in this file's Lua
-- blob still test for 5. The client runs this appendix after that blob, so these
-- definitions win. Proof counts are unchanged.

function TA_4414_have()
  if QF_findQuest(%d) == -1 then
    return 0
  end
%s
  if QF_getQuestItemQuantity(%d, %d) < %d then
    return 0
  end
  if QF_getQuestItemQuantity(%d, %d) < %d then
    return 0
  end
  return 1
end

-- still collecting
function TA_4414_mid()
  if QF_findQuest(%d) == -1 then
    return 0
  end
  if TA_4414_have() == 1 then
    return 0
  end
  return 1
end

-- hand-in
function TA_4414_end()
  return TA_4414_have()
end
%s
""" % (BEGIN, NEED_MAT, NEED_MAT,
       QUEST,
       _proof_checks,
       QUEST, MEAT, NEED_MAT,
       QUEST, SKIN, NEED_MAT,
       QUEST,
       END)


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
        missing = [n for n in ("TA_4414_mid", "TA_4414_end")
                   if ("function " + n) not in txt]
        if BEGIN not in txt or missing:
            print("VERIFY FAILED: appendix present but missing %r" % missing)
            sys.exit(1)
        print("verify OK -- %s carries a %d-byte appendix redefining both gates"
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
