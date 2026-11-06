# FellowEngine's image regression suite

`regression.py` renders the scenes of `project/` — one per group of FellowEngine's features — from their cameras with
`tools/fellow_agent` (a fixed frame rate and random seed: the same picture every run of a build on one machine) and
checks each one:

* against its **golden image** (`goldens/<group>/<scene>.png`, rendered by an editor build on lavapipe);
* for **errors** while it rendered (any `ERROR` fails the scene);
* with `--template`, the **release template's picture against the editor build's**: debug builds check what release
  builds do not, and the other way round (milestone 51 found the editor drawing no positional lights since milestone
  10, because every measurement had run release templates);
* for groups with `same_as`, **against another group** (`stock`, FellowEngine off, against `base`: its default
  options are exact, so scenes of stock features render the same either way).

```
xvfb-run -a python tools/fellow_regression/regression.py run --godot bin/godot.linuxbsd.editor.x86_64 \
    --template bin/godot.linuxbsd.template_release.x86_64          # everything
python tools/fellow_regression/regression.py run --godot ... --only base/water,rt      # some
python tools/fellow_regression/regression.py bless --godot ...                          # new goldens (look at them first)
python tools/fellow_regression/regression.py bless --godot ... --goldens DIR            # another GPU or driver:
python tools/fellow_regression/regression.py run --godot ... --goldens DIR              #   goldens of its own, then runs against them
```

It prints a JSON report (and writes `report.json` with the pictures in `--out`) and exits 0, 1 (a check failed) or 2
(it could not run). With `--template` each group's copy of the project is imported by the editor build first, as a
game's project is before it runs on a template. On the cloud's two CPUs a run takes about 4 minutes, 9 with
`--template`.

| group | settings (`suite.json`) | scenes |
|---|---|---|
| `base` | the defaults | `lights` (omni and spot lights with shadows, a decal, a reflection probe), `shadows` (cascades over a 90 m street of casters), `materials` (SSR, SSAO, SSIL, glow, volumetric fog, metal, emission, glass), `water` (FFT ocean), `rain` (drops, wet ground, puddles, a roof's shelter, a rainbow), `snow` (snow cover, a tracker), `lightning` (a night storm's flash), `fire` (a gas volume burning), `geometry` (a FellowHLOD3D town, a MultiMesh of 4000, a large subdivided ground), `instances` (the same without the FellowHLOD3D), `terrain` (a FellowTerrain3D: five levels of detail, four layers, triplanar, height blend) |
| `stock` | `rendering/fellow/enabled=false` | `lights`, `shadows`, `materials`, `instances` — stock features only, which must match `base` exactly (a FellowHLOD3D builds merged proxies with FellowEngine off, instanced ones with it on) |
| `post` | atmosphere, clouds, bloom, light shafts, vignette, local exposure | `sky` |
| `rt` | ray-traced AO, sun shadows, reflections, GI (lavapipe has `VK_KHR_ray_query`) | `raytracing` |

Pictures are 512 × 288, not a quarter of 1080p: at widths like 480, 960 and 1920 stock leaves the last column of
SSAO's and SSIL's output unwritten (decision 0067; FellowEngine writes it), so stock's frame is not defined there and
`stock` could not match `base`.

**Exact.** The default tolerance passes only identical pictures, and on lavapipe every scene renders the same every
run. The rain scene has no splashes: they are GPU sub-emitter particles (stock Godot's), emitted through an atomic
counter in an order that changes between runs on lavapipe's threads (two runs: 20 of 147 456 pixels more than 8 levels
apart; none without splashes). A scene that cannot be exact gets its own tolerance in `suite.json`
(`scene_tolerance`).

**When a check fails**: look at the picture against the golden (`fellow_agent.py compare` writes a difference image).
A change you meant gets new goldens (`bless`, then look at them and commit them with the change and why); one you did
not is a regression. Goldens are lavapipe's (Mesa 25.2, LLVM 20): another Mesa renders slightly differently — use
`--tolerance max_diff,share_over` there, or bless goldens of its own with `--goldens` (not committed).

**Adding a scene**: a `.tscn` in `project/scenes/` with a `Camera3D` (and its own light and environment: the capture
adds none), built the same every run (scripts seed their random numbers; particles with `use_fixed_seed`), its name
in a group of `suite.json`; then `bless --only group/scene`, look at the picture, commit it. Keep it cheap: lavapipe
draws a few million triangles a second.
