# 0236 — Asynchronous downloads returned whole

Status: **KEEP**, on with FellowEngine (`rendering/fellow/enabled`; a correctness fix) · Branch `fellow/geometry` ·
Code: `servers/rendering/rendering_device.{h,cpp}` (`buffer_get_data_async()`, `texture_get_data_async()`,
`_stall_for_frame()`) · Checks: `benchmarks/project/bench/readback_check.gd`, the regression suite.

## Problem

Checking decision 0234's download staging, a texture read back with `RenderingDevice.texture_get_data_async()` came back
with its last bytes wrong, the same with stock's staging and with FellowEngine off. Two faults in stock's code:

* **A mip's side that halves to zero.** The copy back from the staging sizes a mip as `STEPIFY(width >> mip, block)`,
  `STEPIFY(height >> mip, block)`; for a texture that is not square, the shorter side of its last mips is 0 there (its
  copy to the staging took it as one pixel, as `get_image_format_required_size()` does), so those mips are copied back
  as nothing and their bytes are left as they were — whatever the reused array held. Every texture of the check that is
  not square: the last 1-16 bytes.
* **A download larger than what the staging ring has left.** When a download fills the download staging ring (128 MiB
  by default, `rendering_device/staging_buffer/max_size_mb`) the device submits the frame, waits for every frame and
  reuses the ring; the parts of the download copied before that wait were read back by it with no request asking for
  them, so they were dropped. A texture's bytes for those parts are left as they were; a buffer's later parts are written
  from the start of the result. Stock reserves 4 times a compressed download's data (decision 0234), so 30 downloads
  of 100 MiB in one frame were enough: a BC5 4096 × 2048 texture came back 63 % wrong.

FellowEngine reads back with these calls every frame (occlusion culling's counters, adaptive VRS, the clouds' probe, a
gas volume's emission and burn totals): small, but in a frame that also downloads something large (an editor tool, a
game's screenshot of a render target), any of them can be the one split.

## Change

With FellowEngine on (read when the device is created):

* A mip's side is at least one pixel when it is copied back (`MAX(1, side >> mip)`), as when it was copied.
* When a download must wait for the ring, the parts recorded so far go into the frame as a part request (`fellow_more`):
  the wait reads them back into a kept array (`fellow_download_parts`, at the part's offset for a buffer), and the rest
  of the download, marked `fellow_continued`, is read into the same array and returned whole, once.

## Verification

`readback_check.gd` on lavapipe: 30 textures (BC7, BC1, BC4, BC5, RGBA8, R8; 4 × 4 to 4096 × 2048, square and not, every
mip) created with data and updated, read back synchronously; all 30 read back asynchronously in one frame; then a 144 MiB
buffer, and two 4096 × 4096 RGBA8 textures with their mips (85 MiB each) in one frame, asynchronously:

| | synchronous | asynchronous, 30 in a frame | larger than the ring |
|---|---:|---:|---:|
| FellowEngine off (stock) | 30 of 30 the same | the 12 square the same; of the 18 not square, 17 wrong in their last 1-16 bytes, BC5 4096 × 2048 63 % wrong | the buffer 89 % wrong, the second texture 40 % wrong |
| on | 30 of 30 | 30 of 30 | 3 of 3 |
| on, stock's staging (0234 off) | 30 of 30 | 30 of 30 | 3 of 3 |

The regression suite (36 scenes, the release template against the editor build): the same as its goldens.

## Decision

Keep, with FellowEngine on: what the asynchronous calls return is what the GPU held. Off, stock's behaviour.
