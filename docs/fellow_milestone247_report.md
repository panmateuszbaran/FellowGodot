# FellowEngine — milestone 247 report (the people's small parts)

Status: **FINAL** (2026-11-27). Branch `fellow/geometry`. Decision 0389. No GPU: checked on lavapipe.

## 0. Result

The people's render-thread cost (+1.7 ms for the square's 63 on the RTX) comes from their draw calls: nine meshes a
person in every pass. The five small ones — eyes, eyebrows, eyelashes, teeth, tongue — now cast no shadow (except the
lashes, which shade the eyes) and the mouth's, brows' and lashes' are not drawn far away: seven people side by side
draw 27 % fewer calls, with the same image.

## 1. Changes

* `showcase/scripts/fellow_person.gd`: `parts_lod`, `_small_parts`, `MOUTH_RANGE` (8 m), `FACE_RANGE` (30 m).
* `showcase/tools/person_look.gd`: `--parts=0`; prints the last frame's draw calls and primitives.
* Docs: decision 0389, the changelog.

## 2. Checks

* The cast: 175 → 127 draw calls, 240 162 → 216 990 primitives a frame.
* Marco's face and the cast within the tool's run-to-run noise (51-53 dB against its 54.6 dB between identical runs).

## 3. Note

The showcase's full scene no longer fits the cloud container's memory on lavapipe (7 GB: the square and the osteria are
killed while loading, even at 640×360), so its views wait for the RTX: this milestone's effect on the square, and
whether decision 0388's fitted sun cascades should be on there.
