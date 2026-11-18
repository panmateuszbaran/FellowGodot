# FellowEngine showcase: builds the rooftops scene's character in Blender — the mannequin (mannequin.py), its skeleton
# and its clips (mannequin_anims.py) — and exports it as one .glb.
#
#   blender --background --python build_mannequin.py -- <out_dir>
#   python build_mannequin.py <out_dir>          (with the `bpy` module installed)

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

from mannequin import build_armature, build_mesh  # noqa: E402
from mannequin_anims import bake, clips  # noqa: E402
from mannequin_rig import FPS, Rig  # noqa: E402


def main():
	argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:]
	out = os.path.abspath(argv[0] if argv else "rooftops_models")
	only = set(argv[1:])
	os.makedirs(out, exist_ok=True)
	bpy.ops.wm.read_factory_settings(use_empty=True)
	scene = bpy.context.scene
	scene.render.fps = FPS
	arm = build_armature()
	body = build_mesh(arm)
	rig = Rig(arm)
	end = 1
	for name, (length, loop, fn) in clips().items():
		if only and name not in only:
			continue
		act = bake(rig, name, length, loop, fn)
		end = max(end, int(act.frame_range[1]))
		print(f"{name}: {length:.2f} s{' loop' if loop else ''}", flush=True)
	scene.frame_start = 1
	scene.frame_end = end
	bpy.ops.object.select_all(action="DESELECT")
	arm.select_set(True)
	body.select_set(True)
	bpy.context.view_layer.objects.active = arm
	bpy.ops.export_scene.gltf(filepath=os.path.join(out, "Mannequin.glb"), export_format="GLB", use_selection=True,
			export_yup=True, export_skins=True, export_animations=True, export_animation_mode="NLA_TRACKS",
			export_force_sampling=True, export_frame_step=1, export_def_bones=False, export_optimize_animation_size=False,
			export_anim_slide_to_zero=True, export_bake_animation=False, export_reset_pose_bones=True,
			export_rest_position_armature=True)


main()
