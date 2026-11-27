# 0388 — The sun's cascades fitted to the depths the view shows

Status: **KEEP as an option** (off: it changes the image) · Branch `fellow/geometry` · Setting
`rendering/fellow/shadows/directional_depth_fit` · Code: `servers/rendering/fellow_depth_fit.h` (setting, the ranges'
store and their holding, counters), `renderer_rd/shaders/effects/fellow_depth_range.glsl` and
`renderer_rd/effects/fellow_depth_range.{h,cpp}` (the reduction), `render_forward_clustered.{h,cpp}`
(`_fellow_depth_fit_reduce`, `_fellow_depth_fit_readback`), `renderer_scene_cull.{h,cpp}` (the cascades),
`rendering_light_culler.{h,cpp}` (`fellow_set_directional_distances`)

## Problem

The gap analyses (m230, m239) leave "sun shadows" medium against Unreal's virtual shadow maps, whose texels match the
screen's at every distance. Godot's four cascades split a fixed range — the camera's near plane to the light's shadow
max distance — whatever the view shows: in a courtyard, a street or looking down, the view ends at 10-80 m while the
cascades stretch to 100-200 m, and most of their texels go to depths no pixel has.

## Change

Sample distribution shadow maps (Lauritzen, Salvi and Lefohn, 2011) on Godot's stable cascades:

* **The view's depths.** After the depth pre-pass a compute pass reduces the depth buffer to its nearest and farthest
  opaque surface along the camera's axis (sky left out), read back asynchronously into a store keyed by the render
  buffers (one per viewport). Not with MSAA or in reflection probes.
* **Held.** The store keeps the farthest of the recent readbacks, letting go by 3 % a readback (and the nearest alike),
  so turning back and forth does not narrow and widen the cascades each time; a readback older than the one held is
  dropped.
* **The cascades.** The scene cull narrows the cascades' range to it: the far end so that the farthest surface lies
  before the shadow's fade (`fade_start` of the range) and 10 % beyond, the near end at two thirds of the nearest
  surface, both in quarter-octave steps (a range changing every frame would move the texels: shimmer); never wider
  than stock's. The range is a frame or a few old: it is widened by how far the camera has moved since, and after a
  cut or a turn of more than 15° (what the camera sees now may lie anywhere) the frame is drawn with the stock
  cascades until a range from the new view arrives.
* **The casters.** Godot's light culler keeps, for each cascade, the casters that can shadow its slice of the view — a
  slice it computed from the light's own split offsets and max distance. With the cascades narrowed those were other
  slices: the far cascades lost their casters and came out lit (found in the first test: a courtyard's ground under
  its building came out sunlit). The culler now takes the scene cull's distances when they were fitted.

## Checks (lavapipe, 1280×720, a street of buildings with railings, poles, crates and people-sized capsules, a low sun, shadow max distance 150 m, the 4096 atlas)

PSNR against the same view with a 16384 atlas (the nearest to ideal shadows), and the share of pixels more than 8
levels off it:

| view | the view's depths | stock | depth fit |
|---|---|---|---|
| a courtyard (a facade 10 m away) | 2.0-10.0 m | 25.5 dB, 4.2 % | **29.2 dB, 1.5 %** |
| looking down from 9 m | 4.5-17.4 m | 29.8 dB, 1.8 % | **33.0 dB, 0.8 %** |
| along the street (it ends at 78 m) | 2.2-77.9 m | 31.4 dB, 0.9 % | 31.5 dB, 0.8 % |
| a vista over the roofs | 38-289 m | 37.0 dB, 0.2 % | 37.1 dB, 0.2 % |

`docs/showcase/sun_shadows_m246.jpg`: the railings' and poles' shadows on the facade, sharp where stock blurred them
(their texels' steps now show, as the reference's do not). A cut from the courtyard to the street: the first frame
after it stock (the turn detected), then the street's range — no frame without its far shadows (the first version,
without the detection, lost them for one frame on lavapipe, more frames in flight on a GPU). The depth conversion
checked against a wall 19.5 m away: 19.50 m. Regression suite: all scenes exact (the setting off).

The cost: one compute pass over the depth buffer (16 texels a thread, one atomic per workgroup) and a 16-byte
readback a frame; on the CPU nothing measurable. Not measured on a GPU (the owner: no GPU for now).

## Decision

KEEP as an option, off (it changes the image: sharper, and where stock blurred aliasing the texels' steps can show).
Open: on the RTX, its cost and whether it should be on in the showcase (the rooftops' views end at the city's edge or
the sky — the street's case, where it gains little; indoors, the osteria, and looking down from the roofs it gains
most); splits placed by the depths' distribution, not only their range (SDSM's full form).
