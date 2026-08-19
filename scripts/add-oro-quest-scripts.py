"""Install the client Lua functions the Oro quest triggers call by name.

A `.QSD` reward of type 29 (`RUN_SCRIPT`) is client-only: the server slot is
`F_QST_TRUE`, and the client calls a Lua *global* of that name through
`CSystemProcScript::CallLuaFunction`. Eleven Oro triggers use it and none of the
functions existed here.

That is not a cosmetic gap. `lua_CallFUNC` (src/client/lua_func.cpp) does:

    lua_getglobal(L, function_name);
    if (!lua_isfunction(L, -1)) {
        g_pCApp->ErrorBOX("ERROR :: function not found", szMsg);   // MessageBox
        return -1;
    }

-- a blocking, top-most Win32 message box over the render window. So the first
cactus a player walked into would freeze the client on
"invalid script function( waterskincactus ) name ...".

The triggers only became reachable when the COND_006 radius unit mismatch was
fixed (server compared a metre radius against a centimetre distance), which is
why this was never seen before. The two changes belong together.

Where each name is used:

    waterskincactus          4413 / 4422 / 4423  In Need of Water      (x9)
    salvageableeq            4415                In Need of Repairs    (x6)
    Karkian_Darkness         4427                Karkian Darkness      (x3)
    MurisEntrance_01_*       Gates of Muris entrance event object
    PharaohKey_01_*          Pharaoh Key hiding place near the Oasis
    PharaohKey_02_*          Pharaoh Key hiding place near the Statue
    shinning_stone           3220 Lost Stones          (Junon, QU02-001)
    Debris_Item              3201 Airship Debris       (Junon, QU02-001)

The six Muris/Pharaoh bodies are ported verbatim from the RoseZA dump: they open
an NPC conversation, and we have all three `EM82-*.CON` files they name.

The five pickup bodies are NOT verbatim. Upstream they are one call each:

    SC_ShowTutorialImage("3Ddata/Tutorial/waterskincactus.dds", 300,145,0.3,10,10.3,0)

`SC_ShowTutorialImage` is registered in our client, but `data/3DDATA/TUTORIAL/`
does not exist here at all -- we ship no tutorial art. So instead they write a
line to the quest chat channel through `SC_AppendQuestMsg`, added for this in
`src/client/system/system_func.h`. That matters more than it sounds: the trigger
object is an invisible `warpbox`, so with a silent body a successful pickup and
walking past a decorative cactus look identical. The original call is kept
beside each one as a comment, so switching to the image is a one-line edit if
the art is ever imported.

A modal box was the other option and is the wrong shape here -- a water run has
three pickups and a repairs run six, so it would cost up to a dozen dismissals
per playthrough.

These go in `data/SCRIPTS/TUTORIAL.LUA`, which is where the existing pickup
scripts already live and, more to the point, the only place they get loaded from
-- see the comment on REL below. `data/` is gitignored, so this script is the
committed record of the change. Idempotent; `--dry-run` previews, `--verify`
re-reads and checks, `--restore` puts the `.bak` back. The target file is CP949
and LF-terminated, so it is handled as bytes and the non-ASCII byte count is
asserted unchanged after writing.
"""
import argparse, os, shutil, sys

# Scripts/SystemFunc.lua is the ONLY file CSystemProcScript::InitSystem loads,
# and it chains SC_DoScript("scripts/Tutorial.lua"). That chain is the whole of
# the lua_State REWD_029 calls into, and Tutorial.lua is where the existing
# pickup scripts already live (mushroom, sandglass, owl, genzistone...).
#
# QuestGlobal.lua is NOT in that chain -- QuestFun_Load.lua would `doScript` it,
# but nothing loads QuestFun_Load.lua: not SystemFunc.lua, not any other script,
# not the client. Its lone function G_giveJob_1 has never run. Putting the Oro
# functions there looked natural and did nothing at all.
REL = os.path.join("data", "SCRIPTS", "TUTORIAL.LUA")

# Tutorial.lua is CP949, not UTF-8, and LF-terminated. Read and write it as bytes
# and append ASCII only -- decoding it as text mangles the Korean comments.
EOL = b"\n"

MARKER = b"-- BEGIN oro-quest-scripts"
END_MARKER = b"-- END oro-quest-scripts"

BLOCK = b"""
-- BEGIN oro-quest-scripts (scripts/add-oro-quest-scripts.py -- do not edit by hand)
--
-- Called by name from .QSD REWD_029 (RUN_SCRIPT). A missing global here is not
-- silent: lua_CallFUNC pops a modal error box and the client stops dead, so
-- every name a reachable trigger uses must resolve to a function.

-- Orlo Chapter 1 -- world pickups. These are the only feedback the player gets:
-- the trigger box is invisible, so without a line here a pickup is indis-
-- tinguishable from walking past. Upstream showed a tutorial image instead; we
-- ship no 3Ddata/Tutorial art, so the original call is kept as a comment.

function waterskincactus( iObject, iState, bJustEnd )
\tSC_AppendQuestMsg( "You fill a Waterskin with water from the cactus." );
\t-- SC_ShowTutorialImage("3Ddata/Tutorial/waterskincactus.dds", 300, 145, 0.3, 10, 10.3, 0);
end

function salvageableeq( iObject, iState, bJustEnd )
\tSC_AppendQuestMsg( "You salvage a usable piece of equipment from the wreckage." );
\t-- SC_ShowTutorialImage("3Ddata/Tutorial/salvageableeq.dds", 300, 145, 0.3, 10, 10.3, 0);
end

function Karkian_Darkness( iObject, iState, bJustEnd )
\tSC_AppendQuestMsg( "You note the strange purple smoke rising from the sand." );
\t-- SC_ShowTutorialImage("3Ddata/Tutorial/Karkian_Darkness.dds", 300, 145, 0.3, 10, 10.3, 0);
end

-- Junon pickups that arrived with QU02-001.QSD. Upstream these also blinked the
-- quest menu button; DLG_TYPE_MENU / MENU_BTN_QUEST are not exported to Lua by
-- our client, so that call is left out rather than passed a nil.

function shinning_stone( iObject, iState, bJustEnd )
\tSC_AppendQuestMsg( "You pick up a piece of Shining Stone." );
\t-- SC_ShowTutorialImage("3Ddata/Tutorial/3220_stone.dds", 300, 145, 0.3, 10, 10.3, 0);
end

function Debris_Item( iObject, iState, bJustEnd )
\tSC_AppendQuestMsg( "You recover a piece of Airship Debris." );
\t-- SC_ShowTutorialImage("3Ddata/Tutorial/3201_Item.dds", 300, 145, 0.3, 10, 10.3, 0);
end

-- Orlo Chapter 2 -- these open a conversation, ported verbatim. The NPC ids are
-- the Hebarn Saboteur / Arua Interrogator pair standing at each location.

function MurisEntrance_01_Arua( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2153, "3Ddata\\\\Event\\\\EM82-001.con", -1 );
end

function MurisEntrance_01_Hebarn( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2154, "3Ddata\\\\Event\\\\EM82-001.con", -1 );
end

function PharaohKey_01_Arua( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2188, "3Ddata\\\\Event\\\\EM82-002.con", -1 );
end

function PharaohKey_01_Hebarn( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2189, "3Ddata\\\\Event\\\\EM82-002.con", -1 );
end

function PharaohKey_02_Arua( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2192, "3Ddata\\\\Event\\\\EM82-003.con", -1 );
end

function PharaohKey_02_Hebarn( iObject, iState, bJustEnd )
\tSC_RunEventObjectEvent( 2193, "3Ddata\\\\Event\\\\EM82-003.con", -1 );
end

-- END oro-quest-scripts
"""

EXPECTED = [
    b"waterskincactus", b"salvageableeq", b"Karkian_Darkness",
    b"shinning_stone", b"Debris_Item",
    b"MurisEntrance_01_Arua", b"MurisEntrance_01_Hebarn",
    b"PharaohKey_01_Arua", b"PharaohKey_01_Hebarn",
    b"PharaohKey_02_Arua", b"PharaohKey_02_Hebarn",
]


def strip_block(buf):
    """Remove a previously installed block, so re-running replaces it."""
    i = buf.find(MARKER)
    if i < 0:
        return buf, False
    j = buf.find(END_MARKER, i)
    if j < 0:
        raise SystemExit("found the begin marker but not the end -- fix by hand")
    j += len(END_MARKER)
    while j < len(buf) and buf[j:j + 1] in (b"\r", b"\n"):
        j += 1
    # Also drop the separator the block was appended after. This has to eat CR as
    # well as LF: the file is CRLF, and walking back over LF alone leaves an
    # orphan CR that the rejoin below then pads, so a re-run grows the file by a
    # few bytes every time instead of being a no-op.
    while i > 0 and buf[i - 1:i] in (b"\r", b"\n"):
        i -= 1
    return buf[:i] + buf[j:], True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true", help="check and exit")
    ap.add_argument("--restore", action="store_true", help="put the .bak back")
    args = ap.parse_args()

    path = os.path.join(args.root, REL)
    if not os.path.isfile(path):
        raise SystemExit(f"not found: {path}")

    if args.restore:
        bak = path + ".bak"
        if not os.path.isfile(bak):
            raise SystemExit(f"no backup at {bak}")
        shutil.copyfile(bak, path)
        os.remove(bak)
        print(f"restored {REL} from .bak")
        return

    buf = open(path, "rb").read()
    hi_before = sum(1 for b in buf if b > 127)

    if args.verify:
        missing = [n for n in EXPECTED if b"function " + n not in buf]
        if missing:
            print("VERIFY FAILED, missing: "
                  + ", ".join(m.decode() for m in missing))
            sys.exit(1)
        print(f"verify OK -- all {len(EXPECTED)} functions present in {REL}")
        return

    base, replaced = strip_block(buf)
    # Normalise the tail the same way on both paths -- the pristine file ends in
    # several blank lines and the stripped one does not, so without this a first
    # install and a re-install land on different byte counts.
    base = base.rstrip(b"\r\n")
    out = base + b"\r\n\r\n" + BLOCK.replace(b"\n", b"\r\n")

    if out == buf:
        print("already up to date, nothing to do")
        return

    print(f"{'replace' if replaced else 'append'} {len(EXPECTED)} functions in {REL}")
    print(f"    {len(buf)} -> {len(out)} bytes")
    if args.dry_run:
        print("    (dry run, nothing written)")
        return

    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
    with open(path, "wb") as fh:
        fh.write(out)

    back = open(path, "rb").read()
    missing = [n for n in EXPECTED if b"function " + n not in back]
    if missing or back != out:
        raise SystemExit("VERIFY FAILED after write")
    if hi_before != sum(1 for b in back if b > 127):
        raise SystemExit("VERIFY FAILED: non-ASCII byte count changed -- the CP949 "
                         "comments were mangled; restore from .bak")
    print(f"    written and verified ({len(EXPECTED)} functions, "
          f"{hi_before} CP949 bytes intact)")


if __name__ == "__main__":
    main()
