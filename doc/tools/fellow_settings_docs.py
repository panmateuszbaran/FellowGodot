#!/usr/bin/env python3
"""FellowEngine's project settings' descriptions (milestone 52): the tooltips of Project Settings > Rendering > Fellow
(and, milestone 97, Debug > Fellow).

Writes a <member> for every rendering/fellow/* and debug/fellow/* setting into doc/classes/ProjectSettings.xml, in its
place among the members, from the descriptions below; the types and defaults from an engine's --doctool output:

    godot --headless --doctool /tmp/docs        # any empty directory
    python doc/tools/fellow_settings_docs.py /tmp/docs/doc/classes/ProjectSettings.xml

Every setting must have a description here (the script fails otherwise); docs/FELLOWENGINE_OPTIONS.md and the decision
records in docs/decisions/ say more.
"""

import re
import sys

EXACT = "Exact: the same image or results as stock Godot."
STARTUP = "Read at start-up."

D = {
    # ---------------------------------------------------------------- the switches
    "enabled": "FellowEngine's master switch: off, every FellowEngine setting below is off and the engine behaves as stock Godot 4.7.2 (the nodes FellowEngine adds do nothing where they need its renderer).",
    "postfx/enabled": "FellowEngine's post-processing optimizations and effects ([code]postfx/*[/code], [code]post/*[/code]); off: stock's.",
    "shadows/enabled": "FellowEngine's shadow optimizations ([code]shadows/*[/code]); off: stock's.",
    "geometry/enabled": "FellowEngine's geometry optimizations ([code]geometry/*[/code]: mesh clusters, MultiMesh culling, contribution culling); off: stock's.",
    "lights/enabled": "FellowEngine's light optimizations ([code]lights/*[/code]: the light cluster build, tile culling); off: stock's.",
    "cpu/enabled": "FellowEngine's render-thread optimizations ([code]cpu/*[/code]: render lists, culling and recording on worker threads). " + EXACT + " Decisions 0016-0018.",
    "fluid/enabled": "FellowEngine's simulations: FellowWater3D's waves, FellowShallowWater3D, FellowRain3D's wet surfaces and ripples, FellowGasVolume3D and FellowBurnable3D. Off: the nodes stay, nothing is simulated.",
    # ---------------------------------------------------------------- visual features (post/*)
    "post/motion_blur/enabled": "Motion blur after TAA: a reconstruction filter over the velocity buffer, the sky blurred by the camera's motion. Works with TAA, MSAA or no anti-aliasing; not with FSR 2 or in XR. Decision 0078.",
    "post/motion_blur/amount": "The share of the frame the shutter is open (0.5: half a frame's motion).",
    "post/motion_blur/samples": "Samples along each pixel's motion: more, smoother blur, more cost.",
    "post/motion_blur/max_radius": "The longest blur, in pixels.",
    "post/sharpen/strength": "Contrast-adaptive sharpening (RCAS) of the TAA output, in the tone mapping pass; only with TAA. 0: off. Decision 0110.",
    "post/bloom/enabled": "An energy-conserving bloom of the whole image (no threshold: bright sources glow in proportion), independent of Godot's glow. Decision 0112.",
    "post/bloom/intensity": "How much of the bloom is mixed into the image.",
    "post/bloom/scatter": "How wide the bloom spreads (the weight of its larger levels).",
    "post/bloom/lens_dirt": "The environment's glow map used as a lens dirt mask, added where the bloom (or Godot's glow) is. Needs a glow map. 0: off. Decision 0111.",
    "post/bloom/lens_flare": "Lens flare ghosts and a halo of the sources brighter than [member rendering/fellow/post/bloom/lens_flare_threshold]. Needs the bloom. 0: off. Decision 0113.",
    "post/bloom/lens_flare_threshold": "The scene-linear brightness above which a source makes lens flares.",
    "post/light_shafts/intensity": "Rays from the sun: the bright sky around it blurred towards it, broken by roofs, trees and clouds. 0: off. Decision 0116.",
    "post/light_shafts/threshold": "The sky's brightness from which it casts shafts.",
    "post/light_shafts/length": "How far the shafts reach across the screen (0-1).",
    "post/white_balance/temperature": "White balance in kelvin, as a camera's: 6500 K is neutral, 8000 K warms a sky-lit scene, 5000 K cools it. Decision 0114.",
    "post/white_balance/tint": "White balance's tint: positive towards magenta, negative towards green.",
    "post/clouds/enabled": "Volumetric clouds over the sky's pixels (ray-marched at half resolution, temporal), lit through the atmosphere when it is on, with their shadows on the scene. A FellowWeather3D sets their coverage and density. Decision 0109.",
    "post/clouds/coverage": "The share of the sky the clouds cover (0-1).",
    "post/clouds/density": "The clouds' density: thicker, darker clouds above 1.",
    "post/clouds/altitude": "The cloud layer's base, in kilometres.",
    "post/clouds/thickness": "The cloud layer's thickness, in kilometres.",
    "post/clouds/wind": "How fast the clouds drift, in kilometres per second.",
    "post/clouds/interleaved": "A quarter of the clouds' pixels traced per frame, the rest from the history: about a quarter of the cost. Decision 0115.",
    "post/clouds/shadows": "The clouds' shadows on the scene's surfaces (a map of the sun through the layer, 16 km around the camera). 0: none. Decision 0133.",
    "post/clouds/sky_light": "The sky's ambient light and reflections greyed where the clouds cover it (0: not). Decision 0133.",
    "post/atmosphere/enabled": "A physically based sky atmosphere (Hillaire 2020): tables for a sky shader (global shader parameters [code]fellow_atmosphere_*[/code]), aerial perspective on the opaque surfaces, the sun's colour through the air. Decision 0108.",
    "post/atmosphere/aerial_perspective": "Distant surfaces hazed and tinted by the air between them and the camera.",
    "post/atmosphere/sun_transmittance": "The sun's light coloured by the atmosphere it passes through (reddened low on the horizon).",
    "post/atmosphere/aerial_distance_scale": "Scales the distances the aerial perspective uses: above 1, a hazier world at the same size.",
    "post/atmosphere/mie_scale": "Scales the aerosols' (Mie) scattering: 10-30 for a typical day's haze.",
    "post/atmosphere/rayleigh_scale": "Scales the air's (Rayleigh) scattering: the sky's blue.",
    "post/atmosphere/ground_albedo": "The ground's reflectance the atmosphere's multiple scattering sees.",
    "post/atmosphere/srgb_rayleigh": "Rayleigh coefficients for sRGB primaries; off: Unreal's, a bluer sky.",
    "post/contact_shadows/enabled": "Screen-space contact shadows of the first directional light: what its shadow map misses where objects touch. Needs the depth pre-pass, no MSAA. Decision 0107.",
    "post/contact_shadows/length": "How far each pixel marches towards the sun, in metres.",
    "post/contact_shadows/steps": "Steps of the march.",
    "post/contact_shadows/thickness": "The thickness assumed for what the march passes behind, in metres.",
    "post/exposure/histogram": "With auto exposure on (the camera attributes): eye adaptation from a histogram of the image's luminance, the darkest and brightest pixels ignored, adapted in stops. Decision 0105.",
    "post/exposure/low_percent": "The darkest share of the pixels the histogram ignores.",
    "post/exposure/high_percent": "The share of the pixels below the brightest ones the histogram ignores (90: the brightest 10 %).",
    "post/exposure/speed_up": "How fast the exposure rises (in stops per second) when the scene gets darker.",
    "post/exposure/speed_down": "How fast the exposure falls (in stops per second) when the scene gets brighter.",
    "post/local_exposure/enabled": "Local exposure (Unreal 5's): large-scale contrast compressed around middle grey, local detail kept. Decision 0106.",
    "post/local_exposure/highlight_contrast": "The contrast kept above middle grey (below 1 compresses the highlights).",
    "post/local_exposure/shadow_contrast": "The contrast kept below middle grey (below 1 lifts the shadows).",
    "post/local_exposure/detail_strength": "Local detail's strength (1: as it is).",
    "post/local_exposure/blurred_blend": "The blend between the bilateral grid (0) and the wide blur (1) for each pixel's surroundings.",
    "post/local_exposure/blur_size": "The wide blur's size, as a share of the screen.",
    "post/lens/chromatic_aberration": "Colour fringes towards the screen's edges, in the tone mapping pass. 0: off. Decision 0079.",
    "post/lens/vignette": "Darker corners, in the tone mapping pass. 0: off.",
    "post/lens/vignette_smoothness": "How gradually the vignette darkens.",
    "post/lens/film_grain": "Per-frame grain in the mid-tones, in the tone mapping pass. 0: off.",
    # ---------------------------------------------------------------- ray tracing
    "raytracing/enabled": "Hardware ray tracing with ray queries (a GPU with [code]VK_KHR_ray_query[/code]): acceleration structures for the meshes and MultiMesh instances near the camera. Alone it changes nothing; the effects below use it. " + STARTUP + " Decision 0080.",
    "raytracing/radius": "Instances within this distance of the camera are in the ray tracing scene.",
    "raytracing/skinned": "Skinned and blend shape meshes in the ray tracing scene, rebuilt when they change.",
    "raytracing/multimesh": "MultiMesh instances in the ray tracing scene.",
    "raytracing/multimesh_min_angle": "MultiMesh instances seen under a smaller angle from the camera are left out of the ray tracing scene.",
    "raytracing/ambient_occlusion": "Ray-traced ambient occlusion in place of SSAO where the environment enables SSAO (its radius, intensity, power). Decision 0081.",
    "raytracing/ao_rays": "Ambient occlusion rays per pixel.",
    "raytracing/ao_interleaved": "A quarter of the pixels traced per frame, the others from the history.",
    "raytracing/sun_shadows": "The first directional light's shadow traced per pixel within half the ray tracing radius, soft over the light's angular size, in place of its shadow map there. Decision 0082.",
    "raytracing/sun_shadow_rays": "Shadow rays per pixel and frame.",
    "raytracing/sun_shadows_interleaved": "A quarter of the pixels traced per frame, the others from the history clamped to their traced neighbours.",
    "raytracing/gi": "Dynamic diffuse global illumination: a volume of ray-traced probes around the camera (sun, sky, emission, several bounces) in place of the ambient light. Decision 0083.",
    "raytracing/gi_probe_spacing": "The distance between the probes, in metres.",
    "raytracing/gi_probes_xz": "Probes along each horizontal side of the volume.",
    "raytracing/gi_probes_y": "Probes along the volume's height.",
    "raytracing/gi_rays": "Rays per probe update.",
    "raytracing/gi_hysteresis": "How much of a probe's previous irradiance each update keeps: higher, steadier and slower to change.",
    "raytracing/gi_energy": "Scales the probes' light.",
    "raytracing/gi_sky_leak": "The share of the ambient light kept where the probes are dark.",
    "raytracing/gi_update_interval": "Frames between a probe's updates (a share of the probes traced per frame).",
    "raytracing/gi_cascades": "2: a coarser volume (4 times the spacing, updated half as often) around the fine one, read where the fine one ends.",
    "raytracing/gi_probe_relocation": "Probes moved out of geometry and away from surfaces, within their cell.",
    "raytracing/reflections": "Reflections traced against the ray tracing scene in place of SSR where the environment enables SSR: off-screen and hidden objects reflected. Decision 0084.",
    "raytracing/reflections_max_roughness": "Surfaces up to this roughness get traced reflections.",
    "raytracing/reflections_interleaved": "Surfaces rougher than 0.3 traced in a quarter of their pixels per frame.",
    # ---------------------------------------------------------------- animation
    "animation/elide_modifier_backup": "Skeletons skip the bone backup around modifiers that change nothing. " + EXACT + " Decision 0076.",
    "animation/full_weight_slerp": "A rotation blended at weight 1 is copied, without the slerp's trigonometry. " + EXACT + " Decision 0076.",
    "animation/track_shortcuts": "Animation tracks' lookups done once per track instead of per key. " + EXACT + " Decision 0076.",
    "animation/shared_no_override_pose": "A skeleton without global pose overrides copies each bone's global pose into the one without overrides instead of computing it again. " + EXACT + " Decision 0163.",
    "animation/lazy_simulator_poses": "The compatibility PhysicalBoneSimulator3D of a skeleton without physical bones reads the bones' global poses when a physical bone is bound instead of copying them after every update. The same physical bone poses, except for one bound and simulated before the skeleton's next update in the frame it was bound. Decision 0163.",
    "animation/key_hints": "Animation tracks start their key search where the last one ended. " + EXACT + " Decision 0076.",
    "animation/batched_skin_transforms": "A skin's bone transforms handed to the RenderingServer in one call instead of one per bone (with the separate render thread, animated crowds much faster). " + EXACT + " Decision 0077.",
    # ---------------------------------------------------------------- physics
    "physics/distance_face_cull": "GodotPhysics3D's shape casts and motion tests skip the trimesh faces a conservative test proves out of reach. " + EXACT + " Decision 0085.",
    "physics/distance_face_order": "The faces left by [member rendering/fellow/physics/distance_face_cull] tested in an order that finds a contact sooner. " + EXACT + " Decision 0085.",
    "physics/distance_face_batch": "Faces tested together by [member rendering/fellow/physics/distance_face_cull].",
    "physics/convex_vertex_cache": "GodotPhysics3D's separating-axis tests transform a convex shape's vertices once per test instead of once per axis. " + EXACT + " Decision 0086.",
    "physics/parallel_pair_culls": "GodotPhysics3D's broadphase culls run on worker threads; the pairs are then made in stock's order. " + EXACT + " Decision 0087.",
    "physics/jolt_deferred_sensor_contacts": "Jolt: Area3D contacts recorded per thread during the step and evaluated after it, instead of under one lock. Decision 0088.",
    "physics/parallel_island_solve": "GodotPhysics3D: a large island solved on every thread, each body updated in stock's order. " + EXACT + " Decision 0089.",
    "physics/parallel_island_solve_grab": "Constraints a thread takes at a time in [member rendering/fellow/physics/parallel_island_solve].",
    "physics/jolt_max_jobs": "Jolt: the most jobs per phase of its step; -1: by the work (one per [member rendering/fellow/physics/jolt_bodies_per_job] active bodies, at least 4). Jolt's results do not depend on it. Decision 0090.",
    "physics/jolt_bodies_per_job": "Jolt: active rigid bodies per job when [member rendering/fellow/physics/jolt_max_jobs] is -1.",
    "physics/island_constraint_lists": "GodotPhysics3D builds islands from a contiguous list of each body's constraints. " + EXACT + " Decision 0091.",
    "physics/jolt_job_pumps": "Measured and rejected (no consistent gain): Jolt's jobs through a lock-free queue drained by pump tasks. Decision 0092.",
    "physics/jolt_pump_spins": "Spins of a pump task with [member rendering/fellow/physics/jolt_job_pumps] before it sleeps.",
    "physics/motion_query_cache": "A motion test repeated within one [code]move_and_slide()[/code] is answered from the first. " + EXACT + " Decision 0093.",
    "physics/distance_face_shape_axes": "The face test of [member rendering/fellow/physics/distance_face_cull] uses a capsule or sphere itself, not its box, and the faces' edge axes. " + EXACT + " Decision 0096.",
    "physics/distance_face_cull_cache": "A concave shape's faces culled once per box within one motion query. " + EXACT + " Decision 0097.",
    "physics/ray_prune": "A raycast through a concave shape skips the tree's nodes beyond its closest hit. " + EXACT + " Decision 0098.",
    "physics/distance_face_sweep_reuse": "Faces proven out of reach of a sweep are not tested again for the sweeps within it. " + EXACT + " Decision 0100.",
    "physics/fast/jolt_shape_cast": "Fast mode, not stock's results: Jolt's motion queries ([ShapeCast3D], [code]move_and_slide()[/code]) by one shape cast per body instead of a bisection of collision tests. Deterministic. Decision 0095.",
    "physics/fast/godot_toi": "Fast mode, not stock's results: GodotPhysics3D's motion queries by the time of impact instead of an 8-step bisection. Deterministic. Decision 0102.",
    "physics/verify": "Debug: every face [member rendering/fellow/physics/distance_face_cull] skips is tested anyway, and those found touching are counted.",
    # ---------------------------------------------------------------- TAA and post-processing (exact unless noted)
    "postfx/taa_resolve_v2": "FellowEngine's TAA resolve (reverse-Z dilation, YCoCg variance clipping): a look change from stock's TAA, on from the first milestone. Decision 0001.",
    "postfx/taa_tile16": "Measured and rejected (slower): the TAA resolve in 16 x 16 workgroups. Decision 0023.",
    "postfx/taa_strided_load": "Measured and rejected (no gain): TAA's tile loaded by all 64 invocations of a workgroup. Decision 0075.",
    "postfx/taa_history_pingpong": "TAA alternates two history textures instead of copying the history and velocity each frame. " + EXACT + " Decision 0002.",
    "postfx/taa_shared_velocity": "The TAA resolve reads its tile's velocities from shared memory. " + EXACT + " " + STARTUP + " Decision 0044.",
    "postfx/ss_last_frame_direct": "SSIL's copy of the last frame without its unread full-size level. " + EXACT + " Decision 0044.",
    "postfx/taa_direct_output": "Post-processing reads the TAA resolve's output directly (one full-resolution copy less). " + EXACT + " Decision 0033.",
    "postfx/taa_v2_gamma": "TAA v2's variance clipping box, in standard deviations: smaller, less ghosting and more flicker.",
    "postfx/taa_v2_feedback_min": "TAA v2's least history weight (moving or changing pixels).",
    "postfx/taa_v2_feedback_max": "TAA v2's most history weight (still pixels).",
    "postfx/taa_v2_antiflicker": "TAA v2: more history where the neighbourhood's contrast is high, against flicker (0: off).",
    "postfx/taa_v2_velocity_dilation": "TAA v2 takes the nearest neighbour's velocity (reverse-Z dilation): edges move with their objects.",
    "postfx/taa_v2_motion_soften": "TAA v2: how much moving pixels soften towards the current frame.",
    "postfx/taa_v2_soften_contrast": "TAA v2: the contrast from which motion softening applies.",
    "postfx/taa_camera_motion_vectors": "Measured and rejected (within noise, not exact): static geometry's motion vectors from the depth buffer. Decision 0013.",
    "postfx/ssao_shared_tile": "Measured and rejected (no effect): SSAO's gather from a tile in shared memory. Decision 0011.",
    "postfx/glow_blur": "Glow's blur: 0 stock's, 1 padded shared memory, 2 16 x 16 tiles without bank conflicts (glow about 60 % faster). " + EXACT + " Decision 0052.",
    "postfx/ssr_filter_padded": "Measured and rejected (no gain): SSR's roughness filter with a padded shared layout. Decision 0052.",
    "postfx/screen_copy_mipmaps_on_demand": "The screen texture's copy builds its mipmaps only when a material drawn this frame samples them. " + EXACT + " Decision 0123.",
    "postfx/depth_copy_nearest_mipmaps": "A material sampling [code]hint_depth_texture[/code] with a mipmap filter gets mip levels of the nearest depth of their cells (a Hi-Z pyramid; FellowWater3D's reflections trace over it); off: stock's single level. Decision 0126.",
    "postfx/tonemap_specialized": "Tone mapping compiled for the frame's paths only (FXAA, glow, tone mapper, ...): the pass 12-14 % faster. " + EXACT + " Decision 0055.",
    "postfx/fog_specialized": "Volumetric fog's density pass without the loops the frame does not run: the pass 21-27 % faster. " + EXACT + " Decision 0056.",
    "postfx/fog_light_loop": "Measured and rejected (no frame gain): volumetric fog's light loops walked by the subgroup. Decision 0053.",
    "postfx/ss_coherent_gather": "Measured and rejected (slower): SSAO and SSIL gathers with one sample rotation per workgroup. Decision 0024.",
    "postfx/fast_luminance_reduce": "Auto exposure's luminance reduction with two barriers per workgroup instead of seven (the same sums). " + EXACT + " Decision 0025.",
    "postfx/ss_interleave_exact_bounds": "SSAO and SSIL write their last column at widths such as 1920, which stock leaves unwritten (a correctness fix). Decision 0067.",
    "postfx/ss_depth_mips_exact": "SSAO and SSIL depth mipmaps without stock's racing stores (a correctness fix). Decision 0067.",
    "postfx/ssr_resolve_shared": "Measured and rejected (no gain): SSR's resolve with its samples shared per workgroup. Decision 0074.",
    "postfx/ssao_temporal": "Visually lossless, off by default: SSAO takes half its taps per frame, the other half from the previous frame (reprojected, depth-checked). Decision 0028.",
    "postfx/ssil_temporal": "Visually lossless, off by default: SSIL takes half its taps per frame, the other half from the previous frame. Decision 0030.",
    "postfx/ss_tap_batch": "Measured and rejected (no gain): SSAO and SSIL taps fetched a few at a time (0 or 1: stock's loop). " + STARTUP + " Decision 0041.",
    "postfx/fog_half_rate": "Visually lossless, off by default: volumetric fog lights half its froxels per frame (a checkerboard), the rest reprojected. Decision 0031.",
    "postfx/verify_luminance_reduce": "Debug: stock's luminance reduction also runs every frame and is compared with FellowEngine's.",
    # ---------------------------------------------------------------- fluids
    "fluid/gas_rendering": "FellowGasVolume3D's smoke and fire drawn (Forward+). Off: simulated, not drawn.",
    "fluid/gas_scene_shadows": "The sun's shadow map darkens the smoke.",
    "fluid/gas_solver": "For measurements: the gas's pressure solver, 0 each node's own, 1 SOR, 2 multigrid.",
    "fluid/gas_mg_tail_cells": "For measurements: multigrid levels of at most this many cells solved in one workgroup (0: off; measured slower). Decision 0119.",
    "fluid/gas_mg_fine_sweeps": "The gas multigrid's red-black Gauss-Seidel sweeps before and after the finest level's correction (2 as decision 0117; 1 halves the finest level's smoothing, the cost at small grids, for a larger residual). Not exact unless 2. Milestone 61.",
    "fluid/gas_sparse": "For measurements: 0 each node's own sparse setting, 1 every gas cell simulated, 2 only the bricks near smoke, heat or an emitter. Decision 0119.",
    "fluid/gas_sparse_margin": "For measurements: the sparse simulation's margin in bricks (0: each node's own).",
    "fluid/gas_profile_passes": "Profiling: a GPU timestamp before each group of a gas step's passes.",
    "fluid/burn_vertices_cached": "A burning mesh's vertices march into its voxels once per frame in a compute pass, instead of in every pass that draws it. Decision 0120.",
    # ---------------------------------------------------------------- profiling
    "profiling/pass_timestamps": "Profiling: the renderer's per-pass timestamps kept on (the visual profiler's and the benchmarks' pass times).",
    "profiling/list_markers": "Profiling: extra timestamps inside the render list and cull steps (they split stock's regions).",
    # ---------------------------------------------------------------- shadows
    "shadows/directional_cache": "Directional shadows: each cascade's static casters cached in a guard-banded map, the moving ones drawn over a copy each frame. " + EXACT + " Decision 0004.",
    "shadows/directional_cache_guard_texels": "The cached cascade's guard band on each side, in texels: how far the cascade moves before its cache is rebuilt (and its memory).",
    "shadows/directional_cache_static_frames": "Frames a caster must stay unchanged to count as static (cached).",
    "shadows/directional_cache_lod_travel": "0: cached casters' LODs from the shadow map's texel size ([member rendering/fellow/shadows/directional_cache_texel_lod]); above 0, stock's camera-distance LODs as if the camera were that much closer, the cache rebuilt after that much travel.",
    "shadows/directional_cache_texel_lod": "Cached casters use the coarsest LOD whose error is below this many shadow-map texels.",
    "shadows/directional_cache_max_rebuilds_per_frame": "The most cascade caches rebuilt in one frame.",
    "shadows/directional_cache_min_cost_per_texel": "A cascade is served from the cache only when the static casters it saves cost more than the copy (their primitives and draws against this many per cascade texel): cheap cascades are drawn as stock does.",
    "shadows/directional_cache_debug_mode": "Debug: 0 normal, 1 the lattice without the cache, 2 every cache rebuilt every frame.",
    "shadows/wind_cached_from_cascade": "Casters moved only by the wind are cached from this directional cascade on (their far shadows keep the pose of the cache's last build); -1: never. Decision 0125.",
    "shadows/skip_backfacing_directional": "Directional shadow lookups skipped where the surface faces away from the light. " + EXACT + " Decision 0005.",
    "shadows/skip_unblended_cascade": "The second cascade's lookup skipped outside the blend band. " + EXACT + " Decision 0005.",
    "shadows/skip_backfacing_positional": "Omni and spot shadow lookups skipped where the surface faces away from the light. " + EXACT + " Decision 0005.",
    "shadows/pcss_blend_band": "A look change, off: soft directional shadows blended across cascades in a band. Decision 0005.",
    "shadows/pcss_directional_penumbra": "A look change, off: the directional light's soft shadow penumbra as a parallel light's. Decision 0005.",
    "shadows/directional_max_mip": "Soft directional shadows: a min/max pyramid of the shadow map lets fully lit and fully shadowed pixels skip the filter. " + EXACT + " Decision 0005.",
    "shadows/positional_max_mip": "The same pyramid for omni and spot shadows. " + EXACT + " Decision 0005.",
    "shadows/max_mip_visible_region": "The directional pyramid built only over the part of each cascade the camera can look up. " + EXACT + " Decision 0005.",
    "shadows/atlas_simulated_time": "At a fixed frame rate ([code]--fixed-fps[/code], Movie Maker) the positional shadow atlas waits 500 ms of the scene's time, not the wall clock, before resizing a light's shadow: the same frames every run. Decision 0071.",
    "shadows/pcss_filter_early_out": "Measured and rejected (no gain): directional PCSS decided from the pyramid after the blocker search. Decision 0039.",
    "shadows/batched_taps": "Soft shadow filter taps fetched four at a time, added in stock's order: frame 2-5 % faster. " + EXACT + " Decision 0040.",
    "shadows/pcss_quad_search": "Measured and rejected (not visually lossless): the PCSS blocker search shared by 2 x 2 pixel quads. Decision 0042.",
    "shadows/debug_disable_caster_culling": "Debug: shadow casters drawn without FellowEngine's culling.",
    "shadows/contribution_cull_texels": "Changes the image, off: a caster whose box spans fewer than this many texels of a directional cascade is not drawn into it (0: off). Decision 0121.",
    # ---------------------------------------------------------------- geometry
    "geometry/mesh_clusters": "Large static surfaces split into clusters culled one by one against the view. " + EXACT + " Decision 0007.",
    "geometry/cluster_blended_surfaces": "Transparent surfaces clustered too, their draw order kept.",
    "geometry/cluster_triangles": "The most triangles in a cluster.",
    "geometry/cluster_min_surface_triangles": "Surfaces with at least this many triangles are clustered.",
    "geometry/cluster_front_to_back": "Measured and rejected (no frame gain): large surfaces' clusters drawn front to back. Decision 0058.",
    "geometry/cluster_lod": "Changes the image, off: each cluster of a large surface drawn at the mesh LOD it would get as an instance of its own, without cracks. Decision 0063.",
    "geometry/occlusion_culling": "Measured and rejected (slower in most scenes): the opaque pass culled against a depth pyramid. Decision 0026.",
    "geometry/multimesh_culling": "MultiMesh instances culled one by one on the GPU for each pass (camera, shadows) and drawn indirectly. " + EXACT + " Decision 0034.",
    "geometry/multimesh_instance_lod": "Changes the image, off: each culled MultiMesh instance gets its own mesh LOD level instead of its MultiMesh's. Decision 0035.",
    "geometry/multimesh_cull_vertex_motion": "Changes the image where a material moves vertices beyond its node's extra cull margin, off: MultiMeshes whose material moves vertices (wind) culled too, the margin taken as how far they move. Decision 0036.",
    "geometry/multimesh_cull_min_instances": "MultiMeshes with fewer instances are not culled per instance.",
    "geometry/multimesh_cull_min_indices": "A pass's culled MultiMeshes drawing fewer indices than this with every instance are drawn the stock way (culling them would cost about what it saves).",
    "geometry/prepass_simple_first": "Measured and rejected (no gain): the depth pre-pass draws the shared-depth-material elements first. Decision 0029.",
    "geometry/contribution_cull_pixels": "Changes the image, off: nodes and MultiMesh instances whose bounding sphere covers fewer than this many pixels of the viewport's width are not drawn (0: off). Decision 0121.",
    "geometry/contribution_cull_instances": "MultiMesh instances below [member rendering/fellow/geometry/contribution_cull_pixels]: 0 culled, 1 thinned (a share kept and grown so their coverage stays).",
    # ---------------------------------------------------------------- frame
    "frame/pipeline_fallback": "Measured and rejected: while a pipeline compiles, a compiled one differing in bits that give the same image drawn instead of the ubershader. Decision 0059.",
    "frame/depth_pipeline_keys": "The depth passes' pipeline keys without bits their code does not read: fewer duplicate pipelines. Decision 0069.",
    "frame/clear_new_textures": "A texture a pass can write, created without data, is cleared before its first use (as stock on Direct3D 12): no image depends on what memory held. Decision 0070.",
    "pipelines/record": "Records the pipelines the materials' shaders compile while the game runs (Forward+), each with its shader's code hash and its key's vertex and framebuffer formats described, into [member rendering/fellow/pipelines/path], merged with what the file holds and written a few seconds after something new (a game stopped from the editor is killed, not quit). Not in the editor itself; an exported game that cannot write its own files records into [code]user://[/code]. Play the game through its weathers, lights and places once with it on, then turn it off. Decision 0171.",
    "pipelines/replay": "When a material's shader is created at load, compiles the pipelines a recorded run compiled for it ([member rendering/fellow/pipelines/path], when it exists), in the background before a draw asks for them: what a play-through met later (another weather, a light, autotune's variants) is ready at its first frame. A file recorded with another scene shader (another version of the engine) or other rendering settings (its source and defines differ) is not replayed. Nothing without the file. Decision 0171.",
    "pipelines/path": "The recorded pipelines' file (JSON), read to replay and written to record. In [code]res://[/code] it is exported with the game. Decision 0171.",
    "frame/stable_scene_bits": "The colour passes' specialization bits that follow what a frame holds (absent decals, probes and lights, the positional lights' loop variants and tile culling, the weather's wetness, the clouds' shadow) take the value that compiles the feature's code once the feature exists or has been in a frame, and keep it: no new pipelines for every material in view when a light switches on or the rain starts. The same image either way. Decision 0161.",
    "frame/movie_wait_pipelines": "While Movie Maker mode writes a video, a 3D draw waits for its pipeline instead of drawing with the ubershader: the same recording every run. Decision 0072.",
    "frame/async_ss_split": "With async compute: 0 stock's split, 1 SSIL on the main queue beside the async SSAO, 2 SSR too, 3 autotune decides SSR. Decision 0060.",
    "frame/early_submit": "Measured and rejected: the frame's command buffers submitted early. Decision 0009.",
    "frame/present_thread": "Presentation on a thread of its own. Decision 0017.",
    "frame/parallel_shader_create": "Shaders read from the shader cache created in parallel: scenes load faster. Decision 0061.",
    "frame/lazy_shader_variants": "Shader variants created only when first asked for: scenes load faster. Decision 0061.",
    "frame/lazy_shader_variants_fill": "The variants left for later by frame/lazy_shader_variants made one by one on a worker thread after the load, so that a material appearing in play (rain's, the rainbow's) does not make its variant on the render thread. Decision 0176.",
    "frame/submit_thread": "Command buffer submission on a thread of its own. Decision 0017.",
    "frame/parallel_replay": "The frame's draw lists replayed into secondary command buffers by worker threads (CPU-bound frames). Decision 0017.",
    "frame/replay_max_segments": "The most segments a draw list is replayed in.",
    "frame/replay_frame_plan": "Off: the frame's draw lists split before the primary command buffer is recorded and replayed by the workers at once. Decision 0019.",
    "frame/replay_plan_min_draws": "With [member rendering/fellow/frame/replay_frame_plan]: lists with at least this many draws replayed as one segment.",
    "frame/separate_present_queue": "Measured and rejected: presentation from a second queue. Decision 0009.",
    "frame/async_compute": "Async compute (SSAO, SSIL, SSR on a second queue beside shadows, clusters and fog): 0 off, 1 on, 2 on NVIDIA GPUs. Read when the device is created (project.godot or override.cfg only). Decision 0015.",
    "frame/async_compute_family": "Diagnostics: 0 a dedicated compute queue family, 1 a second queue of the main family, 2 the main queue itself (the same submissions, nothing concurrent).",
    "frame/kernel_spirv_cache": "FellowEngine nodes' compute kernels (water, rain, snow tracks, shallow water) kept as SPIR-V in user://fellow_kernel_cache after they are first compiled, keyed by their source and the engine's build: GLSL is compiled once per machine and build instead of once per session. The same kernels. Milestone 61.",
    "frame/pipeline_cache_max_mb": "The pipeline cache file is ignored at start-up (and replaced by the next save) when larger than this: stock lets it grow, and at 2 GiB its checksum crashed. Decision 0050.",
    # ---------------------------------------------------------------- lights
    "lights/cluster_depth_slices": "Light clusters with logarithmic depth slices and per-element depth ranges. " + EXACT + " Decision 0008.",
    "lights/zero_attenuation_skip": "Lights with zero attenuation at a pixel skipped. " + EXACT + " Decision 0008.",
    "lights/cluster_pack_parallel": "The cluster pack pass with one workgroup per tile. " + EXACT + " Decision 0014.",
    "lights/cluster_pack_elements": "The cluster build packs one invocation per element and leaves its 21.5 MB buffer cleared, instead of clearing it every frame. " + EXACT + " Decision 0051.",
    "lights/cluster_instanced_draws": "The light cluster build's proxy draws instanced. " + EXACT + " Decision 0051.",
    "lights/fields_early": "Omni and spot lights' fields read together at the start of the light's function. " + EXACT + " Decision 0045.",
    "lights/tile_cull": "Per 8 x 8 tile, after the depth pre-pass, the omni and spot lights that can reach its geometry; the opaque pass visits only those. Under autotune: off until measured faster. " + EXACT + " Decision 0049.",
    "lights/hot_fields_buffer": "Measured and rejected: the light loop's fields from a copy in a storage or uniform buffer. Decision 0047.",
    "lights/prefetch_next": "Measured and rejected (more registers, slower): the next light's fields read while the current one is processed. Decision 0046.",
    # ---------------------------------------------------------------- shading
    "shading/absent_features_specialized": "The colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run (bits per feature; 63: all): GPU 5-9 % faster. " + EXACT + " Decision 0057.",
    "shading/blended_alpha_zero_skip": "Mix-blended pixels with alpha 0 skip their lighting. " + EXACT + " Decision 0010.",
    "shading/adaptive_vrs": "Visually lossless, off by default: variable rate shading per 16 x 16 tile, chosen from the previous frame's image; autotune keeps it only where it saves time. Needs [code]VK_KHR_fragment_shading_rate[/code], one view, the depth pre-pass, no MSAA. " + STARTUP + " Decision 0027.",
    "shading/adaptive_vrs_threshold": "Adaptive shading's error budget per tile (luma 0-1); 0: every tile at full rate.",
    "shading/adaptive_vrs_max_rate": "The coarsest rate: 0 2 x 2, 1 4 x 4.",
    "shading/adaptive_vrs_refresh": "Every tile shaded at full rate one frame in this many, so detail lost to coarse shading is seen again (0: never).",
    "shading/adaptive_vrs_max_step": "The largest step between neighbouring pixels a coarse tile may have; 0.01 is the strict setting.",
    "shading/adaptive_vrs_motion_px": "Tiles moving faster than this many pixels per frame shaded at finer rates (0: off).",
    "shading/adaptive_vrs_gloss_guard": "Glossy, curved surfaces kept at full rate.",
    "shading/adaptive_vrs_autotune": "Autotune decides per scene: adaptive shading starts off and is turned on where measured faster.",
    # ---------------------------------------------------------------- render thread
    "cpu/parallel_list_fill": "Render lists filled by worker tasks. " + EXACT + " Decision 0016.",
    "cpu/parallel_instance_data": "Instance data written by worker tasks. " + EXACT + " Decision 0016.",
    "cpu/compact_list_sort": "Render lists sorted on compact records copied out of the surfaces (the same sort). " + EXACT + " Decision 0016.",
    "cpu/parallel_list_sort": "Render lists sorted by worker tasks. " + EXACT + " Decision 0016.",
    "cpu/parallel_partition": "Off: the sort's partitions split across worker tasks. Decision 0016.",
    "cpu/parallel_partition_min": "With [member rendering/fellow/cpu/parallel_partition]: the smallest partition split.",
    "cpu/parallel_recording": "Large draw lists recorded by several threads, joined in order. " + EXACT + " Decision 0018.",
    "cpu/recording_segments_per_thread": "Segments per thread a draw list is recorded in.",
    "cpu/parallel_cull": "The scene cull by worker tasks. " + EXACT + " Decision 0016.",
    "cpu/cull_ranges_per_thread": "Ranges of instances each thread culls (measured slower above 1).",
    "cpu/prefetch": "Off: the list loops load instances and surfaces a few iterations ahead.",
    "cpu/batched_shadow_lists": "Shadow passes' render lists built together. " + EXACT + " Decision 0016.",
    "cpu/compact_shadow_instances": "Shadow passes' instance data compacted. " + EXACT + " Decision 0016.",
    "cpu/verify": "Debug: stock's results computed beside every list build and compared.",
    "cpu/debug_instance_scratch": "Debug: instance data written to system memory instead of the GPU buffer (wrong images).",
    "cpu/instance_buffer_memory": "Measured and rejected: per-frame instance buffers in system memory (0 device memory, as stock). Decision 0021.",
    "cpu/persistent_instances": "One instance data entry per surface kept in device memory and rewritten only when it changes. " + EXACT + " Decision 0022.",
    "cpu/worker_tasks": "Worker tasks of the render thread's parallel loops; -1: automatic (half the logical processors less two, 1-6). Decision 0073.",
    "cpu/hot_workers_usec": "How long, in microseconds, worker threads keep spinning for more of the render thread's work after their last chunk. Decision 0065.",
    "cpu/hot_workers_gate": "The hot workers go cold while the render thread waits for the GPU.",
    "cpu/hot_workers_wait": "How idle hot workers wait: 0 a pause loop, 1-2 UMWAIT (x86 CPUs that have it).",
    "cpu/hot_workers_yield": "Experiment, off: hot workers yield while they spin. Decision 0065.",
    "cpu/hot_workers_idle_priority": "Experiment, off: hot workers at idle priority. Decision 0065.",
    # ---------------------------------------------------------------- autotune
    "autotune/enabled": "Autotune: the scene-dependent exact options switched on and off in alternating blocks of frames and kept the faster way, checked again when the frame time changes. Decision 0020.",
    "autotune/block_frames": "Frames in each block of an evaluation.",
    "autotune/min_gain_percent": "The least gain, as a share of the frame's GPU time, for an option to be switched.",
    "autotune/interval_frames": "Frames between evaluations.",
    "autotune/max_interval_sec": "The longest time between evaluations, in seconds.",
    "autotune/max_duty_percent": "The most of the frames evaluations may take.",
    "autotune/skip_creation_frames": "Frames that compile pipelines or shaders are not counted. Decision 0064.",
    "autotune/continue_undecided": "An undecided evaluation of an option that starts off is continued next when it leans on. Decision 0064.",
    "autotune/prepare_pipelines": "An evaluation that switches a pipeline bit waits until the pipelines of the materials in view are compiled the other way. Decision 0068.",
    # ---------------------------------------------------------------- upscaling
    "upscaling/fsr3": "A viewport in Godot's FSR 2 scaling mode runs AMD's FidelityFX Super Resolution 3.1 upscaler instead of FSR 2.2: less error and flicker in motion. Decision 0148.",
    "upscaling/fsr3_half_precision": "Half precision in FSR 3.1's passes that have it, where the GPU supports it. " + STARTUP,
    "upscaling/fsr3_velocity_factor": "FSR 3.1's velocity factor: 0 steadies bright pixels.",
    "upscaling/fsr3_reactiveness_scale": "FSR 3.1's reactiveness scale: larger reacts faster.",
    "upscaling/fsr3_shading_change_scale": "FSR 3.1's shading change scale: larger reacts faster to lighting changes.",
    "upscaling/fsr3_accumulation_added_per_frame": "FSR 3.1's accumulation added per frame: less ghosts less where the history is rejected.",
    "upscaling/fsr3_min_disocclusion_accumulation": "FSR 3.1's least accumulation on disocclusion.",
    "upscaling/fsr3_debug_view": "FSR 3.1's debug view over the output (motion vectors, depth, reactive masks, the history).",
    # ---------------------------------------------------------------- texture streaming
    "streaming/textures": "Texture streaming: a VRAM-compressed texture with mipmaps loads only its small mips, the rest as the cameras need them, on worker threads. Off: textures load whole, as stock. " + STARTUP + " Decision 0146.",
    "streaming/texture_resident_size": "Mips at or below this size (the larger side) are always resident; smaller textures are not streamed.",
    "streaming/texture_budget_mb": "The streamed textures' memory (0: no limit); over it every streamed texture is coarsened by the same bias.",
    "streaming/texture_lod_bias": "Added to the estimated level: negative finer (a margin), positive coarser.",
    "streaming/texture_instances_per_frame": "Instances evaluated per frame for the levels their textures need.",
    "streaming/texture_max_jobs": "Texture loads at a time.",
    "streaming/texture_upload_mb_per_frame": "The most texture data uploaded per frame.",
    "streaming/texture_release_hold_s": "A texture needing fewer mips keeps them this long before they are released (unless over the budget).",
    "streaming/texture_unreferenced_hold_s": "A streamed texture no instance uses keeps its mips this long, then falls back to its resident mips.",
    "streaming/texture_info_details": "[method RenderingServer.fellow_get_texture_streaming_info] also lists every streamed texture.",
    "streaming/terrain_region_uploads": "FellowTerrain3D's sculpting, painting and blits upload to the GPU only the texels they changed and the mips over them (the heights, the normals, the splat maps), not the whole textures: the same textures. Read once. Decision 0168.",
    "terrain/virtual_texture_atlas_size": "Texels along a side of the atlas every [FellowTerrain3D] with [member FellowTerrain3D.virtual_texture] keeps its baked pages in (two RGBA8 textures: 4080 holds 900 pages of 136 x 136 texels in 127 MiB). Made with the first such terrain, freed with the last; read then. Decision 0178.",
    "terrain/virtual_texture_pages_per_frame": "Pages of the terrains' virtual textures baked in a frame at most (the coarsest and nearest first): the pages a view lacks meanwhile are drawn from a coarser page. Decision 0178.",
    "terrain/virtual_texture_all": "Every FellowTerrain3D drawn from a virtual texture as if its [member FellowTerrain3D.virtual_texture] were on (to compare a project with and without them; read as a terrain enters the tree). Decision 0179.",
    # ---------------------------------------------------------------- diagnostics (debug/*)
    "debug/raytracing_decode_positions": "Diagnostics: compressed vertex positions decoded for the acceleration structures even where the device builds from them.",
    "debug/raytracing_gi_dump_every": "Diagnostics: the DDGI probes written to [code]user://fellow_ddgi_dump.bin[/code] every n updates (stalls the GPU; 0: never).",
    "debug/raytracing_gi_reference": "Diagnostics: n > 0, a path traced per pixel (n bounces, accumulated while the camera stands still) in place of the probes: a reference for DDGI.",
    "debug/raytracing_gi_in_shader": "Diagnostics: DDGI looked up by the opaque passes themselves instead of a screen pass.",
    "debug/raytracing_view": "Diagnostics: with the viewport's SSAO debug draw, the ray tracing scene seen from the camera instead (1 its hit distances, 2 where it matches the depth buffer).",
    "debug/batched_taps_kinds": "Diagnostics: the lights [member rendering/fellow/shadows/batched_taps] applies to (bits: directional, positional).",
    "debug/cluster_verify": "Diagnostics: the light cluster build checked against stock's (2: with a negative control).",
    "debug/cluster_keep_depth_words": "Diagnostics: the light clusters keep their first version's depth words.",
    "debug/tile_cull_passthrough": "Diagnostics: light tile culling keeps every candidate (its code path without its effect).",
    "debug/tile_cull_pass_only": "Diagnostics: light tile culling's pass runs, but the opaque pass does not read its masks.",
    "debug/tile_cull_split": "Diagnostics: light tile culling split around the async compute part (0 when async compute runs, 1 always, 2 never).",
    "debug/segment_barriers": "The render graph's barriers for a resource used again with the same usage after an earlier command buffer of the frame wrote it (0 off, 1 resources, 2 resources and full barriers). Decision 0054.",
    "debug/visibility_barriers": "The render graph's barriers for a read from a stage the last write was not made visible to (a correctness fix). Decision 0062.",
    "debug/occlusion_stats": "Diagnostics: occlusion culling's counts read back.",
    "debug/multimesh_stats": "Diagnostics: MultiMesh culling's instance counts read back (a readback per frame).",
    "debug/scene_shader_experiments": "Diagnostics: experiments on the compiled scene shaders, bits. " + STARTUP + " Decision 0037.",
    "debug/cluster_size": "Diagnostics: the light cluster tile's side in pixels (32: stock's). Decision 0038.",
    "debug/occlusion_cull_approx": "Diagnostics: occlusion culling's per-surface visibility read back.",
}

# Milestone 97 (decision 0205): debug/fellow/*.
DEBUG = {
    "crash_reports/enabled": "Crash reports: a fatal signal or unhandled exception (release templates included) writes a JSON report — the reason, the faulting thread's frames, the last lines of the log, the game's breadcrumbs and values, the machine and GPU, the scene — into [member debug/fellow/crash_reports/directory] before the process ends (on Windows a minidump beside it); the next run finds it ([method FellowCrashReporter.get_pending_report]). Not in the editor. " + STARTUP + " Decision 0205.",
    "crash_reports/directory": "Where the reports are written ([code]user://[/code] or an absolute path). " + STARTUP,
    "crash_reports/log_lines": "The last lines of the log a report keeps (0: none). " + STARTUP,
    "crash_reports/max_reports": "The newest reports kept: older ones (and their minidumps) are deleted at start-up.",
    "crash_reports/hang_seconds": "Hang reports (milestone 98): a watchdog writes a report when the main thread has made no progress for this long — its frames (on Linux and macOS its GDScript frames too; on Windows a minidump) — without stopping the game, and adds how long the stall lasted when it goes on. Not with a debugger attached. 0: off. " + STARTUP + " Decision 0206.",
    "crash_reports/minidumps": "Windows: a minidump beside each crash's report (the threads' stacks and the memory they point to: a few MB), for Visual Studio or WinDbg with the build's PDB. " + STARTUP,
    "crash_reports/upload_url": "Where [method FellowCrashReporter.upload_reports] sends the reports (milestone 102): an [code]http://[/code] or [code]https://[/code] URL taking a POST of [code]multipart/form-data[/code] — the report in [code]report[/code], its minidump in [code]upload_file_minidump[/code] (what Sentry's, BugSplat's and Backtrace's minidump endpoints read), its kind, reason, project, version and commit as fields. Empty: no uploads. Decision 0210.",
    "crash_reports/upload_minidumps": "Windows: a report's minidump sent with it (a few MB each). " + STARTUP,
    "crash_reports/upload_automatically": "The reports waiting in [member debug/fellow/crash_reports/directory] sent to [member debug/fellow/crash_reports/upload_url] on their own, on a thread, two frames after the game starts. Off: the game calls [method FellowCrashReporter.upload_reports] (after asking the player, where its privacy policy wants that). " + STARTUP,
}

PREFIXES = {"rendering/fellow/": D, "debug/fellow/": DEBUG}


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    generated = open(sys.argv[1], encoding="utf-8").read()
    path = "doc/classes/ProjectSettings.xml"
    text = open(path, encoding="utf-8").read()
    described = 0
    for prefix, descriptions in PREFIXES.items():
        p = re.escape(prefix)
        heads = re.findall(r'\t\t<member name="' + p + r'([^"]+)"[^>]*>', generated)
        tags = {name: tag for tag, name in re.findall(r'(\t\t<member name="' + p + r'([^"]+)"[^>]*>)', generated)}
        missing = [h for h in heads if h not in descriptions]
        unknown = [k for k in descriptions if k not in tags]
        if missing or unknown:
            print(prefix, "no description:", missing, "\nnot a setting:", unknown)
            return 1
        block = ""
        for name in sorted(tags):
            block += tags[name] + "\n\t\t\t" + descriptions[name] + "\n\t\t</member>\n"
        # Out with the ones written before, in with the new, before the first member that sorts after them.
        text = re.sub(r'\t\t<member name="' + p + r'[^"]+"[^>]*>\n.*?\n\t\t</member>\n', "", text, flags=re.S)
        members = [(m.start(), m.group(1)) for m in re.finditer(r'\t\t<member name="([^"]+)"', text)]
        at = next(pos for pos, name in members if name > prefix)
        text = text[:at] + block + text[at:]
        described += len(tags)
    open(path, "w", encoding="utf-8", newline="\n").write(text)
    print(f"{described} settings described")
    return 0


if __name__ == "__main__":
    sys.exit(main())
