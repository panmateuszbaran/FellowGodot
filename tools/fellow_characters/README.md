# FellowEngine's character pipeline

`make_characters.py` makes people with MakeHuman's MPFB 2 inside Blender (as a Python module) and writes them as
glTF for the engine (milestone 232, decision 0374).

* **What it needs**: Python with Blender as a module (`pip install bpy`: Blender 5.x) and Pillow; MPFB 2's source
  (`git clone https://github.com/makehumancommunity/mpfb2`; GPLv3, it stays outside this repository and is installed
  into a Blender profile of the tool's own, never into the user's Blender); MakeHuman's system assets pack, unpacked
  (CC0: <https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html>, 267 MB).
* **What it writes**: one `<name>.glb` per person of the spec (`people.json`): the body shaped by MakeHuman's macros
  (applied: MPFB keeps them as shape keys), a skin, eyes, eyebrows, eyelashes, hair and clothes from the pack, MPFB's
  "game_engine" rig (53 bones) renamed to Godot's `SkeletonProfileHumanoid`, the faces under the clothes removed;
  materials opaque but for the hair, eyebrows and eyelashes; textures at most 2048 px (the skin) and 1024 px (the rest),
  encoded again with Pillow (Blender's JPEGs carry an ICC segment Godot's JPEG loader refuses; the cut-out cards' PNGs
  with a palette). About 33 000 triangles and 2 MB a person. Everything in them is CC0.

```
python3 tools/fellow_characters/make_characters.py --mpfb <mpfb2>/src/mpfb --assets <unpacked system assets> \
    --out showcase/people [--only giulia,marco] [--blend]
```

In the engine, `showcase/scripts/fellow_person.gd` (`FellowPerson.dress()`) wears a person over a mannequin: the
mannequin's skeleton keeps everything that drives it and a `RetargetModifier3D` hands its pose to the person's skeleton,
whose limbs' rests are first turned to the mannequin's T-pose (MPFB's rest is an A-pose); the skin gets subsurface
scattering, the hair and lashes alpha scissor. `showcase/tools/person_look.gd` renders one or the whole cast playing a
clip. Import settings (`showcase/people/*.glb.import`): embedded textures as Basis Universal, no light baking.
