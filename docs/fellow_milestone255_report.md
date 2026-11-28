# FellowEngine — milestone 255 report (virtual shadow maps for the sun)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0396.

The owner: where are we short of Unreal Engine and of AAA? (for now without the GPU). The first answer taken up: the
sun's shadows. Godot's cascades (a 4096 atlas in four slices) give texels of centimetres near the camera and of a metre
far away; Unreal's virtual shadow maps keep a texel about a pixel wide everywhere.

* `rendering/fellow/shadows/virtual/enabled` (off): the first shadowed directional light's shadow in clipmap levels of
  64 × 64 pages of 128 × 128 texels around the camera, 2 mm texels at level 0 and twice as large each level (12 levels:
  4 m). The depth pre-pass marks the pages its pixels look up (a compute pass, read back a few frames later); the scene
  cull allocates them in a physical pool (4096 × 4096 R32UI, 1024 pages, 64 MB; coarse levels first, at most 512 a
  frame, the least recently asked for evicted) and decides which to draw: a new page, a page a static caster over it
  changed or left (the shadow caches' invalidation events), a page under a dynamic caster now or last frame. Those are
  cleared and drawn by shadow passes of at most 16 × 16 pages through the batched shadow lists, each fragment writing
  its height towards the light into its page with an atomic max (a new variant of the scene shader). The rest is kept.
* The colour passes take each pixel's shadow from the finest level its size asks for whose pages are resident (a box
  filter two texels wide against the receiver's plane), else from a coarser level, else from the cascades.
* The scene cull reads only InstanceData and the boxes in its scan (copies there of what decides whether a caster is
  dynamic): 0.27-0.29 ms for 10 000 instances, 1.1-1.2 ms before.
* Checked on lavapipe: a shadow lab (`showcase/tools/vsm_look.gd`: poles, fences, lattices, boxes and balls at 2-150 m)
  and the rooftops' square — fence slats, shutters' louvres and rooflines sharp where the cascades smear them
  (`docs/showcase/virtual_shadows_m255.jpg`); two to four pages per 16 384 pixels (115-233 for the square at
  1280 × 720); at rest no page drawn; kept pages against pages drawn every frame the same pixels but 20; a swinging
  ball the same pixels as with every caster dynamic; a closed room dark inside. The regression suite 48/48 exact (off).
* Found: the osteria's front wall and its building's arches meet with millimetre gaps — sunlight in thin dashes on its
  floor, which the cascades blurred away.

Not measured on a GPU. Open: dropping most of the cascades where pages cover the view, soft shadows from the sun's
size, local lights, transparent receivers, a bounded scan in large worlds. Next (the owner's plan): GI at night (the
fire's and the windows' light bounced), then hair.
