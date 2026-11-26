# FellowEngine's character pipeline

`make_characters.py` makes people with MakeHuman's MPFB 2 inside Blender (as a Python module) and writes them as
glTF for the engine (milestone 232, decision 0374).

* **What it needs**: Python with Blender as a module (`pip install bpy`: Blender 5.x) and Pillow; MPFB 2's source
  (`git clone https://github.com/makehumancommunity/mpfb2`; GPLv3, it stays outside this repository and is installed
  into a Blender profile of the tool's own, never into the user's Blender); MakeHuman's system assets pack, unpacked
  (CC0: <https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html>, 267 MB).
* **What it writes**: one `<name>.glb` per person of the spec (`people.json`): the body shaped by MakeHuman's macros
  (applied: MPFB keeps them as shape keys), a skin, eyes, eyebrows, eyelashes, hair and clothes from the pack, MPFB's
  "game_engine" rig (53 bones; 55 with the eyes) renamed to Godot's `SkeletonProfileHumanoid`, the faces under the clothes removed;
  materials opaque but for the hair, eyebrows and eyelashes; textures at most 2048 px (the skin) and 1024 px (the rest),
  encoded again with Pillow (Blender's JPEGs carry an ICC segment Godot's JPEG loader refuses; the cut-out cards' PNGs
  with a palette). About 33 000 triangles a person. Everything in them is CC0.
* **Faces** (milestone 234): with MakeHuman's Faceunits 01 pack (CC0: ARKit's 52 shapes as targets; `--faces <dir>`,
  by default `<assets>/targets/faceunits`) the body gets the 52 shapes as morph targets, and the eyebrows and eyelashes
  (the eye, brow and cheek shapes) and the teeth and tongue (added: the jaw and mouth shapes) the same shapes, each fitted
  again to the body with one shape at 1. About 3.4 MB a person with them.
* **Eyes** (milestone 235): `LeftEye` and `RightEye` bones under the head at the eyeballs' centres, each eyeball
  weighted to its own (55 bones); the irises from the spec's `eye_colour` (the pack's `blue`, `bluegreen`, `brown`,
  `brownlight`, `deepblue`, `green`, `grey`, `ice`, `lightblue`, or `darkbrown`: its brown less red).
* **Hats** (milestone 236): with a hat among the clothes (fedora, hat, cap, helmet, hood) the hair's faces above the
  hat's underside are removed (MPFB fits the hair to the head: it came through the hat).

```
python3 tools/fellow_characters/make_characters.py --mpfb <mpfb2>/src/mpfb --assets <unpacked system assets> \
    --out showcase/people [--only giulia,marco] [--blend]
```

In the engine, `showcase/scripts/fellow_person.gd` (`FellowPerson.dress()`) wears a person over a mannequin: the
mannequin's skeleton keeps everything that drives it and a `RetargetModifier3D` hands its pose to the person's skeleton,
whose limbs' rests are first turned to the mannequin's T-pose (MPFB's rest is an A-pose); the skin gets subsurface
scattering, the hair blended at its edges after an opaque pre-pass and the brows and lashes alpha scissor, and a `FellowFaceAnimator` on the face (blinking; `talking`; its gaze
turning the eye bones, `fellow_person_eyes.gd`; a "pain" expression beside its own). The player's person also gets a
two-bone IK onto the mannequin's hands and feet (milestone 233).
`showcase/tools/person_look.gd` renders one or the whole cast playing a clip (`--face=shape:weight,...`, `--talk=1`, `--look=1`, `--mood=name[:weight]`). Import settings (`showcase/people/*.glb.import`): embedded textures as Basis Universal, no light baking.
