"""Restore item descriptions that exist in a reference dump but not in ours.

Companion to `fix-item-names.py`, which fixed the *names* and picked up 57
descriptions along the way (the ones attached to entries it was creating anyway).
This one sweeps the rest of the tables: items that already resolve to a name but
whose STL description field is empty.

Our tables are 90.7% covered -- 369 of 3,980 items have no description. 69 of
those exist verbatim in a dump we own; this script writes those 69 and leaves the
other 300 alone, because no dump has them and inventing flavour text is a
different decision from restoring it. The 99 jewels and the 33 weapons that
include the 21 dual guns score zero recoverable between them: they are content
authored here, so there is no original to restore.

Matching is by **exact displayed name**, never by row
--------------------------------------------------------
Row numbers mean nothing across clients -- our LIST_PAT row 7 is `Clover Frame`
and 667's is `Gray Racing Frame`. A description adopted by row number would be
plausible and wrong, which is the worst failure mode for text nobody
proof-reads. So a description is taken only when some dump holds an item whose
displayed name is byte-identical to ours, and the match runs through the same
`description_index` that `fix-item-names.py` used, so both passes agree on what
counts as a match.

Guards on what gets adopted (all in `description_index`):
  * the dump's language block is chosen by counting ASCII-looking names, so a
    Korean or Japanese block is never mistaken for the English one;
  * the description itself must be ASCII and longer than 8 characters, which
    drops placeholder junk like "-" and "test";
  * a dump we cannot parse is skipped, not fatal.

Conflicts between dumps are reported rather than silently resolved: `--dry-run`
prints every name where two dumps disagree on the text, so the pick can be
eyeballed before anything is written. First dump wins in the order 667, roseza,
ruff, qq -- 667 being the largest and newest of the four.

Two conflicts exist today and both were checked; neither needs an override:

  * **Rice Cake Soup** -- the four dumps differ only in whether a sentence break
    is one space or two.
  * **Recovery Kiss** -- "Learn Basic Skill" against "Learn emotive expression."
    The item is Type 314 / icon 1704, the emote-and-basic-skill scroll class, and
    **our own table already describes 48 items of that exact class as "Learn
    Basic Skill"** (Sit, Jump, Party, Drive Cart, ...). The default pick is the
    one that matches its 48 siblings, so taking the more florid string would have
    made this row the odd one out. `Ride Request` is the same class and the same
    call.

Only the English language block is touched
------------------------------------------
An STL entry carries five language blocks, and "no English description" does not
mean "no description". `LPAT424 Steel Wheels` holds 57 spaces in its English
field and real Korean, Japanese and Chinese text in blocks 0, 2 and 3. An earlier
draft of this script wrote the recovered string into all five and blanked all
five on `--restore`, which destroyed those three translations; it is only
recoverable because the deployed `rose.vfs` still had them. So: writes go to
`LANG_USA` alone, and the sidecar records the **previous bytes** so restore puts
back exactly what was there -- 57 spaces is not the same value as "".

(`fix-item-names.py` does write all five blocks, and that is correct there: it
appends entries that did not previously exist, so there is no translation to
lose.)

Descriptions are display-only. `GetItemDesc` feeds tooltips and nothing else, so
this cannot affect drops, prices, stats or validity.

Note that a **ride part's** description did not render at all until the tooltip
fix in `CItem::GetToolTip` (item.cpp, `ITEM_TYPE_RIDE_PART`) -- the call had been
commented out since 2004. Cart parts need that client build to show any of this.

Idempotent, verifiable and reversible through a sidecar next to the STBs. `data/`
is gitignored, so this file is the only committed record of the change.

Usage:
    python scripts/fix-item-descriptions.py --dry-run
    python scripts/fix-item-descriptions.py
    python scripts/fix-item-descriptions.py --verify
    python scripts/fix-item-descriptions.py --restore
"""
import argparse
import collections
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SIDECAR = os.path.join(STB_DIR, "item-descriptions.json")


def load(name, mod):
    spec = importlib.util.spec_from_file_location(mod, os.path.join(HERE, name))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def all_candidates(rd, fix, base):
    """name(lowercased) -> [(dump tag, description)], every dump that has one.

    Same filters as fix.description_index, but keeps every hit instead of the
    first, so disagreements between dumps can be reported.
    """
    out = collections.defaultdict(list)
    for tag, path, stb_enc, stl_enc in fix.DUMPS:
        f_stb = os.path.join(path, base + ".STB")
        f_stl = os.path.join(path, base + "_S.STL")
        if not (os.path.exists(f_stb) and os.path.exists(f_stl)):
            continue
        try:
            s = rd.Stb(f_stb, stb_enc)
            stl = rd.Stl(f_stl, stl_enc)
            kc = s.key_column()
            if kc is None:
                kc = s.cols - 1
            best, best_score = 0, -1
            nlang = len(stl.lang_off) if stl.lang_off else 1
            for li in range(nlang):
                try:
                    block = stl.lang(li)
                except Exception:
                    continue
                score = sum(1 for v in block[:300] if fix._ascii(v[0]))
                if score > best_score:
                    best, best_score = li, score
            names = {k.decode("latin-1"): v
                     for (k, _), v in zip(stl.keys, stl.lang(best))}
            for r in range(s.rows):
                n0 = s.s(r, 0).strip()
                if not n0:
                    continue
                v = names.get(s.s(r, kc).strip())
                if not v or len(v) < 2:
                    continue
                nm, ds = v[0].strip(), v[1].strip()
                if nm.lower() == n0.lower() and fix._ascii(ds) and len(ds) > 8:
                    out[n0.lower()].append((tag, ds))
        except Exception:
            continue
    return out


def missing_descriptions(rd, fix, item_type, base):
    """(row, key, name) for every item that has a name but an empty description."""
    stb = fix.StbFile(os.path.join(STB_DIR, base + ".STB"))
    names = rd.Stl(os.path.join(STB_DIR, base + "_S.STL"), "utf-8").by_key(fix.LANG_USA)
    kc = stb.key_col()
    out = []
    for r in range(stb.rows):
        if not stb.get(r, 0).strip():
            continue
        if not fix.is_real_item(item_type, stb, r):
            continue
        key = stb.get(r, kc).strip()
        entry = names.get(key)
        if not entry or not entry[0].strip():
            continue                        # no name -- fix-item-names.py's job
        if len(entry) > 1 and entry[1].strip():
            continue                        # already described
        out.append((r, key, entry[0].strip()))
    return out


def plan(rd, fix):
    jobs = []
    for item_type, (base, prefix) in sorted(fix.TABLES.items()):
        if not os.path.exists(os.path.join(STB_DIR, base + ".STB")):
            continue
        rows = missing_descriptions(rd, fix, item_type, base)
        if not rows:
            continue
        cands = all_candidates(rd, fix, base)
        picks, conflicts = [], []
        for row, key, name in rows:
            hits = cands.get(name.lower())
            if not hits:
                continue
            tag, desc = hits[0]
            if len({d for _, d in hits}) > 1:
                conflicts.append((name, hits))
            picks.append((row, key, name, tag, desc))
        if picks:
            jobs.append(dict(base=base, total_missing=len(rows),
                             picks=picks, conflicts=conflicts))
    return jobs


def report(jobs):
    picked = sum(len(j["picks"]) for j in jobs)
    missing = sum(j["total_missing"] for j in jobs)
    print("%d items have a name but no description; %d recoverable from a dump\n"
          % (missing, picked))
    for j in jobs:
        print("  --- %s  (%d of %d)" % (j["base"], len(j["picks"]), j["total_missing"]))
        for row, key, name, tag, desc in j["picks"]:
            print("      %-10s %-30s [%-6s] %s"
                  % (key, name[:30], tag, desc[:64] + ("..." if len(desc) > 64 else "")))
        if j["conflicts"]:
            print("      ! dumps disagree on %d of these:" % len(j["conflicts"]))
            for name, hits in j["conflicts"]:
                print("        %s" % name)
                for tag, d in hits:
                    print("          [%-6s] %s" % (tag, d[:70]))
    print("\n  %d descriptions to write" % picked)


def apply_and_write(fix, jobs, dry):
    """Write each description into the English block *only*.

    Emphatically not into all five. An entry whose English description is blank
    can still carry a real Korean/Japanese/Chinese one -- LPAT424 `Steel Wheels`
    is exactly that, its English field holding 57 spaces while lang 0/2/3 hold
    proper localised text. Writing every block would overwrite three real
    translations with one English string, and blanking every block on restore
    would then destroy them outright. The English block is the only one this
    pass has anything to say about.

    (`fix-item-names.py` does write all five, correctly: there the entry did not
    exist at all, so there is no translation to lose.)

    The previous English bytes are recorded verbatim so `--restore` puts back
    what was there rather than assuming it was empty -- those 57 spaces are not
    the same thing as "".
    """
    record = {}
    for j in jobs:
        stl = fix.StlFile(os.path.join(STB_DIR, j["base"] + "_S.STL"))
        by_key = {k.decode("latin-1"): i for i, (k, _) in enumerate(stl.keys)}
        wrote = {}
        entries = stl.langs[fix.LANG_USA]
        for row, key, name, tag, desc in j["picks"]:
            i = by_key[key]
            wrote[key] = {"dump": tag,
                          "prev": entries[i][1].decode("utf-8", "surrogateescape")}
            entries[i] = (entries[i][0], desc.encode("utf-8"))
        record[j["base"]] = wrote
        if not dry:
            with open(stl.path, "wb") as fh:
                fh.write(stl.to_bytes())
    return record


def verify(rd, fix, saved):
    """Every key the sidecar claims must now carry a non-empty description."""
    bad = []
    for base, keys in saved.items():
        names = rd.Stl(os.path.join(STB_DIR, base + "_S.STL"), "utf-8").by_key(fix.LANG_USA)
        for key in keys:
            v = names.get(key)
            if not v or len(v) < 2 or not v[1].strip():
                bad.append((base, key))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    rd = load("rose-data-reader.py", "rose_data_reader")
    fix = load("fix-item-names.py", "fix_item_names")

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for base, keys in saved.items():
            stl = fix.StlFile(os.path.join(STB_DIR, base + "_S.STL"))
            by_key = {k.decode("latin-1"): i for i, (k, _) in enumerate(stl.keys)}
            entries = stl.langs[fix.LANG_USA]
            for key, rec in keys.items():
                i = by_key.get(key)
                if i is None:
                    continue
                # Put back the exact bytes, not "" -- see apply_and_write.
                entries[i] = (entries[i][0],
                              rec["prev"].encode("utf-8", "surrogateescape"))
            with open(stl.path, "wb") as fh:
                fh.write(stl.to_bytes())
        os.remove(SIDECAR)
        print("cleared %d descriptions across %d tables; sidecar removed"
              % (sum(len(v) for v in saved.values()), len(saved)))
        return

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the pass has not been applied")
        bad = verify(rd, fix, saved)
        print("%d descriptions recorded across %d tables; %d missing"
              % (sum(len(v) for v in saved.values()), len(saved), len(bad)))
        for base, key in bad[:20]:
            print("   %-16s %s" % (base, key))
        sys.exit(1 if bad else 0)

    if saved:
        print("already applied to %d tables -- nothing to do."
              % len(saved))
        print("re-run with --restore first if you want to redo it.")
        return

    jobs = plan(rd, fix)
    if not jobs:
        print("nothing recoverable")
        return
    report(jobs)

    record = apply_and_write(fix, jobs, args.dry_run)
    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(record, fh, indent=1)

    bad = verify(rd, fix, record)
    if bad:
        print("\nFAILED: %d descriptions did not land" % len(bad))
        for base, key in bad[:20]:
            print("   %-16s %s" % (base, key))
        sys.exit(1)
    print("\nwritten and verified: %d descriptions restored"
          % sum(len(v) for v in record.values()))
    print("sidecar: %s" % os.path.relpath(SIDECAR, ROOT))


if __name__ == "__main__":
    main()
