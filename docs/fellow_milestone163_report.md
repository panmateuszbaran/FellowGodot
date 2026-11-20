# FellowEngine — milestone 163 report (the reference's camera smooth over the eave; the tiles smooth clay)

Status: **FINAL** (2026-11-20). Branch `fellow/geometry`. Everything ran in the cloud (CPU, lavapipe); the laptop's GPU
stays paused at the owner's request.

## 0. Result

The owner: "why are the tiles not smooth like the reference's? why does the camera jump while he pulls himself onto the
roof?" Decision 0304:

* **The camera.** The video's background moves evenly through the mantle, some 13 px a frame. Ours had moved by 6, 26,
  9, 18, 20, 13, 18 and 34 px, then 15 px back. The matchmoved camera jumped at 8 m/s and 83°/s for a frame, where it
  rises over the eave and its tracks hand over from the wall to the roof.

  It is re-solved against its own points, held still, with its path held smooth. Frames 76-85 move by up to 33 cm and
  3.6° onto the even path; the others stay within 3 cm. The climber is refitted through it, with the scene's scale
  held, so the house and the shot before the mantle are as they were.
* **The tiles.** They had carried the roof's photograph, box-projected facet by facet; its grain read as gravel. They
  are now smooth fired clay (`clay_tile.gdshader`, no photograph):
  * each tile its own shade (its id in its UVs, from `Builder.coppo`);
  * a soft sheen;
  * dark valleys and dark crevices under each course, so the courses read as steps;
  * the lower ends rounded over.

  A coppo's lip is kept sharp against its arch: welded, it had shaded the whole tile from end to end.

## 1. Checks (cloud)

* The background's motion over the shot, the video's beside the render's:
  * its frame-to-frame change peaks at 8.3 px, the video's 8.4, and was 51.8 before;
  * the two differ by 1.8 px a frame on average, 3.1 before.
* Frame 84's camera keeps 500 of the video's tracks (304 before), at 1.2 px.
* The tiles beside the video's (frames 84-94): their lit crowns' colour and the contrast of lit to dark as the video's.
* `reference163_sbs.mp4`: the whole shot beside the video.
* `anim_quality.py`: 38 of 38 gates, each at milestone 158's baseline value.
* The laptop: `.tools\bundles\m163.bundle`; `Showcase - Dachy.bat` brings `.tools\wt-fellow` to this milestone. The
  engine (`m156e`) is unchanged.

## 2. Next

* **The view from the roof** (frames 84-96) is not yet the video's. Through the matchmoved camera the replica's
  straight, level eave lies at a slant where the video's is level. The matchmove's own points there lie 0.75 m behind
  the replica's measured lip. To be fixed by refining the camera against the replica's own edges (the eave, the corner,
  the window), or by building the replica to the matchmove's points there.
* The gutter and the rafters under the eave, against the video's.
* The square round the video's house as a set for the shot, if the owner wants it.
