# FellowEngine — milestone 63 report (recorded pipelines, the burning material baked)

Status: **FINAL** for the cloud's part (2026-11-10): checked on lavapipe, headless; the RTX's confirmation is a soak
when the laptop's GPU may be used. Branch `fellow/geometry`. Decision record: `docs/decisions/0171-recorded-pipelines.md`.

## 0. Result

The owner set the showcase aside and agreed the engine's next three milestones: (1) a play-through's pipelines
recorded and replayed at load, and the burning material baked into exports; (2) foliage fading out with distance, tree
impostors baked offline, holes in the terrain, terrain regions streamed with the world's cells; (3) proxies baked
offline for unloaded cells, mesh LOD streaming. This is (1), the last open items of the readiness row on shader
compilation:

* **Recorded pipelines** (decision 0171, `rendering/fellow/pipelines/*`). A run with `record` on writes every
  pipeline the Forward+ scene shaders compile — each under its material shader's code hash, its formats described
  rather than numbered — to `res://fellow_pipelines.json`; any run with the file (`replay` is on by default) compiles
  a shader's recorded pipelines in the background as soon as the shader is made at load. Slice Lab's 8-minute soak
  (weathers, fire, terrain, foliage, autotune): **18 pipelines compiled in play → 0**, draws with the ubershader
  **268 → 7**. The states check: **28 → 0** after the warm-up, every picture the same bit for bit.
* **The burning material baked** (`FellowBakeShaders::register_object_provider()`): Godot's shader baker asks every
  node of an exported scene for the shaders it will make from its own setup; `FellowBurnable3D` gives its burning
  materials' codes. A baked export compiles **0** shaders at its first start where it compiled the burning material
  before (1).

## 1. Development

* `servers/rendering/renderer_rd/fellow_pipeline_record.{h,cpp}` (new): the record (a table of vertex and framebuffer
  format descriptions, the pipelines per shader hash, merged with the file, deduplicated, written by the render thread
  three seconds after something new and when the renderer finishes; `user://` when `res://` is read-only) and the
  replay (the formats made from their descriptions, the keys per shader). Tied to the scene shader by
  `ShaderRD::fellow_source_sha256()`: its source and the defines the project's settings give it, not the engine's
  commit.
* `forward_clustered/scene_shader_forward_clustered.{h,cpp}`: `fellow_code_hash` (set with the code), the record in
  `_create_pipeline()`, `_fellow_replay_pipelines()` (Godot's `compile_pipeline()`, at low priority) when the code is
  set and again after `set_default_specialization()` clears the pipelines.
* `rendering_device.{h,cpp}`: `fellow_vertex_format_get_attributes()`, `fellow_framebuffer_format_get_description()`;
  `renderer_compositor_rd.cpp`: the writes each frame and at the end; `fellow_settings.{h,cpp}`: the three settings,
  `pipelines_recorded` and `pipelines_replayed` in `RenderingServer.fellow_get_stats()`. Environment overrides for
  tests: `FELLOW_PIPELINES_RECORD`, `FELLOW_PIPELINES_REPLAY`, `FELLOW_PIPELINES_PATH`.
* `servers/rendering/fellow_bake_shaders.h`: providers per object (`register_object_provider()`,
  `get_object_codes()`); `editor/export/shader_baker_export_plugin.cpp` asks them for each node of an exported scene;
  `RendererSceneRenderRD::fellow_create_version_for_baking()` (Forward+) makes a shader version from code for the
  baker; `modules/fellow_fluid/fellow_burnable_3d.{h,cpp}`: `fellow_bake_object_codes()` — the codes
  `_make_burn_material()` writes from the target mesh's materials (its surfaces, the free custom channel, the default
  material where a surface has none).
* `doc/classes/ProjectSettings.xml` (from `doc/tools/fellow_settings_docs.py`), `docs/FELLOWENGINE_OPTIONS.md`.

## 2. Verification

* `pipeline_states_check.gd --stable=0` (lavapipe, 320 × 180): recorded (55 pipelines of 5 shaders), then replayed —
  the 21 states' pictures the same, bit for bit.
* A Linux export (release template, the shader baker on) of a test scene with a burnable log, started from an empty
  user directory: the file exported in the `.pck`, 23 pipelines of 3 shaders replayed (`EXPORT_CHECK`:
  `pipelines_replayed` 63 requests — the default specialization replays them again, and materials sharing a code each
  ask), nothing recorded; no shader compiled at the first start, for the burnable in the scene (`burn_static`) and for
  one added by a script to a scene of the same export (`burn_dynamic`: the same code). The same export without a scene
  containing a burnable baked 753 shader variants instead of 781 and compiled the burning material at the first start.
* The image regression suite (editor build and release template, no file: nothing replayed): every picture exact.
* `xmllint` and `make_rst.py --dry-run`: no errors, no warnings.

## 3. Measurements

**The states check** (`--stable=0`, autotune off; specialized pipelines each state compiled):

| | start | rain | omni | spot | decal | probe | night | omni, rain again | after the warm-up |
|---|---|---|---|---|---|---|---|---|---|
| without the file | 9 | 4 | 3 | 3 | 3 | 8 | 3 | 2, 2 | 28 |
| replayed | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0, 0 | **0** |

**Slice Lab's soak** (lavapipe, 160 × 90, 8 minutes; the editor build): the first run recorded 84 pipelines, the
second replayed them.

| | compiled in the soak | after its warm-up | draws with the ubershader | first frame's waits | load to first frame |
|---|---|---|---|---|---|
| recording (as without the file) | 18 | 8 | 268 | 6, 18.9 s | 41.5 s |
| replayed | **0** | **0** | **7** | 5, 14.6 s | 47.9 s |

The replayed pipelines compile while the scene loads: on lavapipe's two vCPUs, with an LLVM compiler, the load took
6.4 s longer and the first frame waited 4.3 s less. On the RTX laptop's 24 cores they compile beside the load on
worker threads; its numbers are for its next soak.

## 4. Limits

* Forward+ only: the Mobile renderer's scene shader, canvas, sky and particle shaders are not recorded (their
  pipelines are few and made with their shaders).
* A format that depends on the GPU (a VRS method another GPU lacks) makes another framebuffer format there: those
  pipelines compile in play as without the file. A file recorded with another scene shader source or other rendering
  settings is not replayed (a warning).
* The record is as good as the play-through: a weather, light or place it did not visit is compiled in play.
* Measured on lavapipe; the RTX's soak (load time and late pipelines with the record) waits for permission to use its
  GPU.

## 5. Next

Milestone 64: foliage fading out with distance, tree impostors baked offline, holes in the terrain, terrain regions
streamed with the world's cells.
