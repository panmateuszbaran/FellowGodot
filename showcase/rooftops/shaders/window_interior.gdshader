// FellowEngine's FellowInteriorMaterial3D (milestone 188): a room behind each window.
shader_type spatial;
// No direct specular: the sun's disc mirrored in old panes whitened whole windows; the sky's reflection stays.
render_mode diffuse_burley, specular_disabled;

uniform bool rooms_from_uv2 = true;
uniform vec3 room_size = vec3(3.6, 3.6, 4.5);
uniform float floor_height = 0.0;
uniform float seed = 0.0;
uniform float daylight = 0.3;
uniform vec4 daylight_color : source_color = vec4(1.0, 0.93, 0.84, 1.0);
uniform float lamp_share = 0.0;
uniform float lamp_energy = 3.0;
uniform vec4 lamp_color : source_color = vec4(1.0, 0.7, 0.42, 1.0);
uniform float curtain_share = 0.6;
uniform float furniture = 1.0;
uniform vec4 glass_tint : source_color = vec4(0.92, 0.95, 0.93, 1.0);
uniform float glass_roughness = 0.08;
uniform float grime = 0.25;
uniform float reflection = 0.45;
uniform vec2 pane_size = vec2(0.0);

varying vec3 obj_pos;
varying vec3 obj_normal;
varying vec3 cam_obj;

float fi_h11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float fi_h21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float fi_h31(vec3 p3) { p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
float fi_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(fi_h21(i), fi_h21(i + vec2(1.0, 0.0)), f.x), mix(fi_h21(i + vec2(0.0, 1.0)), fi_h21(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fi_box(vec2 p, vec2 lo, vec2 hi) { return step(lo.x, p.x) * step(p.x, hi.x) * step(lo.y, p.y) * step(p.y, hi.y); }
// The walls' colours: limewash creams, ochre, sage, teal, a deep red, a dusty pink, a pale blue, white.
vec3 fi_wall(float r) {
	vec3 c[8] = vec3[](vec3(0.82, 0.76, 0.62), vec3(0.72, 0.52, 0.26), vec3(0.42, 0.52, 0.38), vec3(0.26, 0.42, 0.45), vec3(0.55, 0.17, 0.13), vec3(0.72, 0.50, 0.45), vec3(0.55, 0.62, 0.70), vec3(0.86, 0.83, 0.76));
	return c[int(r * 7.999)];
}
vec3 fi_wood(vec2 q, float shade) {
	return mix(vec3(0.20, 0.11, 0.06), vec3(0.34, 0.20, 0.11), shade) * (0.85 + 0.3 * fi_noise(q * vec2(2.0, 24.0)));
}
// A wall's surface at q (along it, up it): plaster or paper, a dado or panelling, a skirting, a cornice.
vec3 fi_wall_surface(vec2 q, float h, vec3 wall, float r1, float r2) {
	vec3 a = wall * (0.92 + 0.1 * fi_noise(q * 3.0));
	if (r1 > 0.62) {
		// Damask paper.
		vec2 p = q * vec2(3.5, 2.6);
		float m = abs(sin(p.x * 3.14159) * sin(p.y * 3.14159 + sin(p.x * 3.14159) * 0.8));
		a = mix(a, a * 0.72, smoothstep(0.35, 0.55, m) * 0.7);
	} else if (r1 > 0.45) {
		a *= 1.0 - 0.12 * step(0.5, fract(q.x * 5.0));
	}
	if (r2 > 0.7) {
		// Wooden panelling to a dado rail.
		float panel = fi_box(fract(q / vec2(0.6, 0.9)), vec2(0.1, 0.12), vec2(0.9, 0.88));
		a = mix(a, fi_wood(q, 0.6) * (1.0 - 0.15 * panel), step(q.y, 1.0));
	} else if (r2 > 0.4) {
		a = mix(a, a * 0.7, step(q.y, 0.95));
	}
	a = mix(a, vec3(0.16, 0.10, 0.065), step(q.y, 0.12));
	a = mix(a, vec3(0.86, 0.84, 0.78), step(h - 0.16, q.y));
	return a;
}

void vertex() {
	obj_pos = VERTEX;
	obj_normal = NORMAL;
	cam_obj = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
}

void fragment() {
	vec3 n = normalize(obj_normal);
	vec3 room = vec3(0.0);
	vec2 win = vec2(0.0);
	vec3 key = vec3(0.0);
	if (abs(n.y) < 0.5) {
		// The facade's frame: across it (its side), up, into the building; the view's ray in it.
		vec3 side = normalize(cross(vec3(0.0, 1.0, 0.0), n));
		vec3 dir = normalize(obj_pos - cam_obj);
		float dx = dot(dir, side);
		float dy = dir.y;
		float dz = max(-dot(dir, n), 1e-4);
		vec3 R = room_size;
		vec2 local;
		if (rooms_from_uv2) {
			// UV2: the metres right of the window's middle and up from its room's floor.
			local = vec2(UV2.x + R.x * 0.5, UV2.y);
			key = floor((obj_pos - side * UV2.x - vec3(0.0, UV2.y, 0.0)) * 4.0 + 0.5);
			win = UV2;
		} else {
			float px = dot(obj_pos, side);
			float py = obj_pos.y - floor_height;
			vec2 id = floor(vec2(px / R.x, py / R.y));
			local = vec2(px - id.x * R.x, py - id.y * R.y);
			key = vec3(id, floor(dot(obj_pos, n) * 4.0 + 0.5));
			win = vec2(local.x - R.x * 0.5, local.y);
		}
		local = clamp(local, vec2(0.02), R.xy - 0.02);
		float r0 = fi_h31(key + seed);
		float r1 = fi_h31(key + seed + 17.1);
		float r2 = fi_h31(key + seed + 41.7);
		float r3 = fi_h31(key + seed + 73.3);
		float r4 = fi_h31(key + seed + 97.9);
		float r5 = fi_h31(key + seed + 151.3);
		float r6 = fi_h31(key + seed + 183.7);
		vec3 rd = normalize(vec3(dx, dy, dz));
		vec3 o = vec3(local, 0.0);

		// Where the ray meets the room: a side wall, the floor or the ceiling, or the back wall.
		float tx = dx > 1e-5 ? (R.x - local.x) / dx : (dx < -1e-5 ? -local.x / dx : 1e9);
		float ty = dy > 1e-5 ? (R.y - local.y) / dy : (dy < -1e-5 ? -local.y / dy : 1e9);
		float tz = R.z / dz;
		float t = min(tx, min(ty, tz));
		vec3 h = vec3(local + vec2(dx, dy) * t, dz * t);
		int face = t == tz ? 0 : (t == tx ? (dx > 0.0 ? 2 : 1) : (dy > 0.0 ? 4 : 3));

		vec3 wall = fi_wall(r0);
		vec3 alb;
		float facing;
		float edge;
		if (face <= 2) {
			vec2 q = face == 0 ? h.xy : vec2(face == 1 ? h.z : R.z - h.z, h.y);
			alb = fi_wall_surface(q, R.y, wall, r1, r2);
			if (face == 0) {
				// Against the back wall: a painting, a door (open on the dark beyond, or shut), a bookcase, a wardrobe.
				if (r3 < 0.22) {
					vec2 c = vec2(R.x * (0.35 + 0.3 * r4), 1.7);
					vec2 s = vec2(0.45 + 0.35 * r2, 0.32 + 0.22 * r1);
					float pic = fi_box(q, c - s + 0.07, c + s - 0.07);
					vec3 art = mix(vec3(0.16, 0.22, 0.14), vec3(0.62, 0.48, 0.30), smoothstep(c.y - 0.15, c.y + 0.25, q.y + 0.1 * fi_noise(q * 4.0))) * (0.7 + 0.5 * fi_noise(q * 9.0));
					alb = mix(alb, mix(vec3(0.55, 0.40, 0.15), art, pic), fi_box(q, c - s, c + s));
				} else if (r3 < 0.42) {
					float x0 = R.x * (0.15 + 0.5 * r4);
					vec2 dq = q - vec2(x0, 0.0);
					float door = fi_box(dq, vec2(0.0), vec2(0.95, 2.2));
					float frame = door * (1.0 - fi_box(dq, vec2(0.07), vec2(0.88, 2.13)));
					float panel = fi_box(fract(dq / vec2(0.44, 0.7)), vec2(0.12), vec2(0.88));
					vec3 leaf = fi_wood(q, 0.3) * (1.0 - 0.2 * panel);
					// Open: the next room, dark.
					leaf = mix(leaf, vec3(0.025, 0.02, 0.015) * (1.0 + 2.0 * smoothstep(0.88, 0.5, dq.x / 0.95)), step(0.6, r5));
					alb = mix(alb, mix(leaf, fi_wood(q, 0.8), frame), door);
				} else if (r3 < 0.64) {
					float x0 = R.x * (0.1 + 0.4 * r4);
					vec2 sp = vec2(q.x - x0, q.y);
					float shelf = step(fract(sp.y / 0.38), 0.07);
					float book_id = floor(sp.x / 0.045) + floor(sp.y / 0.38) * 57.0;
					float book = step(fract(sp.y / 0.38) * 0.38, 0.22 + 0.12 * fi_h11(book_id)) * step(0.08, fi_h11(book_id + 3.0));
					vec3 bcol = mix(vec3(0.42, 0.10, 0.07), vec3(0.10, 0.18, 0.30), fi_h11(book_id + 7.0));
					bcol = mix(bcol, vec3(0.60, 0.48, 0.25), step(0.7, fi_h11(book_id + 11.0)));
					bcol = mix(bcol, vec3(0.15, 0.28, 0.16), step(0.85, fi_h11(book_id + 13.0)));
					vec3 inner = mix(vec3(0.05, 0.035, 0.025), bcol, book);
					alb = mix(alb, mix(inner, fi_wood(q, 0.4), shelf), fi_box(q, vec2(x0, 0.0), vec2(x0 + 1.2 + 0.8 * r2, 2.2)));
				} else if (r3 < 0.84) {
					float x0 = R.x * (0.1 + 0.5 * r4);
					vec2 dq = q - vec2(x0, 0.0);
					float body = fi_box(dq, vec2(0.0, 0.08), vec2(1.2, 2.05));
					float top = fi_box(dq, vec2(-0.05, 2.05), vec2(1.25, 2.18));
					float split = fi_box(dq, vec2(0.585, 0.15), vec2(0.615, 2.0));
					float panel = fi_box(fract(dq / vec2(0.6, 0.95)), vec2(0.14, 0.1), vec2(0.86, 0.9));
					alb = mix(alb, fi_wood(q, 0.55) * (1.0 - 0.2 * panel - 0.5 * split), max(body, top));
				}
			} else {
				// On a side wall: a framed picture or a mirror.
				float zc = R.z * (0.35 + 0.3 * r5);
				vec2 s = vec2(0.3 + 0.2 * r6, 0.25 + 0.15 * r4);
				vec2 c = vec2(face == 1 ? zc : R.z - zc, 1.6);
				float inside = fi_box(q, c - s, c + s) * step(0.35, r6);
				float pic = fi_box(q, c - s + 0.05, c + s - 0.05);
				vec3 art = mix(vec3(0.30, 0.26, 0.20), vec3(0.62, 0.66, 0.70), step(0.7, r5)) * (0.75 + 0.4 * fi_noise(q * 7.0));
				alb = mix(alb, mix(vec3(0.45, 0.33, 0.14), art, pic), inside);
			}
			facing = face == 0 ? 0.8 : 0.7;
			vec2 e = face == 0 ? vec2(min(h.x, R.x - h.x), min(h.y, R.y - h.y)) : vec2(min(h.z, R.z - h.z), min(h.y, R.y - h.y));
			edge = min(e.x, e.y);
		} else if (face == 3) {
			// The floor: cotto tiles, boards, a parquet's herringbone or stone; a rug.
			vec2 q = vec2(h.x, h.z);
			if (r1 < 0.45) {
				vec2 f = fract(q / 0.3);
				vec3 cotto = vec3(0.56, 0.29, 0.18) * (0.8 + 0.32 * fi_h21(floor(q / 0.3) + key.xy));
				alb = mix(cotto, vec3(0.40, 0.34, 0.28), 1.0 - fi_box(f, vec2(0.03), vec2(0.97)));
			} else if (r1 < 0.75) {
				float b = floor(q.x / 0.14);
				float seam = step(fract(q.x / 0.14), 0.04) + step(fract(q.y / 1.9 + fi_h11(b)), 0.01);
				alb = mix(fi_wood(q.yx, fi_h11(b + key.x)) * 1.4, vec3(0.10, 0.06, 0.035), min(seam, 1.0));
			} else if (r1 < 0.9) {
				vec2 p = vec2(q.x + q.y, q.y - q.x) / 0.32;
				float row = floor(p.y);
				float alt = mod(row, 2.0);
				float u = fract(p.x * 0.5 + alt * 0.5);
				alb = fi_wood(q, fi_h11(floor(p.x * 0.5) + row * 7.0)) * 1.4 * mix(0.85, 1.0, step(0.5, u));
			} else {
				alb = vec3(0.72, 0.70, 0.66) * (0.85 + 0.2 * fi_noise(q * 3.0));
			}
			vec2 lo = vec2(R.x * 0.2, R.z * 0.22);
			vec2 hi = vec2(R.x * 0.8, R.z * 0.7);
			vec2 rq = q - lo;
			float border = 1.0 - fi_box(q, lo + 0.14, hi - 0.14);
			vec3 rug = mix(vec3(0.48, 0.10, 0.07), vec3(0.10, 0.15, 0.32), step(0.65, r3));
			rug = mix(rug * (0.75 + 0.35 * step(0.5, fract((rq.x + rq.y) * 3.0)) * step(0.5, fract((rq.x - rq.y) * 3.0))), vec3(0.70, 0.56, 0.32), border);
			alb = mix(alb, rug, fi_box(q, lo, hi) * step(0.4, r2));
			facing = 1.0 + 0.6 * exp(-h.z / 0.8);
			edge = min(min(h.x, R.x - h.x), R.z - h.z);
		} else {
			// The ceiling: beams across it, or plaster with a cornice.
			vec2 q = vec2(h.x, h.z);
			alb = vec3(0.86, 0.83, 0.77);
			if (r2 < 0.55) {
				float beam = step(fract(q.y / 0.6), 0.24);
				alb = mix(alb * vec3(0.95, 0.85, 0.75), fi_wood(q.yx, 0.2), beam);
			}
			facing = 0.45;
			edge = min(min(h.x, R.x - h.x), R.z - h.z);
		}
		float ao = 0.5 + 0.5 * smoothstep(0.0, 0.6, edge);

		// Furniture: across the middle of the room (a table and chairs, a sofa, an armchair and a side table, a desk), and
		// in front of the back wall (a sideboard with a lamp or a vase on it, a bed's head).
		float fkind = fi_h31(key + seed + 133.0);
		float bkind = fi_h31(key + seed + 257.0);
		float zm = R.z * (0.38 + 0.14 * r4);
		float zb = R.z - 0.42;
		float furn = 0.0;
		vec3 fcol = vec3(0.0);
		float fz = 0.0;
		vec3 fh = vec3(0.0);
		for (int layer = 0; layer < 2 && furn == 0.0; layer++) {
			float zp = layer == 0 ? zm : zb;
			float tp = zp / dz;
			vec3 hp = vec3(local + vec2(dx, dy) * tp, zp);
			if (tp >= t || hp.x <= 0.0 || hp.x >= R.x || hp.y <= 0.0) {
				continue;
			}
			float cx = layer == 0 ? R.x * (0.3 + 0.4 * r2) : R.x * (0.25 + 0.5 * r5);
			vec2 a = vec2(abs(hp.x - cx), hp.y);
			float f = 0.0;
			vec3 c = vec3(0.0);
			float k = layer == 0 ? fkind / max(furniture, 1e-3) : bkind / max(furniture, 1e-3);
			if (layer == 0) {
				if (k < 0.3) {
					float top = fi_box(hp.xy, vec2(cx - 0.65, 0.72), vec2(cx + 0.65, 0.79));
					float legs = fi_box(a, vec2(0.52, 0.0), vec2(0.57, 0.75));
					float chair = fi_box(a, vec2(0.78, 0.42), vec2(1.2, 0.48)) + fi_box(a, vec2(1.14, 0.0), vec2(1.2, 1.02)) + fi_box(a, vec2(0.78, 0.0), vec2(0.82, 0.45));
					// A vase on the table.
					float vase = step(length((hp.xy - vec2(cx + 0.2, 0.92)) / vec2(0.07, 0.13)), 1.0);
					f = min(top + legs + chair + vase, 1.0);
					c = mix(fi_wood(hp.xy, 0.4), vec3(0.75, 0.70, 0.62), vase);
					c *= 1.0 + 0.4 * top;
				} else if (k < 0.52) {
					f = min(fi_box(a, vec2(0.0, 0.1), vec2(0.95, 0.86)) + fi_box(a, vec2(0.82, 0.0), vec2(0.87, 0.1)), 1.0);
					c = mix(vec3(0.42, 0.30, 0.20), vec3(0.22, 0.30, 0.24), step(0.5, r1));
					c = mix(c, vec3(0.45, 0.14, 0.12), step(0.8, r1));
					c *= 1.0 - 0.3 * (fi_box(a, vec2(0.0, 0.42), vec2(0.8, 0.47)) + fi_box(a, vec2(0.79, 0.1), vec2(0.82, 0.64)));
					c *= 1.0 + 0.25 * step(0.47, hp.y) * step(hp.y, 0.6) * step(a.x, 0.79);
				} else if (k < 0.68) {
					float chair = fi_box(hp.xy - vec2(cx - 0.4, 0.0), vec2(-0.4, 0.1), vec2(0.4, 1.0));
					float tbl = fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.25, 0.55), vec2(0.25, 0.6)) + fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.03, 0.0), vec2(0.03, 0.58));
					float lampshade = fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.13, 0.85), vec2(0.13, 1.05)) + fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.015, 0.6), vec2(0.015, 0.86));
					f = min(chair + tbl + lampshade, 1.0);
					c = chair > 0.5 ? mix(vec3(0.40, 0.12, 0.10), vec3(0.20, 0.28, 0.22), step(0.5, r6)) : (lampshade > 0.5 ? vec3(0.85, 0.78, 0.6) : fi_wood(hp.xy, 0.3));
				} else if (k < 0.8) {
					float desk = fi_box(hp.xy, vec2(cx - 0.7, 0.74), vec2(cx + 0.7, 0.79)) + fi_box(a, vec2(0.6, 0.0), vec2(0.7, 0.76));
					float chair = fi_box(hp.xy - vec2(cx, 0.0), vec2(-0.22, 0.44), vec2(0.22, 0.95)) * step(0.48, hp.y);
					f = min(desk + chair, 1.0);
					c = fi_wood(hp.xy, 0.25);
				}
			} else {
				if (k < 0.35) {
					float body = fi_box(a, vec2(0.0, 0.1), vec2(0.75, 0.95));
					float drawers = step(fract(hp.y / 0.28), 0.04) * step(hp.y, 0.95);
					float feet = fi_box(a, vec2(0.68, 0.0), vec2(0.74, 0.1));
					// On it a lamp or a vase.
					float lampshade = fi_box(hp.xy - vec2(cx - 0.35, 0.0), vec2(-0.14, 1.25), vec2(0.14, 1.45)) + fi_box(hp.xy - vec2(cx - 0.35, 0.0), vec2(-0.02, 0.95), vec2(0.02, 1.26));
					float vase = step(length((hp.xy - vec2(cx + 0.3, 1.08)) / vec2(0.08, 0.14)), 1.0);
					float on = step(0.5, r6);
					f = min(body + feet + (on > 0.5 ? lampshade : vase), 1.0);
					c = body + feet > 0.5 ? fi_wood(hp.xy, 0.5) * (1.0 - 0.5 * drawers) : vec3(0.82, 0.76, 0.6);
				} else if (k < 0.5) {
					float head = fi_box(a, vec2(0.0, 0.0), vec2(0.8, 1.2)) * (1.0 - step(1.05, hp.y) * step(0.7, a.x));
					f = head;
					c = fi_wood(hp.xy, 0.35);
				}
			}
			if (f > 0.0) {
				furn = f;
				fcol = c;
				fz = zp;
				fh = hp;
			}
		}

		// The light: the day through the window, falling off into the room; a lamp under the ceiling.
		vec3 lp = vec3(R.x * 0.5, R.y - 0.75, R.z * 0.5);
		bool lamp = fi_h31(key + seed + 211.0) < lamp_share;
		vec3 lit = daylight_color.rgb * (daylight * mix(0.1, 1.0, exp(-h.z / (0.3 * R.z))) * facing * ao);
		if (lamp) {
			lit += lamp_color.rgb * lamp_energy / (1.0 + 1.2 * dot(h - lp, h - lp)) * mix(0.6, 1.0, min(facing, 1.0));
		}
		room = alb * lit;
		if (furn > 0.0) {
			vec3 flit = daylight_color.rgb * (daylight * mix(0.1, 1.0, exp(-fz / (0.3 * R.z))) * 0.85 * (0.6 + 0.4 * smoothstep(0.0, 0.4, fh.y)));
			if (lamp) {
				flit += lamp_color.rgb * lamp_energy / (1.0 + 1.2 * dot(fh - lp, fh - lp)) * 0.7;
			}
			room = mix(room, fcol * flit, furn);
		}
		// The hanging lamp: a shade on a flex, lit or not.
		float tc = dot(lp - o, rd);
		float dl = length(o + rd * tc - lp);
		if (tc > 0.0 && tc < t) {
			vec3 pc = o + rd * tc;
			float shade = smoothstep(0.2, 0.17, dl) * step(pc.y, lp.y + 0.12);
			float flex = step(abs(pc.x - lp.x), 0.012) * step(lp.y, pc.y) * smoothstep(0.3, 0.0, abs(pc.z - lp.z));
			vec3 sc = lamp ? lamp_color.rgb * lamp_energy * 2.5 : daylight_color.rgb * daylight * 0.35 * vec3(0.8, 0.7, 0.5);
			room = mix(room, sc, max(shade, flex * 0.8));
		}

		// Curtains just behind the glass: drapes drawn to either side, or a net across it.
		vec2 cw = win + vec2(dx, dy) * (0.1 / dz);
		float ck = fi_h31(key + seed + 301.0);
		if (ck < curtain_share) {
			// Folds of uneven width, softer near the floor where the cloth hangs straight.
			float wave = sin(cw.x * (26.0 + 12.0 * r5) + fi_noise(cw * vec2(4.0, 0.4)) * 6.0);
			if (ck < curtain_share * 0.6) {
				// Drapes tied back: drawn furthest open a little above the sill.
				float open = (0.25 + 0.5 * r4) * 0.5 + 0.12 * smoothstep(0.7, 0.0, abs(cw.y - 1.55));
				float drape = smoothstep(open - 0.015, open + 0.015, abs(cw.x));
				vec3 cc = mix(vec3(0.50, 0.10, 0.08), vec3(0.62, 0.52, 0.30), step(0.4, r3));
				cc = mix(cc, vec3(0.25, 0.32, 0.20), step(0.75, r3));
				cc = mix(cc, vec3(0.78, 0.74, 0.62), step(0.9, r3));
				// Lit from the window; darker in the folds and towards the drape's outer edge.
				float shade = (0.72 + 0.28 * wave) * (1.0 - 0.25 * smoothstep(open, open + 0.5, abs(cw.x)));
				room = mix(room, cc * shade * daylight * daylight_color.rgb * 1.2, drape);
			} else {
				// A net: sheer, its folds soft.
				room = mix(room, vec3(0.85, 0.83, 0.78) * (0.85 + 0.15 * wave) * daylight * daylight_color.rgb * 1.2, 0.35);
			}
		}
	}

	// The glass: what passes it (Fresnel), its grime, the old panes a little out of true.
	float fres = 0.04 + 0.96 * pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 5.0);
	float dirt = grime * (0.4 + 0.6 * fi_noise(win * vec2(3.0, 2.0)));
	if (pane_size.x > 0.0 && pane_size.y > 0.0) {
		vec2 pane = floor(win / pane_size);
		vec2 wob = (vec2(fi_h21(pane + key.xy), fi_h21(pane + key.yx + 5.0)) - 0.5) * 0.03;
		NORMAL = normalize(NORMAL + (VIEW_MATRIX * vec4(wob.x, wob.y, 0.0, 0.0)).xyz);
	}
	EMISSION = room * glass_tint.rgb * (1.0 - fres) * (1.0 - 0.6 * dirt);
	// (Its film lit by the sun is faint: a dusty albedo had hazed the rooms over in the golden hour's light.)
	ALBEDO = vec3(0.06, 0.055, 0.05) * dirt;
	ROUGHNESS = mix(glass_roughness, 0.7, dirt);
	SPECULAR = 0.5;
	// The street's other houses hide most of the sky a window would mirror.
	AO = reflection;
}
