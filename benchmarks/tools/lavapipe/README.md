# lavapipe checks (Linux)

Scripts for checks that need no GPU: Mesa's software Vulkan driver (lavapipe) runs the FellowEngine binary built for
Linux (`scons platform=linuxbsd target=template_release disable_path_overrides=no`). lavapipe is deterministic: two
runs of one build give the same images (warm sequences, captures with enough settle frames), so what differs between
runs on a GPU — memory no pass wrote, racing stores — has to be made visible on purpose.

* `ab_capture.sh` — one scene's captures with two sets of settings (an exact option against its absence: 0 pixels).
* `poison_scan.sh` — reads of memory never written: new allocations zeroed against `LVP_POISON_MEMORY=1` (decision
  0067 found SSAO's last column this way). `FELLOW_DEBUG_CLEAR_TEXTURES=a:b` bisects to the texture.
* `repeat.sh` — consecutive frames of a static view; with the per-frame counters it shows a pipeline whose
  specialized image differs from the ubershader's (decision 0057's derivatives after `discard`).
* `tsan_summary.py` — groups ThreadSanitizer reports (a `use_tsan=yes` build, `TSAN_OPTIONS=log_path=...`) by the
  engine frames of the two accesses; reports inside the driver (lavapipe, LLVM) are counted apart.
* `cmp.py` — the comparison the scripts print.

For exactness checks use `$DEMOTE` (`debug/scene_shader_experiments` 4194304: `discard` as demote, derivatives defined)
and a settle long enough that no draw uses the ubershader in the captured frame. Scenes with external assets (Sponza,
Bistro) need the imported project; the labs are procedural. Each script writes `override.cfg` into `$FELLOW_PROJECT`:
run scripts at the same time only with different project copies.
