# Custom equipment asset feasibility audit

Date: 2026-09-05. Scope: prospective creation of weapons, wings/back items, and armor for **this Rose Next Classic client**.

**Verdict: yes, technically feasible.** I could author original geometry through Blender Python, prepare textures and equipment definitions, and build the conversion tooling needed to deliver ROSE assets. Weapons and static wings are the strongest first projects. Armor is feasible but requires considerably more fitting, skinning, and animation validation. This audit establishes compatibility requirements and a credible production route; it does **not** demonstrate that an original asset has already been exported or tested in game.

Only this report was added to the repository. Source code, game data, installed software, and packed archives were not modified. Inspection scripts ran from standard input; a temporary texture contact sheet was produced outside the repository for visual inspection. No client/server was launched or restarted.

## 1. What “I can make assets with Blender” means here

The practical route is to write and execute Blender Python scripts that create meshes, UVs, materials, armatures, renders, and exports, then inspect the results and iterate. Blender documents background script execution through `blender --background --python ...`. This does not require a dedicated Blender connector. [Blender Python documentation](https://docs.blender.org/api/3.3/info_tips_and_tricks.html).

In this session, shell execution and image inspection are available. Python, Pillow, .NET, ROSE readers/writers, and a vendored DDS converter are present. **Blender was not found on PATH or in the standard installation locations checked; `bpy` is absent from the available Python environment, and no Blender-specific tool is exposed.** This is a scoped environment check, not a claim that no portable installation exists anywhere on the machine. Blender installation or identification is a prerequisite for an actual Blender production trial.

The OpenAI model guidance checked did not establish a Blender-specific production guarantee. My assessment is based on the available automation route and the local asset contract, rather than a promise attached to the Astra name. [Official model guidance](https://developers.openai.com/api/docs/guides/latest-model).

I am most confident about repeatable geometry, technical conversion, validation, and controlled variations. Matching ROSE's painted visual style, making convincing organic wings, and achieving clean armor deformation require visual iteration and your art direction. A beautiful render, concept image, or `.blend` file is only an intermediate deliverable.

## 2. How these assets are made and consumed

The historical source contains explicit references to a **3ds Max znzin exporter** and Character Studio coordinate conventions: see [zz_mesh_tool.cpp](../src/engine/src/zz_mesh_tool.cpp) and [zz_model.cpp](../src/engine/src/zz_model.cpp). This is evidence of the original authoring pipeline, although it does not establish the original studio's complete workflow or exact software version. I found runtime assets and conversion references, not a ready-to-run historical equipment authoring setup.

The reconstructed workflow is conventional modeling/texturing, skinning where required, export to ROSE binary formats, equipment registration, then VFS packaging. Blender can replace the authoring application provided its output satisfies the same runtime contract.

| Component | Purpose |
|---|---|
| `.ZMS` | Triangulated mesh: positions, normals, UVs, optional vertex colors and skin weights, bone palette, bounds and additional mesh metadata |
| `.DDS` | Texture pixels and alpha; ZSC material settings determine their rendering behavior |
| `.ZSC` | Indexed equipment model objects, mesh/material references, attachment selectors, effects and other tagged properties |
| `.ZMD` | Character skeleton and attachment dummy points; reuse the existing male/female files initially |
| `.ZMO` | Motion channels; existing character motions animate equipped armor and carry attached weapons/back items |
| `.STB` | Item properties, requirements, weapon behavior, icon index and ground-drop model index |
| `.STL` | Display names and descriptions |
| `ITEM1.TSI` + DDS sheets | Inventory icon atlas; item icons occupy 40×40 cells |
| `.EFT` / `.PTL` | Optional effects and particle dependencies |
| `data.idx` + `rose*.vfs` | Packed distribution consumed by the deployed client |

The equip path is **item type + item number → the appropriate ZSC object → its mesh/material parts**. The STB text column containing an apparent model filename is vestigial. Editing that string does not install a model. Ground-drop art separately uses STB game column 10 into `LIST_FieldITEM.ZSC`; the icon uses column 9. See [import-item.py](../scripts/import-item.py), [io_basic.cpp](../src/client/io_basic.cpp), and [cmodelchar.cpp](../src/client/cmodelchar.cpp).

The existing Rust pipeline supports copying, DDS conversion, table conversion, ZSC JSON conversion, and packing. **It contains no Blender/FBX/OBJ-to-ZMS baking command.** Its general “asset compiler” description must not be mistaken for an existing mesh exporter. [bake.rs](../src/pipeline/src/bake.rs).

## 3. Evidence from the actual data

These are measurements of the local `data/` snapshot, not figures copied from older documentation. Counts include empty/reserved rows; they are not counts of usable items.

| Category | STB rows | Corresponding ZSC object counts |
|---|---:|---:|
| Weapons | 1,381 | 1,381 |
| Subweapons/shields | 308 | 308 |
| Back items | 1,017 | 1,017 |
| Body armor | 916 | Male 916; female 916 |
| Gloves | 892 | Male 892; female 892 |
| Boots | 890 | Male 890; female 890 |
| Headgear | 989 | Male 989; female 989 |

Read-only validation performed:

- All **11 equipment ZSC tables** reserialized byte-identically using the existing `replace-item-model.py` serializer in memory.
- Every mesh and material texture path listed in those tables resolved on disk. This was not a full recursive audit of effects, motions, icons, or ground-drop dependencies.
- Male and female skeletons both parsed as **ZMD0003, 21 bones, 7 dummies**, consuming their complete files. Their proportions/transforms must still be preserved separately.
- **1,141 table-unique body/glove/boot mesh entries** were checked for skinning. All had skin data, valid palette references into the 21-bone skeleton, nonnegative weights, and weight sums within 0.001 of 1. Maximum observed palette size was 20; maximum meaningful influences per vertex was 4. “Table-unique” deduplicates within each table, not across all tables.
- All **168 unique back meshes** inspected were unskinned ZMS0007/8. No back part carried the character mesh-animation tags 8–28. Thus the inspected back collection provides static attachment examples, not a demonstrated flapping-wing pipeline.

Geometry measurements below are **per unique mesh**, not per complete item, and cover ZMS0007/8 only. Five ZMS0006 weapon meshes and one ZMS0006 subweapon mesh were excluded from these geometry statistics; their version is supported by the engine and was not classified as corruption.

| Category | Meshes measured | Median vertices | Median triangles | Largest triangle count |
|---|---:|---:|---:|---:|
| Weapons | 517 | 152 | 164 | 2,556 |
| Subweapons | 77 | 118 | 120 | 888 |
| Back items | 168 | 265 | 255 | 11,046 |
| Male body armor | 331 | 311 | 366 | 1,600 |
| Female body armor | 335 | 294 | 363 | 1,196 |

Concrete examples make the scale clearer:

- Weapon object 1 (`osw15.zms`): 162 vertices / 88 triangles, one mesh.
- Back object 1 (`back_wing01.zms`): 84 / 104, one mesh.
- Phoenix Wings, back 957: two meshes totaling **327 vertices / 386 triangles**, plus one effect dummy.
- Clockwork Wings, back 959: two meshes totaling **5,103 vertices / 2,700 triangles**.
- Male body object 1: two skinned meshes totaling **369 vertices / 532 triangles**.

Most weapon material entries use 128×256 DDS textures. Back and body material entries commonly use 256×256; gloves/boots commonly use 128×128. Some assets use larger textures. These observations are useful starting references, not hard engine limits or a recommendation to imitate the largest outlier.

Visual inspection of six decoded textures showed painted wood/metal detail, broad fabric/skin shading, and alpha-shaped wing art. The Phoenix feathers are substantially described by texture detail rather than individual feather geometry. This favors **clear silhouettes and carefully painted diffuse/alpha textures**. A detailed sculpt or modern PBR material stack would still need simplification and baking into the material features this client consumes.

## 4. Feasibility by asset category

| Asset | Assessment | Main work |
|---|---|---|
| Sword, axe, hammer, staff | High confidence after export validation | Original silhouette, UVs/texture, grip orientation, correct attachment and effect points |
| Shield | High confidence | Same process with shield attachment and pose clearance |
| Bow, gun, dual weapon | Feasible, more behavior checks | Multiple parts, hand placement, projectile/trail origins and existing weapon-class motions |
| Static wings/back ornament | High confidence technically | Back-relative placement, alpha/material settings, both avatar fits and camera readability |
| Wings with existing particle effects | Feasible | Correct dummy/effect registration, copied dependencies, performance review |
| Independently flapping wings | Separate feasibility trial | Prove the existing morph-motion path or design a compatible extension; do not assume arbitrary Blender armatures work |
| Helmet | Relatively approachable | Usually rigid, but male/female tables, head fit and hair rules still matter |
| Armor texture variant | Strong first armor exercise | Preserve existing mesh/UV/weights and create compatible original texture work |
| New body armor/gloves/boots | Feasible, highest fitting effort here | Separate male/female fits, topology, skin weights, joint seams and pose coverage |
| Flowing cloth or arbitrary new skeleton | Outside the initial standard-equipment scope | Would need additional runtime/animation investigation |

### Weapons

Weapons in the measured sample are rigid, avoiding skin-weight authoring. The shared definitions identify right hand dummy 0, left hand 1, shield 2. A new weapon should inherit the attachment and behavior conventions of a working weapon of the same class, with its own mesh and texture.

The difficulty is not merely polygon generation: a sword must sit correctly in the hand, a two-handed weapon must meet the second hand throughout existing motions, and a gun's projectile must originate at the right place. Larger geometry does not automatically change attack range, timings, damage, or animation class. Those are separate item/gameplay definitions.

Weapon model indices are also used directly by NPCs. Preserve existing indices instead of rearranging the weapon table. [NPC weapon investigation](npc-weapon-models.md).

### Wings and back items

The client loads one shared `LIST_BACK.ZSC` for both sexes, with default **back dummy 3**. A back item does not need its own skeleton to move with the avatar. The inspected Phoenix example demonstrates how little geometry is required for visually detailed wings.

Model-carried cosmetic effects already have a live implementation in [cobjchar.cpp](../src/client/cobjchar.cpp), including the bone-effect budget. The project documentation reports successful in-game Phoenix trail testing; I did not repeat that test. [Jrose import findings](jrose-back-import.md).

Independent wing motion needs a separate qualification. `CCharPART` has a `loadMorpher` branch and per-action mesh-motion lookup, so the engine is not simply “static attachments only.” However, that branch expects a **bone attachment**, its motion data must match the mesh, and the current back assets inspected do not exercise it. Particle motion is not evidence that the wing geometry flaps. Start with static wings and validate morph animation later.

### Armor

Body armor, gloves and boots enter the avatar as skinned render units. Both sex-specific ZSC tables use the same item number, but their meshes are fitted to their respective skeleton transforms. Headgear normally follows the rigid cap attachment path instead.

A full outfit is several equipment slots, often with several meshes per slot, rather than one interchangeable whole-character mesh. Body art can include exposed skin. Preserve the neck, wrist and ankle interfaces and verify combinations with existing equipment; replacing the body slot does not automatically solve every adjoining seam.

The most reliable first original armor would retain the stock skeleton and attachment points, use an existing fitted garment as a geometric reference, and transfer/refine skin weights. New topology would then be checked in shoulder, elbow, hip and knee bends. Matching bone names alone is insufficient: **indices, hierarchy, rest transforms and bind-space geometry** must agree.

## 5. The conversion work that remains

### A. Establish a Blender-to-ROSE exporter

The public RoseBlend README advertises ZMS import and unfinished heightmap import, not a supported equipment export pipeline. It is a reference candidate whose Blender-version compatibility would need testing. [RoseBlend upstream](https://github.com/rminderhoud/rose-tools/tree/master/rose-blend). **Follow-up: the user identified `vektorprime/io_rose`, which does contain a direct Blender ZMS exporter. Section 8 updates the recommendation to qualify/adapt that implementation first.**

Locally, Revise provides ZMS/ZMD/ZMO/ZSC code, and `zz_mesh_tool` contains the actual engine reader plus a native mesh writer. We therefore have enough format information to implement a focused exporter without the old Max plugin. Start with **ZMS0008 rigid meshes with positions, normals and UV0**, then add skinned export once a rigid round trip works.

A `.blend`/FBX/OBJ file alone cannot be passed to `import-item.py`. That script consumes a ROSE-format source data tree and reads a source STB even in `--art-only` mode. A future authoring bridge must either stage a small compatible source data set or expose an asset-registration path using the existing append logic. The current script already solves much of registration; it does not solve modeling/export.

### B. Respect the character loader's actual behavior

**Do not rely on ordinary ZSC position/rotation/scale properties to fix equipment placement.** `CCharPART::Load` reads bone/dummy selectors and mesh-animation tags, skipping the usual transform tags. `Load_ZMODEL` links the resulting visible directly to a bone/dummy or adds it as a skinned render unit. In contrast, other ZSC consumers and effect points handle transforms. Bake equipment placement into the exported geometry in the appropriate attachment or bind space. [io_model.cpp](../src/client/io_model.cpp).

The engine also mixes units: ZMD translations are multiplied by `ZZ_SCALE_IN` (0.01), while the ZMS0007/8 position path reads mesh positions directly. Do not apply a blanket 100× scale conversion across all formats. Establish axis orientation, units, UV direction and triangle winding using a known asset. ZMD rotations are stored **w,x,y,z**. [zz_skeleton.cpp](../src/engine/src/zz_skeleton.cpp), [zz_mesh_tool.cpp](../src/engine/src/zz_mesh_tool.cpp).

### C. Treat Revise as a starting point requiring qualification

Two concrete issues are visible in its current [ZMD implementation](https://github.com/jackwakefield/Revise/blob/master/Revise.Files/ZMD/BoneFile.cs):

- The dummy-loading loop calls `DummyBones.Clear()` instead of adding the decoded dummy.
- The dummy writer emits the parent integer before the name terminator, whereas the reader/engine expect the terminated name before the parent. `WriteString` itself does not terminate strings. [BinaryWriter extensions](https://github.com/jackwakefield/Revise/blob/master/Revise.Extensions/BinaryWriter.cs).

These are findings from source inspection, not a runtime test of Revise. They are enough to reject an assumption that its skeleton round trip is already production-ready. Reusing the original ZMD files avoids rewriting them for initial equipment, but any Blender importer still needs to reconstruct the attachment points correctly. No Revise changes were made.

### D. Export a deliberately small, verifiable subset

- Triangulate evaluated geometry; apply necessary modifiers/transforms. Preserve UV seams and hard normals by splitting exported vertices where their attributes differ.
- Export normals and at least UV0 explicitly. ROSE assigns a material to each equipment part; use separate parts when different materials/render states are required.
- ZMS0007/8 counts/indices use 16-bit storage. Some tools interpret counts as signed; remain well below 32,768 vertices/triangles per mesh rather than treating the binary ceiling as an art budget.
- Skinning stores up to four weights and local palette indices per vertex. Normalize weights and provide valid palette indices even for zero-weight entries: the engine checks all four.
- The shader constants reserve a 22-bone block; the local armor samples use at most 20. Preserve the original 21-bone character rig and modest per-part palettes rather than assuming the skeleton parser's much larger limit is a GPU skinning budget. [zz_renderer.h](../src/engine/include/zz_renderer.h).
- Recompute finite, ordered ZMS bounds from exported positions and produce coherent model bounds. The project has already encountered incorrect exported bounds; a valid mesh must also remain visible through camera movement. [Bounding-box investigation](zsc-bounding-boxes.md).
- Generate legacy DX9-compatible DDS files, appropriate alpha, and complete mip chains. Avoid DX10 headers and unsupported modern texture encodings. The existing mip utility documents the required `-dx9`/`-nowic` behavior. [add-dds-mipmaps.py](../scripts/add-dds-mipmaps.py).

## 6. Registration, packaging and validation

For future implementation, preserve the existing IDs and append a new item using a same-class local stat template. Use `--art-only --template-row ...` with the existing importer when its source-tree contract is satisfied. This also avoids foreign ability IDs that the current client/server consume through unchecked array indices. Begin with its dry-run mode. [Import tool](../scripts/import-item.py).

There is an **11-bit item number limit: 2047 per item type**, confirmed in [citem.h](../src/common/shared/citem.h) and enforced by the importer. The current weapon table permits 667 more appended IDs and the back table 1,031, assuming no other changes. Empty internal rows are not automatically safe to repurpose because other tables and persisted inventories can reference IDs.

Provide a name/description, icon, and a valid ground-drop model. A known local drop model is adequate for an early prototype; a finished original item should have intentionally chosen drop art. The icon allocator must avoid the historical atlas overrun: 13×40 exceeds a 512-pixel sheet, so extension allocation uses 144 fitting cells. Regenerate mips after icon edits. [add-item-icon.py](../scripts/add-item-icon.py), [atlas repair notes](jrose-back-import.md).

Keep `.blend` sources, intermediate PNGs, manifests and backups outside `data/`. The packer filters hidden entries, not unwanted extensions. Package through [pack.ps1](../scripts/pack.ps1), verify the VFS, distribute all generated `rose*.vfs` files with `data.idx`, and restart the relevant processes after a real deployment. The deployed VFS path matters; editing the loose source tree does not prove the running client loaded it. `data/` is gitignored, so retain a reproducible authoring recipe and asset/change manifest outside it.

Acceptance should cover:

1. **Format:** reopen exported files independently; check geometry, UVs, normals, weights, bounds, indices and dependency paths. A no-edit round trip must preserve the required semantics; byte identity is appropriate for the existing ZSC serializer, but not universally for Blender exports that reorder vertices or convert versions.
2. **Appearance:** compare textured views from several angles and at normal gameplay zoom with nearby stock equipment. Check alpha edges, backside visibility and mip appearance.
3. **Attachment/deformation:** test both sexes, idle/walk/run, class attacks, relevant skills, sitting, death and riding where applicable. Inspect grip placement and armor seams, not just the rest pose.
4. **Integration:** equip/unequip, relog, preview, inventory icon, name, ground drop, and another character observing the item.
5. **Effects/performance:** verify trails/projectile origins and grade/gem effects in the real scene; assess repeated visible copies and transparent overdraw. Small triangle counts do not guarantee cheap particles or large translucent wings.

The existing Alt+click preview is useful for rapid base-model comparison. Its idle puppet does not reproduce grade/gem glow and cannot replace real combat-pose tests. [Client preview documentation](../src/client/CLAUDE.md).

## 7. Recommended proof of concept

This is proposed future work; none of these production steps were executed in this audit.

| Stage | Deliverable | What would establish success |
|---|---|---|
| 1. Conversion qualification | Import a known rigid weapon into Blender and export it to a separate staging area | Independent format checks and equivalent appearance/placement in the actual client |
| 2. First original asset | One original sword, axe or staff using an existing class and animations | New silhouette/texture, correct grip, complete icon/drop registration, both sexes tested |
| 3. Back attachment | One original static wing pair | Convincing diffuse/alpha art, clean back placement, acceptable crowd rendering |
| 4. First armor | Original texture variant, then one changed body garment fitted for both sexes | Preserved seams, valid weights and clean representative poses |
| 5. Expansion | Matching gloves/boots/helmet; optional wing motion | Repeatable results without per-item exporter repairs |

For the first three stages, match the complexity of a chosen stock reference, usually hundreds of triangles and a 128–256-pixel-class texture, and increase only when the visual result warrants it. The tooling qualification is a one-time prerequisite; subsequent items can reuse validated export settings, reference rigs, material presets and registration steps. Reliable time-per-item estimates should follow the first completed prototype, not precede it.

**Decision:** proceed to a small original weapon trial when asset production is desired. Standard weapons, static wings and armor on the stock skeleton should fit the existing engine without a client/server code change. The outstanding work is an authoring/export bridge and visual qualification. Armor quality and independently animated wings carry substantially more uncertainty than rigid weapon creation.

## 8. Follow-up: the two Blender projects supplied by the user

This remains theory crafting. External source files were inspected in temporary storage; neither add-on was installed or run. Snapshots reviewed: `vektorprime/io_rose` at `1a9bb39624789d32d527916762a693178676976d`, and `rminderhoud/rose-tools` at `8499297c46ded7d0a46028ba7a369858bca5a23e`.

**Updated assessment: a converter does not have to be built from nothing. Qualifying and adapting `vektorprime/io_rose` is the stronger first option.** An FBX intermediate is optional: Blender geometry can be exported directly to ZMS. FBX would be useful for exchanging work with other applications, not a requirement imposed by ROSE.

| Project | Source-level finding | Proposed role |
|---|---|---|
| `vektorprime/io_rose` | Registers ZMS, ZMO and EFT export operators, plus mesh/skeleton import | Primary candidate for a focused equipment workflow |
| `rminderhoud/rose-tools/rose-blend` | Registers map, ZMD and ZMS import only; declares Blender 2.77 and uses legacy APIs | Format/import reference rather than the first exporter candidate |
| `rminderhoud/rose-tools/rose-lib` | Has a separate Rust ZMS0008 writer, including skin data | Alternative serialization foundation or independent comparison implementation |

Sources: [vektorprime registration](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/__init__.py), [RoseBlend registration](https://github.com/rminderhoud/rose-tools/blob/8499297c46ded7d0a46028ba7a369858bca5a23e/rose-blend/io_rose/__init__.py), [Rust ZMS writer](https://github.com/rminderhoud/rose-tools/blob/8499297c46ded7d0a46028ba7a369858bca5a23e/rose-lib/src/files/zms.rs).

The vektorprime exporter already handles triangles, UV seams, bounds and four-weight skin serialization. Specific qualification points from its source:

- Fresh skinned meshes need a bone palette: export relies on imported `zms_bones` metadata and vertex-group order rather than constructing a mapping from an arbitrary armature.
- It reads `obj.data`, not evaluated modifiers. The manual export also disables world-transform application.
- Normal handling uses vertex normals and does not split by corner normal.
- Imported triangle-strip/material metadata is restored after geometry reconstruction; edited topology requires rebuilding or discarding stale metadata.
- ZMS0009 is offered, and imported version metadata overrides the selection. Our engine supports 5–8; enforce version 8.

These are source-level constraints and inferred risks, not reproduced Blender failures. [Pinned exporter](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/export_zms.py).

The combined mesh/skeleton importer retains `zms_bones`, creates named vertex groups, and includes ZMD dummy joints. That offers a useful route for stock-rig armor work. The Python ZMS reader resolves file palette slots to skeleton IDs, consistent with our loader; comments discussing “global indices” in downstream Blender code must be read in that context. [Combined importer](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/import_zms_zmd.py), [ZMS parser](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/rose/zms.py), [ZMD parser](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/rose/zmd.py).

Although vektorprime retains a Blender 2.77 metadata declaration, its code contains newer API handling and its test documentation uses Blender 4.5. That makes it a better modernization candidate, not proof of compatibility with every current Blender version. Documented tests emphasize zones, textures and effects; they do not establish original equipment skin-export correctness. [Test documentation](https://github.com/vektorprime/io_rose/blob/1a9bb39624789d32d527916762a693178676976d/tests/README.md).

The proposed future path is **Blender authoring → qualified direct ZMS export → DDS/ZSC/item registration using Rose Next tooling → client validation**. Start by preserving a stock rigid weapon through that route, then make an original weapon, then qualify stock-rig armor. Registration, game material behavior, icons and packaging remain distinct work even when mesh export is available. No FBX converter implementation is warranted before evaluating this direct path.
