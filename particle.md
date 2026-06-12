# Particle Effect Performance Notes

## Case Study: Frozen Thorn

Monster `1528`, `Frozen Thorn`, causes a large FPS drop when several copies are spawned. The map or zone content is not the root cause. The issue comes from character-attached particle effects.

`LIST_NPC.CHR` attaches two bone effects to Frozen Thorn:

- bone `2`: `3DData\Effect\_glow_04.eft`
- bone `7`: `3DData\Effect\_glow_04.eft`

`_glow_04.eft` uses:

- particle file: `3DDATA\EFFECT\PARTICLES\_star_06.ptl`
- linked effect: yes
- mesh effects: none
- sound: none

`_star_06.ptl` is expensive for a passive character effect:

- `emit_rate = 40` particles per second
- `num_particles = 50`
- `life = 4` seconds
- `num_loops = 0`, meaning it loops indefinitely
- update coordinates are world-space

Each Frozen Thorn therefore owns two infinite particle emitters. Ten Frozen Thorns create twenty always-on emitters, with up to roughly one thousand active particles plus continuous spawn/update churn.

## Why The Issue Is Easy To Miss

Character bone effects are created through `CCharMODEL::CreateBoneEFFECT`.

That path calls `g_pEffectLIST->Add_EFFECT(...)` without adding the effect to the global active effect list. These effects are attached directly to the character model dummy and started there.

As a result:

- the effect can still render and update through the scene graph
- the effect is not counted by the normal global effect list diagnostics
- the HUD `Fx` count can look harmless while FPS is dropping
- cleanup and visibility behavior differs from regular world effects

This makes CHR bone particles a blind spot: they can be very expensive while remaining mostly invisible to existing effect counters.

## Why This Can Happen Again

The current content pipeline allows any NPC or monster to attach looping particle effects to bones. There does not appear to be a runtime budget that accounts for:

- how many character-attached emitters are active
- how many particles those emitters can hold
- how much particle spawn churn they create per second
- whether the character is far away, off-screen, or duplicated many times
- whether the effect is cosmetic and safe to degrade

A single monster with a harmless-looking glow can become expensive once spawned in groups. The cost scales with monster count, bone-effect count, particle capacity, and emit rate.

## Engine-Side Fix Direction

The durable fix should make character bone effects participate in particle diagnostics and runtime budgeting.

Recommended engine work:

- Track CHR bone effects separately from regular world effects.
- Expose debug counters for character bone effect count, emitter count, particle capacity, and estimated particles spawned per second.
- Add distance or visibility-based LOD for cosmetic character particles.
- Allow low-priority bone effects to stop, pause, or use a cheaper variant when many copies are visible.
- Add a per-character or per-NPC-type budget so duplicated monsters cannot create unlimited passive emitters.

Frozen Thorn is a good validation case for this work. Spawning ten monster `1528` instances should no longer tank FPS once the engine can detect and throttle expensive character-attached particle effects.

