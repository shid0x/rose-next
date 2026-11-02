"""Read-only audit: use items (LIST_USEITEM) that cannot do anything (2026-11-02).

Tester report: the Halloween Tree (10:441) "doesn't work -- you use it, it is not
consumed and nothing happens". Class 313 (USE_ITEM_SKILL_DOING) casts the skill
in col 20, and its LIST_SKILL row 2844 is blank. The server
(classUSER::Use_InventoryITEM) consumes a 313 item only when its skill is a self
skill (types 8/10/12/14/17), a target skill (3-6/9/11/13/19/20), a warp (18) or an
emote (16); anything else returns before the quantity is decremented, and nothing
tells the player. The client checks almost nothing first, so the item looks dead.

What each class needs, as the server decides it:

  313  cast       col 20 = a LIST_SKILL row of a castable type; a summon (14)
                  needs its NPC (skill col 28) in LIST_NPC and in LIST_NPC.CHR
  314  learn      col 20 = a real skill
  316  event      col 22 = a LIST_EVENT row (the client runs its .CON)
  322  package    col 20 = a package id the server declares (FindUseItemPackage
                  in gs_user.cpp, parsed here); unknown ids keep the box
  other           Use_pITEM: col 24 (status) or col 19 (ability) must be set,
                  or the item is consumed for nothing

A finding is listed with where a player can get it: LIST_SELL, ITEM_DROP, a
package reward or a quest reward (QSD). Nothing is written.

First run (2026-11-02) found 73 once the package parser was fixed (see
packages()); scripts/erase-dead-use-items.py erased them.

    python scripts/audit-use-items.py
    python scripts/audit-use-items.py --all     # also items nobody can obtain
"""

import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rosefmt import qsd  # noqa: E402
from rosefmt.chr import Chr  # noqa: E402
from rosefmt.stb import Stb  # noqa: E402
from rosefmt.stl import Stl  # noqa: E402

STB = os.path.join(REPO, "data", "3DDATA", "STB")
GS_USER = os.path.join(REPO, "src", "sho_gameserver", "src", "gs_user.cpp")

T_USE = 10
LEGACY_MAX_ITEM_NO = 999
WIDE_BASE = 100000

SELF_TYPES = {8, 10, 12, 14, 17}
TARGET_TYPES = {3, 4, 5, 6, 9, 11, 13, 19, 20}
CASTABLE = SELF_TYPES | TARGET_TYPES | {16, 18}
ITEM_TYPE_NAMES = {"HELMET": 2, "ARMOR": 3, "GAUNTLET": 4, "BOOTS": 5, "KNAPSACK": 6,
                   "JEWEL": 7, "WEAPON": 8, "SUBWPN": 9, "USE": 10, "ETC": 12,
                   "NATURAL": 12, "QUEST": 13, "RIDE_PART": 14, "FACE_ITEM": 1}


def num(s):
    s = s.strip()
    return int(s) if s.lstrip("-").isdigit() else 0


def code(item_no):
    if item_no <= LEGACY_MAX_ITEM_NO:
        return str(T_USE * 1000 + item_no)
    return str(T_USE * WIDE_BASE + item_no)


def packages():
    """{package id: [(item type, item no)]} from the server's table.

    Comments are stripped first: the first version of this parser expected each
    Package(...) to follow the previous one directly, so a comment between two
    entries merged the next package into the previous one, and six declared
    boxes (770, 775, 811, 830, 900, 920) read as undeclared -- five of them were
    erased by mistake and restored the same day."""
    src = open(GS_USER, encoding="utf-8", errors="replace").read()
    body = src[src.index("FindUseItemPackage(short package_id)"):]
    body = re.sub(r"//[^\n]*", "", body[:body.index("};")])
    out = {}
    for m in re.finditer(r"Package\((\d+),(.*?)\)\),", body, re.S):
        rewards = re.findall(r"Reward\(ITEM_TYPE_(\w+),\s*(\d+)", m.group(2))
        out[int(m.group(1))] = [(ITEM_TYPE_NAMES.get(t, -1), int(n)) for t, n in rewards]
    return out


def sources(pkgs):
    """{code: [where]} for LIST_SELL, ITEM_DROP and package rewards."""
    out = {}
    for name in ("LIST_SELL.STB", "ITEM_DROP.STB"):
        t = Stb(os.path.join(STB, name))
        for r in range(t.rows):
            for c in range(t.cols):
                v = t.s(r, c).strip()
                if v.isdigit():
                    out.setdefault(v, set()).add(name.split(".")[0])
    for pid, rewards in pkgs.items():
        for t, n in rewards:
            if t == T_USE:
                out.setdefault(code(n), set()).add("package %d" % pid)
    for p in glob.glob(os.path.join(REPO, "data", "3DDATA", "QUESTDATA", "**", "*.QSD"), recursive=True):
        for _off, sn, kind in qsd.item_refs(p):
            if kind == "reward":
                out.setdefault(str(sn), set()).add(os.path.basename(p))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true", help="also list items no shop, drop or package gives")
    args = ap.parse_args()

    use = Stb(os.path.join(STB, "LIST_USEITEM.STB"))
    names = Stl(os.path.join(STB, "LIST_USEITEM_S.STL"), encoding="utf-8")
    skill = Stb(os.path.join(STB, "LIST_SKILL.STB"))
    npc = Stb(os.path.join(STB, "LIST_NPC.STB"))
    chars = Chr(os.path.join(REPO, "data", "3DDATA", "NPC", "LIST_NPC.CHR")).chars
    event = Stb(os.path.join(STB, "LIST_EVENT.STB"))
    pkgs = packages()
    where = sources(pkgs)

    def skill_row(s):
        return 0 < s < skill.rows and (num(skill.s(s, 5)) or skill.s(s, 1).strip())

    findings = []
    for r in range(1, use.rows):
        key = use.s(r, use.cols - 1).strip()
        if not key or not names.has(key):
            continue
        name = names.field(key, 0, 0).strip()
        if not name:
            continue
        cls = num(use.s(r, 4))
        val = num(use.s(r, 20))
        problem = None
        if cls == 313:
            if not skill_row(val):
                problem = "skill %d is a blank LIST_SKILL row" % val
            else:
                st = num(skill.s(val, 5))
                if st not in CASTABLE:
                    problem = "skill %d is type %d, which an item cannot cast" % (val, st)
                elif st == 14:
                    pet = num(skill.s(val, 28))
                    if not (0 < pet < npc.rows and npc.s(pet, 0).strip()):
                        problem = "skill %d summons NPC %d, a blank LIST_NPC row" % (val, pet)
                    elif pet >= len(chars) or chars[pet] is None:
                        problem = "skill %d summons NPC %d, which has no LIST_NPC.CHR model" % (val, pet)
        elif cls == 314:
            if not skill_row(val):
                problem = "teaches skill %d, a blank LIST_SKILL row" % val
        elif cls == 316:
            ev = num(use.s(r, 22))
            if not (0 < ev < event.rows and any(event.s(ev, c).strip() for c in range(event.cols))):
                problem = "event %d is a blank LIST_EVENT row" % ev
        elif cls == 322:
            if val not in pkgs:
                problem = "package %d is not declared by the server" % val
        elif cls in (315, 317, 320, 321):
            pass
        elif not num(use.s(r, 24)) and not num(use.s(r, 19)):
            problem = "class %d with no status (col 24) and no ability (col 19): consumed for nothing" % cls
        if problem:
            got = sorted(where.get(code(r), ()))
            findings.append((r, name, cls, problem, got))

    shown = [f for f in findings if args.all or f[4]]
    for r, name, cls, problem, got in shown:
        print("10:%-5d %-34s class %d  %s%s" % (r, name, cls, problem,
                                                "  [%s]" % ", ".join(got) if got else ""))
    hidden = len(findings) - len(shown)
    print("\n%d use item(s) cannot work%s" % (len(findings),
          (" -- %d obtainable listed, %d nobody can get (--all)" % (len(shown), hidden)) if hidden else ""))


if __name__ == "__main__":
    main()
