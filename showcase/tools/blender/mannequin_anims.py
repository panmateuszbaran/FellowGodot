# FellowEngine showcase: the mannequin's clips, made by code — locomotion cycles generated from a gait (the feet planted
# by IK through stance, swung on arcs, the heel and the toes rolling; the hips bobbing, swaying and turning, the chest
# turning against them, the arms swinging), and keyed clips (poses at times, their parameters splined between them):
# idle, the jump, the fall, the landing, the roll, hanging, shimmying, free climbing and its lunge, the mantles (with
# the root's motion, which FellowTraversal3D scales onto the ledge), the vault, the leap of faith and its landing in the
# hay, perching on the bell tower's beam.
#
# The root's frame: -Y forward, +X the character's left, +Z up; the root on the ground under the hips.

import math

from mannequin_rig import FPS, keyed, smooth, smoother

ANKLE = 0.085  # the ankle's height over the sole at rest
BALL = (0.0, -0.126, -0.061)  # the ball of the foot from the ankle
HEEL = (0.0, 0.045, -0.085)  # the heel's contact from the ankle


def stand(**kw):
	p = {
		"root": (0, 0, 0),
		"root_rot": (0, 0, 0),
		"hips": (0, 0, -0.012),
		"hips_rot": (0, 0, 0),
		"spine": (2.0, 0, 0),
		"neck": (0, 0),
		"head": (-2.0, 0, 0),
		"shoulder_L": (0, 0),
		"shoulder_R": (0, 0),
		"arm_L": {"fk": (78.0, 3.0, 14.0, 6.0)},
		"arm_R": {"fk": (78.0, 3.0, 14.0, 6.0)},
		"hand_L": (8.0, 0, 0),
		"hand_R": (8.0, 0, 0),
		"leg_L": {"ik": ((0.105, -0.01, ANKLE), (0.15, -1.0, 0.0))},
		"leg_R": {"ik": ((-0.105, 0.01, ANKLE), (-0.15, -1.0, 0.0))},
		"foot_L": (0, 7.0, 0),
		"foot_R": (0, 7.0, 0),
		"toes_L": 0.0,
		"toes_R": 0.0,
		"grip_L": 0.3,
		"grip_R": 0.3,
	}
	p.update(kw)
	return p


def _rot_x(v, deg):
	a = math.radians(deg)
	c, s = math.cos(a), math.sin(a)
	return (v[0], v[1] * c - v[2] * s, v[1] * s + v[2] * c)


def foot_on_ground(contact, pitch_up):
	"""The ankle over a contact on the ground (y forward of the hips), the foot turned toes-up by pitch_up (rolling on
	its heel) or heel-up by -pitch_up (rolling on its ball)."""
	cy, cx = contact
	if pitch_up >= 0.0:
		piv = (cx, cy + HEEL[1], 0.0)
		off = _rot_x((0.0, -HEEL[1], -HEEL[2]), -pitch_up)
	else:
		piv = (cx, cy + BALL[1], 0.0)
		off = _rot_x((0.0, -BALL[1], -BALL[2]), -pitch_up)
	return (piv[0] + off[0], piv[1] + off[1], piv[2] + off[2])


GAITS = {
	# speed (m/s), cycle (s), stance share, foot lift (m), hips' height offset, bob (m), sway (m), pelvis yaw and roll
	# (degrees), lean, arm swing, elbow (back, forward), arms' lowering, feet apart (half, m), heel kick (m)
	"walk": dict(v=1.6, T=1.08, duty=0.6, lift=0.1, hip=-0.03, bob=0.022, sway=0.025, yaw=6.0, roll=4.0, lean=4.0,
			swing=24.0, elbow=(12.0, 32.0), lower=79.0, width=0.09, kick=0.0, strike=14.0, toeoff=-38.0, run=False),
	"run": dict(v=4.4, T=0.7, duty=0.36, lift=0.2, hip=-0.05, bob=0.045, sway=0.012, yaw=10.0, roll=5.0, lean=11.0,
			swing=48.0, elbow=(78.0, 105.0), lower=74.0, width=0.06, kick=0.2, strike=4.0, toeoff=-48.0, run=True),
	"sprint": dict(v=7.0, T=0.62, duty=0.31, lift=0.24, hip=-0.06, bob=0.05, sway=0.008, yaw=12.0, roll=5.0, lean=18.0,
			swing=62.0, elbow=(72.0, 110.0), lower=74.0, width=0.05, kick=0.32, strike=0.0, toeoff=-55.0, run=True),
}


def gait_pose(name, t):
	G = GAITS[name]
	T = G["T"]
	duty = G["duty"]
	stride = G["v"] * T * duty  # the ground covered under one stance
	p = stand()
	phase_l = (t / T) % 1.0
	feet = {}
	for side, sx, ph in (("L", 1.0, phase_l), ("R", -1.0, (phase_l + 0.5) % 1.0)):
		y_front = -stride * 0.55 + 0.02
		y_back = stride * 0.45 + 0.02
		x = sx * G["width"]
		if ph < duty:
			u = ph / duty
			cy = y_front + (y_back - y_front) * u
			if u < 0.2:
				pitch = G["strike"] * (1.0 - smooth(u / 0.2))
			elif u > 0.55:
				pitch = G["toeoff"] * smooth((u - 0.55) / 0.45) * (0.75 if G["run"] else 1.0)
			else:
				pitch = 0.0
			ankle = foot_on_ground((cy, x), pitch)
			toes = -pitch if pitch < 0.0 else 0.0
		else:
			s = (ph - duty) / (1.0 - duty)
			e = smoother(s)
			cy = y_back + (y_front - y_back) * e
			# The heel kicked up behind (running), then the knee drives forward.
			cy += G["kick"] * 0.35 * math.sin(math.pi * min(1.0, s * 1.6)) * (1.0 - s)
			lift = G["lift"] * math.sin(math.pi * s) ** 0.8 + G["kick"] * math.sin(math.pi * min(1.0, s * 1.4)) * (1.0 - s) ** 1.5
			pitch = G["toeoff"] * (1.0 - smooth(s / 0.35)) * 0.8 + G["strike"] * smooth((s - 0.6) / 0.4)
			ankle = (x, cy + 0.0, ANKLE + lift)
			toes = 0.0
		feet[side] = (ankle, pitch, toes)
		p["leg_" + side] = {"ik": (ankle, (sx * 0.12, -1.0, 0.0))}
		p["foot_" + side] = (pitch if ph >= duty or pitch > 0.0 else pitch, 5.0, 0.0)
		p["toes_" + side] = toes
	w = 2.0 * math.pi * phase_l
	mid = 2.0 * math.pi * duty * 0.5
	bob = G["bob"] * math.cos(2.0 * (w - mid)) * (-1.0 if G["run"] else 1.0)
	sway = G["sway"] * math.cos(w - mid)
	yaw = -G["yaw"] * math.cos(w)
	roll = -G["roll"] * math.cos(w - mid)
	p["hips"] = (sway, 0.0, G["hip"] + bob)
	p["hips_rot"] = (G["lean"] * 0.35, yaw, roll)
	twist = -yaw * 1.5
	p["spine"] = (G["lean"] * 0.65 + (2.0 if G["run"] else 0.0) * math.cos(2.0 * w), twist, -roll * 0.6)
	p["neck"] = (-G["lean"] * 0.4, -(yaw + twist) * 0.6)
	p["head"] = (-G["lean"] * 0.4, -(yaw + twist) * 0.4, -roll * 0.3)
	for side, sx, ph in (("L", 1.0, 0.0), ("R", -1.0, math.pi)):
		c = math.cos(w + ph)
		fwd = -G["swing"] * c + (8.0 if G["run"] else 2.0)
		fwdness = (1.0 - c) * 0.5
		elbow = G["elbow"][0] + (G["elbow"][1] - G["elbow"][0]) * fwdness
		p["arm_" + side] = {"fk": (G["lower"] - (6.0 * fwdness if G["run"] else 0.0), fwd, elbow, 10.0 + (20.0 * fwdness if G["run"] else 0.0))}
		p["hand_" + side] = (10.0, 0.0, 0.0)
		p["grip_" + side] = 0.55 if G["run"] else 0.3
		p["shoulder_" + side] = (0.0, (4.0 if G["run"] else 1.5) * -c)
	return p


# ------------------------------------------------------------------ keyed clips


def legs_stand(**kw):
	return {k: v for k, v in stand().items() if k.startswith(("leg_", "foot_", "toes_"))} | kw


def clip_idle():
	base = stand()
	keys = [
		(0.0, dict(base)),
		(1.3, {"hips": (0.014, 0, -0.016), "spine": (2.6, 2.0, -0.6), "neck": (0, 4.0), "head": (-1.0, 7.0, 0.5),
				"arm_L": {"fk": (77.0, 4.0, 15.0, 6.0)}, "arm_R": {"fk": (78.5, 2.0, 13.0, 6.0)}, "hips_rot": (0, 1.5, -1.2)}),
		(2.7, {"hips": (0.006, 0, -0.01), "spine": (1.6, -1.0, 0.3), "neck": (1.0, -3.0), "head": (-3.0, -6.0, -0.5),
				"arm_L": {"fk": (78.5, 2.5, 13.0, 6.0)}, "arm_R": {"fk": (77.5, 4.0, 15.0, 6.0)}, "hips_rot": (0, -0.5, -0.5)}),
		(4.0, dict(base)),
	]
	return 4.0, True, lambda t: keyed(keys, t, loop=True, length=4.0)


def clip_jump():
	base = stand()
	keys = [
		(0.0, dict(base)),
		(0.1, {"hips": (0, 0.02, -0.15), "hips_rot": (14.0, 0, 0), "spine": (12.0, 0, 0), "head": (-12.0, 0, 0),
				"arm_L": {"fk": (70.0, -35.0, 25.0, 6.0)}, "arm_R": {"fk": (70.0, -35.0, 25.0, 6.0)}}),
		(0.2, {"hips": (0, -0.02, 0.05), "hips_rot": (6.0, 0, 0), "spine": (2.0, 0, 0), "head": (-6.0, 0, 0),
				"leg_L": {"ik": ((0.1, 0.03, 0.17), (0.15, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.05, 0.12), (-0.15, -1, 0))},
				"foot_L": (-45.0, 6.0, 0), "foot_R": (-40.0, 6.0, 0), "toes_L": 20.0, "toes_R": 20.0,
				"arm_L": {"fk": (55.0, 100.0, 35.0, 6.0)}, "arm_R": {"fk": (55.0, 100.0, 35.0, 6.0)}}),
		(0.34, {"hips": (0, 0.0, 0.08), "hips_rot": (2.0, 0, 0),
				"leg_L": {"ik": ((0.11, -0.16, 0.45), (0.15, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.12, 0.3), (-0.15, -1, 0))},
				"foot_L": (5.0, 6.0, 0), "foot_R": (-25.0, 6.0, 0), "toes_L": 0.0, "toes_R": 0.0,
				"arm_L": {"fk": (45.0, 70.0, 40.0, 6.0)}, "arm_R": {"fk": (50.0, 85.0, 45.0, 6.0)}}),
	]
	return 0.34, False, lambda t: keyed(keys, t)


def clip_fall():
	a = {"hips": (0, 0, 0.06), "hips_rot": (-4.0, 0, 0), "spine": (-6.0, 0, 0), "head": (-8.0, 0, 0),
			"leg_L": {"ik": ((0.12, -0.14, 0.34), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.1, 0.15), (-0.2, -1, 0))},
			"foot_L": (5.0, 8.0, 0), "foot_R": (-20.0, 8.0, 0),
			"arm_L": {"fk": (38.0, 25.0, 35.0, 0.0)}, "arm_R": {"fk": (44.0, 10.0, 30.0, 0.0)}, "grip_L": 0.2, "grip_R": 0.2,
			"shoulder_L": (8.0, 0), "shoulder_R": (8.0, 0)}
	b = {"hips": (0, 0, 0.05), "hips_rot": (-6.0, 0, 0), "spine": (-8.0, 0, 0), "head": (-10.0, 0, 0),
			"leg_L": {"ik": ((0.11, 0.08, 0.18), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.12, -0.12, 0.3), (-0.2, -1, 0))},
			"foot_L": (-15.0, 8.0, 0), "foot_R": (8.0, 8.0, 0),
			"arm_L": {"fk": (46.0, 8.0, 28.0, 0.0)}, "arm_R": {"fk": (36.0, 28.0, 38.0, 0.0)}}
	keys = [(0.0, stand() | a), (0.45, b), (0.9, stand() | a)]
	return 0.9, True, lambda t: keyed(keys, t, loop=True, length=0.9)


def clip_land():
	keys = [
		(0.0, stand(hips=(0, 0, 0.0), arm_L={"fk": (55.0, 25.0, 30.0, 0.0)}, arm_R={"fk": (55.0, 25.0, 30.0, 0.0)})),
		(0.1, {"hips": (0, 0.03, -0.28), "hips_rot": (20.0, 0, 0), "spine": (20.0, 0, 0), "head": (-22.0, 0, 0),
				"arm_L": {"fk": (62.0, 40.0, 45.0, 6.0)}, "arm_R": {"fk": (62.0, 40.0, 45.0, 6.0)}}),
		(0.2, {"hips": (0, 0.02, -0.24), "hips_rot": (16.0, 0, 0), "spine": (16.0, 0, 0), "head": (-16.0, 0, 0)}),
		(0.45, stand()),
	]
	return 0.45, False, lambda t: keyed(keys, t)


def clip_roll():
	length = 0.8

	def pose(t):
		u = t / length
		a = 360.0 * smoother(min(1.0, max(0.0, (u - 0.08) / 0.84)))
		r = math.radians(a)
		# The hips' height over the ground through the roll (over the shoulders, on the back, onto the feet).
		hz = 0.62 + 0.14 * math.sin(r) - 0.3 * (1.0 - math.cos(r)) * 0.5 + 0.06 * (1.0 - math.cos(2 * r)) * 0.5
		hy = 0.12 * math.sin(r)
		tuck = math.sin(min(1.0, u / 0.25) * math.pi * 0.5) * (1.0 - smooth((u - 0.85) / 0.15))
		p = stand()
		p["hips"] = (0.0, hy, hz - 0.965)
		p["hips_rot"] = (a + 20.0 * tuck, 0, 0)
		p["spine"] = (35.0 * tuck, 0, 0)
		p["neck"] = (25.0 * tuck, 0)
		p["head"] = (25.0 * tuck, 0, 0)
		p["leg_L"] = {"fk": (115.0 * tuck + 60.0 * (1 - tuck), 8.0, 135.0 * tuck + 100.0 * (1 - tuck), 0)}
		p["leg_R"] = {"fk": (105.0 * tuck + 60.0 * (1 - tuck), 8.0, 140.0 * tuck + 100.0 * (1 - tuck), 0)}
		p["foot_L"] = (-20.0, 0, 0)
		p["foot_R"] = (-20.0, 0, 0)
		p["arm_L"] = {"fk": (55.0, 55.0 * tuck + 20.0, 95.0 * tuck + 30.0, 0)}
		p["arm_R"] = {"fk": (55.0, 65.0 * tuck + 20.0, 85.0 * tuck + 30.0, 0)}
		p["grip_L"] = 0.6
		p["grip_R"] = 0.6
		return p

	return length, False, pose


# Hanging and climbing: the wall's face 0.33 m in front of the root (the capsule off it), the hands on its edge.
WALL = -0.33


def hang_base(**kw):
	p = stand()
	p.update({
		"hips": (0, 0.03, -0.03),
		"hips_rot": (-4.0, 0, 0),
		"spine": (-4.0, 0, 0),
		"neck": (-6.0, 0),
		"head": (-14.0, 0, 0),
		"shoulder_L": (18.0, 4.0),
		"shoulder_R": (18.0, 4.0),
		"arm_L": {"ik": ((0.23, WALL + 0.02, 2.0), (1.0, 0.4, -0.6))},
		"arm_R": {"ik": ((-0.23, WALL + 0.02, 2.0), (-1.0, 0.4, -0.6))},
		"hand_L": (30.0, 0, 0),
		"hand_R": (30.0, 0, 0),
		"grip_L": 0.85,
		"grip_R": 0.85,
		"leg_L": {"ik": ((0.08, -0.06, 0.14), (0.2, -1, 0))},
		"leg_R": {"ik": ((-0.09, -0.02, 0.18), (-0.2, -1, 0))},
		"foot_L": (-35.0, 6.0, 0),
		"foot_R": (-30.0, 6.0, 0),
	})
	p.update(kw)
	return p


def clip_hang_idle():
	a = hang_base()
	b = hang_base(hips=(0.01, 0.06, -0.035), hips_rot=(-7.0, 2.0, 0), spine=(-6.0, -2.0, 0),
			leg_L={"ik": ((0.07, 0.0, 0.15), (0.2, -1, 0))}, leg_R={"ik": ((-0.08, 0.03, 0.2), (-0.2, -1, 0))}, head=(-18.0, 5.0, 0))
	keys = [(0.0, a), (1.3, b), (2.6, a)]
	return 2.6, True, lambda t: keyed(keys, t, loop=True, length=2.6)


def clip_hang_shimmy():
	a = hang_base(hips=(0.05, 0.03, -0.04), hips_rot=(-4.0, 0, -6.0), leg_L={"ik": ((0.14, -0.04, 0.16), (0.3, -1, 0))},
			leg_R={"ik": ((-0.03, -0.02, 0.13), (-0.2, -1, 0))}, spine=(-4.0, 0, 4.0))
	b = hang_base(hips=(-0.05, 0.03, -0.04), hips_rot=(-4.0, 0, 6.0), leg_L={"ik": ((0.03, -0.02, 0.13), (0.2, -1, 0))},
			leg_R={"ik": ((-0.14, -0.04, 0.16), (-0.3, -1, 0))}, spine=(-4.0, 0, -4.0))
	keys = [(0.0, a), (0.45, b), (0.9, a)]
	return 0.9, True, lambda t: keyed(keys, t, loop=True, length=0.9)


def climb_base(**kw):
	p = stand()
	p.update({
		"hips": (0, -0.08, -0.04),
		"hips_rot": (6.0, 0, 0),
		"spine": (2.0, 0, 0),
		"neck": (-12.0, 0),
		"head": (-14.0, 0, 0),
		"shoulder_L": (14.0, 4.0),
		"shoulder_R": (8.0, 4.0),
		"arm_L": {"ik": ((0.25, WALL + 0.04, 1.92), (1.0, 0.5, -0.7))},
		"arm_R": {"ik": ((-0.22, WALL + 0.04, 1.7), (-1.0, 0.5, -0.7))},
		"hand_L": (25.0, 0, 0),
		"hand_R": (25.0, 0, 0),
		"grip_L": 0.8,
		"grip_R": 0.8,
		"leg_L": {"ik": ((0.17, WALL + 0.1, 0.48), (1.0, -0.8, 0))},
		"leg_R": {"ik": ((-0.15, WALL + 0.1, 0.26), (-1.0, -0.8, 0))},
		"foot_L": (5.0, 15.0, 0),
		"foot_R": (5.0, 15.0, 0),
	})
	p.update(kw)
	return p


def clip_climb_idle():
	a = climb_base()
	b = climb_base(hips=(0.01, -0.07, -0.05), spine=(3.0, 2.0, 1.0), head=(-18.0, 6.0, 0))
	keys = [(0.0, a), (1.1, b), (2.2, a)]
	return 2.2, True, lambda t: keyed(keys, t, loop=True, length=2.2)


def clip_climb_move():
	a = climb_base(hips=(0.05, -0.08, -0.08), hips_rot=(6.0, 0, -5.0), spine=(2.0, 0, 5.0))
	b = climb_base(hips=(-0.05, -0.08, -0.08), hips_rot=(6.0, 0, 5.0), spine=(2.0, 0, -5.0),
			arm_L={"ik": ((0.24, WALL + 0.04, 1.7), (1.0, 0.5, -0.7))}, arm_R={"ik": ((-0.23, WALL + 0.04, 1.92), (-1.0, 0.5, -0.7))},
			leg_L={"ik": ((0.17, WALL + 0.1, 0.26), (1.0, -0.8, 0))}, leg_R={"ik": ((-0.15, WALL + 0.1, 0.48), (-1.0, -0.8, 0))})
	mid_a = climb_base(hips=(0.0, -0.09, 0.02), spine=(1.0, 0, 0))
	keys = [(0.0, a), (0.4, mid_a), (0.8, b), (1.2, mid_a), (1.6, a)]
	return 1.6, True, lambda t: keyed(keys, t, loop=True, length=1.6)


def clip_climb_jump():
	a = climb_base()
	keys = [
		(0.0, a),
		(0.16, climb_base(hips=(0, -0.12, -0.2), hips_rot=(10.0, 0, 0), spine=(8.0, 0, 0), head=(-25.0, 0, 0),
				arm_L={"ik": ((0.24, WALL + 0.06, 1.6), (1.0, 0.5, -0.7))}, arm_R={"ik": ((-0.24, WALL + 0.06, 1.6), (-1.0, 0.5, -0.7))},
				leg_L={"ik": ((0.17, WALL + 0.12, 0.32), (1.0, -0.8, 0))}, leg_R={"ik": ((-0.17, WALL + 0.12, 0.32), (-1.0, -0.8, 0))})),
		(0.32, climb_base(hips=(0, -0.06, 0.12), hips_rot=(0.0, 0, 0), spine=(-4.0, 0, 0), head=(-30.0, 0, 0),
				arm_L={"ik": ((0.2, WALL + 0.06, 2.3), (1.0, 0.4, -0.5))}, arm_R={"ik": ((-0.2, WALL + 0.06, 2.3), (-1.0, 0.4, -0.5))},
				leg_L={"ik": ((0.12, WALL + 0.2, 0.1), (0.4, -1, 0))}, leg_R={"ik": ((-0.12, WALL + 0.25, 0.2), (-0.4, -1, 0))},
				foot_L=(-40.0, 8.0, 0), foot_R=(-40.0, 8.0, 0))),
		(0.5, a),
	]
	return 0.5, False, lambda t: keyed(keys, t)


def clip_mantle_high():
	# Hands to an edge 1.85 m over the feet; the root (the feet) up onto it and 0.6 m in.
	E = 1.85
	base = stand()

	def hands(root, hz_rel=None, spread=0.22, y_rel=None):
		return None

	keys = [
		(0.0, dict(base, head=(-20.0, 0, 0))),
		(0.1, {"hips": (0, 0.03, -0.13), "hips_rot": (12.0, 0, 0), "spine": (6.0, 0, 0), "head": (-28.0, 0, 0),
				"arm_L": {"fk": (70.0, -30.0, 22.0, 6.0)}, "arm_R": {"fk": (70.0, -30.0, 22.0, 6.0)}}),
		(0.24, {"root": (0, -0.05, 0.25), "hips": (0, 0.0, 0.04), "hips_rot": (-2.0, 0, 0), "spine": (-4.0, 0, 0), "head": (-22.0, 0, 0),
				"arm_L": {"ik": ((0.22, WALL - 0.02 + 0.05, E - 0.25), (1.0, 0.3, -0.5))},
				"arm_R": {"ik": ((-0.22, WALL - 0.02 + 0.05, E - 0.25), (-1.0, 0.3, -0.5))},
				"hand_L": (35.0, 0, 0), "hand_R": (35.0, 0, 0), "grip_L": 0.85, "grip_R": 0.85,
				"leg_L": {"ik": ((0.11, WALL + 0.12, 0.42), (0.3, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.06, 0.06), (-0.2, -1, 0))},
				"foot_L": (10.0, 8.0, 0), "foot_R": (-35.0, 6.0, 0)}),
		(0.46, {"root": (0, -0.1, 0.85), "hips": (0, -0.04, 0.0), "hips_rot": (8.0, 0, 0), "spine": (14.0, 0, 0), "head": (-10.0, 0, 0),
				"arm_L": {"ik": ((0.24, WALL + 0.07, E - 0.85), (1.0, 0.8, -0.3))},
				"arm_R": {"ik": ((-0.24, WALL + 0.07, E - 0.85), (-1.0, 0.8, -0.3))},
				"leg_L": {"ik": ((0.11, WALL + 0.18, 0.35), (0.3, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.1, 0.0), (-0.2, -1, 0))},
				"foot_L": (15.0, 8.0, 0), "foot_R": (-30.0, 6.0, 0)}),
		(0.64, {"root": (0, -0.24, 1.45), "hips": (0, 0.02, -0.12), "hips_rot": (24.0, 0, 0), "spine": (30.0, 0, 0), "head": (-16.0, 0, 0),
				"arm_L": {"ik": ((0.25, WALL + 0.24, E - 1.45 + 0.02), (1.0, 1.0, 0.2))},
				"arm_R": {"ik": ((-0.25, WALL + 0.24, E - 1.45 + 0.02), (-1.0, 1.0, 0.2))},
				"hand_L": (70.0, 0, 0), "hand_R": (70.0, 0, 0),
				"leg_L": {"ik": ((0.12, -0.12, E - 1.45 + ANKLE + 0.02), (0.3, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.2, -0.05), (-0.2, -1, 0))},
				"foot_L": (0.0, 8.0, 0), "foot_R": (-25.0, 6.0, 0)}),
		(0.84, {"root": (0, -0.46, 1.78), "hips": (0, 0.02, -0.18), "hips_rot": (20.0, 0, 0), "spine": (16.0, 0, 0), "head": (-8.0, 0, 0),
				"arm_L": {"fk": (72.0, 25.0, 35.0, 6.0)}, "arm_R": {"fk": (72.0, 15.0, 30.0, 6.0)},
				"hand_L": (10.0, 0, 0), "hand_R": (10.0, 0, 0), "grip_L": 0.4, "grip_R": 0.4,
				"leg_L": {"ik": ((0.11, 0.1, E - 1.78 + ANKLE), (0.15, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.14, E - 1.78 + ANKLE + 0.12), (-0.15, -1, 0))},
				"foot_L": (0.0, 7.0, 0), "foot_R": (-10.0, 7.0, 0)}),
		(1.1, dict(base, root=(0, -0.6, E))),
	]
	return 1.1, False, lambda t: keyed(keys, t)


def clip_mantle_low():
	E = 1.0
	base = stand()
	keys = [
		(0.0, dict(base, head=(-10.0, 0, 0))),
		(0.14, {"root": (0, -0.1, 0.06), "hips": (0, 0.0, -0.1), "hips_rot": (24.0, 0, 0), "spine": (18.0, 0, 0), "head": (-18.0, 0, 0),
				"arm_L": {"ik": ((0.2, WALL + 0.06, E - 0.06 + 0.02), (1.0, 0.8, 0.0))},
				"arm_R": {"ik": ((-0.2, WALL + 0.06, E - 0.06 + 0.02), (-1.0, 0.8, 0.0))},
				"hand_L": (60.0, 0, 0), "hand_R": (60.0, 0, 0), "grip_L": 0.5, "grip_R": 0.5,
				"leg_L": {"ik": ((0.1, -0.15, 0.5), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.06, 0.05), (-0.2, -1, 0))},
				"foot_L": (5.0, 8.0, 0), "foot_R": (-30.0, 7.0, 0)}),
		(0.32, {"root": (0, -0.3, 0.72), "hips": (0, 0.03, -0.12), "hips_rot": (26.0, 0, 0), "spine": (22.0, 0, 0), "head": (-14.0, 0, 0),
				"arm_L": {"ik": ((0.22, WALL + 0.26, E - 0.72 + 0.02), (1.0, 1.0, 0.2))},
				"arm_R": {"ik": ((-0.22, WALL + 0.26, E - 0.72 + 0.02), (-1.0, 1.0, 0.2))},
				"leg_L": {"ik": ((0.1, -0.12, E - 0.72 + ANKLE), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.3, 0.02), (-0.2, -1, 0))},
				"foot_L": (0.0, 8.0, 0), "foot_R": (-30.0, 7.0, 0)}),
		(0.46, {"root": (0, -0.46, 0.97), "hips": (0, 0.02, -0.1), "hips_rot": (14.0, 0, 0), "spine": (10.0, 0, 0), "head": (-6.0, 0, 0),
				"arm_L": {"fk": (72.0, 20.0, 30.0, 6.0)}, "arm_R": {"fk": (72.0, 10.0, 25.0, 6.0)}, "grip_L": 0.35, "grip_R": 0.35,
				"leg_L": {"ik": ((0.1, 0.02, E - 0.97 + ANKLE), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.05, E - 0.97 + ANKLE + 0.15), (-0.2, -1, 0))},
				"foot_L": (0.0, 7.0, 0), "foot_R": (-5.0, 7.0, 0)}),
		(0.6, dict(base, root=(0, -0.55, E))),
	]
	return 0.6, False, lambda t: keyed(keys, t)


def clip_vault():
	E = 1.0
	run = gait_pose("run", 0.0)
	keys = [
		(0.0, dict(run)),
		(0.12, {"root": (0, -0.38, 0.32), "hips": (0, 0.02, -0.08), "hips_rot": (18.0, 0, -18.0), "spine": (16.0, 8.0, -8.0), "head": (-10.0, -8.0, 0),
				"arm_L": {"ik": ((0.14, -0.22, E - 0.32 + 0.03), (1.0, 0.8, 0.2))}, "arm_R": {"fk": (35.0, 30.0, 40.0, 0.0)},
				"hand_L": (65.0, 0, 0), "grip_L": 0.4,
				"leg_L": {"ik": ((0.12, 0.05, 0.25), (0.6, -1, 0))}, "leg_R": {"ik": ((-0.02, 0.1, 0.38), (-0.2, -1, 0.3))},
				"foot_L": (0.0, 10.0, 0), "foot_R": (-20.0, 10.0, 0), "toes_L": 0.0, "toes_R": 0.0}),
		(0.27, {"root": (0, -0.8, 0.62), "hips": (0, 0.0, -0.06), "hips_rot": (10.0, 0, -28.0), "spine": (10.0, 12.0, -10.0), "head": (-6.0, -6.0, 0),
				"arm_L": {"ik": ((0.2, 0.2, E - 0.62 + 0.03), (1.0, 0.6, 0.4))}, "arm_R": {"fk": (30.0, 20.0, 30.0, 0.0)},
				"leg_L": {"ik": ((0.04, -0.2, 0.35), (1.0, -1, 0))}, "leg_R": {"ik": ((-0.12, -0.15, 0.42), (-0.3, -1, 0.3))},
				"foot_L": (5.0, 10.0, 0), "foot_R": (0.0, 10.0, 0)}),
		(0.42, {"root": (0, -1.25, 0.3), "hips": (0, 0.0, -0.04), "hips_rot": (4.0, 0, -8.0), "spine": (6.0, 4.0, -2.0), "head": (-4.0, 0, 0),
				"arm_L": {"fk": (55.0, -10.0, 30.0, 0.0)}, "arm_R": {"fk": (45.0, 30.0, 40.0, 0.0)}, "grip_L": 0.4,
				"leg_L": {"ik": ((0.09, -0.18, 0.05), (0.2, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.12, 0.2), (-0.2, -1, 0))},
				"foot_L": (5.0, 6.0, 0), "foot_R": (-25.0, 6.0, 0)}),
		(0.56, dict(gait_pose("run", 0.36), root=(0, -1.6, 0.0))),
	]
	return 0.56, False, lambda t: keyed(keys, t)


def dive_base(**kw):
	p = stand()
	p.update({
		"hips": (0, 0, 0.0),
		"hips_rot": (78.0, 0, 0),
		"spine": (-24.0, 0, 0),
		"neck": (-18.0, 0),
		"head": (-28.0, 0, 0),
		"shoulder_L": (16.0, -4.0),
		"shoulder_R": (16.0, -4.0),
		"arm_L": {"fk": (12.0, 8.0, 8.0, -20.0)},
		"arm_R": {"fk": (12.0, 8.0, 8.0, -20.0)},
		"hand_L": (-10.0, 0, 0),
		"hand_R": (-10.0, 0, 0),
		"grip_L": 0.1,
		"grip_R": 0.1,
		"leg_L": {"fk": (-12.0, 2.0, 8.0, 0)},
		"leg_R": {"fk": (-10.0, 2.0, 14.0, 0)},
		"foot_L": (-45.0, 0, 0),
		"foot_R": (-45.0, 0, 0),
	})
	p.update(kw)
	return p


def clip_dive():
	a = dive_base()
	b = dive_base(hips_rot=(80.0, 0, 2.0), arm_L={"fk": (16.0, 4.0, 10.0, -20.0)}, arm_R={"fk": (9.0, 10.0, 6.0, -20.0)},
			leg_L={"fk": (-8.0, 2.0, 14.0, 0)}, leg_R={"fk": (-14.0, 2.0, 6.0, 0)})
	keys = [(0.0, a), (0.6, b), (1.2, a)]
	return 1.2, True, lambda t: keyed(keys, t, loop=True, length=1.2)


def clip_dive_start():
	keys = [
		(0.0, stand(hips=(0, 0, -0.02))),
		(0.18, {"hips": (0, 0.04, -0.2), "hips_rot": (25.0, 0, 0), "spine": (20.0, 0, 0), "head": (-20.0, 0, 0),
				"arm_L": {"fk": (70.0, -40.0, 20.0, 6.0)}, "arm_R": {"fk": (70.0, -40.0, 20.0, 6.0)}}),
		(0.36, {"hips": (0, -0.05, 0.05), "hips_rot": (50.0, 0, 0), "spine": (-6.0, 0, 0), "head": (-30.0, 0, 0),
				"leg_L": {"ik": ((0.1, 0.25, 0.25), (0.15, -1, 0))}, "leg_R": {"ik": ((-0.1, 0.35, 0.45), (-0.15, -1, 0))},
				"foot_L": (-50.0, 0, 0), "foot_R": (-50.0, 0, 0),
				"arm_L": {"fk": (25.0, 30.0, 15.0, -10.0)}, "arm_R": {"fk": (25.0, 30.0, 15.0, -10.0)}}),
		(0.55, dive_base()),
	]
	return 0.55, False, lambda t: keyed(keys, t)


def clip_hay_exit():
	# Lying on the back in the hay (the root on the cart's bed), sitting up, standing, stepping onto the side, down.
	BED = 0.85
	lying = stand(root=(0, 0, BED), hips=(0, 0.25, 0.42 - 0.965), hips_rot=(-82.0, 0, 0), spine=(8.0, 0, 0), neck=(10.0, 0), head=(12.0, 0, 0),
			arm_L={"fk": (35.0, -10.0, 40.0, 0)}, arm_R={"fk": (30.0, -15.0, 35.0, 0)},
			leg_L={"fk": (55.0, 12.0, 70.0, 0)}, leg_R={"fk": (40.0, 10.0, 50.0, 0)}, foot_L=(-10.0, 0, 0), foot_R=(-10.0, 0, 0), grip_L=0.2, grip_R=0.2)
	sit = stand(root=(0, 0, BED), hips=(0, 0.12, 0.4 - 0.965), hips_rot=(-10.0, 0, 0), spine=(20.0, 0, 0), neck=(5.0, 0), head=(5.0, 0, 0),
			arm_L={"fk": (70.0, 20.0, 40.0, 0)}, arm_R={"fk": (70.0, 25.0, 45.0, 0)},
			leg_L={"fk": (95.0, 8.0, 110.0, 0)}, leg_R={"fk": (85.0, 8.0, 90.0, 0)}, foot_L=(0.0, 0, 0), foot_R=(0.0, 0, 0))
	keys = [
		(0.0, lying),
		(0.45, sit),
		(0.8, stand(root=(0, -0.05, BED), hips=(0, 0.04, -0.35), hips_rot=(30.0, 0, 0), spine=(20.0, 0, 0), head=(-10.0, 0, 0),
				arm_L={"fk": (65.0, 30.0, 40.0, 0)}, arm_R={"fk": (65.0, 30.0, 40.0, 0)})),
		(1.1, stand(root=(0, -0.3, BED), hips=(0, 0, -0.03))),
		(1.35, stand(root=(0, -0.72, BED + 0.3), hips=(0, 0, -0.1), hips_rot=(10.0, 0, 0),
				leg_L={"ik": ((0.1, -0.05, ANKLE - 0.05), (0.15, -1, 0))}, leg_R={"ik": ((-0.1, 0.25, 0.25), (-0.15, -1, 0))},
				foot_R=(-30.0, 7.0, 0), arm_L={"fk": (60.0, 20.0, 30.0, 0)}, arm_R={"fk": (60.0, -10.0, 30.0, 0)})),
		(1.6, stand(root=(0, -1.25, 0.45), hips=(0, 0, 0.0), hips_rot=(4.0, 0, 0),
				leg_L={"ik": ((0.1, -0.1, 0.0), (0.15, -1, 0))}, leg_R={"ik": ((-0.1, 0.1, 0.1), (-0.15, -1, 0))},
				foot_L=(10.0, 7.0, 0), foot_R=(-10.0, 7.0, 0), arm_L={"fk": (45.0, 15.0, 30.0, 0)}, arm_R={"fk": (45.0, 15.0, 30.0, 0)})),
		(1.8, stand(root=(0, -1.5, 0.0), hips=(0, 0.02, -0.22), hips_rot=(16.0, 0, 0), spine=(14.0, 0, 0),
				arm_L={"fk": (62.0, 30.0, 40.0, 0)}, arm_R={"fk": (62.0, 30.0, 40.0, 0)})),
		(2.2, stand(root=(0, -1.6, 0.0))),
	]
	return 2.2, False, lambda t: keyed(keys, t)


def clip_perch():
	base = stand(
		hips=(0, 0.06, -0.47), hips_rot=(28.0, 0, 0), spine=(18.0, 0, 0), neck=(-10.0, 0), head=(-14.0, 0, 0),
		leg_L={"ik": ((0.13, -0.12, ANKLE), (0.4, -1, 0))}, leg_R={"ik": ((-0.12, 0.16, ANKLE + 0.06), (-0.4, -1, 0))},
		foot_L=(0.0, 10.0, 0), foot_R=(-40.0, 10.0, 0), toes_R=40.0,
		arm_L={"ik": ((0.2, -0.38, 0.58), (1.0, 0.5, -0.5))}, arm_R={"fk": (78.0, 35.0, 35.0, 0)}, hand_L=(10.0, 0, 0),
		grip_L=0.5, grip_R=0.4)
	keys = [
		(0.0, base),
		(1.2, {"neck": (-8.0, 20.0), "head": (-12.0, 18.0, 2.0), "spine": (17.0, 6.0, 0)}),
		(2.4, {"neck": (-12.0, -18.0), "head": (-16.0, -16.0, -2.0), "spine": (19.0, -5.0, 0)}),
		(3.6, base),
	]
	return 3.6, True, lambda t: keyed(keys, t, loop=True, length=3.6)


def clips():
	out = {}
	for name in ("walk", "run", "sprint"):
		G = GAITS[name]
		out[name] = (G["T"], True, (lambda n: (lambda t: gait_pose(n, t)))(name))
	out["idle"] = clip_idle()
	out["jump"] = clip_jump()
	out["fall"] = clip_fall()
	out["land"] = clip_land()
	out["roll"] = clip_roll()
	out["hang_idle"] = clip_hang_idle()
	out["hang_shimmy"] = clip_hang_shimmy()
	out["climb_idle"] = clip_climb_idle()
	out["climb_move"] = clip_climb_move()
	out["climb_jump"] = clip_climb_jump()
	out["mantle_high"] = clip_mantle_high()
	out["mantle_low"] = clip_mantle_low()
	out["vault"] = clip_vault()
	out["dive"] = clip_dive()
	out["dive_start"] = clip_dive_start()
	out["hay_exit"] = clip_hay_exit()
	out["perch"] = clip_perch()
	return out


def bake(rig, name, length, loop, fn):
	from mannequin_rig import write_action

	n = max(2, int(round(length * FPS)))
	frames = []
	count = n if loop else n + 1
	for i in range(count):
		t = min(length, i / FPS)
		rig.build(fn(t))
		frames.append(rig.snapshot())
	return write_action(rig, name, frames, loop)
