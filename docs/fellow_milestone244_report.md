# FellowEngine — milestone 244 report (the hair's highlights; strands traced from it)

Status: **FINAL** (2026-11-27). Branch `fellow/geometry`. Decision 0386. Checked on lavapipe.

## 0. Result

The third step of the people's realism (after the skin and the eyes, 243): their hair now shines in a band across its
painted strands, as hair does, instead of one flat sheen — each hairstyle's flow map, found in its own texture, drives
the material's anisotropy. Real strands were tried too: FellowHair3D can now take a hairstyle's guides
(`guide_shapes`), and a tracer makes them from each person's MakeHuman hair; but at the showcase's distances the
strands, under a pixel wide, are a grey haze over the cards — they stay off there.

## 1. Changes

* `modules/fellow_fluid/fellow_hair_3d.{h,cpp}`: `guide_shapes`, `guide_shape_points` (the class reference too).
* `tools/fellow_characters/hair_guides.py` (new): the flow field, the guides' tracing, their JSON.
* `make_skin_maps.py --hair --hair_names` (the flow maps), `make_characters.py` (`<name>_hair.json`, `--hair_dump`).
* `showcase/scripts/fellow_person.gd`: `hair_flow` (on), `strand_hair` (off); `person_look.gd --flow=0`,
  `--strands=1`. `showcase/people/hair_*_flow.png` for the seven hairstyles.
* Docs: decision 0386, `FELLOWENGINE_OPTIONS.md` (Hair), the characters' README, the changelog,
  `docs/showcase/hair_flow_m244.jpg`.

## 2. Checks

* Marco 43.0 dB, Giulia 37.7 dB against 243's hair (a band of light across it); the highlight along the strands (the
  wrong axis) compared and rejected.
* The strands on Marco: 304 traced guides, 24 000 strands: a haze at 960×540 (the image's third panel).
* Regression suite: `hair` and `geometry` exact.

## 3. Open

* Strands that read as hair at play distances (wider when far, their coverage resolved without dithering, the guides
  stopped at the hairline); the faces' shapes (MakeHuman's averages).
