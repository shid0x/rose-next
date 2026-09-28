"""Relabel the skill tooltip in plain English: labels, skill types, targets, stats.

The skill tooltip (`CIconSkill::GetToolTip`, shared by the classic windows and
UI2) is assembled from strings in five STLs, English block 1 of each:

  * LIST_STRING_S.STL -- the labels ("Classification", "Required Mana",
    "Status/Condition" ... GetString(n), typeresource.h names them);
  * STR_SKILLTYPE.STL -- the engine's skill type, printed as the tooltip's
    "type" ("Damage Action", "Regional Magick Damage", "Continuing ");
  * STR_SKILLTARGET.STL -- the target filter ("Hostile Character",
    "Fainted Friendly Character");
  * STR_ABILITY.STL -- stat names, shared with the item tooltips;
  * LIST_CLASS_S.STL -- the class line ("Scouter" where every other table says
    Scout).

Part A of the 2026-09-28 tooltip review: data only, so the format strings stay
as the code has them ("Type:Attack", "Duration:5Seconds"; the spacing, the list
commas and an honest status-chance line are part B, a code pass). Two labels
were misleading rather than just awkward:

  * "Required Mana" is printed for every cost -- "Required Mana: HP 110" on
    Leap Attack, Zuly on the coin skills. It is now "Cost". The item tooltips
    use the same label for magic items and read "[Cost: MP 12]".
  * The damage-formula names (SKILL_DAMAGE_TYPE, col 15) did not match
    CCal::Get_SkillDAMAGE: 0 "Continuing Attack" is the normal-attack formula
    (Attack Power, Critical), 3 "Natural Magic" is the bare-handed one (skill
    power and INT, no weapon). Now Physical / Weapon / Magic / Unarmed.

Shared strings, checked before renaming: "Classification" (106) is also the
item tooltip's type label -- "Type" reads right for both; "Seconds" (315) and
"Minute" (15) are the quest timer's too.

Part B (same day) moved the punctuation into the code -- ciconskill.cpp now
prints "Label: value", commas in lists, "5 sec", a Buff/Debuff type computed
from the status, and a chance line that mirrors the server's roll -- and with
it these labels: 15/315 "min"/"sec" (the quest timer became "%d %s %d %s"),
318 "Chance", and 321 "Class" (part A's "Class:" fitted the old "[%s %s]").
A deployed client older than part B shows "[Class Soldier Class]" and
"Duration:5sec" against these strings; ship the two together.

LIST_STRING_S.STL was corrupt: entry 514 (key 653, the maintenance notice)
carries a length prefix of 159 over 158 bytes of text. The client reads each
language block sequentially (CStringManager::LoadTypeTable ignores the
per-entry offset table), so it swallowed the next entry's length byte and read
every later string one byte off -- 266 of 784 garbled from key 654 on ("Trade",
"Community", the cart-ride prompt, strings with %s/%d in them). It has been so
since at least 2026-06-18; no script here ever wrote the file. This codec
takes each entry's text from its offset-table span, so any rewrite repairs it;
`--selftest` proves the repair is exactly that one byte (0x9f -> 0x9e).

Every entry is (expected old text, new text). A text that is neither -- some
later import changed it -- refuses the write, as in fix-skill-book-names.py.
The sidecar data/3DDATA/STB/tooltip-labels.json keeps the replaced text per
field; `--restore` puts back exactly those fields (never the corrupt prefix).
Client data only: re-bake the VFS.

Usage:
    python scripts/fix-tooltip-labels.py --selftest
    python scripts/fix-tooltip-labels.py --dry-run
    python scripts/fix-tooltip-labels.py
    python scripts/fix-tooltip-labels.py --verify
    python scripts/fix-tooltip-labels.py --restore
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SIDECAR = os.path.join(STB_DIR, "tooltip-labels.json")
ENGLISH = 1                          # LANGUAGE_USA, the block the client reads here
FIELDS = {b"NRST01": 1, b"ITST01": 2, b"QEST01": 4}

LABELS = {
    "LIST_STRING_S.STL": {           # GetString(n); typeresource.h
        "15": ("Minute", "min"),                                 # STR_MINUTE (quest timer)
        "315": ("Seconds", "sec"),                               # STR_SECOND (tooltip, quest timer)
        "318": ("Success Rate", "Chance"),                       # STR_SUCCESS_RATE
        "34": ("Required Summon Amount", "Summon Gauge Cost"),   # STR_REQUIRE_SUMMONQUANTITY
        "80": ("Continuing Attack", "Physical Attack"),          # STR_SKILLPOWER_EFFECT_0
        "83": ("Natural Magic", "Unarmed Attack"),               # STR_SKILLPOWER_EFFECT_3
        "106": ("Classification", "Type"),                       # STR_ITEM_TYPE (items too)
        "272": ("Experience Point Recovery", "EXP Restored"),    # STR_RECOVERY_EXP
        "309": ("Distance", "Range"),                            # STR_SHOOT_RANGE
        "310": ("Area of Effect", "Area"),                       # STR_APPLY_RANGE
        "317": ("Skill Power", "Power"),                         # STR_SKILL_POWER
        "319": ("Required Mana", "Cost"),                        # STR_CONSUME_ABILITY (items too)
        "320": ("Required Equipment", "Equipment"),              # STR_REQUIRE_EQUIP
        "321": ("Job Name", "Class", "Class:"),                  # STR_REQUIRE_JOB; part A's
                                                                 # "Class:" fitted "[%s %s]"
        "322": ("Required Skills", "Requires"),                  # STR_REQUIRE_SKILL
        "323": ("Required Ability", "Requires"),                 # STR_REQUIRE_ABILITY
        "506": ("Required Skill Points", "Skill Points"),        # STR_REQUIRE_SKILLPOINT
        "514": ("Absorption", "Drains"),                         # STR_ABSORPTION
        "515": ("Upgraded Status", "Bonus"),                     # STR_CHANGE_ABILITY (passives)
        "516": ("Status/Condition", "Effect"),                   # STR_STATE
    },
    "STR_SKILLTYPE.STL": {           # SKILL_TYPE, LIST_SKILL col 5
        "1": ("Basic Motion", "Basic Action"),
        "2": ("Craft Skill", "Crafting"),
        "3": ("Damage Action", "Attack"),
        "4": ("Weapon Power Up", "Weapon Enchant"),
        "5": ("Weapon Power Up", "Weapon Enchant"),
        "6": ("Magick Attack", "Ranged Attack"),                 # projectile skills
        "7": ("Regional Magick Damage", "Area Attack"),          # area around the target
        "8": ("Continuing(Self)", "Buff"),                       # self / party buffs
        "9": ("Continuing ", "Buff / Debuff"),                   # Support and Mild alike
        "10": ("Recovering Magick", "Heal"),
        "11": ("Recovering Magick", "Heal"),
        "12": ("Continuing", "Self Effect"),                     # Stealth, Detect
        "13": ("Continuing", "Buff / Debuff"),                   # Purify, Silence, Vanish
        "14": ("Summoning Magick", "Summon"),
        "16": ("Motion Expression", "Emote"),
        "17": ("Total Attack(Self)", "Area Attack"),             # area around you
        "18": ("Reservation", "Warp"),                           # io_skill.h: warp skill
        "19": ("Absorption Damage Action", "Drain Attack"),
    },
    "STR_SKILLTARGET.STL": {         # SKILL_CLASS_FILTER, LIST_SKILL col 7
        "0": ("Yourself", "Self"),
        "1": ("Party Member", "Party"),
        "2": ("Clan Member", "Clan"),
        "3": ("Friendly Forces", "Ally"),
        "5": ("Hostile Character", "Enemy"),
        "6": ("Hostile Avatar", "Enemy Player"),
        "7": ("All Avatars", "Any Player"),
        "8": ("All Characters", "Anyone"),
        "9": ("Fainted Friendly Character", "Fallen Ally"),
        "10": ("Hostile Monster", "Enemy Monster"),
    },
    "STR_ABILITY.STL": {             # AT_* ids; item tooltips show these too
        "20": ("Hit Rate", "Accuracy"),
        "22": ("Dodge Rate", "Dodge"),
        "56": ("HP Recovery Amount", "HP Recovery"),
        "57": ("MP Recovery Rate", "MP Recovery"),
        "58": ("Bagpack Capacity", "Carry Capacity"),
        "59": ("Sales Discount", "Shop Discount"),
        "60": ("Sales Premium", "Sale Price Bonus"),
        "62": ("Summon Gauge Increase", "Summon Gauge"),
        "63": ("Item Drop Rate Increase", "Drop Rate"),
        "72": ("Drop rate", "Drop Rate"),
        "98": ("Res", "Magic Resistance"),
        "99": ("Hit", "Accuracy"),
        "101": ("Dodge Rate", "Dodge"),
        "102": ("Shield Def", "Shield Defense"),
        "106": ("Exp rate increase", "EXP Rate"),
        "107": ("Drop rate increase", "Drop Rate"),
        "109": ("No Drop Penitaly", "No Drop Penalty"),
    },
    "LIST_CLASS_S.STL": {            # STR_JOB and every other table say Scout
        "LCLA016": ("Scouter", "Scout"),
        "LCLA066": ("Scouter Class", "Scout Class"),
    },
}


# ------------------------------------------------------------------ codec

def varint(n):
    out = b""
    while n > 0x7F:
        out += bytes([(n & 0x7F) | 0x80])
        n >>= 7
    return out + bytes([n])


def read_varint(b, o):
    n, shift = 0, 0
    while True:
        c = b[o]
        o += 1
        n |= (c & 0x7F) << shift
        if c < 0x80:
            return n, o
        shift += 7


class Stl:
    """NRST01 / ITST01 / QEST01. Language blocks that share an offset stay
    shared. Text comes from each entry's offset-table span, so a wrong length
    prefix (LIST_STRING_S entry 514) is repaired, and reported in `bad`."""

    def __init__(self, path):
        self.path = path
        b = self.raw = open(path, "rb").read()
        n, o = read_varint(b, 0)
        self.tag = b[o:o + n]
        o += n
        if self.tag not in FIELDS:
            raise ValueError("%s: unknown STL tag %r" % (path, self.tag))
        self.nf = FIELDS[self.tag]
        count, = struct.unpack_from("<I", b, o)
        o += 4
        self.keys = []
        for _ in range(count):
            n, o = read_varint(b, o)
            k = b[o:o + n]
            o += n
            idx, = struct.unpack_from("<I", b, o)
            o += 4
            self.keys.append((k, idx))
        nlang, = struct.unpack_from("<I", b, o)
        o += 4
        self.lang_off = list(struct.unpack_from("<%dI" % nlang, b, o))
        self.block_starts = sorted(set(self.lang_off))
        self.blocks = {}                  # block offset -> [entry tuple]
        self.bad = []
        for bi, start in enumerate(self.block_starts):
            end = self.block_starts[bi + 1] if bi + 1 < len(self.block_starts) else len(b)
            offs = list(struct.unpack_from("<%dI" % count, b, start)) + [end]
            entries = []
            for i in range(count):
                p, fields = offs[i], []
                for f in range(self.nf):
                    ln, q = read_varint(b, p)
                    if f == self.nf - 1 and q + ln != offs[i + 1]:
                        self.bad.append((start, i, self.keys[i][0], ln, offs[i + 1] - q))
                        ln = offs[i + 1] - q
                    fields.append(b[q:q + ln])
                    p = q + ln
                entries.append(fields)
            self.blocks[start] = entries

    def index(self):
        return {k.decode("latin-1"): i for i, (k, _) in enumerate(self.keys)}

    def english(self):
        return self.blocks[self.lang_off[ENGLISH]]

    def to_bytes(self):
        count = len(self.keys)
        head = varint(len(self.tag)) + self.tag + struct.pack("<I", count)
        for k, idx in self.keys:
            head += varint(len(k)) + k + struct.pack("<I", idx)
        head += struct.pack("<I", len(self.lang_off))
        pos = len(head) + 4 * len(self.lang_off)
        body, where = b"", {}
        for start in self.block_starts:
            where[start] = pos + len(body)
            base = where[start] + 4 * count
            offs, blob = [], b""
            for fields in self.blocks[start]:
                offs.append(base + len(blob))
                for f in fields:
                    blob += varint(len(f)) + f
            body += struct.pack("<%dI" % count, *offs) + blob
        head += struct.pack("<%dI" % len(self.lang_off),
                            *(where[s] for s in self.lang_off))
        return head + body


# ------------------------------------------------------------------ passes

def load_all():
    return {name: Stl(os.path.join(STB_DIR, name)) for name in LABELS}


def selftest(files):
    ok = True
    for name, stl in files.items():
        out = stl.to_bytes()
        if name == "LIST_STRING_S.STL" and stl.bad:     # before the repair
            diff = [i for i in range(min(len(out), len(stl.raw))) if out[i] != stl.raw[i]]
            want = len(stl.bad) == 1 and stl.bad[0][2] == b"653" and \
                len(out) == len(stl.raw) and len(diff) == 1 and \
                (stl.raw[diff[0]], out[diff[0]]) == (0x9F, 0x9E)
            print("%-20s %s (bad prefixes %s, bytes changed %s)" % (
                name, "OK" if want else "FAIL",
                [(k.decode(), was, real) for _, _, k, was, real in stl.bad],
                ["%d: %02x->%02x" % (i, stl.raw[i], out[i]) for i in diff[:3]]))
            ok &= want
        else:
            same = out == stl.raw and not stl.bad
            print("%-20s %s" % (name, "round-trips byte for byte" if same else "FAIL"))
            ok &= same
    return ok


def plan(files, side):
    changes, foreign = [], []
    for name, table in LABELS.items():
        stl = files[name]
        idx, eng = stl.index(), stl.english()
        for key, (old, new, *earlier) in table.items():
            if key not in idx:
                sys.exit("%s: key %s missing" % (name, key))
            cur = eng[idx[key]][0].decode("utf-8", "replace")
            if cur == new:
                continue
            prev = side.get(name, {}).get(key, old)
            if cur not in (old, prev, *earlier):     # earlier = what this script wrote before
                foreign.append("%s %s is %r, expected %r" % (name, key, cur, old))
            changes.append((name, key, cur, new))
    return changes, foreign


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for flag in ("--selftest", "--dry-run", "--verify", "--restore"):
        ap.add_argument(flag, action="store_true")
    args = ap.parse_args()

    files = load_all()
    side = {}
    if os.path.isfile(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            side = json.load(fh)

    if args.selftest:
        sys.exit(0 if selftest(files) else 1)

    if args.restore:
        if not side:
            sys.exit("no sidecar -- nothing to restore")
        n = 0
        for name, table in side.items():
            stl = files[name]
            idx, eng = stl.index(), stl.english()
            for key, old in table.items():
                eng[idx[key]][0] = old.encode("utf-8")
                n += 1
            with open(stl.path, "wb") as fh:
                fh.write(stl.to_bytes())
        os.remove(SIDECAR)
        print("restored %d label(s); sidecar removed" % n)
        return

    changes, foreign = plan(files, side)
    if args.verify:
        for name, key, cur, new in changes:
            print("DIFF %s %s: %r != %r" % (name, key, cur, new))
        bad = sum(len(s.bad) for s in files.values())
        if bad:
            print("%d entr(ies) with a length prefix that disagrees with its span" % bad)
        total = sum(len(t) for t in LABELS.values())
        print("verify:", "OK (%d labels)" % total if not changes and not bad
              else "%d label(s) differ" % len(changes))
        sys.exit(0 if not changes and not bad else 1)

    if not selftest(files):
        sys.exit("selftest failed -- refusing to write")
    for name, key, cur, new in changes:
        print("%-20s %-8s %-28r -> %r" % (name, key, cur, new))
    if foreign:
        sys.exit("\nchanged since reviewed:\n  " + "\n  ".join(foreign))
    if args.dry_run:
        print("dry run; %d label(s) would change, nothing written" % len(changes))
        return
    if not changes and not any(s.bad for s in files.values()):
        print("nothing to do; all %d labels already in place"
              % sum(len(t) for t in LABELS.values()))
        return

    touched = set()
    for name, key, cur, new in changes:
        stl = files[name]
        side.setdefault(name, {}).setdefault(key, cur)
        stl.english()[stl.index()[key]][0] = new.encode("utf-8")
        touched.add(name)
    touched |= {n for n, s in files.items() if s.bad}   # the prefix repair
    for name in touched:
        with open(files[name].path, "wb") as fh:
            fh.write(files[name].to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(side, fh, indent=1, sort_keys=True)

    after, _ = plan(load_all(), side)
    if after or any(s.bad for s in load_all().values()):
        sys.exit("VERIFY FAILED after writing: %s" % after)
    print("wrote %d label(s) across %d file(s); verified by re-read. "
          "Re-bake the VFS (client-only)." % (len(changes), len(touched)))


if __name__ == "__main__":
    main()
