"""Teach Dr. Ega's dialog the new "In Need of Repairs" terms, and stop it
demanding a fate skill to hand the quest in.

Same shape as `scripts/patch-skully-water-dialog.py`: the QSD is only half the
gate, because the conversation decides which branch to show from its own
compiled Lua inside `EM74-004.CON`. Probed under the shipped `lua4.exe` with
stubbed bindings, the retail checks are:

    TA_4415_mid          true while Salvage < 6 OR Captures < 3   (in progress)
    TA_4415_arua_end     Salvage >= 6 AND Captures >= 3 AND Arua's Fate
    TA_4415_hebarn_end   Salvage >= 6 AND Captures >= 3 AND Hebarn's Fate

Two problems with that.

1. `scripts/simplify-oro-repairs-quest.py` drops the salvage requirement to
   three. Left alone, the server would accept three and the dialog would keep
   asking for six -- the same trap the Waterskins hit.

2. Both hand-in branches require a fate. A character that never took Nova's
   choice satisfies the QSD (`4415-02` has no such condition) and still cannot
   hand in, because neither branch is offered. The two branches differ only in
   flavour text -- menu 26 and menu 28 both end on the same `AT_4415_End` click
   -- so there is nothing behind the fate split except wording.

So all three are redefined through the QEX1 appendix, which `cevent.cpp` runs
into the same `lua_State` after the main blob, in both the dialog path and the
quest-icon path. The Arua wording becomes the default and Hebarn's is used only
when the player actually follows Hebarn, which keeps exactly one option visible
in all three cases -- Arua, Hebarn, and no fate at all.

`QF_hasHebarnFate` is used rather than `QF_checkQuestCondition("Hebarn_Skill")`
because it is an engine call that reads the learned skill directly, so it cannot
be knocked out by a missing QSD trigger.

Requires the QEX1-aware client, which we ship. `data/` is gitignored, so this
script is the committed record. Idempotent -- a second run replaces its own
chunk rather than stacking another. `--verify` decodes the result back and
checks the main blob is byte-identical to the original.
"""
import argparse, os, shutil, struct, sys

CON_REL = os.path.join("data", "3DDATA", "EVENT", "EM74-004.CON")
FILE_HEADER_LEN = 524
MAGIC = b"QEX1"
BEGIN = "-- QE:BEGIN repairs-three-salvage"
END = "-- QE:END repairs-three-salvage"

SALVAGE = 13181            # questitem:181 Salvageable Equipment
CAPTURE = 13182            # questitem:182 Captured Venomous Hooded Asper
QUEST = 4415
NEED_SALVAGE = 3
NEED_CAPTURE = 3

APPENDIX = """%s
-- "In Need of Repairs" now asks for %d Salvageable Equipment, not 6 -- see
-- scripts/simplify-oro-repairs-quest.py. The compiled checks in this file's Lua
-- blob still test for 6, and both hand-in branches also demand a fate skill
-- that the QSD does not require. The client runs this appendix after that blob,
-- so these definitions win.

function TA_4415_have()
  if QF_findQuest(%d) == -1 then
    return 0
  end
  if QF_getQuestItemQuantity(%d, %d) < %d then
    return 0
  end
  if QF_getQuestItemQuantity(%d, %d) < %d then
    return 0
  end
  return 1
end

-- still collecting
function TA_4415_mid()
  if QF_findQuest(%d) == -1 then
    return 0
  end
  if TA_4415_have() == 1 then
    return 0
  end
  return 1
end

-- hand-in. Arua's wording is the default so a player with no fate still gets
-- exactly one option; Hebarn's is used only when they actually follow Hebarn.
function TA_4415_arua_end()
  if TA_4415_have() == 0 then
    return 0
  end
  if QF_hasHebarnFate() == 1 then
    return 0
  end
  return 1
end

function TA_4415_hebarn_end()
  if TA_4415_have() == 0 then
    return 0
  end
  return QF_hasHebarnFate()
end
%s
""" % (BEGIN, NEED_SALVAGE,
       QUEST,
       QUEST, SALVAGE, NEED_SALVAGE,
       QUEST, CAPTURE, NEED_CAPTURE,
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
        missing = [n for n in ("TA_4415_mid", "TA_4415_arua_end", "TA_4415_hebarn_end")
                   if ("function " + n) not in txt]
        if BEGIN not in txt or missing:
            print("VERIFY FAILED: appendix present but missing %r" % missing)
            sys.exit(1)
        print("verify OK -- %s carries a %d-byte appendix redefining all three gates"
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
