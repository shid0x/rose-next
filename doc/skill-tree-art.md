# Adding a Skill to the Skill Tree

How to make a new skill appear in the skill-tree window *with its box and connector*,
rather than as a bare floating icon or not at all.

Worked example: Calibrated Burst (row 7002), added 2026-09-01 as its own branch off
Craft Mastery rank 6. The mechanised version lives in `scripts/import-artisan-skill.py`
(`art_add` / `art_remove`, covered by `--restore`, reported by `--verify`).

## The one thing to know

**The boxes and the connectors are painted into the category's DDS. There is no
line-drawing code.**

`CSkillTreeDlg::Draw` (`src/client/interface/dlgs/skilltreedlg.cpp`) blits *one page
image* — the `IMAGE="DEALER_*.DDS"` of the first base skill in the list, i.e. the
selected tab — at `m_sPosition + (20, 75)`, then draws 40x40 icons on top:

```cpp
::drawSprite(iter_base->m_texture, ..., m_sPosition + (20, 75), ...);
for (all base_skills)            iter_base->m_icon->Draw();       // the tab column
for (children of begin() only)   iter_children->m_icon->Draw();   // selected page
```

So the XML only *positions an icon*. Add a node without touching the art and you get
an icon sitting on blank background with nothing joining it to anything.

## Coordinates

- `OFFSETX`/`OFFSETY` are **absolute inside the 564x540 dialog**, *not* relative to the
  parent node — `MoveWindow` does `ptDraw = m_sPosition + m_offset`. Assuming
  parent-relative is how the first attempt landed exactly on top of Refine Item (2601);
  the later-declared node paints over the earlier one, so the skill simply vanished.
- **art-local + `(20, 75)` = dialog-local.**
- Icons are **40x40** (`CIcon::CIcon`). The panel is 564 wide, so the largest usable
  `OFFSETX` is about **524**.
- Boxes in the art are **42x42 with a 1px border**, colour `(74, 81, 99, 255)`.
- The icon sits **+1px horizontally and +2px vertically** from the box's top-left. That
  is the retail relationship: 2621's box is at art `(178, 275)`, its icon at dialog
  `(199, 352)`.

Only the *selected* category's children are drawn, so children of different categories
may legitimately share coordinates — e.g. 2201 and 2041 are both at `(131, 79)`. That
is not a collision; do not "fix" it.

## Arrowheads mean rank, not prerequisite

In this artwork an arrowhead means **"same skill, higher rank"**. The only two in the
Dealer tree are `2081 -> 2081 LEVEL=6` and `2221 -> 2221 LEVEL=11` (Twin Shot into
Triple Shot). Every other link — the branches hanging off the left spine — is a plain
line. A new *distinct* skill therefore gets a plain connector; an arrowhead would
misrepresent it as a rank continuation.

## Recipe

1. **Pick a free slot.** Render the page art to PNG and look at it. Check the slot
   against every existing node on that page, and keep `OFFSETX + 40 <= 564`.

   ```python
   from PIL import Image
   im = Image.open('data/3DDATA/CONTROL/RES/DEALER_CRAFT_MASTERY.DDS').convert('RGBA')
   bg = Image.new('RGBA', im.size, (110, 110, 120, 255)); bg.alpha_composite(im)
   bg.convert('RGB').save('page.png')      # transparent areas show as flat grey
   ```

2. **Paint the box and connector** into the DDS, in `(74, 81, 99, 255)`. To hang a new
   branch off the left spine, extend the spine down to the new box's vertical centre,
   run a stub across, then draw the 42x42 outline. See `_art_draw` in
   `scripts/import-artisan-skill.py`.

3. **Back up the original as a *hidden* file.** `pack.rs` walks the data tree filtering
   only hidden entries and applies **no extension filter**, so a plain `.bak` next to
   the art gets baked into the `.vfs`. Set `FILE_ATTRIBUTE_HIDDEN` (`_hide()` in the
   same script).

4. **Add the XML node** under the right parent in
   `data/3DDATA/CONTROL/xml/skilltree_<class>.xml`, deriving the offset from the box
   rather than hardcoding it:

   ```xml
   <SKILL INDEX="7002" OFFSETX="199" OFFSETY="432"/>
   ```

   The file ships **LF-only** — read and write it with `newline=""` or you rewrite
   every line.

5. **Deploy.** See below — the two files do *not* deploy the same way.

## Deploying: the xml and the DDS behave differently

| file | how the client reads it | what you must do |
|---|---|---|
| `skilltree_<class>.xml` | plain `fopen("3DData\\Control\\xml\\...")` — bypasses the VFS entirely | copy the **loose** file into the game dir; no bake |
| `DEALER_*.DDS` | `CVFS_Manager::OpenFile` | **re-bake the VFS** |

`CVFS_Manager::OpenFile` tests `FileExistsInVfs()` first and, for a name that is in the
archive, **never falls back to disk**. A loose DDS in the game dir is silently ignored,
so an art change that is not baked looks exactly like an art change that did not work.
The xml is the same trap as `UI_strID.ID`, in the opposite direction.

Restart the client either way — the xml is parsed once, at dialog construction.

## Things that will bite

- **Skill rows must stay under 16383.** `gsv_EFFECT_OF_SKILL::m_nSkillIDX` is a
  bitfield, widened from 12 to 14 bits on 2026-09-01. A row above the cap wraps on the
  wire, the client resolves an unrelated row, and damage never presents — while the
  animation still plays correctly, because the *cast* packets carry a full `short`.
  Note `import-artisan-skill.py` appends at `rows - 1`, and LIST_SKILL has ~2000
  name-only placeholder rows above 4095 pushing that append point upward.
- **Pillow (10.4) can only write uncompressed BGRA DDS.** The shipped art is DXT5 with
  mips; a re-save costs ~350 KB -> ~1 MB and drops the mip chain. Harmless for UI, which
  is blitted 1:1, and `data/SCRIPTS/INIT.LUA` caps mips at 3 anyway — but it is a real
  format change, which is why step 3 exists. Use `texconv` if you want to keep DXT5.
- **A skill with no icon (`LIST_SKILL.STB` col 51 = 0) cannot be learned**, including via
  `/add skill` — `Cheat_add` guards on `SKILL_ICON_NO`. It will still draw in the tree.
- `data/` is gitignored, so **the script is the only committed record** of a tree
  placement or an art edit. Put the reasoning there, not just in a commit message.
