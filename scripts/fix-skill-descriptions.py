"""Rewrite broken-English skill descriptions in LIST_SKILL_S.STL.

The English tooltip text is language block 1 of `data/3DDATA/STB/LIST_SKILL_S.STL`,
keyed by the STB's last column (`LSkill0921` for every rank of Healing). Only the
English block is touched; the other four languages are left as they are.

Why each rewrite (alpha test #2, 2026-09-22):

  * Healing (921-930, type 10, target filter 1 = party, radius 1300-2000): the server
    (`CObjCHAR::Skill_START_10_11`) heals the caster and every party member inside
    the radius. The old text -- "You can recover your and your party member's HP at
    the same time" -- was read as unclear/broken by testers.
  * Cure (931-940, type 11, target filter 3 = any friendly, range 2000): a single
    target heal, usable on yourself.

Rewritten in batches by class family from 2026-09-28; each batch was reviewed
old text -> new text before it went into `TEXT`, and the batch comments record
where the old text was factually wrong.

Idempotent: re-running finds the text already in place and writes nothing.
`--dry-run` prints the plan, `--verify` fails if any entry differs from the table
(and on any text breaking the house style: non-ASCII, over MAX_LEN, stray
spaces). The sidecar `data/3DDATA/STB/skill-descriptions.json` records the
English text each key held before this script replaced it, so `--restore` puts
back exactly those fields and nothing else -- `fix-skill-book-names.py` writes
skill *names* into the same STL, and a whole-file restore would undo it (the
whole-file backup hazard). A key this script wrote before that now holds some
third text has been changed by something else; the write refuses until
`--allow-changed`. Re-bake the VFS afterwards; the server never reads it.

`--dump` (read-only) writes `build/skill-descriptions/review.md`: one entry per
STL key -- every rank of a skill line shares one key, so one description has to
be true for all of them -- with what the STB says the line actually does
(type, target, area, statuses, stat changes, cost, weapons, class), the text of
any skill book that teaches it, and the current description. The rewrite is
written against that, not against the old sentence, which is sometimes wrong
(Leap Attack says "confused" and applies Fainted; Berserk's text and its book
disagree on which stat goes up). Flags:

  * `VARIES` -- a mechanic differs between ranks (a status only from rank N, a
    second stat, single target -> area): the text must hold for every rank.
  * `SHARED` -- one key serves more than one skill line.
  * `SPLIT` -- one line changes key part-way (Double Attack: rank 1 on
    LSkill0321, ranks 2-10 on LSkill0322), so the line has two descriptions and
    both need the rewrite. Reachability is judged per line for this reason.
  * `NO-PATH` -- no rank of the line has a class gate or a book, and it is not a
    basic skill: monster-only, quest-granted or unused. Listed apart; not
    rewritten unless a player can see it.

Rows whose col 86 is not an STL key at all (Korean text, or an attribute number
-- io_skill.h also calls col 86 SKILL_ATTRIBUTE) are counted, not listed: the
client finds no string for them, so there is no description to fix.
"""
import argparse
import collections
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
STL = os.path.join(STB_DIR, "LIST_SKILL_S.STL")
BACKUP_DIR = os.path.join(ROOT, "build", "skill-descriptions")
REVIEW = os.path.join(BACKUP_DIR, "review.md")
ENGLISH = 1
SIDECAR = os.path.join(STB_DIR, "skill-descriptions.json")
MAX_LEN = 240           # the classic tooltip wraps, but a long text makes it tall

# key -> English description. One key covers every rank listed in the review
# sheet, so each text must be true at all of them. House style (2026-09-28):
# second person; the image of the action first, then exactly what it does; no
# numbers (power, duration, cost and range are per rank and the tooltip prints
# them); plain ASCII (the text is ANSI and UI2 converts it); stat names as the
# UI spells them. "Costs HP instead of MP" stays on HP-cost skills because the
# tooltip labels the cost "Required Mana".
TEXT = {
    # alpha test #2 (2026-09-22), restyled with batch 2. Healing heals the
    # caster and every party member in its radius (CObjCHAR::Skill_START_10_11);
    # Cure is single target and can be cast on yourself.
    "LSkill0921": "A wash of healing light that closes wounds, restoring the HP of "
                  "you and every party member near you.",
    "LSkill0931": "Lay hands on an ally and knit their wounds shut, restoring HP. "
                  "Can be cast on yourself.",

    # --- batch 1: Soldier / Knight / Champion (approved 2026-09-28) ----------
    # Factual corrections against LIST_SKILL.STB: Leap Attack applies Fainted,
    # not confusion; Berserk raises Attack Power and lowers Defense (the old text
    # had it reversed); Divine Force/Lightning lower Magic Resistance, and
    # Lightning's area is around the target; Spin/Twist Attack take any melee
    # weapon; Blood Attack is single target; Triple Attack's Dodge debuff exists
    # only at ranks 19-20.
    "LSkill0201": "Years of drilling with blade and haft. Increases Attack Power "
                  "with one-handed and two-handed melee weapons.",
    "LSkill0231": "Brutal conditioning that toughens you to take a beating. "
                  "Increases Max HP.",
    "LSkill0251": "You learn to take blows on the plate instead of the flesh. "
                  "Increases Defense.",
    "LSkill0271": "Practice until every bolt bites deeper. Increases Attack Power "
                  "with crossbows.",
    "LSkill0281": "Light, practiced footwork. Increases Movement Speed.",
    "LSkill0291": "Hard mental discipline that deepens your reserves. Increases Max MP.",
    "LSkill0301": "Put your full weight behind a single melee blow and smash it "
                  "into the target.",
    "LSkill0321": "Strike the target twice in quick succession with your melee weapon.",
    "LSkill0322": "Strike the target twice in quick succession with your melee weapon.",
    "LSkill0331": "Strike the target three times in quick succession with your "
                  "melee weapon.",
    "LSkill0332": "Three savage strikes in quick succession. At its highest ranks "
                  "the onslaught can also throw the target off balance, lowering "
                  "its Dodge.",
    "LSkill0341": "Leap at the target and bring your weapon crashing down on its "
                  "skull, with a chance to leave it stunned. Costs HP instead of MP.",
    "LSkill0361": "Give in to bloodlust. Your Attack Power rises sharply, but you "
                  "stop guarding yourself and your Defense drops.",
    "LSkill0381": "Raise your shield and brace your comrades against sorcery. "
                  "Increases the Magic Resistance of you and party members near you.",
    "LSkill0391": "Hurl a sphere of holy force at a distant enemy, with a chance to "
                  "weaken its Magic Resistance.",
    "LSkill0396": "Call down holy lightning on a distant enemy and everything "
                  "standing near it, with a chance to weaken their Magic Resistance.",
    "LSkill0401": "Whirl your weapon in a full circle and cut into every enemy "
                  "around you.",
    "LSkill0406": "A wider, more violent spin that tears through every enemy "
                  "around you.",
    "LSkill0411": "Roar a challenge at a monster, forcing it to turn on you and "
                  "attack only you for a while.",
    "LSkill0421": "Dig in behind your shield. Your Defense rises greatly, but you "
                  "move slowly while it lasts.",
    "LSkill0471": "Put a bolt into the target and goad it into coming after you.",
    "LSkill0481": "Draw the crossbow to its limit and loose a bolt that hits with "
                  "brutal force.",
    "LSkill0651": "Carve into the target and drain part of its lifeblood into your "
                  "own HP.",
    "LSkill0211": "Sword-and-shield discipline. Increases Attack Power with "
                  "one-handed swords and blunt weapons.",
    "LSkill0491": "A crippling bolt that damages the target, with a chance to slow "
                  "its movement.",
    "LSkill0501": "A carefully aimed bolt that strikes hard from farther away than "
                  "a normal shot.",
    "LSkill0511": "Drive your weapon into the ground and send a shockwave ripping "
                  "through every enemy around you.",
    "LSkill0521": "Batter the target with lightning-charged blows, with a chance to "
                  "leave it stunned.",
    "LSkill0531": "Spill your own blood to unleash holy wrath on every enemy around "
                  "you. Costs HP instead of MP.",
    "LSkill0221": "The weight of a great weapon becomes part of your arms. Increases "
                  "Attack Power with two-handed swords, spears and two-handed axes.",
    "LSkill0661": "Cut the air hard enough to send a blade of spirit into a distant "
                  "enemy.",
    "LSkill0671": "Leap high and bring your weapon down through the top of the "
                  "enemy's skull. Costs HP as well as MP.",

    # --- batch 2: Muse / Magician / Cleric (approved 2026-09-28) -------------
    # Factual corrections: Lightning applies Sleep (#31), not a stun; Freezing
    # Bolt is single target and slows movement only; Curse also poisons; Luna
    # Stone and Ice Bang are type 7, an area around the *target*. The summons
    # were read from their AI (sur_*.aip): the Butterfly fights and casts 2976
    # (Accuracy down), it confuses nothing; the Bonfire never attacks and casts
    # type-10 heals of HP and MP on every PC (filter 7) within 10 m; the Phantom
    # Sword is melee (reach 2.5 m); the Firegon attacks from 13 m and casts 3032
    # (area, Flame Heat); the Elemental has the biggest HP/ATK of the five.
    # Mild names its rank threshold ("From rank 6") on purpose.
    "LSkill0801": "Attune yourself to the focus in your hand. Increases Attack "
                  "Power with magic staffs and wands.",
    "LSkill0821": "Still the mind and let mana pool deeper. Increases Max MP and "
                  "MP recovery.",
    "LSkill0841": "Shape your spells with less waste. Reduces the MP cost of your "
                  "skills.",
    "LSkill0861": "Bind more of the summoned to your will. Increases your summon "
                  "gauge, so you can keep more summons at once.",
    "LSkill0901": "Loose a bolt of raw mana at a distant enemy.",
    "LSkill0906": "Drive a lance of mana into a distant enemy, with a chance to "
                  "weaken both its Defense and Magic Resistance.",
    "LSkill0911": "Wrap the target in a ring of fire that eats through its guard, "
                  "with a chance to lower its Defense.",
    "LSkill0941": "Crack the target across the skull with your staff, with a chance "
                  "to leave it stunned.",
    "LSkill0951": "Strike a distant enemy with a bolt of lightning, with a chance to "
                  "put it to sleep.",
    "LSkill0956": "A savage bolt that can leave the target stunned. At its highest "
                  "rank the blast also catches enemies close to the target.",
    "LSkill0961": "Quicken an ally's stride, increasing their Movement Speed.",
    "LSkill0971": "A blessing that hardens the flesh, increasing the Max HP of you "
                  "and party members near you.",
    "LSkill0981": "Hurl a jagged shard of ice at a distant enemy.",
    "LSkill0986": "A heavier shard of ice that damages the target, with a chance to "
                  "slow its movement.",
    "LSkill0991": "Sanctify your armor, greatly increasing your Defense for a short "
                  "time.",
    "LSkill1021": "Pour strength into an ally, increasing their Attack Power.",
    "LSkill1031": "Sap the target's strength, with a chance to lower its Attack "
                  "Power. From rank 6 it can also slow its Attack Speed.",
    "LSkill1051": "A blessing that deepens the mind, increasing the Max MP of you "
                  "and party members near you.",
    "LSkill1081": "Call up a storm of cutting wind that lashes every enemy around "
                  "you.",
    "LSkill1086": "Unleash a howling tornado that tears at every enemy in a wide "
                  "circle around you.",
    "LSkill1141": "Lay a curse on a distant enemy that rots it from within, with a "
                  "chance to poison it.",
    "LSkill1151": "Summon a swarm of enchanted butterflies that fights at your side "
                  "and blurs your enemies' aim, lowering their Accuracy.",
    "LSkill1161": "Kindle a magic bonfire. Everyone who stands near its warmth "
                  "recovers HP and MP.",
    "LSkill1171": "Conjure a spectral blade that hunts down your enemies and cuts "
                  "them apart at close range.",
    "LSkill1221": "Steady an ally's hand, increasing their Accuracy.",
    "LSkill1001": "Chill the target's blood, with a chance to slow its movement.",
    "LSkill1061": "Hurl a searing bolt of fire at the target, with a chance to "
                  "weaken its Defense.",
    "LSkill1066": "Bury the target in molten rock that splashes onto every enemy "
                  "near it, with a chance to weaken their Defense.",
    "LSkill1071": "Seal the target's throat, with a chance to stop it casting magic "
                  "for a while.",
    "LSkill1091": "Call a stone down from the moon to crush the target and every "
                  "enemy around it.",
    "LSkill1101": "Shatter a burst of ice around the target, damaging every enemy "
                  "near it with a chance to slow them.",
    "LSkill1011": "Surround your party with a warding aura, increasing the Defense "
                  "of you and party members near you.",
    "LSkill1041": "Burn away the foul magic clinging to an ally, with a chance to "
                  "remove every harmful effect on them.",
    "LSkill1131": "Drag a fallen ally back from death, reviving them where they fell.",
    "LSkill1132": "Drag a fallen ally back from death, reviving them where they fell "
                  "and restoring part of the experience they lost.",
    "LSkill1181": "Summon an elemental, the mightiest of your servants, to fight at "
                  "your side.",
    "LSkill1191": "Summon a fire dragon that bombards your enemies from range, its "
                  "fireballs setting them alight.",
    "LSkill1201": "Drive an ally into a frenzy, increasing their Attack Speed.",
    "LSkill1211": "Bless an ally's weapon so their attacks and skills deal "
                  "additional damage.",
    "LSkill1231": "Guide an ally's strikes to the weak spots, increasing their "
                  "Critical.",
    "LSkill1241": "Cloak your party against sorcery, increasing the Magic Resistance "
                  "of you and party members near you.",
    "LSkill1251": "Make your party hard to pin down, increasing the Dodge of you and "
                  "party members near you.",

    # --- batch 3: Hawker / Raider / Scout (approved 2026-09-28) --------------
    # Factual corrections: Battle Mastery raises Attack Speed only (bows too),
    # never Attack Power; Spirit Heart slows Attack Speed, not movement; Vanish
    # is a Sleep (#31), area from rank 6, monsters only -- nothing runs away
    # (rebalance-hawker-skills.py keeps it on purpose); Red Cloud also damages.
    # Rest Mastery's bonus applies standing too, at a sixth of the sitting rate
    # (CObjAI::Get_RecoverHP), hence "most of all while you sit". The hawk
    # (npc 890, sur_flamehawk.aip) is melee and casts damage skill 3034.
    "LSkill1401": "Endless practice at the target range. Increases Attack Power "
                  "with bows.",
    "LSkill1421": "Close-quarters training with blade and claw. Increases Attack "
                  "Power with katars and dual swords.",
    "LSkill1441": "Your wounds close faster, most of all while you sit and rest. "
                  "Increases HP recovery; from rank 6, MP recovery as well.",
    "LSkill1451": "Sharpen your hunter's instincts, increasing your Accuracy.",
    "LSkill1461": "Relentless drilling in the rhythm of the fight. Increases Attack "
                  "Speed with bows, katars and dual swords.",
    "LSkill1481": "Hold your breath, draw deep and drive an arrow into the target "
                  "with great force.",
    "LSkill1501": "Drive your katar or blades into the target with everything you "
                  "have.",
    "LSkill1521": "Loose two arrows at the target in quick succession.",
    "LSkill1531": "Loose three arrows at the target in quick succession. At its "
                  "highest ranks the volley can also cripple the target, slowing "
                  "its movement.",
    "LSkill1541": "Strike the target twice in quick succession with your katar or "
                  "dual swords.",
    "LSkill1551": "Strike the target three times in quick succession. At its "
                  "highest ranks the flurry can also leave the target stunned.",
    "LSkill1561": "Break into a flat-out run, increasing your Movement Speed.",
    "LSkill1571": "Spin on the spot and lash out with your heels, kicking every "
                  "enemy around you.",
    "LSkill1581": "An arrow that pins the target's legs, with a chance to slow its "
                  "movement. From rank 6 it can also slow its Attack Speed.",
    "LSkill1591": "Spin into the target blades first, with a chance to tear open "
                  "its guard and lower its Defense.",
    "LSkill1601": "Fire a heart-shaped sphere of spirit at a distant enemy, with a "
                  "chance to slow its Attack Speed.",
    "LSkill1641": "Send a blazing hawk of spirit fire streaking into a distant enemy.",
    "LSkill1681": "Fall into a rapid firing rhythm, increasing your Attack Speed "
                  "with bows and crossbows.",
    "LSkill1721": "Throw a stupefying dust over a monster, with a chance to put it "
                  "to sleep. From rank 6 it catches every monster around the "
                  "target as well.",
    "LSkill1511": "The Raider's answer to Power Attack: a single katar strike of "
                  "devastating force.",
    "LSkill1611": "Close in and bury both blades in the target for massive damage.",
    "LSkill1831": "Bleed yourself to feed your magic, turning your own HP into MP.",
    "LSkill1841": "Melt into the shadows and move unseen. Attacking breaks your "
                  "concealment.",
    "LSkill1851": "Throw a poisoned blade at a distant enemy, with a chance to "
                  "poison it. From rank 6 the poison is stronger.",
    "LSkill1861": "Kick up a choking red cloud that damages every enemy around you, "
                  "with a chance to lower their Dodge.",
    "LSkill1871": "Throw an enchanted blade at a distant enemy, with a chance to "
                  "weaken its Magic Resistance.",
    "LSkill1621": "An envenomed arrow, with a chance to poison the target.",
    "LSkill1671": "Sweep the area with a hunter's eye, revealing hidden enemies "
                  "around you.",
    "LSkill1691": "Hone your arrowheads to a killing edge, increasing your Attack "
                  "Power.",
    "LSkill1701": "A long, arcing shot that strikes an enemy far beyond normal bow "
                  "range.",
    "LSkill1706": "A shot loosed from extreme range that can leave the target "
                  "stunned.",
    "LSkill1711": "Summon a trained war hawk that dives at your enemies and tears "
                  "into them.",

    # --- batch 4: Dealer / Bourgeois / Artisan (approved 2026-09-28) ---------
    # Factual corrections: Economy Research gives +2 Charm at rank 1 only (150
    # at rank 20); Weapon Research raises Concentration (not "dexterity") and
    # Critical from rank 11; Sniping Shot is plain high power, not a critical;
    # Union Weapon raises Attack Speed, not accuracy. Craft Mastery adds gun
    # Attack Speed from rank 6. Prerequisites (cols 39-44): Weapon Research ->
    # the six weapon crafts, Armor Research -> the three armor crafts. The
    # mercenaries (sur_empl_a/_r, sur_guard): Warrior melee 2.8 m, Hunter
    # ranged 20 m, Terror Knight the strongest, with area skill 3031.
    # Calibrated Burst (LSkill7002) keeps the text import-artisan-skill.py wrote.
    "LSkill2001": "A merchant's education in how money moves. Increases Charm.",
    "LSkill2021": "Long hours on the firing line. Increases Attack Power with guns "
                  "and launchers.",
    "LSkill2041": "Pack like a caravan master. Increases how much you can carry.",
    "LSkill2051": "Haggle like you mean it. NPC shops sell to you for less.",
    "LSkill2081": "The groundwork of every craft. Increases Max MP; from rank 6, "
                  "Attack Speed with guns as well.",
    "LSkill2091": "A scavenger's eye for anything worth taking. Monsters you kill "
                  "drop items more often.",
    "LSkill2101": "Keep more hired blades on the payroll. Increases your summon "
                  "gauge, so you can keep more summons at once.",
    "LSkill2111": "Study how weapons are made and where they break. Increases "
                  "Concentration; from rank 11, Critical as well. Required for the "
                  "weapon crafts.",
    "LSkill2131": "Study how armor is built and how it fails. Increases Defense. "
                  "Required for the armor crafts.",
    "LSkill2141": "Faster hands on the trigger. Increases Attack Speed with guns and "
                  "launchers.",
    "LSkill2201": "Pack a heavy charge into your gun and blast the target with it.",
    "LSkill2211": "Take your time, pick your spot, and put a single devastating "
                  "round into the target.",
    "LSkill2221": "Fire two rounds into the target in quick succession.",
    "LSkill2222": "Fire two rounds into the target in quick succession.",
    "LSkill2231": "Fire three rounds into the target in quick succession. At its "
                  "highest ranks the volley can also cripple the target, slowing its "
                  "movement.",
    "LSkill2241": "Push your weapon past its limits, increasing your Attack Speed.",
    "LSkill2261": "Steady your aim and hit a target from much farther away than a "
                  "normal shot.",
    "LSkill2271": "Fire a venom-laced round that bursts on impact, damaging the "
                  "target and enemies near it with a chance to poison them. The "
                  "poison grows stronger at rank 6 and again at rank 10.",
    "LSkill2281": "Club an adjacent enemy with the butt of your gun. At rank 10 the "
                  "blow can also leave it stunned.",
    "LSkill2311": "Flick a hardened coin into a distant enemy. Costs Zuly as well "
                  "as MP.",
    "LSkill2351": "Hire a mercenary warrior to fight at your side at close range. "
                  "Costs Zuly as well as MP.",
    "LSkill2401": "Craft everyday goods from natural materials.",
    "LSkill2411": "Craft shields, bags, back equipment and wings.",
    "LSkill2431": "Forge one-handed swords, two-handed swords and dual swords.",
    "LSkill2451": "Forge one-handed blunt weapons, axes, spears and katars.",
    "LSkill2471": "Craft bows and crossbows.",
    "LSkill2491": "Craft magic staffs and wands.",
    "LSkill2511": "Build guns and launchers.",
    "LSkill2531": "Sew clothes, hats, gloves, shoes and face accessories.",
    "LSkill2551": "Forge armor, helmets, gauntlets and boots.",
    "LSkill2571": "Craft magic robes, hats, gloves and boots.",
    "LSkill2621": "Pry the gems out of an item, or break it down into materials you "
                  "can craft with.",
    "LSkill2071": "Talk up everything you sell. NPC shops pay you more.",
    "LSkill2301": "Hurl a storm of coins that tears into every enemy around you. "
                  "Costs Zuly as well as MP.",
    "LSkill2361": "Hire a mercenary hunter who shoots your enemies from long range. "
                  "Costs Zuly as well as MP.",
    "LSkill2371": "Buy the service of a Terror Knight, the deadliest mercenary money "
                  "can hire, whose sweeping blows cut down every enemy around it. "
                  "Costs Zuly as well as MP.",
    "LSkill2591": "Build the parts that tune a cart.",
    "LSkill2601": "Build the parts that tune a castle gear.",
    "LSkill2611": "Cut a rough, low-grade gem into a finer one.",
    "LSkill2631": "Refine a piece of equipment, raising its quality by one grade "
                  "when it succeeds.",
    "LSkill2641": "Craft rings, earrings and necklaces.",

    # --- batch 5: basic skills and emotes (approved 2026-09-28) --------------
    # Read from the client's command handlers (CBasicCommand::Execute): Auto
    # Target's case is empty, so the command does nothing; Pick Up takes the
    # nearest item you may loot within 10 m; Ride Request is the driver
    # inviting a player within 5 m. Throwing 100 Zuly costs and gives 50 --
    # renamed "Toss 50 Zuly" in fix-skill-book-names.py rather than changing
    # the economy. Rows 44/45 (Hand Clapping, Chuckle) are small type-11 heals
    # under emote names, copies of the two kisses; row 50 is the real emote.
    "LSkill0011": "Sit down and rest. You recover HP and MP much faster while "
                  "seated.",
    "LSkill0012": "Pick up the nearest item on the ground that you are allowed to "
                  "loot.",
    "LSkill0013": "Leap into the air.",
    "LSkill0014": "Beat your wings and leap high into the air. Requires wings.",
    "LSkill0015": "Not in use: this command does nothing.",
    "LSkill0016": "Attack your current target with your equipped weapon.",
    "LSkill0017": "Climb into your cart and take the controls. Use it again to "
                  "climb out.",
    "LSkill0018": "Ask another player to become your friend.",
    "LSkill0019": "Invite another player to join your party.",
    "LSkill0020": "Offer another player a one-on-one trade.",
    "LSkill0021": "Set up a personal shop to buy or sell goods.",
    "LSkill0025": "Invite a player standing next to you to ride as a passenger in "
                  "your cart.",
    "LSkill0030": "Plant a kiss on a friendly character, restoring a little of "
                  "their HP.",
    "LSkill0031": "Plant a kiss on a friendly character, restoring a little of "
                  "their MP.",
    "LSkill0032": "Work the knots out of a friendly character's shoulders, "
                  "restoring some of their HP.",
    "LSkill0033": "Toss 10 Zuly to another character.",
    "LSkill0034": "Toss 50 Zuly to another character.",
    "LSkill0041": "Wave and say hello.",
    "LSkill0042": "Bow politely to say goodbye.",
    "LSkill0043": "Bow to show your thanks.",
    "LSkill0044": "Clap a character on the back, restoring a little of their HP.",
    "LSkill0045": "Share a laugh with a character, restoring a little of their MP.",
    "LSkill0046": "Pump your fist and cheer your companions on.",
    "LSkill0047": "Clasp your hands and beg shamelessly.",
    "LSkill0048": "Stamp your feet in a fit of anger.",
    "LSkill0049": "Fly into a rage.",
    "LSkill0050": "Clap your hands in applause.",
}


def check_style():
    bad = []
    for key, text in TEXT.items():
        if not all(32 <= ord(c) < 127 for c in text):
            bad.append("%s: not plain ASCII" % key)
        if len(text) > MAX_LEN:
            bad.append("%s: %d characters (max %d)" % (key, len(text), MAX_LEN))
        if text != text.strip() or "  " in text:
            bad.append("%s: stray whitespace" % key)
    return bad


def load_sidecar(stl_read, current):
    """key -> {"old": text this script replaced, "wrote": text it last wrote}.

    Both are needed to tell a revision of our own table (Healing's alpha text
    restyled in batch 2) from a change made by something else: only a text that
    is neither is foreign.

    The first version of this script kept a whole-file copy of the STL in
    build/skill-descriptions/ instead. Restoring that copy would also revert
    everything written to the file since -- fix-skill-book-names.py renames
    skills in the same STL -- so its Healing/Cure texts are carried into the
    per-field sidecar and the copy is kept for forensics only. Entries from the
    first sidecar format (a bare string) are read the same way, with "wrote"
    taken as the text the file holds now.
    """
    side = {}
    if os.path.isfile(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            side = json.load(fh)
    for key, v in side.items():
        if isinstance(v, str):
            side[key] = {"old": v, "wrote": current(key).decode("latin-1")}
    legacy = os.path.join(BACKUP_DIR, "LIST_SKILL_S.STL")
    if os.path.isfile(legacy):
        lkeys, llangs = stl_read(legacy)
        lindex = {k.decode("latin-1"): i for i, (k, _) in enumerate(lkeys)}
        for key in TEXT:
            if key in lindex and key not in side:
                old = llangs[ENGLISH][lindex[key]][1]
                if old != current(key):
                    side[key] = {"old": old.decode("latin-1"),
                                 "wrote": current(key).decode("latin-1")}
    return side


def load_stl_codec():
    spec = importlib.util.spec_from_file_location("azn", os.path.join(HERE, "add-zone-name.py"))
    mod = importlib.util.module_from_spec(spec)
    saved = sys.argv
    sys.argv = [saved[0]]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod.stl_read, mod.stl_write


# ------------------------------------------------------------------ review sheet

# LIST_SKILL columns (io_skill.h); game index = get_int32 column
C_LINE, C_RANK, C_TAB, C_TYPE = 1, 2, 4, 5
C_RANGE, C_FILTER, C_SCOPE, C_POWER = 6, 7, 8, 9
C_STATUS = (11, 12)
C_SUCCESS, C_DURATION, C_DMGTYPE = 13, 14, 15
C_COST = ((16, 17), (18, 19))
C_RELOAD = 20
C_ABIL = ((21, 22, 23), (24, 25, 26))   # ability, flat, rate %
C_SUMMON = 28
C_WEAPONS = range(30, 35)
C_CLASS = 35
C_KEY = 86
TAB = {0: "basic", 1: "active", 2: "passive", 3: "clan"}
# LIST_STATUS: STATE_APPLY_ING_STB / value (5,6) (7,8), string key 20
ST_ABIL, ST_KEY = ((5, 6), (7, 8)), 20
CLASS_KEY = 11                          # LIST_CLASS: CLASS_STRING_ID


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


def english(rd, name, nfield=0):
    """key -> field of the English block (block 0 when the file has only one)."""
    stl = rd.Stl(os.path.join(STB_DIR, name), "cp1252")
    block = ENGLISH if stl.lang_off is None or len(stl.lang_off) > ENGLISH else 0
    return {k: v[nfield] for k, v in stl.by_key(block).items()}


def survey(rd):
    sk = rd.Stb(os.path.join(STB_DIR, "LIST_SKILL.STB"))
    desc = {k: v for k, v in rd.Stl(STL, "cp1252").by_key(ENGLISH).items()}
    abil = english(rd, "STR_ABILITY.STL")
    stype = english(rd, "STR_SKILLTYPE.STL")
    starget = english(rd, "STR_SKILLTARGET.STL")
    itype = english(rd, "STR_ITEMTYPE.STL")
    try:                                               # STR_SKILLPOWER_EFFECT_n = 80 + n
        strings = english(rd, "LIST_STRING_S.STL")
    except (IndexError, ValueError):                   # not a layout rose-data-reader knows
        strings = {}

    cls = rd.Stb(os.path.join(STB_DIR, "LIST_CLASS.STB"))
    cls_names = english(rd, "LIST_CLASS_S.STL")
    st = rd.Stb(os.path.join(STB_DIR, "LIST_STATUS.STB"))
    st_names = english(rd, "LIST_STATUS_S.STL")
    npc = rd.Stb(os.path.join(STB_DIR, "LIST_NPC.STB"))
    npc_names = english(rd, "LIST_NPC_S.STL")
    npc_kc = npc.key_column()

    def a_name(a):
        return abil.get(str(a), "ability %d" % a)

    def class_name(c):
        if not c:
            return "(no class)"
        if c >= cls.rows:
            return "class %d (out of range)" % c
        return cls_names.get(cls.s(c, CLASS_KEY).strip(), "class %d" % c)

    def status_name(s):
        if s <= 0 or s >= st.rows:
            return "status %d" % s
        n = st_names.get(st.s(s, ST_KEY).strip(), "") or "(nameless)"
        mods = ["%s %+d" % (a_name(st.i(s, a)), st.i(s, v))
                for a, v in ST_ABIL if st.i(s, a)]
        return "%s #%d%s" % (n, s, " [%s]" % ", ".join(mods) if mods else "")

    def npc_name(n):
        if n <= 0 or n >= npc.rows or npc_kc is None:
            return "npc %d" % n
        return "%s (npc %d)" % (npc_names.get(npc.s(n, npc_kc).strip(), "?"), n)

    # books: LIST_USEITEM class 314, col 20 = the exact LIST_SKILL row taught
    bk = load("fix-skill-book-names")
    pr = load("prune-dead-skill-books")
    use = rd.Stb(os.path.join(STB_DIR, "LIST_USEITEM.STB"))
    book_stl = rd.Stl(os.path.join(STB_DIR, "LIST_USEITEM_S.STL"), "cp1252").by_key(ENGLISH)
    sellers = bk.sold_by(rd, pr)
    books = collections.defaultdict(list)            # skill row -> [book]
    for no in range(use.rows):
        if use.i(no, bk.USE_COL_CLASS) != bk.USE_ITEM_SKILL_LEARN:
            continue
        e = book_stl.get(use.s(no, use.cols - 1).strip())
        books[use.i(no, bk.USE_COL_SKILL)].append(
            {"row": no, "name": e[0] if e else "?", "desc": e[1] if e else "",
             "sold": sellers.get(no, [])})

    # Reachability belongs to the skill *line* (col 1), not the key: several lines
    # switch key part-way (Double Attack rank 1 is LSkill0321, ranks 2-10 are
    # LSkill0322), and the continuation key is reached by ranking up, not by a
    # book or a class gate of its own.
    line_gates = collections.defaultdict(list)       # line -> [(rank, class)]
    line_books = collections.defaultdict(list)
    line_keys = collections.defaultdict(collections.OrderedDict)
    groups = collections.OrderedDict()
    not_keys = []                                     # col 86 holds text / an attribute
    for r in range(sk.rows):
        line = sk.i(r, C_LINE)
        if sk.i(r, C_CLASS):
            line_gates[line].append((sk.i(r, C_RANK), sk.i(r, C_CLASS)))
        line_books[line].extend(books.get(r, []))
        key = sk.s(r, C_KEY).strip()
        if not key:
            continue
        if not re.fullmatch(r"[A-Za-z]{3,8}\d{3,5}", key):
            not_keys.append(r)
            continue
        groups.setdefault(key, []).append(r)
        line_keys[line].setdefault(key, []).append(sk.i(r, C_RANK))

    out = []
    for key, rows in groups.items():
        first, last = rows[0], rows[-1]

        def per_rank(fn):
            vals = [fn(r) for r in rows]
            return vals[0] if len(set(map(repr, vals))) == 1 else vals

        def mech(r):
            return {
                "type": sk.i(r, C_TYPE),
                "target": sk.i(r, C_FILTER),
                "area": sk.i(r, C_SCOPE) > 0,
                "status": tuple(sorted(s for s in (sk.i(r, c) for c in C_STATUS) if s)),
                "abilities": tuple(sk.i(r, a) for a, _, _ in C_ABIL if sk.i(r, a)),
                "rate": tuple(bool(sk.i(r, p)) for a, _, p in C_ABIL if sk.i(r, a)),
                "dmg": sk.i(r, C_DMGTYPE),
                "cost": tuple(sk.i(r, a) for a, _ in C_COST if sk.i(r, a)),
                "weapons": tuple(sorted(w for w in (sk.i(r, c) for c in C_WEAPONS) if w)),
            }

        mechs = [mech(r) for r in rows]
        varies = sorted({f for m in mechs for f in m if m[f] != mechs[0][f]})
        m = mechs[-1]
        # the class gate is declared on the rank that starts a tier and left 0 on
        # the rest (rank 1 Soldier, rank 6 Knight): list the declarations
        lines = sorted({sk.i(r, C_LINE) for r in rows})
        gates = sorted({g for ln in lines for g in line_gates[ln]})
        taught = [b for r in rows for b in books.get(r, [])]
        reached = gates or any(line_books[ln] for ln in lines)
        entry = desc.get(key)
        rec = {
            "key": key, "rows": rows, "lines": lines,
            "name": entry[0] if entry else "(no STL entry)",
            "desc": entry[1] if entry and len(entry) > 1 else "",
            "ranks": "%d-%d" % (sk.i(first, C_RANK), sk.i(last, C_RANK)),
            "tab": TAB.get(sk.i(first, C_TAB), str(sk.i(first, C_TAB))),
            "type": "%d %s" % (m["type"], stype.get(str(m["type"]), "")),
            "target": starget.get(str(m["target"]), "filter %d" % m["target"]),
            "range": per_rank(lambda r: sk.i(r, C_RANGE) // 100),
            "scope": per_rank(lambda r: sk.i(r, C_SCOPE) // 100),
            "power": per_rank(lambda r: sk.i(r, C_POWER)),
            "success": per_rank(lambda r: sk.i(r, C_SUCCESS)),
            "duration": per_rank(lambda r: sk.i(r, C_DURATION)),
            "reload": per_rank(lambda r: sk.i(r, C_RELOAD)),
            "dmg": strings.get(str(80 + m["dmg"]), "type %d" % m["dmg"]),
            "status": [status_name(s) for s in m["status"]],
            "abilities": [
                "%s %s" % (a_name(sk.i(last, a)),
                           "%+d%%" % sk.i(last, p) if sk.i(last, p)
                           else "%+d" % sk.i(last, f))
                for a, f, p in C_ABIL if sk.i(last, a)],
            "cost": ["%s %d" % (a_name(sk.i(last, a)), sk.i(last, v))
                     for a, v in C_COST if sk.i(last, a)],
            "weapons": [itype.get(str(w), str(w)) for w in m["weapons"]],
            "class": class_name(gates[0][1]) if gates else class_name(0),
            "gates": ["%s from rank %d" % (class_name(c), rk) for rk, c in gates],
            "summon": npc_name(sk.i(last, C_SUMMON)) if sk.i(last, C_SUMMON) else "",
            "books": taught,
            "flags": [],
        }
        if varies:
            rec["flags"].append("VARIES(%s)" % ",".join(varies))
        if len(lines) > 1:
            rec["flags"].append("SHARED(lines %s)" % ", ".join(map(str, lines)))
        for ln in lines:
            if len(line_keys[ln]) > 1:
                rec["flags"].append("SPLIT(line %d: %s)" % (ln, ", ".join(
                    "%s ranks %d-%d" % (k, min(v), max(v))
                    for k, v in line_keys[ln].items())))
        if not reached and rec["tab"] != "basic":
            rec["flags"].append("NO-PATH")
        rec["varies_detail"] = {f: [mm[f] for mm in mechs] for f in varies}
        out.append(rec)
    return out, not_keys


def fmt_rank(v):
    if isinstance(v, list):
        return "%s -> %s" % (v[0], v[-1]) if len(set(v)) > 2 or v[0] == v[-1] else \
            " / ".join(map(str, v))
    return str(v)


def write_review(recs, not_keys):
    fams = collections.OrderedDict()
    nopath = []
    for r in recs:
        (nopath if "NO-PATH" in r["flags"] else fams.setdefault(r["class"], [])).append(r)

    L = ["# Skill descriptions -- review sheet", "",
         "Generated by `scripts/fix-skill-descriptions.py --dump`. One entry per",
         "LIST_SKILL_S.STL key; every rank listed shares the description. Numbers",
         "are the last rank's unless shown as a range. `current` is the English",
         "text today. `stats` gives the magnitude only: whether it is a gain or a",
         "loss comes from the status (Slow Run, Def Decreased ...). `class` lists",
         "each rank that declares a gate; ranks in between inherit it.", ""]
    total = sum(len(v) for v in fams.values())
    L.append("%d keys a player can reach, %d with no player path.  " % (total, len(nopath)))
    L.append("Flags: %d VARIES, %d SHARED, %d SPLIT.  " % tuple(
        sum(any(f.startswith(t) for f in r["flags"]) for r in recs)
        for t in ("VARIES", "SHARED", "SPLIT")))
    L.append("Left out: %d rows whose col 86 is not an STL key (Korean text or an "
             "attribute number; the client shows no name or text for them): rows %d-%d."
             % (len(not_keys), not_keys[0], not_keys[-1]) if not_keys else "")
    L.append("")

    def entry(r):
        L.append("### %s -- %s  (rows %d-%d, ranks %s)%s" % (
            r["key"], r["name"], r["rows"][0], r["rows"][-1], r["ranks"],
            "  **%s**" % " ".join(r["flags"]) if r["flags"] else ""))
        L.append("- %s, type %s, target %s, damage %s" % (
            r["tab"], r["type"], r["target"], r["dmg"]))
        geo = []
        if r["range"] not in (0, [0]):
            geo.append("range %s m" % fmt_rank(r["range"]))
        if r["scope"] not in (0, [0]):
            geo.append("area %s m" % fmt_rank(r["scope"]))
        for k in ("power", "success", "duration", "reload"):
            if r[k] not in (0, [0]):
                geo.append("%s %s" % (k, fmt_rank(r[k])))
        if geo:
            L.append("- " + ", ".join(geo))
        for label, k in (("class", "gates"), ("status", "status"), ("stats", "abilities"),
                         ("cost", "cost"), ("weapons", "weapons")):
            if r[k]:
                L.append("- %s: %s" % (label, "; ".join(r[k])))
        if r["summon"]:
            L.append("- summons: %s" % r["summon"])
        for f, vals in r["varies_detail"].items():
            L.append("- varies `%s` by rank: %s" % (f, vals))
        for b in r["books"]:
            L.append("- book %d \"%s\" (%s): %s" % (
                b["row"], b["name"], ", ".join(b["sold"]) or "not sold", b["desc"]))
        L.append("- current: \"%s\"" % r["desc"])
        L.append("")

    for fam, rs in fams.items():
        L.append("## %s (%d)" % (fam, len(rs)))
        L.append("")
        for r in rs:
            entry(r)
    L.append("## No player path (%d)" % len(nopath))
    L.append("")
    for r in nopath:
        entry(r)

    os.makedirs(BACKUP_DIR, exist_ok=True)
    with open(REVIEW, "w", encoding="utf-8") as fh:
        fh.write("\n".join(L))
    return total, len(nopath)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--allow-changed", action="store_true",
                    help="overwrite descriptions changed since this script wrote them")
    ap.add_argument("--dump", action="store_true",
                    help="write the read-only review sheet to %s" % REVIEW)
    args = ap.parse_args()

    if args.dump:
        total, nopath = write_review(*survey(load("rose-data-reader")))
        print("review sheet: %s (%d keys with a player path, %d without)"
              % (REVIEW, total, nopath))
        return

    bad = check_style()
    if bad:
        sys.exit("TEXT fails the house style:\n  " + "\n  ".join(bad))

    stl_read, stl_write = load_stl_codec()
    keys, langs = stl_read(STL)
    index = {k.decode("latin-1"): i for i, (k, _) in enumerate(keys)}
    missing = [k for k in TEXT if k not in index]
    if missing:
        sys.exit(f"keys not in STL: {missing}")
    def current(key):
        return langs[ENGLISH][index[key]][1]

    side = load_sidecar(stl_read, current)

    if args.restore:
        n = 0
        for key, rec in side.items():
            if key in index:
                name = langs[ENGLISH][index[key]][0]
                langs[ENGLISH][index[key]] = (name, rec["old"].encode("latin-1"))
                n += 1
        stl_write(STL, keys, langs)
        os.remove(SIDECAR)
        print(f"restored {n} description(s); sidecar removed")
        return

    changes, foreign = [], []
    for key, desc in TEXT.items():
        old = current(key)
        new = desc.encode("ascii")
        if old == new:
            continue
        # A key we wrote before that holds neither the text we last wrote nor
        # the text we replaced has been changed by something else since (an
        # import, another script). Do not overwrite it blind.
        if key in side and old.decode("latin-1") not in (side[key]["old"],
                                                         side[key]["wrote"]):
            foreign.append(key)
        changes.append((key, old, new))

    if args.verify:
        for key, old, new in changes:
            print(f"DIFF {key}: {old.decode('latin-1')!r} != {new.decode()!r}")
        print("verify:", "OK (%d descriptions)" % len(TEXT) if not changes
              else f"{len(changes)} entries differ")
        sys.exit(0 if not changes else 1)

    if not changes:
        print("nothing to do; all %d descriptions already in place" % len(TEXT))
        return
    for key, old, new in changes:
        name = langs[ENGLISH][index[key]][0].decode("latin-1")
        print(f"{key} ({name}):\n   - {old.decode('latin-1')}\n   + {new.decode()}")
    if foreign and not args.allow_changed:
        sys.exit("\n%d description(s) changed since this script last wrote them: %s\n"
                 "review, then re-run with --allow-changed" % (len(foreign), foreign))
    if args.dry_run:
        print("dry run; nothing written")
        return

    for key, old, new in changes:
        if key not in side or key in foreign:
            side[key] = {"old": old.decode("latin-1")}
        side[key]["wrote"] = new.decode("ascii")
        name = langs[ENGLISH][index[key]][0]
        langs[ENGLISH][index[key]] = (name, new)
    stl_write(STL, keys, langs)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(side, fh, indent=1, sort_keys=True)

    keys2, langs2 = stl_read(STL)
    for key, desc in TEXT.items():
        assert langs2[ENGLISH][index[key]][1] == desc.encode("ascii"), key
    print(f"wrote {len(changes)} description(s) to {STL}; verified by re-read. "
          "Re-bake the VFS (client-only).")


if __name__ == "__main__":
    main()
