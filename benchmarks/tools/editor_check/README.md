# Editor check (milestone 50)

FellowEngine's editor support (`modules/fellow_editor`, decision 0151) checked in a real editor:

```
python run_editor_check.py --godot <editor binary> --out <new directory> [--headless]
```

copies `project/` to `<out>/project`, opens it in the editor, and lets its plugin
(`project/addons/fellow_editor_check/plugin.gd`) check, once the first import is done:

* the icons of the 17 FellowEngine classes in the editor's theme;
* `walk.bvh` imported by the `fellow.bvh` importer as an `Animation` (its tracks, the hips' first position, its length);
* `gizmos.tscn`: one FellowEngine gizmo on each of eight nodes, and a picture of the 3D view (`gizmos.png`, not with
  `--headless`);
* `split.tscn`: Project > Tools > Split into Streaming Cells run through its menu item and dialog on 40 boxes; the
  cells, their scenes, their owner, what is left in the level; the cells' content shown in the editor; the saved
  level (no box in it);
* the Performance monitors.

It prints `EDITOR_CHECK {json}` and quits; then `stream_back.gd` loads the saved level in a game and streams it back
(`STREAM_BACK {json}`: every box where it was). `editor_check.json` in `--out` has both, the editor's errors and the
exit codes; the script exits 1 on any failure. `--plain N` opens the project without the check and quits after N
frames (the editor's own log); `--editor-args=--verbose` passes arguments to the editor.

The class reference is checked with the editor's `--doctool` into a copy of the docs, compared with the repository's
(`git diff --no-index`; job f340 in the milestone 50 report).
