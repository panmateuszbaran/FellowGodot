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


def foot_on_ground(contact, pitch_up, toe_out=0.0):
	"""The ankle of a foot whose flat place on the ground has its ankle over contact (y forward of the hips negative),
	turned out by toe_out degrees (about the up axis, as the rig turns it) and toes-up by pitch_up (rolling on its heel)
	or heel-up by -pitch_up (rolling on the sole under its ball). Milestone 155: the pivots are the turned foot's own
	(before, the heel's and the ball's were placed as if the foot were not turned: changing from one to the other moved
	the foot 1.5 cm sideways), and the ball's is on the sole, not at the ball's joint 2.4 cm over it (the foot sank as its
	heel began to rise)."""
	cy, cx = contact
	a = math.radians(toe_out)
	c, s_ = math.cos(a), math.sin(a)

	def turn(v):
		return (v[0] * c - v[1] * s_, v[0] * s_ + v[1] * c, v[2])

	local = (0.0, HEEL[1], -ANKLE) if pitch_up >= 0.0 else (0.0, BALL[1], -ANKLE)
	at = turn(local)
	piv = (cx + at[0], cy + at[1], 0.0)
	off = turn(_rot_x((-local[0], -local[1], -local[2]), -pitch_up))
	return (piv[0] + off[0], piv[1] + off[1], piv[2] + off[2])


LEG = 0.43  # the thigh's and the shin's length (m)
HIP_JOINT = (0.09, 0.94)  # the hip joints at rest: apart (half), height


def _keys(keys, u):
	"""A curve through (u, value) keys, eased between them."""
	if u <= keys[0][0]:
		return keys[0][1]
	for (u0, v0), (u1, v1) in zip(keys, keys[1:]):
		if u <= u1:
			return v0 + (v1 - v0) * smooth((u - u0) / max(1e-6, u1 - u0))
	return keys[-1][1]


GAITS = {
	# speed (m/s), cycle (s), stance share, the stance's share ahead of the hips, foot lift (m), the hips' offset (added to
	# the height the legs give), sway (m), pelvis yaw and roll (degrees), lean, arm swing, elbow (back, forward), arms'
	# lowering, feet apart (half, m), heel kick (m), the heel's strike pitch; knee: the stance knee's flexion over the stance
	# (0 the strike, 1 the toe-off); push: where in the stance the heel may begin to rise; heel_max: its rise at most
	# (degrees, 60 by default); retract: the share of the ground's speed the foot has (backwards under the hips) as it
	# leaves and reaches the ground (1 by default: none on the ground; running, less — the reach forward kept short).
	# Milestone 155 (docs/showcase_animation_quality.md): the hips as high as the supporting legs reach at their knee's
	# flexion, the heel raised as far as the knee's flexion needs — the knee is never left to the IK's reach (which
	# locked it straight: the ankle had been asked to go 1.0 m from the hip on a 0.86 m leg).
	"walk": dict(v=1.6, T=0.95, duty=0.58, front=0.46, push=0.5, lift=0.1, hip=0.0, sway=0.025, yaw=6.0, roll=4.0, lean=4.0,
			swing=24.0, elbow=(12.0, 32.0), lower=79.0, width=0.09, kick=0.0, strike=16.0, run=False,
			knee=[(0.0, 4.0), (0.2, 13.0), (0.5, 6.0), (0.75, 16.0), (1.0, 42.0)]),
	# Milestone 155: a jog between the walk and the run, so that a blend of two gaits mixes strides of like stances (a
	# walk's foot planted while a run's swings dragged the foot as the body sped up).
	"jog": dict(v=3.0, T=0.8, duty=0.4, front=0.45, push=0.5, heel_max=65.0, retract=0.6, lift=0.2, hip=0.0, sway=0.018,
			yaw=8.0, roll=4.5, lean=7.0, swing=36.0, elbow=(55.0, 85.0), lower=76.0, width=0.07, kick=0.1, strike=8.0, run=True,
			knee=[(0.0, 18.0), (0.45, 38.0), (1.0, 20.0)]),
	"run": dict(v=4.4, T=0.7, duty=0.32, front=0.45, push=0.5, heel_max=70.0, retract=0.7, lift=0.28, hip=0.0, sway=0.012, yaw=10.0, roll=5.0, lean=11.0,
			swing=48.0, elbow=(78.0, 105.0), lower=74.0, width=0.06, kick=0.2, strike=4.0, run=True,
			knee=[(0.0, 22.0), (0.45, 47.0), (1.0, 22.0)]),
	"sprint": dict(v=7.0, T=0.62, duty=0.22, front=0.45, push=0.5, heel_max=70.0, retract=0.7, lift=0.38, hip=0.0, sway=0.008, yaw=12.0, roll=5.0, lean=18.0,
			swing=62.0, elbow=(72.0, 110.0), lower=74.0, width=0.05, kick=0.0, strike=0.0, run=True,
			knee=[(0.0, 26.0), (0.45, 48.0), (1.0, 24.0)]),
}


TOE_OUT = 5.0  # the gaits' feet turned out (degrees)


def _foot(contact, pitch_up):
	"""A gait's foot on the ground (foot_on_ground, turned out as the rig turns it: outwards on either side)."""
	return foot_on_ground(contact, pitch_up, TOE_OUT if contact[1] >= 0.0 else -TOE_OUT)


def _cy(G, u):
	"""The stance foot's place (its ankle's, flat) at u, forward of the hips negative."""
	stride = G["v"] * G["T"] * G["duty"]
	return -stride * G["front"] + 0.02 + stride * u


def _reach(k_deg):
	return 2.0 * LEG * math.cos(math.radians(k_deg) * 0.5)


def _hip(sx, sway, hz):
	return (sx * HIP_JOINT[0] + sway, 0.0, HIP_JOINT[1] + hz)


# The knee's least flexion for a target the soft IK reaches exactly (mannequin_rig.SOFT of the leg's reach): a stance
# foot asked for less would be pulled off its place on the ground (it slides).
K_SOFT = math.degrees(2.0 * math.acos(0.99))
# A planted leg's least flexion in its push (the heel rising): K_SOFT and a margin for what the reach above leaves out
# (the pelvis's turn and roll move the hip joints by 1-2 cm).
K_PUSH = K_SOFT + 6.0


def _height_for(x, ankle, k_deg, sway):
	"""The hips' offset at which a leg reaching ankle has its knee flexed k_deg (at least K_SOFT)."""
	d = _reach(max(k_deg, K_SOFT))
	dx = ankle[0] - ((1.0 if x >= 0.0 else -1.0) * HIP_JOINT[0] + sway)
	return ankle[2] + math.sqrt(max(0.0, d * d - dx * dx - ankle[1] * ankle[1])) - HIP_JOINT[1]


def _strike(G, u):
	return G["strike"] * (1.0 - smooth(u / 0.2)) if u < 0.2 else 0.0


def _sway(G, phase_l):
	w = 2.0 * math.pi * phase_l
	return G["sway"] * math.cos(w - math.pi * G["duty"])


def _hips_z(G, phase_l):
	"""The hips' height offset over the cycle: as the supporting legs and the flight give it, no higher than every planted
	foot can be reached (the heel raised as far as it may) — a foot asked for more would slide towards the hips."""
	z = _hips_z_free(G, phase_l)
	duty = G["duty"]
	sway = _sway(G, phase_l)
	for sx, ph in ((1.0, phase_l), (-1.0, (phase_l + 0.5) % 1.0)):
		x = sx * G["width"]
		if ph < duty:
			u = ph / duty
			pitch = _strike(G, u) if u <= G["push"] else -G.get("heel_max", 60.0)
			z = min(z, _height_for(x, _foot((_cy(G, u), x), pitch), K_SOFT if u <= G["push"] else K_PUSH, sway) + G["hip"])
		else:
			# The foot about to strike reaches its place exactly (short of it, it would come down and then slide on to it).
			s_ = (ph - duty) / (1.0 - duty)
			if s_ > 0.6:
				cap = _height_for(x, _foot((_cy(G, 0.0), x), _strike(G, 0.0)), K_SOFT, sway) + G["hip"]
				z = min(z, cap + (1.0 - smooth((s_ - 0.6) / 0.4)) * 0.3)
	return z


def _hips_z_free(G, phase_l):
	"""The hips' height offset over the cycle, from the supporting legs (and in the air, running, the flight's arc)."""
	duty = G["duty"]
	sway = _sway(G, phase_l)
	legs = []
	for sx, ph in ((1.0, phase_l), (-1.0, (phase_l + 0.5) % 1.0)):
		legs.append((sx * G["width"], ph))
	if not G["run"]:
		# Walking: each foot holds the hips up through its stance (fading out as its heel rises) and the foot about to
		# strike draws them down to where it will reach.
		num = den = 0.0
		for x, ph in legs:
			if ph < duty:
				u = ph / duty
				w = 1.0 - smooth((u - 0.55) / 0.35)
				if w > 0.0:
					ankle = _foot((_cy(G, u), x), _strike(G, u))
					num += w * _height_for(x, ankle, _keys(G["knee"], u), sway)
					den += w
			else:
				s_ = (ph - duty) / (1.0 - duty)
				w = smooth((s_ - 0.45) / 0.55)
				if w > 0.0:
					ankle = _foot((_cy(G, 0.0), x), _strike(G, 0.0))
					num += w * _height_for(x, ankle, _keys(G["knee"], 0.0), sway)
					den += w
		return num / den + G["hip"]
	# Running: the stance leg holds the hips at its knee's flexion until its heel rises; from there the body flies on an
	# arc (the push off, the flight) to where the next foot's strike will take it.
	push = G["push"]
	for x, ph in legs:
		if ph < duty * push:
			u = ph / duty
			ankle = _foot((_cy(G, u), x), _strike(G, u))
			return _height_for(x, ankle, _keys(G["knee"], u), sway) + G["hip"]
	T = G["T"]
	# The leg that passed its push last, and the other's next strike.
	best = None
	for i, (x, ph) in enumerate(legs):
		since = ((ph - duty * push) % 1.0) * T
		if best is None or since < best[0]:
			best = (since, i)
	since, i = best
	x0, _ = legs[i]
	x1, ph1 = legs[1 - i]
	until = ((1.0 - ph1) % 1.0) * T
	total = since + until
	u0 = push
	z0 = _height_for(x0, _foot((_cy(G, u0), x0), _strike(G, u0)), _keys(G["knee"], u0), sway)
	z1 = _height_for(x1, _foot((_cy(G, 0.0), x1), _strike(G, 0.0)), _keys(G["knee"], 0.0), sway)
	return z0 + (z1 - z0) * since / max(1e-6, total) + 0.5 * 9.8 * since * (total - since) + G["hip"]


def _push_pitch(G, x, u, hip):
	"""The heel's rise (degrees) at which the stance knee is flexed as G's knee curve says at u (the foot on its ball)."""
	d = _reach(max(_keys(G["knee"], u), K_PUSH))
	cy = _cy(G, u)

	def dist(p):
		return math.dist(hip, _foot((cy, x), -p))

	top = G.get("heel_max", 60.0)
	if dist(0.0) <= d:
		return 0.0
	if dist(top) >= d:
		return top
	lo, hi = 0.0, top
	for _ in range(30):
		mid = 0.5 * (lo + hi)
		if dist(mid) > d:
			lo = mid
		else:
			hi = mid
	return 0.5 * (lo + hi)


def _stance_pose(G, x, u, phase_l):
	"""The stance foot at u: its ankle and pitch (toes up positive)."""
	if u <= G["push"]:
		pitch = _strike(G, u)
		return _foot((_cy(G, u), x), pitch), pitch
	hip = _hip(1.0 if x >= 0.0 else -1.0, _sway(G, phase_l), _hips_z(G, phase_l) - G["hip"])
	pitch = -_push_pitch(G, x, u, hip)
	return _foot((_cy(G, u), x), pitch), pitch


def gait_pose(name, t):
	G = GAITS[name]
	T = G["T"]
	duty = G["duty"]
	p = stand()
	phase_l = (t / T) % 1.0
	w = 2.0 * math.pi * phase_l
	mid = 2.0 * math.pi * duty * 0.5
	sway = _sway(G, phase_l)
	hz = _hips_z(G, phase_l)
	for side, sx, ph in (("L", 1.0, phase_l), ("R", -1.0, (phase_l + 0.5) % 1.0)):
		x = sx * G["width"]
		if ph < duty:
			ankle, pitch = _stance_pose(G, x, ph / duty, phase_l)
			toes = -pitch if pitch < 0.0 else 0.0
		else:
			s_ = (ph - duty) / (1.0 - duty)
			e = smoother(s_)
			# From where the toe-off left the ankle (the heel up) to where the strike will put it (milestone 155: before, the
			# swing began with the heel back on the ground — the ankle jumped and the knee went straight).
			off_phase = (phase_l - (ph - duty)) % 1.0
			a0, p0 = _stance_pose(G, x, 1.0, off_phase)
			a1 = _foot((_cy(G, 0.0), x), _strike(G, 0.0))
			# Leaving and reaching the ground at the ground's speed (backwards at v under the hips; milestone 155: before,
			# the foot came down still moving forwards with the body and slid on landing) — a Hermite curve.
			m = G["v"] * (1.0 - duty) * T * G.get("retract", 1.0)
			h00, h10, h01, h11 = 2 * s_**3 - 3 * s_**2 + 1, s_**3 - 2 * s_**2 + s_, -2 * s_**3 + 3 * s_**2, s_**3 - s_**2
			cy = h00 * a0[1] + h10 * m + h01 * a1[1] + h11 * m
			# The heel kicked up behind (running), then the knee drives forward.
			cy += G["kick"] * 0.35 * math.sin(math.pi * min(1.0, s_ * 1.6)) * (1.0 - s_)
			lift = G["lift"] * math.sin(math.pi * s_ ** G.get("lift_late", 1.0)) ** 0.8 + G["kick"] * math.sin(math.pi * min(1.0, s_ * 1.4)) * (1.0 - s_) ** 1.5
			# The foot kept pointing down while it lifts (turned flat at once, the ball dragged forwards on the ground).
			pitch = p0 * (1.0 - smooth((s_ - 0.12) / 0.4)) + _strike(G, 0.0) * smooth((s_ - 0.6) / 0.4)
			ankle = (x, cy, a0[2] + (a1[2] - a0[2]) * e + lift)
			toes = 0.0
		p["leg_" + side] = {"ik": (ankle, (sx * 0.12, -1.0, 0.0))}
		p["foot_" + side] = (pitch, TOE_OUT, 0.0)
		p["toes_" + side] = toes
	yaw = -G["yaw"] * math.cos(w)
	roll = -G["roll"] * math.cos(w - mid)
	p["hips"] = (sway, 0.0, hz)
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
# Hanging braced (FellowTraversal3D's hang_brace_height): the feet against the wall this high over the root, the balls
# of the feet on it (the ankle 0.12 m out), the knees bent towards it (milestone 153: 0.42, the feet under the hips,
# not drawn up to the chest).
BRACE = 0.42
# Milestone 153: how far the hanging torso leans in to the wall (degrees) over every hanging clip's own lean.
HANG_LEAN = 18.0
BRACE_Y = WALL + 0.12


def _hands(zl=2.0, zr=2.0, xl=0.23, xr=-0.23, yl=WALL + 0.02, yr=WALL + 0.02):
	return {"arm_L": {"ik": ((xl, yl, zl), (1.0, 0.4, -0.6))}, "arm_R": {"ik": ((xr, yr, zr), (-1.0, 0.4, -0.6))}}


def _feet(zl=BRACE, zr=BRACE, xl=0.11, xr=-0.11, yl=BRACE_Y, yr=BRACE_Y):
	return {"leg_L": {"ik": ((xl, yl, zl), (0.35, -1.0, 0.35))}, "leg_R": {"ik": ((xr, yr, zr), (-0.35, -1.0, 0.35))}}


def hang_base(**kw):
	"""Hanging braced: the arms up to the edge, the hips back off the wall, the knees bent and the feet on the wall."""
	p = stand()
	p.update({
		"hips": (0, 0.13, -0.05),
		"hips_rot": (10.0, 0, 0),
		"spine": (-12.0, 0, 0),
		"neck": (-8.0, 0),
		"head": (-16.0, 0, 0),
		"shoulder_L": (18.0, 4.0),
		"shoulder_R": (18.0, 4.0),
		"hand_L": (30.0, 0, 0),
		"hand_R": (30.0, 0, 0),
		"grip_L": 0.85,
		"grip_R": 0.85,
		"foot_L": (8.0, 6.0, 0),
		"foot_R": (8.0, 6.0, 0),
		"toes_L": 25.0,
		"toes_R": 25.0,
	})
	p.update(_hands())
	p.update(_feet())
	p.update(kw)
	# Milestone 153: the torso leaning in to the wall over the hips (the chest near the stone, the arms nearly straight up
	# to the hold; it had leaned back, the shoulders out of the arms' reach), the head kept looking up at the hold.
	sp = p["spine"]
	p["spine"] = (sp[0] + HANG_LEAN, sp[1], sp[2])
	hr = p["hips_rot"]
	p["hips_rot"] = (hr[0] + HANG_LEAN * 0.4, hr[1], hr[2])
	hd = p["head"]
	p["head"] = (hd[0] - HANG_LEAN * 0.8, hd[1], hd[2])
	return p


def clip_hang_idle():
	a = hang_base()
	b = hang_base(hips=(0.012, 0.12, -0.06), hips_rot=(10.0, 2.0, 0), spine=(-12.0, -2.0, 0), head=(-20.0, 6.0, 0),
			**_feet(zl=BRACE - 0.02, zr=BRACE + 0.02))
	keys = [(0.0, a), (1.3, b), (2.6, a)]
	return 2.6, True, lambda t: keyed(keys, t, loop=True, length=2.6)


def clip_hang_shimmy():
	# The hips swaying under the hands, the feet stepping along the wall in turn.
	a = hang_base(hips=(0.05, 0.1, -0.06), hips_rot=(8.0, 0, -6.0), spine=(-10.0, 0, 4.0),
			**_feet(xl=0.17, xr=-0.05, zl=BRACE + 0.04, zr=BRACE))
	b = hang_base(hips=(-0.05, 0.1, -0.06), hips_rot=(8.0, 0, 6.0), spine=(-10.0, 0, -4.0),
			**_feet(xl=0.05, xr=-0.17, zl=BRACE, zr=BRACE + 0.04))
	keys = [(0.0, a), (0.45, b), (0.9, a)]
	return 0.9, True, lambda t: keyed(keys, t, loop=True, length=0.9)


def clip_hang_reach(lead):
	"""Up to the ledge above hand over hand (0.75 s, FellowTraversal3D's TRANSFER_REACH_UP; its hands and feet are put on
	the stone by the pose's IK): the leading hand reaching up, the body stretching and turning towards it, then pulled
	up, the knees driving up the wall, the other hand following; the head looking up at the hold. lead: "L" or "R"."""
	s = 1.0 if lead == "L" else -1.0
	R = 1.15  # the ledge's height over the old one (the IK puts the hands where it really is)
	base = hang_base()

	def side(xl, xr, zl, zr):
		# The hands' places with the leading one first (mirrored for the right).
		return _hands(zl=zl, zr=zr, xl=xl, xr=xr) if s > 0 else _hands(zl=zr, zr=zl, xl=-xr, xr=-xl)

	def feet(z_lead, z_other):
		# The feet's heights: the leading hand's side's foot, the other's (the other steps first).
		return _feet(zl=z_lead, zr=z_other) if s > 0 else _feet(zl=z_other, zr=z_lead)

	keys = [
		(0.0, base),
		(0.18, hang_base(hips=(-0.04 * s, 0.12, -0.12), hips_rot=(6.0, -6.0 * s, 4.0 * s), spine=(-16.0, 10.0 * s, -6.0 * s),
				neck=(-12.0, 6.0 * s), head=(-28.0, 8.0 * s, 0), **side(0.2, -0.23, 2.0 + R * 0.7, 2.0),
				**feet(BRACE - 0.05, BRACE - 0.05))),
		(0.34, hang_base(hips=(-0.03 * s, 0.1, -0.16), hips_rot=(4.0, -4.0 * s, 3.0 * s), spine=(-14.0, 8.0 * s, -4.0 * s),
				neck=(-10.0, 4.0 * s), head=(-24.0, 6.0 * s, 0), **side(0.24, -0.23, 2.0 + R, 2.0),
				**feet(BRACE, BRACE + 0.25))),
		(0.5, hang_base(hips=(0.02 * s, 0.04, 0.02), hips_rot=(12.0, 3.0 * s, -2.0 * s), spine=(-2.0, -4.0 * s, 2.0 * s),
				neck=(-6.0, -2.0 * s), head=(-18.0, -6.0 * s, 0), **side(0.24, -0.18, 2.0 + R * 0.55, 2.0 + R * 0.25),
				**feet(BRACE + 0.3, BRACE + 0.15))),
		(0.64, hang_base(hips=(0.01 * s, 0.07, -0.02), hips_rot=(9.0, 1.0 * s, 0), spine=(-8.0, -2.0 * s, 0),
				head=(-16.0, -2.0 * s, 0), **side(0.23, -0.23, 2.04, 2.08), **feet(BRACE + 0.06, BRACE + 0.1))),
		(0.75, base),
	]
	return 0.75, False, lambda t: keyed(keys, t)


def clip_hang_reach_side(lead):
	"""Across to a ledge to the side hand over hand (0.8 s, FellowTraversal3D's TRANSFER_REACH_SIDE, milestone 152; the
	pose's IK puts the hands and feet on the stone): the leading hand reaching out to the side, the head and the torso
	turning to it, the leading foot stepping out; the hips swinging across under the hands, the arms wide; the other hand
	and foot following in. lead: "L" or "R" (the side the body goes)."""
	s = 1.0 if lead == "L" else -1.0
	W = 0.75  # how far across (the IK puts the hands where the holds really are)
	base = hang_base()

	def side(x_lead, x_other, z_lead=2.0, z_other=2.0):
		# The hands' places, the leading one first (mirrored for the right).
		return _hands(zl=z_lead, zr=z_other, xl=x_lead, xr=x_other) if s > 0 else _hands(zl=z_other, zr=z_lead, xl=-x_other, xr=-x_lead)

	def feet(x_lead, x_other, z_lead=BRACE, z_other=BRACE):
		return _feet(xl=x_lead, xr=x_other, zl=z_lead, zr=z_other) if s > 0 else _feet(xl=-x_other, xr=-x_lead, zl=z_other, zr=z_lead)

	keys = [
		(0.0, base),
		(0.2, hang_base(hips=(-0.05 * s, 0.11, -0.08), hips_rot=(7.0, -4.0 * s, -6.0 * s), spine=(-12.0, 8.0 * s, 8.0 * s),
				neck=(-8.0, 14.0 * s), head=(-14.0, 22.0 * s, 0), **side(0.23 + W * 0.7, -0.23, 2.06),
				**feet(0.11 + W * 0.25, -0.11, BRACE + 0.12, BRACE))),
		(0.42, hang_base(hips=(0.06 * s, 0.13, -0.12), hips_rot=(6.0, 2.0 * s, 4.0 * s), spine=(-10.0, 4.0 * s, -4.0 * s),
				neck=(-8.0, 8.0 * s), head=(-14.0, 12.0 * s, 0), **side(0.23 + W * 0.55, -0.23 - W * 0.45),
				**feet(0.11 + W * 0.35, -0.11 - W * 0.3, BRACE, BRACE - 0.08))),
		(0.62, hang_base(hips=(0.03 * s, 0.11, -0.08), hips_rot=(7.0, 1.0 * s, 2.0 * s), spine=(-10.0, 2.0 * s, -2.0 * s),
				neck=(-8.0, 4.0 * s), head=(-15.0, 6.0 * s, 0), **side(0.23 + W * 0.1, -0.23 - W * 0.2, 2.0, 2.06),
				**feet(0.11 + W * 0.05, -0.11 - W * 0.1, BRACE, BRACE + 0.14))),
		(0.8, base),
	]
	return 0.8, False, lambda t: keyed(keys, t)


def clip_hang_leap_up():
	"""The leap up to a ledge too far to reach (0.6 s: FellowTraversal3D's TRANSFER_JUMP_UP with hang_leap_windup 0.15;
	the controller scales it to the move): coiled against the wall, the head up at the ledge; the legs driving the body
	up off it, straightening, the arms thrown up overhead (the pose's IK takes the hands to the ledge); in the air the
	legs together, a little bent, the toes pointed; caught, the knees swinging up to the wall to brace again."""
	base = hang_base()
	keys = [
		(0.0, base),
		(0.13, hang_base(hips=(0, 0.04, 0.02), hips_rot=(18.0, 0, 0), spine=(-4.0, 0, 0), neck=(-12.0, 0), head=(-34.0, 0, 0),
				shoulder_L=(24.0, 6.0), shoulder_R=(24.0, 6.0),
				**_hands(zl=1.94, zr=1.94), **_feet(zl=BRACE + 0.06, zr=BRACE + 0.06))),
		(0.24, hang_base(hips=(0, 0.08, 0.04), hips_rot=(-4.0, 0, 0), spine=(-16.0, 0, 0), neck=(-14.0, 0), head=(-32.0, 0, 0),
				shoulder_L=(30.0, 8.0), shoulder_R=(30.0, 8.0),
				**_hands(zl=2.5, zr=2.5, xl=0.2, xr=-0.2, yl=WALL + 0.06, yr=WALL + 0.06),
				**_feet(zl=BRACE - 0.25, zr=BRACE - 0.22, yl=BRACE_Y + 0.05, yr=BRACE_Y + 0.05),
				foot_L=(-30.0, 6.0, 0), foot_R=(-30.0, 6.0, 0), toes_L=15.0, toes_R=15.0)),
		(0.38, hang_base(hips=(0, 0.1, 0.02), hips_rot=(2.0, 0, 0), spine=(-12.0, 0, 0), neck=(-10.0, 0), head=(-24.0, 0, 0),
				shoulder_L=(26.0, 6.0), shoulder_R=(26.0, 6.0),
				**_hands(zl=2.4, zr=2.4, xl=0.21, xr=-0.21, yl=WALL + 0.04, yr=WALL + 0.04),
				**_feet(zl=0.3, zr=0.34, xl=0.07, xr=-0.07, yl=0.16, yr=0.2),
				foot_L=(-38.0, 6.0, 0), foot_R=(-34.0, 6.0, 0), toes_L=10.0, toes_R=10.0)),
		(0.5, hang_base(hips=(0, 0.14, -0.06), hips_rot=(12.0, 0, 0), spine=(-12.0, 0, 0), head=(-18.0, 0, 0),
				**_feet(zl=BRACE - 0.06, zr=BRACE - 0.1, yl=BRACE_Y + 0.1, yr=BRACE_Y + 0.12))),
		(0.6, base),
	]
	return 0.6, False, lambda t: keyed(keys, t)


def clip_hang_leap_side(lead):
	"""A leap along the wall to the next ledge (0.65 s: TRANSFER_JUMP_ALONG with hang_leap_windup 0.15; DIAGONAL, BACK
	and CORNER scaled to theirs): coiled, the body drawn away from the side it goes to, the head turned to the ledge;
	flung across, the leading arm reaching out for it, the body stretched, the legs together trailing; caught, the
	legs swinging on past under the hands and braced again. lead: "L" (to the body's left) or "R"."""
	s = 1.0 if lead == "L" else -1.0
	base = hang_base()

	def hands(xl, xr, zl, zr):
		return _hands(zl=zl, zr=zr, xl=xl, xr=xr) if s > 0 else _hands(zl=zr, zr=zl, xl=-xr, xr=-xl)

	def feet(xl, xr, zl, zr, yl=BRACE_Y, yr=BRACE_Y):
		return _feet(zl=zl, zr=zr, xl=xl, xr=xr, yl=yl, yr=yr) if s > 0 else _feet(zl=zr, zr=zl, xl=-xr, xr=-xl, yl=yr, yr=yl)

	keys = [
		(0.0, base),
		(0.13, hang_base(hips=(-0.08 * s, 0.05, 0.0), hips_rot=(14.0, -5.0 * s, -10.0 * s), spine=(-6.0, -8.0 * s, 6.0 * s),
				neck=(-6.0, 10.0 * s), head=(-14.0, 24.0 * s, 0), **hands(0.21, -0.26, 1.95, 1.97),
				**feet(0.0, -0.24, BRACE + 0.08, BRACE + 0.02))),
		(0.27, hang_base(hips=(0.06 * s, 0.09, 0.03), hips_rot=(2.0, 8.0 * s, 14.0 * s), spine=(-12.0, 10.0 * s, -10.0 * s),
				neck=(-8.0, 12.0 * s), head=(-16.0, 22.0 * s, 0), **hands(0.7, 0.05, 2.1, 1.92),
				**feet(-0.06, -0.24, BRACE - 0.18, BRACE - 0.26, yl=BRACE_Y + 0.05, yr=BRACE_Y + 0.08),
				foot_L=(-32.0, 6.0, 0), foot_R=(-32.0, 6.0, 0))),
		(0.42, hang_base(hips=(0.02 * s, 0.11, 0.0), hips_rot=(6.0, 4.0 * s, 8.0 * s), spine=(-12.0, 6.0 * s, -6.0 * s),
				head=(-16.0, 12.0 * s, 0), **feet(-0.12, -0.26, 0.3, 0.26, yl=0.16, yr=0.2),
				foot_L=(-36.0, 6.0, 0), foot_R=(-36.0, 6.0, 0))),
		(0.55, hang_base(hips=(0.06 * s, 0.13, -0.05), hips_rot=(10.0, -2.0 * s, -6.0 * s), spine=(-12.0, -2.0 * s, 4.0 * s),
				head=(-16.0, 4.0 * s, 0), **feet(0.24, 0.02, BRACE - 0.04, BRACE - 0.1, yl=BRACE_Y + 0.08, yr=BRACE_Y + 0.12))),
		(0.65, base),
	]
	return 0.65, False, lambda t: keyed(keys, t)


def clip_hang_climb():
	"""Up from a hang onto the ledge held (1.25 s, FellowTraversal3D's hang_clip: its root's motion scaled onto the
	ledge): from the hanging pose, the body pulled up, the feet walking up the wall, the head up; the chest over the edge,
	the hands turning to press down on the top; pushed up over them, the left knee onto the top, the right foot still on
	the wall; standing up onto it, the right foot stepping through. The edge 2 m over the root at the start (the hands'
	place), 0.33 m in front of it."""
	E = 2.0
	base = hang_base()

	def hands(root, z_rel, y=WALL + 0.02, x=0.23, wrist=30.0, pole=(1.0, 0.4, -0.6)):
		ry, rz = root[1], root[2]
		return {"arm_L": {"ik": ((x, y - ry, z_rel), pole)}, "arm_R": {"ik": ((-x, y - ry, z_rel), (-pole[0], pole[1], pole[2]))},
				"hand_L": (wrist, 0, 0), "hand_R": (wrist, 0, 0)}

	def legs(root, l, r, pole_l=(0.35, -1.0, 0.35), pole_r=(-0.35, -1.0, 0.35)):
		ry, rz = root[1], root[2]
		return {"leg_L": {"ik": ((l[0], l[1] - ry, l[2] - rz), pole_l)}, "leg_R": {"ik": ((r[0], r[1] - ry, r[2] - rz), pole_r)}}

	r1 = (0, 0.02, 0.4)
	r2 = (0, -0.04, 0.98)
	r3 = (0, -0.22, 1.42)
	r4 = (0, -0.42, 1.76)
	r5 = (0, -0.56, 1.96)
	r6 = (0, -0.62, E)
	keys = [
		(0.0, base),
		(0.24, hang_base(root=r1, hips=(0, 0.1, -0.02), hips_rot=(6.0, 0, 0), spine=(-6.0, 0, 0), neck=(-8.0, 0), head=(-26.0, 0, 0),
				shoulder_L=(10.0, 2.0), shoulder_R=(10.0, 2.0), **hands(r1, E - r1[2]),
				**legs(r1, (0.11, BRACE_Y, BRACE + 0.12), (-0.11, BRACE_Y, BRACE + 0.3)))),
		(0.46, hang_base(root=r2, hips=(0, 0.06, 0.0), hips_rot=(4.0, 0, 0), spine=(6.0, 0, 0), neck=(-4.0, 0), head=(-20.0, 0, 0),
				shoulder_L=(0.0, 0.0), shoulder_R=(0.0, 0.0), **hands(r2, E - r2[2], pole=(1.0, 0.2, -1.0)),
				**legs(r2, (0.11, BRACE_Y, 1.05), (-0.11, BRACE_Y + 0.12, 0.62)), foot_L=(10.0, 6.0, 0), foot_R=(-20.0, 6.0, 0))),
		(0.66, hang_base(root=r3, hips=(0, 0.0, -0.06), hips_rot=(26.0, 0, 0), spine=(24.0, 0, 0), neck=(4.0, 0), head=(-10.0, 0, 0),
				shoulder_L=(-6.0, 0.0), shoulder_R=(-6.0, 0.0), **hands(r3, E - r3[2] + 0.02, y=WALL - 0.06, x=0.26, wrist=70.0, pole=(1.0, 1.0, 0.2)),
				**legs(r3, (0.12, WALL - 0.02, E + 0.12), (-0.11, BRACE_Y + 0.02, 1.25), pole_l=(0.3, -1.0, 0.0)),
				foot_L=(30.0, 6.0, 0), foot_R=(-10.0, 6.0, 0))),
		(0.86, stand(root=r4, hips=(0, 0.02, -0.16), hips_rot=(28.0, 0, 0), spine=(22.0, 0, 0), neck=(4.0, 0), head=(-12.0, 0, 0),
				grip_L=0.6, grip_R=0.6, **hands(r4, E - r4[2] + 0.02, y=WALL - 0.12, x=0.27, wrist=70.0, pole=(1.0, 1.0, 0.3)),
				**legs(r4, (0.12, WALL - 0.22, E + ANKLE), (-0.11, WALL + 0.06, E - 0.12), pole_l=(0.2, -1.0, 0.0), pole_r=(-0.2, -1.0, 0.0)),
				foot_L=(0.0, 7.0, 0), foot_R=(-30.0, 6.0, 0), toes_R=20.0)),
		(1.06, stand(root=r5, hips=(0, 0.02, -0.08), hips_rot=(14.0, 0, 0), spine=(10.0, 0, 0), head=(-6.0, 0, 0),
				arm_L={"fk": (70.0, 20.0, 30.0, 6.0)}, arm_R={"fk": (72.0, 12.0, 26.0, 6.0)}, grip_L=0.4, grip_R=0.4,
				**legs(r5, (0.11, WALL - 0.24, E + ANKLE), (-0.11, WALL - 0.42, E + ANKLE + 0.12), pole_l=(0.15, -1.0, 0.0), pole_r=(-0.15, -1.0, 0.0)),
				foot_L=(0.0, 7.0, 0), foot_R=(-15.0, 7.0, 0))),
		(1.25, stand(root=r6)),
	]
	return 1.25, False, lambda t: keyed(keys, t)


def clip_jump_wall():
	"""The run's jump at a wall for its first hold (0.7 s, its last pose held until the hold is caught): the take-off
	from the sprint, the arms swinging up; the knees drawn up, the arms reaching up and forward for the hold; the feet
	reaching for the wall."""
	run = gait_pose("sprint", 0.05)

	def arms(z, y=-0.38, x=0.24):
		return {"arm_L": {"ik": ((x, y, z), (1.0, 0.3, -0.6))}, "arm_R": {"ik": ((-x, y, z), (-1.0, 0.3, -0.6))},
				"hand_L": (20.0, 0, 0), "hand_R": (20.0, 0, 0), "grip_L": 0.5, "grip_R": 0.5}

	keys = [
		(0.0, dict(run)),
		(0.1, dict(run, hips=(0, -0.02, -0.12), hips_rot=(16.0, 0, 0), spine=(14.0, 0, 0), head=(-18.0, 0, 0),
				leg_L={"ik": ((0.1, -0.26, 0.42), (0.15, -1, 0))}, leg_R={"ik": ((-0.1, 0.2, 0.05), (-0.15, -1, 0))},
				foot_L=(10.0, 6.0, 0), foot_R=(-45.0, 6.0, 0), toes_R=25.0,
				arm_L={"fk": (70.0, -25.0, 30.0, 6.0)}, arm_R={"fk": (70.0, -25.0, 30.0, 6.0)})),
		(0.26, dict(stand(), hips=(0, 0.0, 0.04), hips_rot=(-2.0, 0, 0), spine=(-6.0, 0, 0), neck=(-6.0, 0), head=(-22.0, 0, 0),
				shoulder_L=(10.0, 4.0), shoulder_R=(10.0, 4.0), **arms(1.45, y=-0.42),
				leg_L={"ik": ((0.1, -0.24, 0.5), (0.15, -1, 0))}, leg_R={"ik": ((-0.1, 0.12, 0.2), (-0.15, -1, 0))},
				foot_L=(0.0, 6.0, 0), foot_R=(-35.0, 6.0, 0))),
		(0.46, dict(stand(), hips=(0, 0.02, 0.02), hips_rot=(4.0, 0, 0), spine=(-10.0, 0, 0), neck=(-8.0, 0), head=(-28.0, 0, 0),
				shoulder_L=(22.0, 6.0), shoulder_R=(22.0, 6.0), **arms(2.05, y=-0.42),
				leg_L={"ik": ((0.1, -0.3, 0.45), (0.2, -1, 0))}, leg_R={"ik": ((-0.1, -0.2, 0.36), (-0.2, -1, 0))},
				foot_L=(10.0, 6.0, 0), foot_R=(5.0, 6.0, 0))),
		(0.7, dict(stand(), hips=(0, 0.04, 0.0), hips_rot=(6.0, 0, 0), spine=(-10.0, 0, 0), neck=(-8.0, 0), head=(-26.0, 0, 0),
				shoulder_L=(22.0, 6.0), shoulder_R=(22.0, 6.0), **arms(2.05, y=-0.4),
				leg_L={"ik": ((0.11, -0.3, 0.42), (0.3, -1, 0.3))}, leg_R={"ik": ((-0.11, -0.28, 0.4), (-0.3, -1, 0.3))},
				foot_L=(15.0, 6.0, 0), foot_R=(15.0, 6.0, 0))),
	]
	return 0.7, False, lambda t: keyed(keys, t)


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


def clip_sit():
	"""Sitting on a bench (its top 0.47 m over the feet's ground): the hips on it, the thighs forward, the hands on the
	knees; breathing, the head turning to look about (the square's passers-by: rooftops_crowd.gd)."""
	base = stand(hips=(0, 0.16, -0.44), hips_rot=(-4.0, 0, 0), spine=(10.0, 0, 0), neck=(-2.0, 0), head=(-4.0, 0, 0),
			leg_L={"ik": ((0.13, -0.36, ANKLE), (0.3, -1.0, 0.4))}, leg_R={"ik": ((-0.12, -0.33, ANKLE), (-0.3, -1.0, 0.4))},
			foot_L=(0.0, 8.0, 0), foot_R=(0.0, 8.0, 0),
			arm_L={"ik": ((0.17, -0.3, 0.6), (1.0, 0.6, -0.3))}, arm_R={"ik": ((-0.17, -0.28, 0.6), (-1.0, 0.6, -0.3))},
			hand_L=(-10.0, 0, 0), hand_R=(-10.0, 0, 0), grip_L=0.4, grip_R=0.4)
	keys = [
		(0.0, base),
		(2.0, {"spine": (11.5, 4.0, 0), "neck": (-3.0, 12.0), "head": (-5.0, 16.0, 1.0)}),
		(4.5, {"spine": (9.0, -3.0, 0), "neck": (-1.0, -10.0), "head": (-3.0, -14.0, -1.0)}),
		(6.0, base),
	]
	return 6.0, True, lambda t: keyed(keys, t, loop=True, length=6.0)


def clip_talk():
	"""Standing talking with someone: the weight shifting from foot to foot, one hand then the other gesturing, the head
	nodding (the square's groups: rooftops_crowd.gd)."""
	base = stand(hips=(0.02, 0, -0.014), hips_rot=(0, 0, -2.0))
	keys = [
		(0.0, base),
		(0.9, {"arm_R": {"fk": (55.0, 45.0, 75.0, 30.0)}, "hand_R": (-20.0, 0, 0), "grip_R": 0.15, "head": (-4.0, 6.0, 2.0),
				"spine": (3.0, 4.0, 0)}),
		(1.6, {"arm_R": {"fk": (62.0, 38.0, 70.0, 20.0)}, "head": (2.0, 4.0, 0.0)}),
		(2.4, {"arm_R": {"fk": (78.0, 3.0, 14.0, 6.0)}, "hand_R": (8.0, 0, 0), "grip_R": 0.3, "head": (-2.0, 0, 0)}),
		(3.4, {"hips": (-0.02, 0, -0.014), "hips_rot": (0, 0, 2.0), "arm_L": {"fk": (58.0, 40.0, 72.0, 30.0)}, "hand_L": (-20.0, 0, 0),
				"grip_L": 0.15, "head": (-3.0, -6.0, -2.0), "spine": (3.0, -4.0, 0)}),
		(4.2, {"arm_L": {"fk": (65.0, 30.0, 65.0, 20.0)}, "head": (3.0, -3.0, 0)}),
		(5.0, {"arm_L": {"fk": (78.0, 3.0, 14.0, 6.0)}, "hand_L": (8.0, 0, 0), "grip_L": 0.3}),
		(6.0, base),
	]
	return 6.0, True, lambda t: keyed(keys, t, loop=True, length=6.0)


def clips():
	out = {}
	for name in ("walk", "jog", "run", "sprint"):
		G = GAITS[name]
		out[name] = (G["T"], True, (lambda n: (lambda t: gait_pose(n, t)))(name))
		# Milestone 154: the same cycle in 1 s (the left foot down at 0, the right at 0.5 for every gait), so that a blend of
		# two gaits mixes the same moment of the stride; the tree plays them at the cycle's rate for the body's speed.
		out["gait_" + name] = (1.0, True, (lambda n, T: (lambda t: gait_pose(n, t * T)))(name, G["T"]))
	out["idle"] = clip_idle()
	out["jump"] = clip_jump()
	out["fall"] = clip_fall()
	out["land"] = clip_land()
	out["roll"] = clip_roll()
	out["hang_idle"] = clip_hang_idle()
	out["hang_shimmy"] = clip_hang_shimmy()
	out["hang_reach_l"] = clip_hang_reach("L")
	out["hang_reach_r"] = clip_hang_reach("R")
	out["hang_across_l"] = clip_hang_reach_side("L")
	out["hang_across_r"] = clip_hang_reach_side("R")
	out["hang_leap_up"] = clip_hang_leap_up()
	out["hang_leap_l"] = clip_hang_leap_side("L")
	out["hang_leap_r"] = clip_hang_leap_side("R")
	out["hang_climb"] = clip_hang_climb()
	out["jump_wall"] = clip_jump_wall()
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
	out["sit"] = clip_sit()
	out["talk"] = clip_talk()
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
