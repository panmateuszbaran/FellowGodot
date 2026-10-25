# 0107 — Contact shadows (screen-space, the sun)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/contact_shadows/enabled` (false), `length` (0.3 m), `steps` (8; 16 until milestone 19),
`thickness` (0.02 m) · Code: `effects/fellow_contact_shadow.*`, `shaders/effects/fellow_contact_shadow.glsl`,
`forward_clustered/render_forward_clustered.cpp` (`_fellow_process_contact_shadow`), `scene_forward_clustered.glsl`
(specialization bit `fellow_contact_shadow`).

## Problem

A shadow map's texels are centimetres to decimetres wide and need a depth bias: the shadow does not reach the point
where an object touches the ground (feet, a lamp post's base, a box on a floor) — the objects float. Unreal (and
most engines) add contact shadows: a short screen-space ray march towards the light that catches what the shadow map
misses.

## Change

After the depth pre-pass, one compute pass per pixel marches `steps` steps over `length` metres towards the first
directional light (the one the colour passes number 0), in view space, each step projected with the frame's jittered
projection and compared with the depth buffer: when the surface seen there is in front of the march by more than a
bias (1 cm + 0.2 % of the distance, the depth buffer's precision and the jitter) and by less than two steps' change of
depth (at least `thickness`), the pixel is in contact shadow. The tolerance is what keeps the march from shadowing
behind everything seen in front of it (objects are not solid all the way back): a march passing behind a lamp post
seen in front shadows only where it meets the post's surface. The start is dithered per pixel and frame
(interleaved gradient noise; TAA averages it). The result (an RG8 mask) is bound where the ray-traced sun shadows'
mask is (decision 0082; the two are exclusive, the traced shadows win), and the opaque passes, in specialized
pipelines only, multiply the light's shadow by it (with the light's shadow opacity). Needs the depth pre-pass and no
MSAA; the light must cast shadows.

## Verification

Off (the default) the image is the previous build's bit for bit (lavapipe: City Lab, Crowd Lab, Lights Lab, 0
differing pixels; PBR Lab differs by ±1 between two runs of the same build).

## Results

Lavapipe, 960 × 540, on against off: Crowd Lab's close camera 5 300 pixels darker — the creases under the characters'
stacked body discs and where the feet meet the ground, which the shadow map's bias leaves lit — and no broad surface
shadowed by itself; City Lab's street 70 pixels (the lamp posts' bases). The first version (thickness 0.3 m, a fixed
tolerance) shadowed the characters' sunlit sides behind their own silhouettes: the tolerance of two steps' depth
change fixed it.

RTX (milestone 18's report, `m18_f255`/`m18_f256`, 1920 × 1080): the pass 0.18 ms in City Lab, 0.29 ms in Sponza,
0.25 ms in Bistro exterior. A second version marches in clip space (the ray's clip position is linear along it: one
projection for all steps, the depth buffer's view-space depth from the inverse projection's z and w rows alone, which do
not depend on x and y): the same image within rounding (lavapipe: 1-2 pixels by 4 levels); RTX (`m18_f257`) 0.171, 0.265
and 0.264 ms — the 16 depth fetches per pixel are the cost. Skipping the pixels facing away from the light (their normal
from the depth buffer, the smaller change of depth on each axis) was tried and dropped: at the creases between the
characters' body discs, seen at a grazing angle, the reconstructed normal points away from the sun and the contact
shadow there — the effect's main use — was lost (2 300 pixels brighter).

Milestone 19: 8 steps by default (Unreal's count), half the fetches. Crowd Lab on lavapipe: 6 100 pixels darkened
against 5 300 with 16 (the tolerance of two steps' depth change is twice as long), the total darkening +5 % (close) and
−1 % (street); side by side the two are not told apart.
