// FellowEngine showcase (rooftops): the ground — by where it is (world space, metres):
// * the square: large slabs of brown-grey pietra forte laid in staggered courses, crossed every 7 m by bands of pale
//   travertine (Florence's squares' lines);
// * round the fountain: a ring of small stones laid in concentric courses (a fan), edged by bands of white marble;
// * along the houses' fronts: pavements of long grey pietra serena flags (the raised pavement's top, its kerb);
// * the streets: sampietrini in fish-scale arcs (coda di pavone);
// each stone its own tone and tilt, its edges worn round, its joints dark with grime and, in the damp by the walls,
// green with moss; dirt and damp at the foot of the walls; puddles in the low places (still water: the sky in them);
// close to the camera a parallax relief of the stones' heights.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec4 walls[12]; // the houses' footprints, axis-aligned: (centre x, centre z, half x, half z)
uniform int wall_count = 0;
uniform vec4 square = vec4(-16.0, -13.0, 17.5, 14.0); // the open square: x min, z min, x max, z max
uniform vec2 fountain = vec2(-3.5, 1.0);
uniform float pavement_width = 1.2;
uniform float puddles = 0.5;
uniform float relief_distance = 14.0; // the parallax relief within this distance of the camera (m)
uniform float relief_depth = 0.022;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float sd_box(vec2 p, vec4 r) {
	vec2 d = abs(p - r.xy) - r.zw;
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float wall_distance(vec2 p) {
	float d = 1e6;
	for (int i = 0; i < 12; i++) {
		if (i >= wall_count) {
			break;
		}
		d = min(d, sd_box(p, walls[i]));
	}
	return d;
}

// A stone's look: x height (m, 0 on the face), y its id's hash, z the joint (1 in it), w the zone (0 slabs, 1 marble,
// 2 fan cobbles, 3 pavement, 4 street cobbles).
struct Stone {
	float h;
	float id;
	float joint;
	float zone;
	vec2 tilt;
};

// The distance inside a stone's rounded outline: e the distances to its sides along each axis, r the corners' radius.
float rounded(vec2 e, float r) {
	return r - length(max(vec2(r) - e, 0.0)) + min(max(e.x, e.y) - r, 0.0) * 0.0;
}

Stone slabs(vec2 p, vec2 size, float offset, float joint_w, float bevel, float seed) {
	vec2 id;
	vec4 b = rt_blocks(p + vec2(seed * 7.3, seed * 3.1), size, offset, id);
	// Each slab's edges a little out of true (hand-cut stone): the joint's line wanders.
	vec2 e = vec2(min(b.x, 1.0 - b.x) * size.x, min(b.y, 1.0 - b.y) * size.y);
	e += (rt_noise(p * 6.0 + seed) - 0.5) * joint_w * 0.8;
	float ed = rounded(e, bevel * 1.5);
	Stone s;
	float j = 1.0 - smoothstep(joint_w * 0.5, joint_w * 0.5 + bevel, ed);
	s.joint = 1.0 - smoothstep(joint_w * 0.3, joint_w * 0.55, ed);
	vec2 c = b.xy - 0.5;
	s.h = -0.016 * j + 0.003 * (1.0 - dot(c, c) * 2.0);
	s.id = b.w;
	s.tilt = (rt_hash22(id + seed) - 0.5);
	s.h += dot(c * size, s.tilt) * 0.01 * (1.0 - j);
	s.zone = 0.0;
	return s;
}

// The fan round the fountain: courses 0.13 m deep round it, stones about 0.12 m along each.
Stone fan(vec2 p, vec2 centre, float r0) {
	vec2 d = p - centre;
	float r = length(d);
	float course = floor((r - r0) / 0.13);
	float rc = r0 + (course + 0.5) * 0.13;
	float per = floor(TAU * rc / 0.12);
	float a = (atan(d.y, d.x) / TAU + 0.5) * per + rt_hash12(vec2(course, 2.0)) * 3.0;
	float ai = floor(a);
	float fa = fract(a);
	float fr = fract((r - r0) / 0.13);
	vec2 e = vec2(min(fa, 1.0 - fa) * 0.12, min(fr, 1.0 - fr) * 0.13);
	vec2 jit = rt_hash22(vec2(course, ai)) - 0.5;
	e += jit * 0.016 + (rt_noise(p * 30.0) - 0.5) * 0.008;
	float edge = rounded(e, 0.03);
	Stone s;
	s.joint = 1.0 - smoothstep(0.002, 0.007, edge);
	float dome = clamp(edge / 0.055, 0.0, 1.0);
	s.h = -0.02 + 0.026 * (1.0 - (1.0 - dome) * (1.0 - dome));
	s.id = rt_hash12(vec2(course, ai));
	s.tilt = rt_hash22(vec2(course, ai) + 4.0) - 0.5;
	s.zone = 2.0;
	return s;
}

// Sampietrini in fish scales: arcs about centres on rows 0.7 m apart (shifted by half a scale every other row); the
// highest scale over the point covers it; within a scale, courses 0.11 m deep along its arcs.
Stone scales(vec2 p) {
	float w = 1.4;
	float hh = 0.7;
	float R = 0.99;
	float j0 = floor(p.y / hh);
	vec2 best_c = vec2(0.0);
	float best_r = 1e6;
	float best_j = -1e6;
	for (int k = 0; k < 3; k++) {
		float j = j0 - float(k);
		float sx = mod(j, 2.0) * 0.5 * w;
		float i = floor((p.x - sx) / w + 0.5);
		vec2 c = vec2(i * w + sx, j * hh);
		float r = length(p - c);
		if (r < R && p.y >= c.y && j > best_j) {
			best_j = j;
			best_c = c;
			best_r = r;
		}
	}
	Stone s;
	s.zone = 4.0;
	if (best_j < -1e5) {
		s.h = 0.0;
		s.joint = 0.0;
		s.id = 0.5;
		s.tilt = vec2(0.0);
		return s;
	}
	float course = floor(best_r / 0.11);
	float rc = (course + 0.5) * 0.11;
	vec2 d = p - best_c;
	float per = max(2.0, floor(PI * rc / 0.11));
	float a = atan(d.y, d.x) / PI * per + rt_hash12(vec2(course, best_j)) * 2.0;
	float fa = fract(a);
	float fr = fract(best_r / 0.11);
	vec2 e = vec2(min(fa, 1.0 - fa) * PI * rc / per, min(fr, 1.0 - fr) * 0.11);
	e += (rt_noise(p * 30.0) - 0.5) * 0.008;
	float edge = min(rounded(e, 0.026), R - best_r);
	s.joint = 1.0 - smoothstep(0.002, 0.008, edge);
	float dome = clamp(edge / 0.05, 0.0, 1.0);
	s.h = -0.02 + 0.026 * (1.0 - (1.0 - dome) * (1.0 - dome));
	s.id = rt_hash12(vec2(course + best_c.x * 3.0, floor(a) + best_c.y * 7.0));
	s.tilt = rt_hash22(vec2(s.id * 91.0, course)) - 0.5;
	return s;
}

Stone stone_at(vec2 p, float wd) {
	if (wd < pavement_width) {
		// The pavement's flags along the wall (their long side along the nearer wall's side).
		Stone s = slabs(p, vec2(1.15, 0.55), 0.5, 0.008, 0.012, 3.0);
		s.zone = 3.0;
		return s;
	}
	bool in_square = p.x > square.x && p.y > square.y && p.x < square.z && p.y < square.w;
	if (in_square) {
		float rf = length(p - fountain);
		if (rf < 7.9) {
			if (rf < 3.75 || rf > 7.45) {
				// The marble bands: slabs cut round, joints radial every metre or so.
				float a = atan(p.y - fountain.y, p.x - fountain.x);
				float seg = fract(a / TAU * 24.0);
				float edge = min(min(seg, 1.0 - seg) * rf * TAU / 24.0, min(abs(rf - 3.35), abs(rf - 7.9)));
				edge = min(edge, rf < 3.75 ? 3.75 - rf : rf - 7.45);
				Stone s;
				s.joint = 1.0 - smoothstep(0.003, 0.008, edge);
				s.h = -0.012 * s.joint;
				s.id = rt_hash12(vec2(floor(a / TAU * 24.0), rf < 3.75 ? 1.0 : 2.0));
				s.tilt = vec2(0.0);
				s.zone = 1.0;
				return s;
			}
			return fan(p, fountain, 3.75);
		}
		// The travertine bands every 7 m.
		vec2 g = abs(fract(p / 7.0 + 0.5) - 0.5) * 7.0;
		if (min(g.x, g.y) < 0.16) {
			Stone s = slabs(p.yx * (g.x < g.y ? 1.0 : 1.0), vec2(0.6, 0.32), 0.5, 0.004, 0.006, 5.0);
			s.zone = 1.0;
			float e = 0.16 - min(g.x, g.y);
			s.joint = max(s.joint, 1.0 - smoothstep(0.003, 0.008, e));
			s.h -= 0.012 * (1.0 - smoothstep(0.003, 0.01, e));
			return s;
		}
		return slabs(p, vec2(0.95, 0.6), 0.37, 0.01, 0.016, 0.0);
	}
	return scales(p);
}

void fragment() {
	vec3 wp = world_pos;
	vec2 p = wp.xz;
	float wd = wall_distance(p);
	float cam_d = length(CAMERA_POSITION_WORLD - wp);
	// Parallax relief: the view's ray marched into the stones' heights (a few steps, near the camera only).
	float relief = (1.0 - smoothstep(relief_distance * 0.6, relief_distance, cam_d)) * relief_depth;
	if (relief > 0.001) {
		vec3 v = normalize(wp - CAMERA_POSITION_WORLD);
		vec2 step_xy = v.xz / max(-v.y, 0.15) * relief / 8.0;
		float layer = 0.0;
		vec2 q = p;
		float prev_gap = 0.0;
		for (int i = 0; i < 8; i++) {
			Stone st = stone_at(q, wd);
			float surf = 1.0 + st.h / 0.04; // 1 at the stones' faces, lower in the joints
			float depth = layer;
			float gap = depth - (1.0 - clamp(surf, 0.0, 1.0));
			if (gap >= 0.0) {
				float t = prev_gap / max(prev_gap - gap, 1e-4);
				q -= step_xy * (1.0 - t);
				break;
			}
			prev_gap = gap;
			q += step_xy;
			layer += 1.0 / 8.0;
		}
		p = q;
	}
	Stone s = stone_at(p, wd);
	float fade = rt_detail_fade(p, 0.02);
	// The stones' normal from their heights a few millimetres apart (in the world: the ground is flat), each slab's
	// tilt with it; faded with distance (no shimmer far off).
	float ex = 0.004;
	float hx = stone_at(p + vec2(ex, 0.0), wd).h;
	float hz = stone_at(p + vec2(0.0, ex), wd).h;
	float nfade = 1.0 - smoothstep(18.0, 60.0, cam_d);
	vec3 col;
	float rough;
	float tone = s.id;
	if (s.zone < 0.5) {
		// Pietra forte: brown-grey to ochre-grey, some darker, some greyer.
		col = mix(vec3(0.4, 0.36, 0.3), vec3(0.56, 0.5, 0.41), tone);
		col = mix(col, vec3(0.34, 0.33, 0.31), step(0.82, tone) * 0.7);
		col = mix(col, vec3(0.52, 0.5, 0.47), step(tone, 0.12) * 0.6);
		rough = 0.72;
	} else if (s.zone < 1.5) {
		// Travertine and marble: pale, a little warm.
		col = mix(vec3(0.78, 0.75, 0.68), vec3(0.88, 0.86, 0.8), tone);
		rough = 0.55;
	} else if (s.zone < 2.5) {
		// The fan's cobbles: grey-brown river stones, a few pale, a few near black.
		col = mix(vec3(0.27, 0.25, 0.22), vec3(0.43, 0.4, 0.35), tone);
		col = mix(col, vec3(0.56, 0.52, 0.45), step(0.9, tone) * 0.6);
		col = mix(col, vec3(0.18, 0.17, 0.16), step(tone, 0.12) * 0.7);
		rough = 0.62;
	} else if (s.zone < 3.5) {
		// Pietra serena: grey with a blue cast.
		col = mix(vec3(0.38, 0.39, 0.38), vec3(0.5, 0.5, 0.48), tone);
		col *= 0.85 + 0.25 * rt_fbm(p * 0.8 + tone * 5.0, 3);
		rough = 0.66;
	} else {
		// Sampietrini: dark grey basalt, worn smooth on top.
		col = mix(vec3(0.3, 0.29, 0.28), vec3(0.44, 0.42, 0.39), tone);
		col = mix(col, vec3(0.52, 0.47, 0.4), step(0.85, tone) * 0.5);
		rough = 0.6;
	}
	// The stone's grain, its pits, its worn rim.
	float grain = rt_fbm(p * 9.0 + tone * 13.0, 3);
	col *= 0.86 + 0.24 * grain;
	col *= 0.92 + 0.12 * rt_fbm(p * 1.3 + tone, 2);
	// Grime, damp and moss: at the foot of the walls, in the joints.
	float damp = 1.0 - smoothstep(0.0, 1.2, wd);
	float grime = smoothstep(0.35, 0.8, rt_fbm(p * 0.35, 4));
	col *= 1.0 - 0.18 * grime;
	col = mix(col, col * vec3(0.72, 0.7, 0.66), damp * 0.6);
	vec3 joint_col = mix(vec3(0.11, 0.1, 0.09), vec3(0.14, 0.17, 0.08), smoothstep(0.4, 0.7, rt_noise(p * 2.0)) * (0.35 + 0.65 * damp));
	// Far off, where a stone is a few pixels, its joints are an average darkening (no moire).
	float stone_size = s.zone < 0.5 ? 0.6 : (s.zone < 1.5 ? 0.6 : (s.zone < 2.5 ? 0.12 : (s.zone < 3.5 ? 0.55 : 0.11)));
	vec2 fw = fwidth(world_pos.xz);
	float resolve = clamp(1.0 - max(fw.x, fw.y) * 6.0 / stone_size, 0.0, 1.0);
	float joint_share = s.zone > 1.5 && s.zone < 2.5 || s.zone > 3.5 ? 0.22 : 0.05;
	col = mix(col, joint_col, mix(joint_share, s.joint, resolve));
	rough = mix(rough, 0.95, s.joint);
	// Wear: the faces of the stones polished where feet go (away from the walls).
	float wear = smoothstep(0.5, 0.75, rt_fbm(p * 0.12 + 7.0, 3)) * (1.0 - damp);
	rough -= 0.15 * wear * (1.0 - s.joint);
	col *= 1.0 + 0.06 * wear;
	// Puddles: still water in the low places, the joints full.
	float pud = smoothstep(0.66, 0.7, rt_fbm(p * 0.09 + 31.0, 4) + 0.04 * rt_noise(p * 3.0)) * puddles;
	float wet = max(pud, smoothstep(0.62, 0.66, rt_fbm(p * 0.09 + 31.0, 4)) * puddles * 0.6);
	col *= 1.0 - 0.45 * wet;
	rough = mix(rough, 0.04, pud);
	rough = mix(rough, rough * 0.6, wet);
	vec2 grad = vec2(hx - s.h, hz - s.h) / ex * nfade * resolve * (1.0 - pud);
	grad += vec2(rt_noise(p * 40.0) - 0.5, rt_noise(p * 40.0 + 9.0) - 0.5) * 0.06 * fade * (1.0 - pud) * (1.0 - s.joint);
	vec3 nw = normalize(vec3(-grad.x, 1.0, -grad.y));
	ALBEDO = col;
	ROUGHNESS = clamp(rough, 0.03, 1.0);
	SPECULAR = mix(0.45, 0.6, pud);
	AO = 1.0 - 0.5 * s.joint;
	AO_LIGHT_AFFECT = 0.4;
	NORMAL = normalize((VIEW_MATRIX * vec4(nw, 0.0)).xyz);
}
