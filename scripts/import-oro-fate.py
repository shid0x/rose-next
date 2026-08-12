"""Import the Arua/Hebarn "fate" system the Oro content is built on.

Why this exists
---------------
Oro's quests and dialogs branch constantly on which of two gods the player
follows: 100 QSD conditions and 30 conversations read it. It is recorded as a
learned marker skill -- 2880 "Arua's Fate" or 2881 "Hebarn's Fate" -- and read
back two ways that must agree:

  * QSD  : COND_009 "has skill 2880/2881", wrapped in the triggers Arua_Skill
           and Hebarn_Skill, which the dialogs probe with QF_checkQuestCondition.
  * Engine: QF_hasAruaFate() / QF_hasHebarnFate() / QF_hasFate(), which we now
           implement by looking up the same learned skills (qf_quest.cpp).

Three pieces were missing on our side, and together they dead-ended the very
first Oro quest chain: Nova (NPC 2101) offered only "(I need to choose my
fate...)", a menu item with no child and no click function.

  1. LIST_SKILL.STB rows 2880-2883 are blank here. 2882 (Wayfinder) and 2883
     (Vizier's Key) are already *granted* by quests stage 6 imported, so those
     were broken too, not just the fate pair.
  2. The Arua_Skill / Hebarn_Skill triggers live in RoseZA's QP101.QSD -- the
     Junon Pyramid file. Ours is the older Junon quest set and has neither, so
     both probes returned "no such trigger" = false.
  3. Nothing grants a fate. On RoseZA it comes from Pyramid quest 1828
     (1828-02 -> switch 145 + skill 2880, 1828-03 -> switch 146 + skill 2881),
     and we deliberately deferred the Pyramid.

So this script adds the skills, adds the triggers, and puts the choice itself
on Nova, who is where the player first hits the wall.

How the dialog hook works
-------------------------
EM73-001.CON menu 20 already has the three items. Items 0 and 1 ("Oh, for
Arua's sake!!" / "Oh, for Hebarn's plight!!") are gated by the compiled-Lua
functions TA_arua_skill / TA_hebarn_skill, which just forward to the two
triggers -- so a player with no fate sees neither, and item 2 is a dead end by
construction. The Lua is bytecode and cannot be edited.

Two things make it work anyway:

  * The check/click function *names* are plain 32-byte string fields in the
    80-byte menu nodes, not bytecode. Items 0 and 1 have an empty click field,
    so we can point them at functions of our own.
  * The QEX1 appendix (see convo.rs / cevent.cpp) runs extra Lua *source* into
    the same lua_State after the main blob, so a later definition of a global
    function wins. We redefine TA_arua_skill / TA_hebarn_skill to also return
    true when the player has no fate at all -- which is exactly "here is the
    choice" -- and leave TA_nofate_Skill alone, since it already hides itself
    once a fate is set.

Idempotent, --dry-run, --stage N, --selftest. Verifies by re-parsing what it
wrote (and by running the patched dialog's Lua under thirdparty lua4.exe when
--selftest is given). Requires Pillow for the icon step.

Usage:
    python scripts/import-oro-fate.py --dry-run
    python scripts/import-oro-fate.py
    python scripts/import-oro-fate.py --stage 2
    python scripts/import-oro-fate.py --selftest
"""
import argparse
import importlib.util
import io
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC_ROOT = r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data"

DATA = os.path.join(ROOT, "data")
OUR_SKILL_STB = os.path.join(DATA, "3DDATA", "STB", "LIST_SKILL.STB")
OUR_SKILL_STL = os.path.join(DATA, "3DDATA", "STB", "LIST_SKILL_S.STL")
OUR_QP101 = os.path.join(DATA, "3DDATA", "QUESTDATA", "QP101.QSD")
OUR_NOVA_CON = os.path.join(DATA, "3DDATA", "EVENT", "EM73-001.CON")
SRC_SKILL_STB = os.path.join(SRC_ROOT, "3DDATA", "STB", "LIST_SKILL.STB")
SRC_SKILL_TSI = os.path.join(SRC_ROOT, "3DDATA", "CONTROL", "RES", "SKILLICON.TSI")
SRC_QP101 = os.path.join(SRC_ROOT, "3DDATA", "QUESTDATA", "QP101.QSD")

# ---------------------------------------------------------------- skills
SKILL_ARUA, SKILL_HEBARN = 2880, 2881
SKILL_WAYFINDER, SKILL_VIZIER = 2882, 2883
FATE_SKILLS = (SKILL_ARUA, SKILL_HEBARN, SKILL_WAYFINDER, SKILL_VIZIER)

# Descriptions are ours: RoseZA leaves col 86 (the STL key) empty on all four,
# so these skills have no name or tooltip even there. They do occupy a slot in
# the skill window, so they get both here.
SKILL_TEXT = {
    SKILL_ARUA: ("Arua's Fate", "You follow Arua, goddess of life. Marks your allegiance on Orlo."),
    SKILL_HEBARN: ("Hebarn's Fate", "You follow Hebarn, god of death. Marks your allegiance on Orlo."),
    SKILL_WAYFINDER: ("Wayfinder Skill", "Proof that you have learned to navigate the Wasteland."),
    SKILL_VIZIER: ("Vizier's Key", "Proof of the Vizier's trust. Opens the way deeper into Muris."),
}
SKILL_STL_KEY = "LSkill%04d"
SKILL_ICON_COL = 51                  # SKILL_ICON_NO
SKILL_STL_COL = 86                   # key into LIST_SKILL_S.STL
SKILL_STB_COLS = 87                  # cols 0..86 align with RoseZA's 114-col schema
SRC_ICON_INDEX = {                   # sprite index in RoseZA's SKILLICON.TSI
    SKILL_ARUA: 988, SKILL_HEBARN: 989, SKILL_WAYFINDER: 990, SKILL_VIZIER: 991,
}

# ---------------------------------------------------------------- switches
# RoseZA records the choice in a user switch as well as the skill; quest 1828
# sets these. Nothing in the Oro content reads them (it all reads the skill),
# but the Pyramid content does, so set them too and a later Pyramid import stays
# coherent instead of offering the choice a second time.
SWITCH_ARUA, SWITCH_HEBARN = 145, 146

# ---------------------------------------------------------------- QSD
FATE_PATTERN = "OroFate"
# Copied verbatim from RoseZA's QP101.QSD; every Oro dialog probes these.
COPY_TRIGGERS = ("Arua_Skill", "Hebarn_Skill")
TRIG_CHOOSE_ARUA = "Oro-ChooseArua"
TRIG_CHOOSE_HEBARN = "Oro-ChooseHebarn"

COND_009, COND_014 = 9, 14
REWD_014 = 0x01000000 | 14
REWD_015 = 0x01000000 | 15

# ---------------------------------------------------------------- .CON
NOVA_FATE_MENU = 20
NODE_SIZE = 80
NODE_CLICK_OFF = 44                  # char szFunc2[32]
CLICK_ARUA = "AT_OroFateArua"
CLICK_HEBARN = "AT_OroFateHebarn"
APPENDIX_MAGIC = b"QEX1"
APPENDIX_BEGIN = "-- >>> oro-fate\n"
APPENDIX_END = "-- <<< oro-fate\n"

# TA_arua_skill / TA_hebarn_skill in the main blob return
# QF_checkQuestCondition("Arua_Skill"/"Hebarn_Skill"), i.e. "do you already
# follow this god". Redefining them to *also* fire when the player follows
# neither turns menu 20 into the choice itself. The click functions are the only
# thing that actually commits it, and they refuse once a fate is set, so
# re-opening the dialog cannot switch sides.
APPENDIX_LUA = """function TA_arua_skill(E)
\tif QF_hasAruaFate() > 0 then return 1 end
\tif QF_hasFate() < 1 then return 1 end
\treturn 0
end
function TA_hebarn_skill(E)
\tif QF_hasHebarnFate() > 0 then return 1 end
\tif QF_hasFate() < 1 then return 1 end
\treturn 0
end
function AT_OroFateArua(E)
\tif QF_hasFate() < 1 then QF_doQuestTrigger("%s") end
\treturn 1
end
function AT_OroFateHebarn(E)
\tif QF_hasFate() < 1 then QF_doQuestTrigger("%s") end
\treturn 1
end
""" % (TRIG_CHOOSE_ARUA, TRIG_CHOOSE_HEBARN)


# ================================================================ helpers
def load_oro():
    """Reuse the stage-1..6 importer's STB/STL codecs rather than re-deriving."""
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def backup(path):
    bak = path + ".bak"
    if not os.path.exists(bak):
        shutil.copy2(path, bak)


def write_file(path, blob, dry):
    if dry:
        return
    backup(path)
    with open(path, "wb") as fh:
        fh.write(blob)


# ---------------------------------------------------------------- QSD codec
# Format recap: u32 size_field (always 12); u32 pattern_count; pstr desc; then
# patterns. A pattern is u32 trigger_count; pstr name; triggers. A trigger is
# u8 check_next; u32 cond_count; u32 rewd_count; pstr name; entities. An entity
# is u32 size; i32 type; payload -- and *nothing* in the file is an offset, so
# whole triggers can be spliced in by appending a pattern and bumping the count.
def qsd_pstr(s):
    v = s.encode("latin-1") + b"\0"
    return struct.pack("<h", len(v)) + v


def qsd_walk(blob):
    """Yield (trigger_name, entity_type, entity_bytes, trigger_bytes)."""
    b, o = blob, 4
    npat, = struct.unpack_from("<I", b, o); o += 4
    n, = struct.unpack_from("<h", b, o); o += 2 + n
    for _ in range(npat):
        ntrig, = struct.unpack_from("<I", b, o); o += 4
        n, = struct.unpack_from("<h", b, o); o += 2 + n
        for _ in range(ntrig):
            trig_start = o
            o += 1
            ncond, = struct.unpack_from("<I", b, o); o += 4
            nrew, = struct.unpack_from("<I", b, o); o += 4
            n, = struct.unpack_from("<h", b, o); o += 2
            name = b[o:o + n].rstrip(b"\0").decode("latin-1"); o += n
            ents = []
            for _ in range(ncond + nrew):
                esz, = struct.unpack_from("<I", b, o)
                etype, = struct.unpack_from("<i", b, o + 4)
                ents.append((etype, b[o:o + esz]))
                o += esz
            yield name, ents, b[trig_start:o]


def qsd_trigger_bytes(blob, name):
    for tname, _, raw in qsd_walk(blob):
        if tname == name:
            return raw
    return None


def qsd_find_entity(blob, trigger, etype):
    for tname, ents, _ in qsd_walk(blob):
        if tname != trigger:
            continue
        for t, raw in ents:
            if t == etype:
                return raw
    return None


def qsd_has_trigger(blob, name):
    return any(tname == name for tname, _, _ in qsd_walk(blob))


def qsd_build_trigger(name, conds, rewds):
    return (bytes([0])
            + struct.pack("<II", len(conds), len(rewds))
            + qsd_pstr(name)
            + b"".join(conds) + b"".join(rewds))


def qsd_append_pattern(blob, pattern, triggers):
    out = bytearray(blob)
    npat, = struct.unpack_from("<I", out, 4)
    struct.pack_into("<I", out, 4, npat + 1)
    out += struct.pack("<I", len(triggers)) + qsd_pstr(pattern)
    out += b"".join(triggers)
    return bytes(out)


def qsd_patch(template, *fields):
    """Copy a real entity and overwrite payload fields.

    Each field is (payload_offset, struct_fmt, values). The entity keeps its
    original length -- its u32 size header is what the loader walks by, so
    never resize one by slicing.
    """
    e = bytearray(template)
    for off, fmt, values in fields:
        struct.pack_into(fmt, e, 8 + off, *values)
    return bytes(e)


def qsd_parse_ok(blob):
    """Re-walk and confirm the whole file is consumed exactly."""
    b, o = blob, 4
    npat, = struct.unpack_from("<I", b, o); o += 4
    n, = struct.unpack_from("<h", b, o); o += 2 + n
    for _ in range(npat):
        ntrig, = struct.unpack_from("<I", b, o); o += 4
        n, = struct.unpack_from("<h", b, o); o += 2 + n
        for _ in range(ntrig):
            o += 1
            ncond, = struct.unpack_from("<I", b, o); o += 4
            nrew, = struct.unpack_from("<I", b, o); o += 4
            n, = struct.unpack_from("<h", b, o); o += 2 + n
            for _ in range(ncond + nrew):
                esz, = struct.unpack_from("<I", b, o)
                o += esz
    return o == len(b), o


# ---------------------------------------------------------------- .CON codec
def con_xor_key(v1, v2):
    return (v1 & 0xFF) if (v1 & 1) else (v2 & 0xFF)


def con_menu_offsets(blob):
    conv_off, = struct.unpack_from("<I", blob, 516)
    menu_num, menu_start = struct.unpack_from("<iI", blob, conv_off + 8)
    return conv_off + menu_start, menu_num


def con_set_click(blob, menu_idx, item_idx, func):
    """Rewrite one menu item's click-function name in place.

    Menu collection bodies past their first 8 bytes are XOR'd with a key derived
    from (sub-count, length); neither changes here, so the collection can be
    decoded, patched and re-encoded without disturbing any offset.
    """
    out = bytearray(blob)
    base, menu_num = con_menu_offsets(out)
    if not 0 <= menu_idx < menu_num:
        raise SystemExit(f"menu {menu_idx} out of range (have {menu_num})")
    mmt, = struct.unpack_from("<I", out, base + 4 * menu_idx)
    coll = base + mmt
    length, nsub = struct.unpack_from("<ii", out, coll)
    if not 0 <= item_idx < nsub:
        raise SystemExit(f"menu {menu_idx} item {item_idx} out of range (have {nsub})")
    key = con_xor_key(nsub, length)
    body = bytearray(out[coll:coll + length])
    for i in range(8, len(body)):
        body[i] ^= key
    sub, = struct.unpack_from("<I", body, 8 + 4 * item_idx)
    name = func.encode("latin-1")
    if len(name) >= 32:
        raise SystemExit(f"click function name too long: {func}")
    body[sub + NODE_CLICK_OFF:sub + NODE_CLICK_OFF + 32] = name + b"\0" * (32 - len(name))
    for i in range(8, len(body)):
        body[i] ^= key
    out[coll:coll + length] = body
    return bytes(out)


def con_get_item(blob, menu_idx, item_idx):
    """(type, child, check, click, str_id) for one menu item."""
    base, _ = con_menu_offsets(blob)
    mmt, = struct.unpack_from("<I", blob, base + 4 * menu_idx)
    coll = base + mmt
    length, nsub = struct.unpack_from("<ii", blob, coll)
    key = con_xor_key(nsub, length)
    body = bytearray(blob[coll:coll + length])
    for i in range(8, len(body)):
        body[i] ^= key
    sub, = struct.unpack_from("<I", body, 8 + 4 * item_idx)
    _, mtype, child = struct.unpack_from("<3i", body, sub)
    chk = bytes(body[sub + 12:sub + 44]).split(b"\0")[0].decode("latin-1")
    clk = bytes(body[sub + 44:sub + 76]).split(b"\0")[0].decode("latin-1")
    sid, = struct.unpack_from("<i", body, sub + 76)
    return mtype, child, chk, clk, sid


def con_split(blob):
    """(head, lua_source, appendix_source) with both blobs XOR-decoded."""
    script_off, = struct.unpack_from("<I", blob, 520)
    lua_len, = struct.unpack_from("<i", blob, script_off)
    lua_len = max(0, lua_len)
    total = len(blob)
    lua = bytes(x ^ con_xor_key(lua_len, total)
                for x in blob[script_off + 4: script_off + 4 + lua_len])
    appendix = b""
    ap_off = script_off + 4 + lua_len
    if ap_off + 8 <= total and blob[ap_off:ap_off + 4] == APPENDIX_MAGIC:
        ap_len, = struct.unpack_from("<i", blob, ap_off + 4)
        if ap_len > 0 and ap_off + 8 + ap_len <= total:
            appendix = bytes(x ^ con_xor_key(ap_len, total)
                             for x in blob[ap_off + 8: ap_off + 8 + ap_len])
    return blob[:script_off], lua, appendix


def con_join(head, lua, appendix):
    """Rebuild the tail. Both blobs are keyed on the *final* file size, which
    includes the appendix block, so the main blob has to be re-encoded whenever
    the appendix changes size -- the client decodes with the size it sees."""
    ap_block = 8 + len(appendix) if appendix else 0
    total = len(head) + 4 + len(lua) + ap_block
    out = bytearray(head)
    out += struct.pack("<i", len(lua))
    out += bytes(x ^ con_xor_key(len(lua), total) for x in lua)
    if appendix:
        out += APPENDIX_MAGIC + struct.pack("<i", len(appendix))
        out += bytes(x ^ con_xor_key(len(appendix), total) for x in appendix)
    assert len(out) == total, (len(out), total)
    return bytes(out)


def appendix_upsert(appendix, body):
    """Replace our section, leaving any other tool's sections alone."""
    s = appendix.decode("latin-1")
    start = s.find(APPENDIX_BEGIN)
    if start >= 0:
        end = s.find(APPENDIX_END, start)
        if end >= 0:
            s = s[:start] + s[end + len(APPENDIX_END):]
    if s and not s.endswith("\n"):
        s += "\n"
    return (s + APPENDIX_BEGIN + body + APPENDIX_END).encode("latin-1")


# ================================================================ stage 1
def stage_skills(oro, dry):
    print("== stage 1: fate marker skills")
    icon_index = install_icons(dry)

    src = oro.Stb(SRC_SKILL_STB)
    dst = oro.Stb(OUR_SKILL_STB)
    stl = oro.Stl(OUR_SKILL_STL)

    changed = 0
    for sn in FATE_SKILLS:
        name, desc = SKILL_TEXT[sn]
        key = SKILL_STL_KEY % sn
        for c in range(SKILL_STB_COLS):
            dst.set(sn, c, src.get(sn, c))
        dst.set(sn, SKILL_ICON_COL, str(icon_index[sn]))
        dst.set(sn, SKILL_STL_COL, key)
        if not stl.has(key):
            k = key.encode("latin-1")
            stl.keys.append((k, sn))
            for rows in stl.langs:
                rows.append([name.encode("latin-1"), desc.encode("latin-1")])
        changed += 1
        print(f"   skill {sn:>4} {name!r:<20} icon={icon_index[sn]} stl={key}")

    if not dry:
        backup(OUR_SKILL_STB)
        with open(OUR_SKILL_STB, "wb") as fh:
            fh.write(dst.to_bytes())
        stl.save(dry)

    # verify
    if not dry:
        chk = oro.Stb(OUR_SKILL_STB)
        for sn in FATE_SKILLS:
            assert chk.get(sn, 0) == src.get(sn, 0), sn
            assert chk.get(sn, SKILL_STL_COL) == (SKILL_STL_KEY % sn).encode(), sn
        chks = oro.Stl(OUR_SKILL_STL)
        for sn in FATE_SKILLS:
            assert chks.has(SKILL_STL_KEY % sn), sn
    print(f"   {changed} skill rows written")


ICON_LABEL = "fate%d"


def tsi_sprite_labels(ico, path):
    """{label: global sprite index} for every sprite in a SKILLICON-style TSI.

    Each 54-byte sprite entry is <h4iI> followed by a 32-byte label, and the
    label is the only stable handle we have -- indices shift only by appending,
    so a name lookup is what makes re-running this script a no-op instead of a
    second copy of every icon.
    """
    _, blocks = ico.tsi_read(path)
    labels, gi = {}, 0
    for cnt, raw in blocks:
        for i in range(cnt):
            name = raw[i * 54 + 22:(i + 1) * 54].split(b"\0")[0].decode("ascii", "replace")
            labels.setdefault(name, gi)
            gi += 1
    return labels


def tsi_drop_trailing(ico, path, count, dry):
    """Remove the last `count` sprites of the last sheet.

    Only ever used to undo duplicate appends. Trailing sprites are safe to drop
    because every other index stays put; the orphaned DDS cells are simply
    overwritten by the next add.
    """
    textures, blocks = ico.tsi_read(path)
    cnt, raw = blocks[-1]
    if count > cnt:
        raise SystemExit(f"cannot drop {count} sprites from a {cnt}-sprite sheet")
    blocks[-1] = (cnt - count, raw[:(cnt - count) * 54])
    if not dry:
        ico.tsi_write(path, textures, blocks, False)


def install_icons(dry):
    """Copy the four fate icons out of RoseZA's atlas into ours, once."""
    spec = importlib.util.spec_from_file_location(
        "add_skill_icon", os.path.join(HERE, "add-skill-icon.py"))
    ico = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ico)
    from PIL import Image

    have = tsi_sprite_labels(ico, ico.TSI)
    dupes = sum(1 for sn in FATE_SKILLS if ICON_LABEL % sn in have)
    if dupes and dupes != len(FATE_SKILLS):
        raise SystemExit("our skill atlas has some but not all fate icons -- "
                         "restore SKILLICON.TSI from .bak-icons and re-run")

    src_tex, src_blk = ico.tsi_read(SRC_SKILL_TSI)
    flat = []
    for (sheet, _), (cnt, raw) in zip(src_tex, src_blk):
        for i in range(cnt):
            flat.append((sheet, raw[i * 54:(i + 1) * 54]))

    out = {}
    scratch = os.path.join(os.environ.get("TEMP", "."), "oro-fate-icons")
    os.makedirs(scratch, exist_ok=True)
    for sn in FATE_SKILLS:
        label = ICON_LABEL % sn
        if label in have:
            out[sn] = have[label]
            print(f"   icon {sn}: already installed at sprite index {out[sn]}")
            continue
        sheet, ent = flat[SRC_ICON_INDEX[sn]]
        _, x1, y1, x2, y2 = struct.unpack_from("<hiiii", ent, 0)
        dds = os.path.join(SRC_ROOT, "3DDATA", "CONTROL", "RES", sheet)
        if not os.path.exists(dds):
            for alt in os.listdir(os.path.dirname(dds)):
                if alt.lower() == sheet.lower():
                    dds = os.path.join(os.path.dirname(dds), alt)
                    break
        img = Image.open(dds).convert("RGBA").crop((x1, y1, x2 + 1, y2 + 1))
        png = os.path.join(scratch, label + ".png")
        img.save(png)
        if dry:
            out[sn] = -1
            print(f"   icon {sn}: would add from {sheet} {(x1, y1)}")
            continue
        out[sn] = _add_icon_fallback(ico, png, label)
        print(f"   icon {sn}: {sheet} {(x1, y1)} -> our sprite index {out[sn]}")
    return out


def _add_icon_fallback(ico, png, name):
    """add-skill-icon.py drives itself from argv; call it that way."""
    saved = sys.argv
    sys.argv = ["add-skill-icon.py", png, "--name", name]
    buf, real = io.StringIO(), sys.stdout
    try:
        sys.stdout = buf
        ico.main()
    except SystemExit:
        pass
    finally:
        sys.stdout, sys.argv = real, saved
    for line in buf.getvalue().splitlines():
        if "index" in line.lower():
            for tok in line.replace(":", " ").split():
                if tok.isdigit():
                    return int(tok)
    raise SystemExit(f"could not read the new icon index from:\n{buf.getvalue()}")


# ================================================================ stage 2
def stage_triggers(dry):
    print("== stage 2: fate triggers in QP101.QSD")
    ours = open(OUR_QP101, "rb").read()
    src = open(SRC_QP101, "rb").read()

    if qsd_has_trigger(ours, TRIG_CHOOSE_ARUA):
        print("   already present, nothing to do")
        return

    triggers = []
    for name in COPY_TRIGGERS:
        raw = qsd_trigger_bytes(src, name)
        if raw is None:
            raise SystemExit(f"{name} not found in {SRC_QP101}")
        if qsd_has_trigger(ours, name):
            raise SystemExit(f"{name} unexpectedly already in our QP101")
        triggers.append(raw)
        print(f"   copied {name} ({len(raw)} bytes)")

    # Templates come from real entities so the on-disk shape is exactly what the
    # server already parses; only the payload fields are overwritten.
    t_cond09 = qsd_find_entity(src, "Arua_Skill", COND_009)
    t_rewd14 = qsd_find_entity(src, "Arua_NoSkill", REWD_014)
    t_rewd15 = qsd_find_entity(src, "1828-02", REWD_015)
    for label, t in (("COND_009", t_cond09), ("REWD_014", t_rewd14), ("REWD_015", t_rewd15)):
        if t is None:
            raise SystemExit(f"no template for {label}")

    # "has neither fate skill" -- a range condition, which is why the SN2 typo in
    # F_QSTCOND009 had to be fixed first (it compared SN1 twice).
    # STR_COND_009 payload: int iSkillSN1; int iSkillSN2; BYTE btOp (0 = lacks).
    no_fate = qsd_patch(t_cond09, (0, "<iiB", (SKILL_ARUA, SKILL_HEBARN, 0)))
    for trig, switch, skill in ((TRIG_CHOOSE_ARUA, SWITCH_ARUA, SKILL_ARUA),
                                (TRIG_CHOOSE_HEBARN, SWITCH_HEBARN, SKILL_HEBARN)):
        triggers.append(qsd_build_trigger(
            trig,
            [no_fate],
            # STR_REWD_015: short nSN; BYTE btOp.
            # STR_REWD_014: BYTE btOp (1 = give); 3 bytes padding; int iSkillNo.
            [qsd_patch(t_rewd15, (0, "<hB", (switch, 1))),
             qsd_patch(t_rewd14, (0, "<B", (1,)), (4, "<i", (skill,)))],
        ))
        print(f"   built {trig}: switch {switch} = 1, grant skill {skill}")

    out = qsd_append_pattern(ours, FATE_PATTERN, triggers)
    ok, consumed = qsd_parse_ok(out)
    if not ok:
        raise SystemExit(f"rebuilt QP101 does not re-parse ({consumed}/{len(out)})")
    for name in list(COPY_TRIGGERS) + [TRIG_CHOOSE_ARUA, TRIG_CHOOSE_HEBARN]:
        if not qsd_has_trigger(out, name):
            raise SystemExit(f"{name} missing after rebuild")
    write_file(OUR_QP101, out, dry)
    print(f"   QP101.QSD {len(ours)} -> {len(out)} bytes, re-parsed clean")


# ================================================================ stage 3
def stage_dialog(dry):
    print("== stage 3: fate choice on Nova (EM73-001.CON)")
    blob = open(OUR_NOVA_CON, "rb").read()

    for idx, want in ((0, "TA_arua_skill"), (1, "TA_hebarn_skill")):
        _, _, chk, _, _ = con_get_item(blob, NOVA_FATE_MENU, idx)
        if chk != want:
            raise SystemExit(
                f"menu {NOVA_FATE_MENU} item {idx} check is {chk!r}, expected {want!r} "
                f"-- the dialog is not the one this script was written against")

    out = con_set_click(blob, NOVA_FATE_MENU, 0, CLICK_ARUA)
    out = con_set_click(out, NOVA_FATE_MENU, 1, CLICK_HEBARN)
    head, lua, appendix = con_split(out)
    out = con_join(head, lua, appendix_upsert(appendix, APPENDIX_LUA))

    # verify by decoding the result the way the client will
    head2, lua2, ap2 = con_split(out)
    if lua2 != lua:
        raise SystemExit("main Lua blob did not survive the re-encode")
    if APPENDIX_LUA not in ap2.decode("latin-1"):
        raise SystemExit("appendix did not round-trip")
    for idx, want in ((0, CLICK_ARUA), (1, CLICK_HEBARN)):
        _, _, chk, clk, _ = con_get_item(out, NOVA_FATE_MENU, idx)
        if clk != want:
            raise SystemExit(f"click patch failed on item {idx}: {clk!r}")
        print(f"   menu {NOVA_FATE_MENU} item {idx}: check={chk} click={clk}")
    write_file(OUR_NOVA_CON, out, dry)
    print(f"   EM73-001.CON {len(blob)} -> {len(out)} bytes, appendix {len(ap2)} bytes")


# ================================================================ selftest
def selftest():
    print("== selftest")
    blob = open(OUR_NOVA_CON, "rb").read()
    head, lua, appendix = con_split(blob)
    rebuilt = con_join(head, lua, appendix)
    assert rebuilt == blob, "con_split/con_join is not identity on the shipped file"
    print("   .CON split/join round-trips byte-identically")

    patched = con_set_click(blob, NOVA_FATE_MENU, 0, "AT_test")
    assert con_get_item(patched, NOVA_FATE_MENU, 0)[3] == "AT_test"
    assert len(patched) == len(blob)
    back = con_set_click(patched, NOVA_FATE_MENU, 0,
                         con_get_item(blob, NOVA_FATE_MENU, 0)[3])
    assert back == blob, "click patch is not reversible -- it disturbed something else"
    print("   .CON click patch is in-place and reversible")

    ours = open(OUR_QP101, "rb").read()
    ok, consumed = qsd_parse_ok(ours)
    assert ok, f"our QP101 does not parse cleanly ({consumed}/{len(ours)})"
    print("   QP101.QSD parses exactly")

    lua4 = os.path.join(ROOT, "bin", "release", "thirdparty", "lua4.exe")
    if os.path.exists(lua4):
        run_dialog_probe(lua4)
    else:
        print(f"   (skipping the Lua probe -- {lua4} not built)")


def run_dialog_probe(lua4):
    """Run the dialog's Lua and confirm the menu offers what we expect.

    Probes the *patched* conversation whether or not stage 3 has run yet, so
    this doubles as a pre-flight check: the appendix is applied in memory when
    the on-disk file does not have it.
    """
    import subprocess
    import tempfile
    blob = open(OUR_NOVA_CON, "rb").read()
    _, lua, appendix = con_split(blob)
    if APPENDIX_LUA not in appendix.decode("latin-1"):
        appendix = appendix_upsert(appendix, APPENDIX_LUA)
        print("   (probing the patched dialog -- stage 3 has not been applied yet)")
    tmp = tempfile.mkdtemp()
    main_blob = os.path.join(tmp, "main.lub")
    open(main_blob, "wb").write(lua)
    harness = """
ARUA = ARUA or 0
HEBARN = HEBARN or 0
function QF_hasAruaFate() return ARUA end
function QF_hasHebarnFate() return HEBARN end
function QF_hasFate() if ARUA > 0 or HEBARN > 0 then return 1 end return 0 end
-- stands in for the stage-2 triggers: COND_009 "has skill 2880 / 2881"
function QF_checkQuestCondition(t)
  if t == "Arua_Skill" then return ARUA end
  if t == "Hebarn_Skill" then return HEBARN end
  return 0
end
function QF_doQuestTrigger(t) FIRED = t return 1 end
function QF_getUserSwitch(n) return 0 end
function QF_findQuest(q) return -1 end
function QF_getEventOwner(h) return 1 end
function GF_GetTarget() return 1 end
"""
    for label, setup, want in (
            ("no fate", "ARUA=0 HEBARN=0", (1, 1, 1)),
            ("arua", "ARUA=1 HEBARN=0", (1, 0, 0)),
            ("hebarn", "ARUA=0 HEBARN=1", (0, 1, 0))):
        script = os.path.join(tmp, "run.lua")
        with open(script, "w") as fh:
            fh.write(harness + setup + "\n")
            fh.write('dofile("%s")\n' % main_blob.replace("\\", "/"))
            fh.write(appendix.decode("latin-1") + "\n")
            fh.write('print("R "..TA_arua_skill().." "..TA_hebarn_skill()'
                     '.." "..TA_nofate_Skill())\n')
            fh.write('AT_OroFateArua()\nprint("FIRED "..tostring(FIRED))\n')
        r = subprocess.run([lua4, script], capture_output=True, text=True)
        got = None
        fired = None
        for line in (r.stdout + r.stderr).splitlines():
            if line.startswith("R "):
                got = tuple(int(x) for x in line.split()[1:4])
            if line.startswith("FIRED "):
                fired = line.split(None, 1)[1]
        if got != want:
            raise SystemExit(f"   probe [{label}]: menu flags {got}, expected {want}\n"
                             f"{r.stdout}{r.stderr}")
        expect_fire = (label == "no fate")
        if (fired != "nil") != expect_fire:
            raise SystemExit(f"   probe [{label}]: trigger fired={fired}, "
                             f"expected {'a trigger' if expect_fire else 'nil'}")
        print(f"   probe [{label:<7}] arua/hebarn/nofate = {got}, click fired {fired}")


# ================================================================ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--stage", type=int, choices=(1, 2, 3), action="append")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return

    for path in (OUR_SKILL_STB, OUR_SKILL_STL, OUR_QP101, OUR_NOVA_CON,
                 SRC_SKILL_STB, SRC_QP101, SRC_SKILL_TSI):
        if not os.path.exists(path):
            raise SystemExit(f"missing: {path}")

    stages = args.stage or [1, 2, 3]
    oro = load_oro()
    if 1 in stages:
        stage_skills(oro, args.dry_run)
    if 2 in stages:
        stage_triggers(args.dry_run)
    if 3 in stages:
        stage_dialog(args.dry_run)
    print("\ndone." + ("  (dry run -- nothing written)" if args.dry_run else ""))


if __name__ == "__main__":
    main()
