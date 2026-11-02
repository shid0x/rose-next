"""Player-facing names for the skills monsters cast (2026-10-16).

The client names a skill only from the name table: CStringManager::GetSkillName reads the
key in LIST_SKILL col 86 and looks it up in LIST_SKILL_S.STL; with no entry it returns an
empty string (it never falls back to col 0). None of the 243 skills only monsters cast had
an entry, so players saw no monster skill name anywhere -- the damage meter's "Best: N -
Goblin (<skill>)" line, for one, dropped the skill. The user: give them proper names, as
long as it is risk free.

Risk: data-only and display-only. The server never reads LIST_SKILL_S.STL, and col 86 is
read by nothing but that lookup (several monster rows held a Korean description there,
which matched no key). The name table round-trips byte-for-byte through rosefmt.stl.

What it writes, for every skill in NAMES (each cast by a monster AI and learned by no
class):
  * LIST_SKILL col 86 = "LMobSkill<row>" (no existing key starts with LMobSkill)
  * a LIST_SKILL_S.STL entry under that key, id = the row: NAMES[row] as the name and
    DESC[row] (the old Korean descriptions, translated) as the description, the same
    English text in all five language slots (Korean, English, Japanese, two Chinese), so
    every client language shows it

Names were chosen per skill from what it does and who casts it (name-monster-skills.py
labels show both in our tools); skills that already had a good English name (Kera Stun
Blast, Prison Slumber, Astral Nova ...) keep it.

    python scripts/name-monster-skills-players.py --dry-run
    python scripts/name-monster-skills-players.py
    python scripts/name-monster-skills-players.py --verify
    python scripts/name-monster-skills-players.py --restore

Undo: the manifest (build/monster-skill-players/) keeps every col-86 cell it replaced;
restore puts them back and drops the LMobSkill entries from the name table. Client-side
data: re-bake the VFS (no server restart needed).
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import stl  # noqa: E402

STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_SKILL.STB")
STL = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_SKILL_S.STL")
MANIFEST = os.path.join(REPO, "build", "monster-skill-players", "manifest.json")
KEY_COL = 86
PREFIX = b"LMobSkill"

NAMES = {
    # -- kept English names / gun shots
    2067: "Sniping", 2466: "Poison Bullet", 2979: "Pang Jump Attack",
    2980: "Artillery Jump Attack", 2990: "Puppet Dispel", 3044: "Inguz Stun Wave",
    3551: "Gargoyle Stun", 3582: "Golem Stun Blast", 3588: "Jailer Fury",
    3593: "Cannibal Frenzy", 3595: "Kera Stun Blast", 3596: "Dispel Buffs",
    3597: "Area Slow", 3598: "Prison Slumber", 3603: "Charge", 3604: "Fireball",
    3609: "Dispel Buffs", 3610: "Area Slow", 3611: "Stun Blast", 3613: "Karkia Stun",
    3616: "Questarua Burst", 3627: "Kleitos Silence", 3686: "Karkia Ward",
    3711: "Ferdinand Stun", 3722: "Astral Guard", 3723: "Astral Might", 3724: "Astral Hex",
    3726: "Astral Nova", 3744: "Stunning Strike", 3760: "Silencing Wave",
    3771: "Vlad's Retaliation", 3779: "Armor Shatter", 3780: "Concussive Wave",
    3781: "Toxic Spike", 3782: "Rot Flame", 3783: "Rot Inferno", 7012: "Heal Ally",
    7013: "Long Shot", 7014: "Engineer Stun", 7015: "Puppet Bolt", 7016: "Twin Shot",
    7017: "Master Weapon", 7018: "Toxic Rain", 7019: "Hellfire Stomp",
    7020: "Infernal Charge", 7021: "Infernal Leap", 7022: "Hellfire Breath",
    7023: "Seal Burst",
    # -- shared self buffs (area ones lift the pack around the caster)
    2901: "Rallying Cry", 2902: "Battle Tempo", 2903: "Hawk Eye", 2904: "Shield Wall",
    2906: "Blur", 2911: "Battle Fury", 2912: "Haste", 2913: "Steady Aim", 2914: "Harden",
    2915: "Bulwark", 2916: "Evasion", 2917: "Sprint", 2920: "Vanish", 2921: "Alpha Howl",
    2922: "Quickstep", 2923: "Hunter's Focus", 2926: "Mirror Image",
    2931: "Dusk Empowerment", 2932: "Dusk Haste", 2933: "Dusk Focus", 2934: "Dusk Aegis",
    2936: "Dusk Veil", 2910: "Purify", 2940: "Moonlit Cleansing", 3006: "Arcane Ward",
    3505: "Igloo Shell", 3512: "Scurry", 3515: "Toxic Fervor", 3528: "Lock On",
    3535: "Dash", 3536: "Frost Rage", 3537: "Thick Fur", 3539: "Glacial Dash",
    3541: "Pack Leader's Rage", 3542: "Mountain Rage", 3550: "Wing Rush",
    3560: "Dragon Aura", 3574: "Rising Fury", 3576: "Lukete Fury", 3578: "Carapace",
    3581: "Moss Carapace", 3585: "Prisoner's Rage",
    # -- auras that weaken what stands near the caster, and self sacrifices
    3001: "Aura of Weakness", 3003: "Aura of Confusion", 3004: "Aura of Frailty",
    3557: "Torpor", 3599: "Reckless Abandon",
    # -- debuffs
    2941: "Enfeeble", 2942: "Sluggish Curse", 2943: "Blinding Haze", 2944: "Armor Break",
    2945: "Hex of Frailty", 2948: "Ensnare", 2953: "Moon Glare", 2954: "Rend Armor",
    2955: "Moon Curse", 2958: "Lunar Snare", 2961: "Nullify", 2968: "Slumber",
    2969: "Daze", 2970: "Petrifying Gaze", 2976: "Taunting Flutter", 3060: "Venom Bite",
    3068: "Hamstring", 3516: "Deadly Poison", 3540: "Frost Bite", 3552: "Stone Silence",
    3553: "Ignite", 3554: "Searing Ignite", 3555: "Blazing Ignite", 3562: "Kin Call",
    3563: "Hatchling Shriek", 3579: "Corrode",
    # -- heals
    2909: "Sunset Renewal", 2971: "Healing Warmth", 2972: "Warming Glow",
    2973: "Ember Mend", 2974: "Rune of Mending", 2975: "Rune of Renewal",
    3064: "Shell Mending", 3066: "Shadow Mend", 3594: "Second Wind",
    # -- strikes
    2986: "Leaping Strike", 2987: "Crushing Leap", 2988: "Triple Strike",
    3019: "Heavy Blow", 3034: "Talon Strike", 3071: "Elemental Burst",
    3072: "Elemental Lash", 3503: "Thug's Haymaker", 3504: "Iceblade Cut",
    3513: "Pincer Strike", 3514: "Venom Sting", 3518: "Maul", 3525: "Pounce",
    3526: "Rending Pounce", 3527: "Spectral Touch", 3533: "Thorn Gore",
    3538: "Bebeg Smash", 3543: "Avalanche Smash", 3544: "Frozen Fang",
    3545: "Antler Charge", 3548: "Stone Claw", 3549: "Diving Claw",
    3567: "Tiller Strike", 3568: "Pickaxe Strike", 3571: "Sweeper Strike",
    3572: "Savage Bite", 3573: "Mecha Crush", 3586: "Desperate Strike",
    3590: "Jailer's Lash",
    # -- bolts and shots
    2981: "Rock Throw", 2989: "Spore Shot", 2997: "Frost Bolt", 2998: "Greater Frost Bolt",
    2999: "Fire Bolt", 3000: "Greater Fire Bolt", 3021: "Ice Shard", 3022: "Frost Lance",
    3023: "Frost Volley", 3024: "Venom Arrow", 3025: "Arcane Bolt", 3026: "Spirit Bolt",
    3027: "Sting", 3028: "Slag Spit", 3032: "Flame Spit", 3049: "Fire Arrow",
    3050: "Flame Shot", 3052: "Frozen Shard", 3053: "Paralyzing Dart",
    3506: "Glacial Bolt", 3575: "Shadow Bolt", 3577: "Greater Shadow Bolt",
    3580: "Acid Spit", 3589: "Infiltrator's Dart", 3600: "Guard's Dart",
    # -- area attacks
    2983: "Ground Slam", 2984: "Quake", 2991: "Venom Whirl", 2992: "Whirlwind",
    2994: "Earthshaker", 3011: "Frost Stomp", 3012: "Arcane Shatter", 3014: "Titan Stomp",
    3015: "Falling Star", 3016: "Broadside", 3017: "Shockwave", 3018: "Behemoth Roar",
    3020: "Abyssal Pulse", 3031: "Dread Sweep", 3033: "Tail Sweep", 3042: "Searing Burst",
    3045: "Stunning Roar", 3046: "Thunder Roar", 3055: "Silencing Hum",
    3056: "Hex of Silence", 3062: "Drowsy Roar", 3069: "Hook Sweep",
    3508: "Pumpkin Blast", 3509: "Gourd Bomb", 3510: "Jack's Inferno", 3517: "Sandstorm",
    3556: "Firestorm", 3561: "Frost Breath", 3564: "Fire Breath",
    3569: "Overseer's Wrath", 3570: "Warden's Wrath", 3587: "Cannibal Feast",
    3591: "Predator's Howl", 3592: "Chef's Cleaver", 7024: "Searing Burst",
    # -- pulses of the stones, eyes and flames that guard places
    3074: "Flame Pulse", 3075: "Flame Pulse", 3076: "Flame Pulse",
    3077: "Mana Pulse", 3078: "Mana Pulse", 3079: "Mana Pulse",
    3080: "Third Eye Gaze", 3081: "Third Eye Gaze", 3082: "Third Eye Gaze",
    3083: "Third Eye Gaze", 3084: "Third Eye Gaze",
    3085: "Wizard Eye Gaze", 3086: "Wizard Eye Gaze", 3087: "Wizard Eye Gaze",
    3088: "Wizard Eye Gaze", 3089: "Wizard Eye Gaze", 3090: "Twilight Pulse",
    3093: "Dark Invocation", 3094: "Stone Gaze", 3095: "Abyssal Chant",
    3096: "Petrify", 3097: "Spirit Wail", 3098: "Dark Chant", 3099: "Frost Tumble",
}
DESC = {   # the Korean descriptions these rows carried, translated
    2971: "Restores the HP and MP of nearby allies.",
    2976: "Provokes a distant enemy into attacking the caster.",
    2986: "A strong jumping strike with a melee weapon that stuns the target.",
    2987: "A strong jumping strike with a melee weapon that stuns the target.",
    2988: "Strikes the target three times in a row with a melee weapon.",
    2991: "An endless spinning attack with a two-handed weapon that hits everything around.",
    2992: "An endless spinning attack with a two-handed weapon that hits everything around.",
    2997: "Fires an ice shard that hits the target and the characters around it.",
    2998: "Fires an ice shard that hits the target and the characters around it.",
    2999: "Fires a fire bolt that hits the target and the characters around it.",
    3000: "Fires a fire bolt that hits the target and the characters around it.",
}


def key(row):
    return PREFIX + str(row).encode()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    raw = open(STB, "rb").read()
    stb = oro.Stb(STB)
    if stb.to_bytes() != raw:
        print("ABORT: the STB writer does not round-trip LIST_SKILL.STB byte-for-byte")
        return 1
    t = stl.Stl(STL)
    keys, langs = stl.read_table(STL)
    man = json.load(open(MANIFEST, encoding="utf-8")) if os.path.isfile(MANIFEST) else None

    if args.restore:
        if not man:
            print("nothing to restore")
            return 0
        for r, old in man["col86"].items():
            if stb.get(int(r), KEY_COL).strip() == key(int(r)):
                stb.set(int(r), KEY_COL, old.encode("latin-1"))
        keep = [i for i, k in enumerate(keys) if not k[0].startswith(PREFIX)]
        stl.write_table(STL, [keys[i] for i in keep],
                        [[lang[i] for i in keep] for lang in langs], False, fmt=t.fmt)
        open(STB, "wb").write(stb.to_bytes())
        os.remove(MANIFEST)
        print(f"restored {len(man['col86'])} col-86 cells; name table back to {len(keep)} entries")
        return 0

    have = {k[0]: i for i, k in enumerate(keys)}
    if args.verify:
        bad = [r for r, n in NAMES.items()
               if stb.get(r, KEY_COL).strip() != key(r) or key(r) not in have
               or langs[1][have[key(r)]][0] != n.encode()]
        print("ALL CHECKS PASSED" if not bad else f"VERIFY FAILED: {len(bad)} row(s): {bad[:10]}")
        return 1 if bad else 0
    if man:
        print("already applied; --restore first to redo")
        return 0
    taken = {k for k in have if not k.startswith(PREFIX)}
    for r in NAMES:
        if r >= stb.rows or not stb.occupied(r):
            print(f"ABORT: LIST_SKILL {r} is not a skill row")
            return 1
        if stb.get(r, KEY_COL).strip() in taken:
            print(f"ABORT: LIST_SKILL {r} already has a name-table key {stb.get(r, KEY_COL)!r}")
            return 1
    print(f"{len(NAMES)} monster skills get a player-facing name, for example:")
    for r in (3042, 2911, 2981, 3021, 3561, 3595, 3599, 2971):
        print(f"   {r:>5}  {NAMES[r]:<20} {DESC.get(r, '')}")
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    col86 = {}
    for r, n in NAMES.items():
        col86[str(r)] = stb.get(r, KEY_COL).strip().decode("latin-1")
        stb.set(r, KEY_COL, key(r))
        keys.append((key(r), r))
        entry = (n.encode(), DESC.get(r, "").encode())
        for lang in langs:
            lang.append(entry)
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    json.dump({"col86": col86}, open(MANIFEST, "w", encoding="utf-8"), indent=1)
    open(STB, "wb").write(stb.to_bytes())
    stl.write_table(STL, keys, langs, False, fmt=t.fmt)
    print(f"wrote {len(NAMES)} keys and name-table entries (undo: --restore)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
