# FellowEngine — milestone 253 report (the people in period clothes)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0394.

The owner, of the showcase: modern people dropped into a town of an Assassin's Creed kind, and it looks very bad.

* The seven people (`tools/fellow_characters/people.json`) dressed for a Tuscan town of the 15th century from
  MakeHuman's community asset packs (dress01, dress02, skirts01, suits02, shirts03; CC0 and CC BY, credited in
  `tools/fellow_characters/CREDITS.md`): Giulia a long gown with draped sleeves; Marco and Luca a wool tunic, hose and
  boots; Rosa a peasant blouse, a full skirt and an apron; Aldo a friar's habit; Amara a tunic over a full skirt; Kenji
  a labourer's tunic with a sash. Nothing modern is left: no T-shirts, jeans, trainers, dungarees or suit.
* `make_characters.py`: garments with a modern print or emblem get a plain woven cloth of the tool's own
  (`PLAIN_CLOTH`, their normal maps kept); garments worn over others are layered (`layer_clothes`, `LAYERS`): the
  hidden garment drawn in under the outer one along lines from the body's axis, a tucked-in blouse's faces under the
  waistband removed — the blouse no longer comes out through the skirt, nor the skirt's folds through the apron.
* `FellowPerson` (`showcase/scripts/fellow_person.gd`): each garment dyed from the period's dyes (`PERIOD_DYES`:
  madder reds, woad blues, weld yellow, walnut brown, russet, green, black, undyed grey wool; `UNDYED` linens), by the
  person's seed and the garment's name; the sheen on wool only. Before, all of a person's clothes took one random tint
  of any hue.
* The shoulders (the owner: why are some people's shoulders pushed up?): the people's clavicles were turned to the
  mannequin's when their rests were matched to its T-pose — the mannequin's rise 9 degrees, MPFB's fall 6-10 — so
  everyone stood shrugged by 16-20 degrees (about 5 cm). The clavicles now keep their own rests (`LIMB_TAILS`).
* Checked on lavapipe: the cast before and after (`docs/showcase/period_clothes_m253.jpg`), Rosa walking from the
  front and the side, the layers in Blender's rest pose, the shoulders standing and walking. The square with its
  people goes to the RTX laptop (lavapipe runs out of the container's 8 GB with the crowd).

Open: headwear of the time (coifs and veils, caps and hoods), the men's hair (modern cuts), clothes of the well-off (a
doublet, a giornea).
