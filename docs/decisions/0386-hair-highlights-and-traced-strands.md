# 0386 — The people's hair: highlights across its painted strands; strands traced from it (tried, off)

Status: **KEEP** (the highlights) · **KEEP as a feature, off in the showcase** (the strands) · Branch `fellow/geometry` ·
Code: `modules/fellow_fluid/fellow_hair_3d.{h,cpp}` (`guide_shapes`, `guide_shape_points`),
`tools/fellow_characters/hair_guides.py` (new), `make_skin_maps.py` (`hair_<name>_flow.png`), `make_characters.py`
(`<name>_hair.json`), `showcase/scripts/fellow_person.gd` (`hair_flow`, `_hair_highlights`; `strand_hair`, `_strands`)

## Problem

After the skin and the eyes (0385) the hair is what most gives the people away: MakeHuman's hair is a shell and cards
with painted strands, lit as one rough surface — "a helmet". Real hair shines in bands across its strands (each strand
a cylinder: smooth along it, every normal round it).

## Change

* **Highlights across the painted strands.** `hair_guides.flow_field` finds the painted strands' direction at every
  texel of a hairstyle's texture (its structure tensor: the direction across which the brightness changes least);
  `make_skin_maps.py --hair --hair_names` writes, per hairstyle, a flow map of the direction *across* them (Godot's
  anisotropic GGX is rough along its tangent and smooth across it), its alpha how sure it is. The hair's material
  (`fellow_person.gd`): BaseMaterial3D's anisotropy (0.85) with that flow map, roughness 0.5, specular 0.4 (236's flat
  hair: 0.8, 0.25). The highlight now runs as a band across the strands. A first version along the strands drew the
  highlight along them (the wrong axis); both were compared.
* **Strands traced from the hair (tried).** `FellowHair3D.guide_shapes` / `guide_shape_points`: a hairstyle's guides
  given instead of combed from the scalp (resampled to equal segments; each strand between a guide and the two nearest
  it along their whole length — by their roots alone, guides either side of a parting were paired and strands crossed
  the gap). `hair_guides.trace` makes them from a person's hair: roots where the hair covers the scalp, then along the
  painted flow over the hair's surface at a depth under it, until the texture is transparent or the surface ends;
  `make_characters.py` writes `<name>_hair.json` (Marco: 304 guides, 1-58 cm). `fellow_person.gd` hangs them on the
  head bone (the guides in its bind space) over the cards, darkened.

## Checks (lavapipe, `person_look.gd --view=face --light=key`; `docs/showcase/hair_flow_m244.jpg`)

* The highlights: Marco's hair 43.0 dB against 0385's (3.1 % of pixels more than 2 levels apart), Giulia's 37.7 dB
  (5.3 %): a band of light across the hair where 0385 had a flat sheen.
* The strands, at 960×540 with the face filling a third of the height: the strands (24 000, 0.2-0.08 mm) are under a
  pixel wide, so FellowHair3D dithers their coverage and TAA blends it — a grey haze over the cards, and some strands
  past the hairline, not hair. Not better than the cards at the showcase's distances: off
  (`FellowPerson.strand_hair`, `person_look.gd --strands=1`).
* Regression suite: `hair` (FellowHair3D's combed guides) and `geometry` exact.

## Decision

KEEP the highlights. KEEP `guide_shapes` (a hairstyle given to FellowHair3D: tools, other content) and the tracer; the
showcase's strands stay off until they look like hair at its distances — more strands drawn wider where they are far,
their coverage resolved better than by dithering, and the guides stopped at the hairline.

## Addendum (milestone 250, 2026-11-28): cards from the guides — rejected

Tried: each traced guide made into two narrow cards (12 mm at the root, tapering), textured with 80 painted strands of
coverage in alpha, coloured from the shell's mean colour, normals out from the head, shining across the strands, over the
shell darkened to 0.75 (`person_look.gd --view=face --light=key`, Marco and Rosa). The guides' first points rise along
the head's radius (from the scalp to the hair's surface) and stood out as spikes until cut; after that, Marco's hair was
a little darker with a few strands at its silhouette catching the sky, and Rosa's light bob duller and streaked grey
(the darkened shell between the cards). Not better than MakeHuman's painted shell at 0.6 m, so not kept: hair that reads
as strands needs authored cards (many more, their strands drawn per hairstyle) or strands resolved better than by
dithering, not cards laid along a few hundred guides.
