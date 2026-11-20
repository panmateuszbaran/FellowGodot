# 0304 — The reference's camera smooth over the eave; the tiles smooth clay

Status: **KEEP** (the showcase's: its shot's camera, its tiles' material; nothing in the engine) · Branch
`fellow/geometry` · Code and data:
* `showcase/tools/blender/reference_performance.json`: the camera re-solved in frames 76-85, the climber refitted
  through it;
* `showcase/rooftops/shaders/clay_tile.gdshader` (new);
* `showcase/tools/blender/ac_common.py`: `Builder.coppo`'s UVs, a coppo's lip sharp against its arch;
* `showcase/tools/build_rooftops.gd`: `COPPO`, the coppi's materials.

Checks: the background's image motion frame by frame, the video's beside the render's; the tiles cropped beside the
video's frames 84-93; the 38 animation gates.

## Problem

The owner, watching milestone 162's shot beside the video: "why are the tiles not smooth like the reference's? why does
the camera jump while he pulls himself onto the roof?"

**The camera.** The video's background moves evenly through the mantle: some 13 px a frame down, 2 px left, frame
after frame. Ours, from frame 76 to 87, moved by 6, 26, 9, 18, 20, 13, 18 and 34 px, then 15 px *back* (84 to 85),
then 17. The matchmoved camera (decision 0301) jumped there at 8 m/s and turned at 83°/s for a frame. Its speed is
2 m/s and its turn 10°/s on either side.

That is where the camera rises over the eave and its tracks hand over from the wall to the roof. Frames 82-85 kept only
260-430 tracks, all near one plane, and the free solve's cameras jittered about the right path. The video's motion over
any three frames there sums to ours, but each frame was off. The jump had also thrown good tracks out as outliers: the
solve kept 304 of frame 84's.

**The tiles.** The video's tiles are smooth fired clay with a soft sheen: each tile's arch shaded from its lit crown to
its dark sides, each its own shade. Ours (decisions 0300 and 0303) carried the roof's photograph, blurred. Its grain
and its blotches stayed, box-projected facet by facet on each tile's seven faces, and its roughness map's tiles
speckled the sheen. Up close they read as gravel. Each tile's lip was welded to its arch at one ring of vertices, so
their normals were averaged and each tile was shaded from end to end.

## Change

**The camera** is re-solved against its own points, held still: the structure is kept. Each frame's camera is fitted to
every observation of a reconstructed point, Huber-reweighted, with the path's second differences penalised (place
1 mm, turn 0.5 mrad a frame²).

* Frames 1-75 and 86-96 do not move (under 3 cm, 0.05°).
* Frames 76-85 move by up to 33 cm and 3.6°, onto the even path the video's background takes.
* Frame 84 keeps 500 tracks (304 before), at 1.2 px.
* The camera's acceleration peaks at 12 m/s² (243 before), its turn at 38°/s, which is the run's turn at frame 35
  (83°/s before).

The climber is fitted again through it, the scene's scale held (0.808), so that the house and everything before the
mantle stay as they were. Its joints lie 8.2 px from the video's (8.5 px before).

A joint adjustment that let the points move too was tried and rejected. It bent the scene's late part: the roof's
points turned 10° round, and the wall leaned 6.5° off the ground's normal.

**The tiles**: a shader of their own (`clay_tile.gdshader`), with no photograph:

* each tile's id written into its UVs by `Builder.coppo` (u = 2 × its id + the place across the arch; v along it);
* each tile its own brightness (±14 %) and a leaning to a warmer hue;
* the grime of the valleys and of the crevice under each course's lip (the albedo to 0.72 and 0.62 there; the pans
  at 0.6), so that the courses read as dark steps, as the video's do;
* broad pale patches of weathering over the roof, and a faint mottling of the clay that fades with distance;
* roughness 0.5, specular 0.4: a soft sheen;
* the arch's sides darker where they dip into the valley; the band under the course above's lip in its shadow; the pans
  deep in the valleys;
* the lower end's edge rounded over (its normal leaning down the tile over its last 7 %) with a darker, worn crust.

A coppo's lip is kept sharp against its arch, so its normals no longer shade the whole tile. The three firings' colours
come from the video's tiles: lit crowns about (194, 132, 97) in sRGB.

## Checks

* The background's motion over the whole shot, the video's beside the render's (`zz_flowcmp.py`, the median optical
  flow in the frame's outer thirds, cloud only):
  * its change from frame to frame peaks at 8.3 px, the video's at 8.4 (51.8 before), and averages 1.7 px (the
    video's 1.6, ours 4.4 before);
  * the render's motion differs from the video's by 1.8 px a frame on average (3.1 before), 6.4 at most (29.4).
* The tiles at frames 84, 88, 90, 93 and 94 beside the video's: smooth, sheened, each tile its own shade, the courses'
  steps dark. The lit crowns are (192-203, 132-145, 107-117) beside the video's (180-197, 131-145, 103-114). The
  contrast of their lit to their dark parts is 3.9-5.5, the video's 4.5-4.8 (3.5-3.7 at first, without the
  crevices' grime).
* `reference163_sbs.mp4`: the whole shot beside the video.
* 38 of 38 animation gates, every one at milestone 158's baseline value.

## Decision

KEEP.

## Rejected / not done

* The bundle adjustment with the camera's path held smooth and the points free (above).
* The view from the roof (frames 84-96) is still not the video's. The replica's eave, horizontal and straight, projects
  through the matchmoved camera at a slant where the video's lies level. The matchmove's own points along the eave lie
  0.75 m further back than the replica's measured lip. Next: the camera refined against the replica's own edges (the
  eave, the corner, the window), or the replica built to the matchmove's points there.
