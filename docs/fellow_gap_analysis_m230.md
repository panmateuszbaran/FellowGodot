# FellowEngine — where it stands against AAA and Unreal Engine (after milestone 230)

Status: assessment, 2026-11-26. The owner: "assess how far we are from AAA and from Unreal Engine; should we invest in
1. Blueprints, 2. MakeHuman?" Reference: Unreal Engine 5.8 (June 2026). The scores are judgment, from FellowEngine's
measured features (README, the decisions) and Epic's published features — not a benchmark.

## 1. Area by area

| area | FellowEngine (milestone 230) | Unreal Engine 5.8 | gap |
|---|---|---|---|
| weather, water, fire, smoke, snow, rain | one weather controller driving wind, rain, snow, lightning, clouds and the sun; FFT water with buoyancy; GPU smoke and fire; wood that burns to ash; wet surfaces, puddles, snow cover and tracks | sky atmosphere, volumetric clouds, a water plugin, Niagara Fluids; no weather system in the engine (Fab products) | **FellowEngine ahead** |
| procedural sound, acoustics | rain, wind, thunder, fire, water, contacts, footsteps and engines synthesised; sound through geometry and portals | MetaSounds, the audio mixer (tools, little ready-made synthesis) | FellowEngine strong; Unreal's tools broader |
| stutter | pipelines warmed at load, recorded and replayed | 5.8's PSO work | comparable |
| frame cost | Forward+ 16-54 % faster than stock Godot with the same image | heavier by design (deferred, Nanite, Lumen) | not comparable directly |
| diffuse GI, reflections | SDFGI with screen-probe gather; hardware ray-traced AO, sun shadows, GI probes; SSR | Lumen (and Lumen Lite), hardware RT, reflections | medium |
| many shadowed lights | each omni light a cube or dual paraboloid re-rendered when anything moves (the osteria: two lights 1.9 ms) | MegaLights: hundreds of shadowed lights | **large** |
| sun shadows | cascades with a static cache, contact shadows, cloud shadows | virtual shadow maps | medium |
| geometric detail at scale | HLOD, impostors, world and texture streaming | Nanite (now foliage too), World Partition | **large** |
| character systems | motion matching, physical animation with balance, foot placement, joint limits, traversal, cloth, strand hair, a face from the voice, NVIDIA MotionBricks | Control Rig, IK Rig, motion matching, ML Deformer, MetaHuman Animator, MetaHuman Crowd (5.8) | small to medium |
| characters as seen | wooden mannequins | MetaHuman (scanned faces, grooms) | **the largest visible gap** |
| materials | Godot's standard material, visual shaders, FellowEngine's interior and weathering materials | Substrate layered materials | medium |
| tools | Godot's editor, FellowEngine's gizmos, presets, warnings | Sequencer, Blueprints, PCG graphs, Unreal Insights, an MCP plugin | large |
| platforms | Windows, Linux, Android, web; consoles only through third parties | consoles, Switch 2 | large (a business matter) |
| content | none of its own | Fab, Megascans, MetaHuman | very large |

## 2. Summary

* **Against AAA**, as the technology for a mid-sized game by a small team: the simulation and atmosphere side (weather,
  water, fire, sound) is at AAA level and in places beyond it; the frame is fast and does not stutter. What keeps the
  showcases looking AA is not the renderer but three things: the characters (mannequins), the geometry's density
  (no Nanite-class detail) and the lights (each shadowed light expensive).
* **Against Unreal Engine** as a whole: a fraction of its breadth (tools, platforms, networking, content, a thousand
  engineers' years). FellowEngine should not chase that breadth; it should keep its lead where it has one and close
  the three gaps the eye sees.

## 3. Blueprints — not in the engine

* Godot removed its VisualScript in 4.0 for lack of use; GDScript is the accessible language.
* FellowEngine's code is written with an AI: text diffs, reviews and generation; node graphs are hard to diff,
  review and generate, and a visual scripting system is a large, permanent maintenance cost in the engine.
* If a designer needs graphs: Orchestrator (Crater Crash Studios; Apache-2.0; a GDExtension; v2.5.x for Godot 4.7) as
  an optional add-on — at no cost to the engine; to check that it loads in FellowEngine's 4.7.2 fork first.

## 4. MakeHuman — yes, as a character pipeline beside the engine

* MPFB 2 (the MakeHuman plugin for Blender, Blender 4.2+; its code GPLv3, its core assets — base mesh, targets,
  proxies — CC0 since 2020) makes rigged human bodies with clothes and exports glTF: the GPL code stays in Blender, the
  CC0 assets may go into the public repository. Community assets carry their own licences (some CC-BY): to check one
  by one.
* It closes the largest visible gap (mannequins) for little: bodies of any age and build on a humanoid skeleton the
  existing animation (mocap clips, motion matching) retargets to; FellowCloth3D for skirts and coats,
  FellowHair3D for hair, the face's blend shapes for FellowFaceAnimator.
* Its limits: mid-poly bodies, simple skin textures — below MetaHuman's scanned faces. The engine's side: skin
  shading (subsurface scattering tuned for it), an import preset (materials, the skeleton profile, LODs).
* Needs Blender on the owner's laptop (where it runs beside WanGP and blender-mcp): with the owner's go-ahead.

## 5. What next in the engine (proposed order)

1. **The shadowed lights**: a static/dynamic split for omni and spot shadows (static casters cached per face, the
   moving ones drawn over the copy each frame — what decision 0004 did for the sun), then a budget per frame
   (resolution by size on screen, update rates). Correctness checkable on lavapipe; the gain on the RTX.
2. **Characters**: the MakeHuman pipeline and skin shading.
3. **Geometric detail**: cluster LODs with GPU culling (a step towards Nanite's idea), longer term.
4. **Cinematics**: a sequencer-like timeline, if the owner wants films from the engine.

## Sources

* Unreal Engine 5.7 (Nanite foliage, MegaLights): notebookcheck.net, 13 Nov 2025; unrealengine.com news.
* Unreal Engine 5.8 (MegaLights production-ready, Lumen Lite, Mesh Terrain, MetaHuman Crowd, an MCP plugin):
  cgchannel.com, 17 Jun 2026.
* MakeHuman's licences: static.makehumancommunity.org (about/license, the 2020 FAQ); MPFB 2: cgchannel.com, 11 Mar 2025.
* Orchestrator: github.com/Vahera/godot-orchestrator.
