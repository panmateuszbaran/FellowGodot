# FellowEngine showcase (milestone 192): Mixamo's motion-captured clips on the rooftops' mannequin. Each FBX of a folder
# (downloaded by the owner from mixamo.com: the Y Bot, "Without Skin", 30 fps, no keyframe reduction, in place) is
# retargeted onto the mannequin's skeleton (mannequin.py) and the clips exported as one .glb that
# showcase/tools/build_mocap_clips.gd turns into the player's library of mocap clips.
#
# Mixamo's licence lets a game use its clips but not hand out the files: neither the FBX nor what is made from them is
# committed (the folder and the output are the owner's). Without them the showcase plays its own clips.
#
#   python mixamo_retarget.py <mixamo_dir> <out_dir>        (with the `bpy` module installed)
#   blender --background --python mixamo_retarget.py -- <mixamo_dir> <out_dir>
#
# The retarget: every frame each mapped bone turns, in the world, as Mixamo's turns from its rest pose (both skeletons
# rest in a T-pose standing on +Z, facing -Y); the hips move as Mixamo's scaled by the hips' heights; the legs then
# reach by IK for Mixamo's ankles scaled the same way (the shins' lengths differ: the feet would float or sink), the
# knees towards Mixamo's, the feet turned as Mixamo's. A loop's cycle is found (the left ankle's way back and forth),
# cut to one stride and, for the gaits, started at the left foot's strike and played over a second (the player's gait
# tree mixes the same moments of strides); clips.json beside the .glb gives each gait's speed and stride's length.

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
import addon_utils  # noqa: E402  (after bpy: the module's)
from mathutils import Matrix, Quaternion, Vector  # noqa: E402

from mannequin import build_armature, build_mesh  # noqa: E402
from mannequin_rig import FPS, Rig, write_action  # noqa: E402

SRC_FPS = 30

# Mixamo's bone (after its "mixamorig:" prefix) for each of the mannequin's.
MAP = {
	"Hips": "Hips",
	"Spine": "Spine",
	"Chest": "Spine1",
	"UpperChest": "Spine2",
	"Neck": "Neck",
	"Head": "Head",
}
for _side in ("Left", "Right"):
	MAP.update({
		_side + "Shoulder": _side + "Shoulder",
		_side + "UpperArm": _side + "Arm",
		_side + "LowerArm": _side + "ForeArm",
		_side + "Hand": _side + "Hand",
		_side + "ThumbMetacarpal": _side + "HandThumb1",
		_side + "ThumbProximal": _side + "HandThumb2",
		_side + "IndexProximal": _side + "HandIndex1",
		_side + "IndexIntermediate": _side + "HandIndex2",
		_side + "MiddleProximal": _side + "HandMiddle1",
		_side + "MiddleIntermediate": _side + "HandMiddle2",
		_side + "RingProximal": _side + "HandRing1",
		_side + "RingIntermediate": _side + "HandRing2",
		_side + "LittleProximal": _side + "HandPinky1",
		_side + "LittleIntermediate": _side + "HandPinky2",
		_side + "UpperLeg": _side + "UpLeg",
		_side + "LowerLeg": _side + "Leg",
		_side + "Foot": _side + "Foot",
		_side + "Toes": _side + "ToeBase",
	})

# The clips: the mannequin's clip, the Mixamo file names it may come from (lower case, the first found), how it is cut.
# kind: "gait" (a cycle, one stride over a second from the left foot's strike), "loop" (a cycle, one period),
# "once" (as it is, from `start` s before the feet leave the ground for "takeoff"), "land" (from the feet's touchdown).
CLIPS = [
	("idle", ["breathing idle", "idle", "happy idle"], "loop"),
	("gait_walk", ["walking", "walk", "standard walk"], "gait"),
	("gait_jog", ["jogging", "slow run", "jog forward"], "gait"),
	("gait_run", ["running", "standard run", "run"], "gait"),
	("gait_sprint", ["fast run", "sprint", "running fast"], "gait"),
	("jump", ["jump", "standing jump"], "takeoff"),
	("jump_run", ["running jump"], "takeoff"),
	("fall", ["falling idle", "falling"], "loop"),
	("land", ["falling to landing", "landing"], "land"),
	("land_hard", ["hard landing"], "land"),
	("roll", ["falling to roll", "stand to roll", "running forward roll", "roll"], "land"),
	("hang_idle", ["braced hang", "hanging idle"], "loop"),
	("hang_shimmy_l", ["left braced hang shimmy", "braced hang shimmy left"], "loop"),
	("hang_shimmy_r", ["right braced hang shimmy", "braced hang shimmy right", "braced hang shimmy"], "loop"),
	("hang_leap_up", ["braced hang hop up"], "once"),
	("hang_leap_l", ["braced hang hop left"], "once"),
	("hang_leap_r", ["braced hang hop right"], "once"),
	("hang_climb", ["braced hang to crouch", "climbing to top"], "once"),
	("climb_move", ["climbing up wall", "climbing"], "loop"),
	("vault", ["vault", "jump over", "running over obstacle"], "once"),
]


def strip(name):
	return name.split(":")[-1]


def import_fbx(path):
	before = set(bpy.data.objects)
	bpy.ops.import_scene.fbx(filepath=path, use_anim=True, ignore_leaf_bones=True, automatic_bone_orientation=False)
	new = [o for o in bpy.data.objects if o not in before]
	arm = next(o for o in new if o.type == "ARMATURE")
	return arm, new


class Source:
	"""A Mixamo armature sampled in the world: its bones' rest and posed matrices."""

	def __init__(self, arm):
		self.arm = arm
		self.bones = {strip(b.name): b.name for b in arm.data.bones}
		act = arm.animation_data.action
		self.first, self.last = act.frame_range
		self.length = (self.last - self.first) / SRC_FPS
		mw = arm.matrix_world
		self.turn = Matrix.Identity(4)
		rest = {k: mw @ arm.data.bones[v].matrix_local for k, v in self.bones.items()}
		if rest["LeftUpLeg"].translation.x < rest["Hips"].translation.x:
			# Facing +Y: turned to face -Y as the mannequin does (its left on +X).
			self.turn = Matrix.Rotation(math.pi, 4, "Z")
		self.rest = {k: self.turn @ m for k, m in rest.items()}
		self.rest_q = {k: m.to_3x3().normalized().to_quaternion() for k, m in self.rest.items()}

	def sample(self, t):
		f = self.first + t * SRC_FPS
		i = int(math.floor(f))
		bpy.context.scene.frame_set(i, subframe=f - i)
		mw = self.turn @ self.arm.matrix_world
		return {k: mw @ self.arm.pose.bones[v].matrix for k, v in self.bones.items()}


def retarget(rig, src, t, scale, ankle_dz, centre):
	"""The mannequin's pose at Mixamo's time t (s)."""
	pose = src.sample(t)
	rig.reset()
	cache = {}
	for name in rig.names:
		m = MAP.get(name)
		if name == "Hips":
			p = pose["Hips"].translation
			want = Vector(((p.x - centre.x) * scale, (p.y - centre.y) * scale, p.z * scale))
			base = rig.matrix("Root", cache) @ rig.rest_rel["Hips"]
			rig.loc["Hips"] = base.to_quaternion().inverted() @ (want - base.translation)
		if m is None or m not in pose:
			rig.matrix(name, cache)
			continue
		delta = pose[m].to_3x3().normalized().to_quaternion() @ src.rest_q[m].inverted()
		rig.set_global_rot(name, delta @ rig.rest_q[name], cache)
		cache.pop(name, None)
		rig.matrix(name, cache)
	# The legs: Mixamo's ankles, scaled, reached by IK; the knees towards Mixamo's; the feet and toes turned as its.
	for side in ("Left", "Right"):
		up, lo, ft = pose[side + "UpLeg"].translation, pose[side + "Leg"].translation, pose[side + "Foot"].translation
		target = Vector(((ft.x - centre.x) * scale, (ft.y - centre.y) * scale, ft.z * scale + ankle_dz))
		mid = (up + ft) * 0.5
		pole = lo - mid
		if pole.length < 1e-4:
			pole = Vector((0, -1, 0))
		rig.ik(side + "UpperLeg", side + "LowerLeg", side + "Foot", target, pole.normalized(), (0, -1, 0))
		cache = rig.fk()
		for b, m in ((side + "Foot", side + "Foot"), (side + "Toes", side + "ToeBase")):
			if m in pose:
				delta = pose[m].to_3x3().normalized().to_quaternion() @ src.rest_q[m].inverted()
				rig.set_global_rot(b, delta @ rig.rest_q[b], cache)
				cache = rig.fk()
	return pose


def ankle_track(src, step):
	"""The left and right ankles' world positions every step seconds."""
	out = []
	t = 0.0
	while t <= src.length + 1e-6:
		p = src.sample(t)
		out.append((t, p["LeftFoot"].translation.copy(), p["RightFoot"].translation.copy(), p["Hips"].translation.copy()))
		t += step
	return out


def find_period(track):
	"""A cycle's period (s): the lag at which the left ankle's forward position best repeats itself."""
	ys = [a[1].y for a in track]
	n = len(ys)
	step = track[1][0] - track[0][0]
	mean = sum(ys) / n
	best, best_lag = -1e9, n - 1
	for lag in range(int(0.4 / step), n - int(0.2 / step)):
		num = sum((ys[i] - mean) * (ys[i + lag] - mean) for i in range(n - lag))
		den = math.sqrt(sum((ys[i] - mean) ** 2 for i in range(n - lag)) * sum((ys[i + lag] - mean) ** 2 for i in range(n - lag))) + 1e-9
		c = num / den
		if c > best + 0.02:
			best, best_lag = c, lag
		if best > 0.9 and c < best - 0.2:
			break
	return best_lag * step, best


def strike_time(track, period):
	"""The left foot's strike in the first period: where the left ankle is furthest forward (-Y) relative to the hips."""
	best, bt = 1e9, 0.0
	for t, la, ra, hp in track:
		if t > period:
			break
		y = la.y - hp.y
		if y < best:
			best, bt = y, t
	return bt


def stance_speed(track, period, t0):
	"""The body's speed implied by a planted foot sliding back under the hips (an in-place clip): the left ankle's
	backward speed relative to the hips over its lowest third."""
	pts = [(t, la.y - hp.y, la.z) for t, la, ra, hp in track if t0 <= t <= t0 + period]
	zmin = min(p[2] for p in pts)
	zmax = max(p[2] for p in pts)
	low = [p for p in pts if p[2] < zmin + (zmax - zmin) * 0.25]
	if len(low) < 3:
		return 0.0
	low.sort()
	speeds = []
	for a, b in zip(low, low[1:]):
		if b[0] - a[0] < 0.05:
			speeds.append((b[1] - a[1]) / (b[0] - a[0]))
	speeds.sort()
	return speeds[len(speeds) // 2] if speeds else 0.0


def leave_ground(track):
	"""The time both ankles first rise more than 6 cm over their lowest (a jump's takeoff), else 0."""
	z0 = min(min(a[1].z, a[2].z) for a in track)
	for t, la, ra, hp in track:
		if la.z > z0 + 0.06 and ra.z > z0 + 0.06:
			return t
	return 0.0


def touch_down(track):
	"""The first time after the feet's highest that an ankle comes within 3 cm of its lowest (a landing), else 0."""
	z0 = min(min(a[1].z, a[2].z) for a in track)
	high = max(range(len(track)), key=lambda i: min(track[i][1].z, track[i][2].z))
	for t, la, ra, hp in track[high:]:
		if min(la.z, ra.z) < z0 + 0.03:
			return t
	return 0.0


def blend_snapshots(a, b, w):
	"""Two of the rig's snapshots mixed: b's share w."""
	out = {}
	for n in a:
		la, qa = a[n]
		lb, qb = b[n]
		out[n] = (la.lerp(lb, w), qa.slerp(qb, w))
	return out


def smooth_step(x):
	x = min(1.0, max(0.0, x))
	return x * x * (3.0 - 2.0 * x)


SEAM = 0.4  # s: a loop that is not a cycle (an idle, a fall) has its end crossfaded into its start


def bake_clip(rig, src, name, kind, scale, ankle_dz, info):
	track = ankle_track(src, 1.0 / SRC_FPS)
	start, length, loop, play = 0.0, src.length, False, 1.0
	seam = 0.0
	if kind == "gait":
		# Mixamo's in-place gaits loop as they are: one cycle (two steps), or several (a shorter period repeating).
		period, corr = src.length, 1.0
		if src.length > 1.6:
			p, c = find_period(track)
			if c > 0.9 and p < src.length * 0.6:
				period, corr = p, c
		loop = True
		length = period
		start = strike_time(track, period)
		v = abs(stance_speed(track, period, start))
		info["gaits"][name] = {"period": period, "speed": v, "stride": v * period, "repeat": corr}
		play = period  # a second of the clip is a stride
		info["clips"][name] = {"kind": kind, "period": period, "repeat": corr}
	elif kind == "loop":
		loop = True
		seam = min(SEAM, src.length * 0.25)
		length = src.length - seam
		info["clips"][name] = {"kind": kind, "seam": seam}
	elif kind == "takeoff":
		t = leave_ground(track)
		start = max(0.0, t - 0.12)
		length = src.length - start
		info["clips"][name] = {"kind": kind, "takeoff": t, "from": start}
	elif kind == "land":
		t = touch_down(track)
		start = max(0.0, t - 0.05)
		length = src.length - start
		info["clips"][name] = {"kind": kind, "touchdown": t, "from": start}
	else:
		info["clips"][name] = {"kind": kind}
	# The centre the hips move about: a loop's mean, a one-shot's start (in place: the player's body moves it).
	hips = [a[3] for a in track if start <= a[0] <= start + length]
	centre = Vector((sum(h.x for h in hips) / len(hips), sum(h.y for h in hips) / len(hips), 0.0))
	if not loop:
		centre = Vector((hips[0].x, hips[0].y, 0.0))
	out_len = 1.0 if kind == "gait" else length
	n = max(2, int(round(out_len * FPS)))
	frames = []
	for i in range(n if loop else n + 1):
		u = min(out_len, i / FPS) / out_len
		t = start + u * length
		if loop:
			t = start + math.fmod(u * length, length)
			if t > src.length:
				t -= length
		retarget(rig, src, min(t, src.length), scale, ankle_dz, centre)
		snap = rig.snapshot()
		if seam > 0.0 and t < seam:
			# The loop's start: from where its end goes on (t + length) into the clip's own (t).
			retarget(rig, src, min(t + length, src.length), scale, ankle_dz, centre)
			snap = blend_snapshots(rig.snapshot(), snap, smooth_step(t / seam))
		frames.append(snap)
	debug = os.environ.get("MIXAMO_DEBUG")
	if debug:
		# The source's joints (scaled about the centre) and the mannequin's at eight moments, for a look (stick figures).
		dump = []
		for k in range(8):
			t = start + length * k / 8.0
			pose = retarget(rig, src, min(t, src.length), scale, ankle_dz, centre)
			cache = rig.fk()
			sj = {m: [(p.translation.x - centre.x) * scale, (p.translation.y - centre.y) * scale, p.translation.z * scale] for m, p in pose.items()}
			tj = {b: list(m.translation) for b, m in cache.items()}
			dump.append({"t": t, "src": sj, "tgt": tj})
		os.makedirs(debug, exist_ok=True)
		with open(os.path.join(debug, name + ".json"), "w") as f:
			json.dump({"map": MAP, "parent": rig.parent, "frames": dump}, f)
	write_action(rig, name, frames, loop)
	info["clips"][name].update({"length": out_len, "loop": loop, "source_length": src.length})
	return play


def main():
	argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else sys.argv[1:]
	src_dir = os.path.abspath(argv[0])
	out = os.path.abspath(argv[1] if len(argv) > 1 else "mocap")
	os.makedirs(out, exist_ok=True)
	addon_utils.enable("io_scene_fbx", default_set=True)
	files = {os.path.splitext(f)[0].lower(): os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.lower().endswith(".fbx")}
	bpy.ops.wm.read_factory_settings(use_empty=True)
	scene = bpy.context.scene
	scene.render.fps = SRC_FPS
	arm = build_armature()
	body = build_mesh(arm)
	rig = Rig(arm)
	info = {"gaits": {}, "clips": {}, "files": {}}
	end = 1
	for name, choices, kind in CLIPS:
		path = next((files[c] for c in choices if c in files), None)
		if path is None:
			continue
		src_arm, objs = import_fbx(path)
		src = Source(src_arm)
		# The scale: the hips' heights at rest; the ankles' rest heights matched.
		scale = rig.head["Hips"].z / src.rest["Hips"].translation.z
		ankle_dz = rig.head["LeftFoot"].z - src.rest["LeftFoot"].translation.z * scale
		bake_clip(rig, src, name, kind, scale, ankle_dz, info)
		info["files"][name] = os.path.basename(path)
		act = bpy.data.actions.get(name)
		end = max(end, int(act.frame_range[1]))
		print(f"{name}: {os.path.basename(path)} {info['clips'][name]}", flush=True)
		for o in objs:
			bpy.data.objects.remove(o, do_unlink=True)
	if not info["clips"]:
		print("no Mixamo clips found in", src_dir)
		return
	scene.render.fps = FPS
	scene.frame_start = 1
	scene.frame_end = end
	bpy.ops.object.select_all(action="DESELECT")
	arm.select_set(True)
	body.select_set(True)
	bpy.context.view_layer.objects.active = arm
	bpy.ops.export_scene.gltf(filepath=os.path.join(out, "Mocap.glb"), export_format="GLB", use_selection=True,
			export_yup=True, export_skins=True, export_animations=True, export_animation_mode="NLA_TRACKS",
			export_force_sampling=True, export_frame_step=1, export_def_bones=False, export_optimize_animation_size=False,
			export_anim_slide_to_zero=True, export_bake_animation=False, export_reset_pose_bones=True,
			export_rest_position_armature=True)
	with open(os.path.join(out, "clips.json"), "w") as f:
		json.dump(info, f, indent=1)


main()
