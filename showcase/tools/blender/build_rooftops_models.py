# FellowEngine showcase: builds the rooftops scene's models in Blender and exports each as a .glb.
#
#   blender --background --python build_rooftops_models.py -- <out_dir>
#   python build_rooftops_models.py <out_dir>          (with the `bpy` module installed)
#
# Then `godot --headless --path showcase --script tools/build_rooftops.gd -- <out_dir>` turns them into the scene.

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

from ac_buildings import barrel, bollard, campanile, crate, drain, fountain, hay_cart, house, skyline, skyline_houses, stall  # noqa: E402
from ac_common import export_glb, reset_scene  # noqa: E402

# name: the function making it, its arguments.
HOUSES = {
	"PalazzoOcra": dict(w=14.0, d=10.0, floors=4, plaster="plaster_ochre", roof="hip", seed=11, shutters="shutter_green",
			balcony=True, banner=[(0, 3.2, 2), (0, 10.8, 2)], benches=True),
	"CasaRosa": dict(w=10.0, d=9.0, floors=3, plaster="plaster_rose", roof="terrace", seed=12, shutters="shutter_brown",
			altana=True, banner=[(1, 4.5, 2)]),
	"CasaCrema": dict(w=8.4, d=8.0, floors=3, plaster="plaster_cream", roof="gable", seed=13, shutters="shutter_green",
			nobile="pediment"),
	"PalazzoSiena": dict(w=9.6, d=11.0, floors=5, plaster="plaster_sienna", roof="hip", seed=14, shutters="shutter_brown",
			arched_upper=True, banner=[(0, 4.8, 3)], benches=True),
	"CasaBianca": dict(w=7.4, d=9.0, floors=3, plaster="plaster_white", roof="terrace", seed=15, shutters="shutter_green",
			altana=False),
	"CasaOcraBassa": dict(w=11.0, d=8.0, floors=2, plaster="plaster_ochre", roof="hip", seed=16, shutters="shutter_brown",
			ground="doors", nobile="plain"),
}


def main():
	argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:]
	out = os.path.abspath(argv[0] if argv else "rooftops_models")
	only = set(argv[1:])
	os.makedirs(out, exist_ok=True)
	manifest = {}

	def done(name, objects, **info):
		export_glb(os.path.join(out, name + ".glb"), objects)
		manifest[name] = info
		tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objects if o.type == "MESH" and "_col" not in o.name)
		print(f"{name}: {tris} triangles", flush=True)

	for name, kw in HOUSES.items():
		if only and name not in only:
			continue
		reset_scene()
		obj, col, top = house(name, **kw)
		done(name, list(bpy.context.scene.objects), top=top, w=kw["w"], d=kw["d"], roof=kw["roof"])
	for name, fn in (("Campanile", campanile), ("HayCart", hay_cart), ("Fountain", fountain), ("Skyline", skyline),
			("Bollard", bollard), ("Barrel", barrel), ("Crate", crate), ("Drain", drain)):
		if only and name not in only:
			continue
		reset_scene()
		res = fn()
		info = {}
		if name == "Campanile":
			info["top"] = res[2]
		done(name, list(bpy.context.scene.objects), **info)
	if not only or "SkylineHouses" in only:
		# Milestone 153: the city's houses, prototypes placed many times (SkylineHouses.json: their places).
		reset_scene()
		objects, places = skyline_houses()
		done("SkylineHouses", objects, count=len(places))
		with open(os.path.join(out, "SkylineHouses.json"), "w") as f:
			json.dump(places, f)
		print(f"SkylineHouses: {len(places)} houses", flush=True)
	for name, seed in (("StallA", 5), ("StallB", 6)):
		if only and name not in only:
			continue
		reset_scene()
		stall(name, seed)
		done(name, list(bpy.context.scene.objects))
	with open(os.path.join(out, "manifest.json"), "w") as f:
		json.dump(manifest, f, indent=1)


main()
