/**************************************************************************/
/*  fellow_interior_material_3d.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fellow_interior_material_3d.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex FellowInteriorMaterial3D::shader_mutex;
RID FellowInteriorMaterial3D::shader;

const char *FellowInteriorMaterial3D::get_shader_code() {
	// MSVC takes string literals of at most 16380 bytes (C2026): the shader in adjacent pieces.
	return R"(// FellowEngine's FellowInteriorMaterial3D (milestone 188): a room behind each window.
shader_type spatial;
// No direct specular: the sun's disc mirrored in old panes whitened whole windows; the sky's reflection stays.
render_mode diffuse_burley, specular_disabled;

uniform bool rooms_from_uv2 = true;
uniform vec3 room_size = vec3(3.6, 3.6, 4.5);
uniform float floor_height = 0.0;
uniform float seed = 0.0;
uniform float daylight = 1.0;
uniform vec4 daylight_color : source_color = vec4(1.0, 0.93, 0.84, 1.0);
uniform float lamp_share = 0.0;
uniform float lamp_energy = 3.0;
uniform vec4 lamp_color : source_color = vec4(1.0, 0.7, 0.42, 1.0);
uniform float curtain_share = 0.6;
uniform float furniture = 1.0;
uniform vec4 glass_tint : source_color = vec4(0.92, 0.95, 0.93, 1.0);
uniform float glass_roughness = 0.08;
uniform float grime = 0.25;
uniform float reflection = 0.35;
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
// Every pattern below is filtered over w, the pixel's footprint where the ray meets it (metres; in a pattern's periods
// where it repeats): a pattern finer than a pixel fades to its average instead of flickering as the view moves.
// A step at e (x >= e), filtered.
float fi_step(float e, float x, float w) { return clamp((x - e) / max(w, 1e-5) + 0.5, 0.0, 1.0); }
// The integral of a pulse over [a, b] repeating every 1; a pulse train box-filtered over w (x and w in its periods).
float fi_ramp(float x, float a, float b) { return floor(x) * (b - a) + clamp(fract(x), a, b) - a; }
float fi_pulse(float x, float a, float b, float w) {
	w = max(w, 1e-4);
	return clamp((fi_ramp(x + 0.5 * w, a, b) - fi_ramp(x - 0.5 * w, a, b)) / w, 0.0, 1.0);
}
// Inside the box [lo, hi], filtered; inside the box [lo, hi] of a cell of a grid of cells c wide.
float fi_box(vec2 p, vec2 lo, vec2 hi, float w) { return fi_step(lo.x, p.x, w) * fi_step(p.x, hi.x, w) * fi_step(lo.y, p.y, w) * fi_step(p.y, hi.y, w); }
float fi_grid(vec2 p, vec2 c, vec2 lo, vec2 hi, float w) { return fi_pulse(p.x / c.x, lo.x, hi.x, w / c.x) * fi_pulse(p.y / c.y, lo.y, hi.y, w / c.y); }
// Value noise of features 1 / f wide, fading to its mean where they are finer than the footprint.
float fi_fnoise(vec2 p, float f, float w) { return mix(fi_noise(p), 0.5, smoothstep(0.3, 0.9, w * f)); }
// The walls' colours: limewash creams, ochre, sage, teal, a deep red, a dusty pink, a pale blue, white.
vec3 fi_wall(float r) {
	vec3 c[8] = vec3[](vec3(0.82, 0.76, 0.62), vec3(0.72, 0.52, 0.26), vec3(0.42, 0.52, 0.38), vec3(0.26, 0.42, 0.45), vec3(0.55, 0.17, 0.13), vec3(0.72, 0.50, 0.45), vec3(0.55, 0.62, 0.70), vec3(0.86, 0.83, 0.76));
	return c[int(r * 7.999)];
}
vec3 fi_wood(vec2 q, float shade, float w) {
	return mix(vec3(0.20, 0.11, 0.06), vec3(0.34, 0.20, 0.11), shade) * (0.85 + 0.3 * fi_fnoise(q * vec2(2.0, 24.0), 24.0, w));
}
// A wall's surface at q (along it, up it): plaster or paper, a dado or panelling, a skirting, a cornice.
vec3 fi_wall_surface(vec2 q, float h, vec3 wall, float r1, float r2, float w) {
	vec3 a = wall * (0.92 + 0.1 * fi_fnoise(q * 3.0, 3.0, w));
	if (r1 > 0.62) {
		// Damask paper (its motif 14 cm across: its mean beyond).
		vec2 p = q * vec2(3.5, 2.6);
		float m = abs(sin(p.x * 3.14159) * sin(p.y * 3.14159 + sin(p.x * 3.14159) * 0.8));
		a = mix(a, a * 0.72, mix(smoothstep(0.35, 0.55, m), 0.4, smoothstep(0.03, 0.08, w)) * 0.7);
	} else if (r1 > 0.45) {
		a *= 1.0 - 0.12 * fi_pulse(q.x * 5.0, 0.5, 1.0, w * 5.0);
	}
	if (r2 > 0.7) {
		// Wooden panelling to a dado rail.
		float panel = fi_grid(q, vec2(0.6, 0.9), vec2(0.1, 0.12), vec2(0.9, 0.88), w);
		a = mix(a, fi_wood(q, 0.6, w) * (1.0 - 0.15 * panel), fi_step(q.y, 1.0, w));
	} else if (r2 > 0.4) {
		a = mix(a, a * 0.7, fi_step(q.y, 0.95, w));
	}
	a = mix(a, vec3(0.16, 0.10, 0.065), fi_step(q.y, 0.12, w));
	a = mix(a, vec3(0.86, 0.84, 0.78), fi_step(h - 0.16, q.y, w));
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

		// The pixel's footprint: on the glass, then where the ray meets a surface (wider with the distance travelled and
		// with the angle the ray meets it at).
		float px = 0.7 * length(fwidth(obj_pos));
		float dist = max(length(obj_pos - cam_obj), 1e-3);
		float cosf = face == 0 ? rd.z : (face <= 2 ? abs(rd.x) : abs(rd.y));
		float w = px * (dist + t) / dist / max(cosf, 0.25);

		vec3 wall = fi_wall(r0);
		vec3 alb;
		float facing;
		float edge;
		if (face <= 2) {
			vec2 q = face == 0 ? h.xy : vec2(face == 1 ? h.z : R.z - h.z, h.y);
			alb = fi_wall_surface(q, R.y, wall, r1, r2, w);
			if (face == 0) {
				// Against the back wall: a painting, a door (open on the dark beyond, or shut), a bookcase, a wardrobe.
				if (r3 < 0.22) {
					vec2 c = vec2(R.x * (0.35 + 0.3 * r4), 1.7);
					vec2 s = vec2(0.45 + 0.35 * r2, 0.32 + 0.22 * r1);
					float pic = fi_box(q, c - s + 0.07, c + s - 0.07, w);
					vec3 art = mix(vec3(0.16, 0.22, 0.14), vec3(0.62, 0.48, 0.30), smoothstep(c.y - 0.15, c.y + 0.25, q.y + 0.1 * fi_fnoise(q * 4.0, 4.0, w))) * (0.7 + 0.5 * fi_fnoise(q * 9.0, 9.0, w));
					alb = mix(alb, mix(vec3(0.55, 0.40, 0.15), art, pic), fi_box(q, c - s, c + s, w));
				} else if (r3 < 0.42) {
					float x0 = R.x * (0.15 + 0.5 * r4);
					vec2 dq = q - vec2(x0, 0.0);
					float door = fi_box(dq, vec2(0.0), vec2(0.95, 2.2), w);
					float frame = door * (1.0 - fi_box(dq, vec2(0.07), vec2(0.88, 2.13), w));
					float panel = fi_grid(dq, vec2(0.44, 0.7), vec2(0.12), vec2(0.88), w);
					vec3 leaf = fi_wood(q, 0.3, w) * (1.0 - 0.2 * panel);
					// Open: the next room, dark.
					leaf = mix(leaf, vec3(0.025, 0.02, 0.015) * (1.0 + 2.0 * smoothstep(0.88, 0.5, dq.x / 0.95)), step(0.6, r5));
					alb = mix(alb, mix(leaf, fi_wood(q, 0.8, w), frame), door);
				} else if (r3 < 0.64) {
					float x0 = R.x * (0.1 + 0.4 * r4);
					vec2 sp = vec2(q.x - x0, q.y);
					float shelf = fi_pulse(sp.y / 0.38, 0.0, 0.07, w / 0.38);
					float book_id = floor(sp.x / 0.045) + floor(sp.y / 0.38) * 57.0;
					float book = step(fract(sp.y / 0.38) * 0.38, 0.22 + 0.12 * fi_h11(book_id)) * step(0.08, fi_h11(book_id + 3.0));
					vec3 bcol = mix(vec3(0.42, 0.10, 0.07), vec3(0.10, 0.18, 0.30), fi_h11(book_id + 7.0));
					bcol = mix(bcol, vec3(0.60, 0.48, 0.25), step(0.7, fi_h11(book_id + 11.0)));
					bcol = mix(bcol, vec3(0.15, 0.28, 0.16), step(0.85, fi_h11(book_id + 13.0)));
					vec3 inner = mix(vec3(0.05, 0.035, 0.025), bcol, book);
					// Books 4.5 cm wide: their mean (two thirds of a shelf's height, mostly red and blue) beyond.
					inner = mix(inner, vec3(0.24, 0.18, 0.145), smoothstep(0.012, 0.035, w));
					alb = mix(alb, mix(inner, fi_wood(q, 0.4, w), shelf), fi_box(q, vec2(x0, 0.0), vec2(x0 + 1.2 + 0.8 * r2, 2.2), w));
				} else if (r3 < 0.84) {
					float x0 = R.x * (0.1 + 0.5 * r4);
					vec2 dq = q - vec2(x0, 0.0);
					float body = fi_box(dq, vec2(0.0, 0.08), vec2(1.2, 2.05), w);
					float top = fi_box(dq, vec2(-0.05, 2.05), vec2(1.25, 2.18), w);
					float split = fi_box(dq, vec2(0.585, 0.15), vec2(0.615, 2.0), w);
					float panel = fi_grid(dq, vec2(0.6, 0.95), vec2(0.14, 0.1), vec2(0.86, 0.9), w);
					alb = mix(alb, fi_wood(q, 0.55, w) * (1.0 - 0.2 * panel - 0.5 * split), max(body, top));
				}
			} else {
				// On a side wall: a framed picture or a mirror.
				float zc = R.z * (0.35 + 0.3 * r5);
				vec2 s = vec2(0.3 + 0.2 * r6, 0.25 + 0.15 * r4);
				vec2 c = vec2(face == 1 ? zc : R.z - zc, 1.6);
				float inside = fi_box(q, c - s, c + s, w) * step(0.35, r6);
				float pic = fi_box(q, c - s + 0.05, c + s - 0.05, w);
				vec3 art = mix(vec3(0.30, 0.26, 0.20), vec3(0.62, 0.66, 0.70), step(0.7, r5)) * (0.75 + 0.4 * fi_fnoise(q * 7.0, 7.0, w));
				alb = mix(alb, mix(vec3(0.45, 0.33, 0.14), art, pic), inside);
			}
			facing = face == 0 ? 0.8 : 0.7;
			vec2 e = face == 0 ? vec2(min(h.x, R.x - h.x), min(h.y, R.y - h.y)) : vec2(min(h.z, R.z - h.z), min(h.y, R.y - h.y));
			edge = min(e.x, e.y);
		} else if (face == 3) {
			// The floor: cotto tiles, boards, a parquet's herringbone or stone; a rug.
			vec2 q = vec2(h.x, h.z);
			if (r1 < 0.45) {
				float tone = mix(fi_h21(floor(q / 0.3) + key.xy), 0.5, smoothstep(0.08, 0.2, w));
				vec3 cotto = vec3(0.56, 0.29, 0.18) * (0.8 + 0.32 * tone);
				alb = mix(cotto, vec3(0.40, 0.34, 0.28), 1.0 - fi_grid(q, vec2(0.3), vec2(0.03), vec2(0.97), w));
			} else if (r1 < 0.75) {
				float b = floor(q.x / 0.14);
				float seam = fi_pulse(q.x / 0.14, 0.0, 0.04, w / 0.14) + fi_pulse(q.y / 1.9 + fi_h11(b), 0.0, 0.01, w / 1.9);
				float tone = mix(fi_h11(b + key.x), 0.5, smoothstep(0.04, 0.1, w));
				alb = mix(fi_wood(q.yx, tone, w) * 1.4, vec3(0.10, 0.06, 0.035), min(seam, 1.0));
			} else if (r1 < 0.9) {
				vec2 p = vec2(q.x + q.y, q.y - q.x) / 0.32;
				float row = floor(p.y);
				float alt = mod(row, 2.0);
				float tone = mix(fi_h11(floor(p.x * 0.5) + row * 7.0), 0.5, smoothstep(0.06, 0.15, w));
				alb = fi_wood(q, tone, w) * 1.4 * mix(0.85, 1.0, fi_pulse(p.x * 0.5 + alt * 0.5, 0.5, 1.0, w * 2.2));
			} else {
				alb = vec3(0.72, 0.70, 0.66) * (0.85 + 0.2 * fi_fnoise(q * 3.0, 3.0, w));
			}
			vec2 lo = vec2(R.x * 0.2, R.z * 0.22);
			vec2 hi = vec2(R.x * 0.8, R.z * 0.7);
			vec2 rq = q - lo;
			float border = 1.0 - fi_box(q, lo + 0.14, hi - 0.14, w);
			vec3 rug = mix(vec3(0.48, 0.10, 0.07), vec3(0.10, 0.15, 0.32), step(0.65, r3));
			float diamonds = fi_pulse((rq.x + rq.y) * 3.0, 0.5, 1.0, w * 4.2) * fi_pulse((rq.x - rq.y) * 3.0, 0.5, 1.0, w * 4.2);
)"
		   R"(			rug = mix(rug * (0.75 + 0.35 * diamonds), vec3(0.70, 0.56, 0.32), border);
			alb = mix(alb, rug, fi_box(q, lo, hi, w) * step(0.4, r2));
			facing = 1.0 + 0.6 * exp(-h.z / 0.8);
			edge = min(min(h.x, R.x - h.x), R.z - h.z);
		} else {
			// The ceiling: beams across it, or plaster with a cornice.
			vec2 q = vec2(h.x, h.z);
			alb = vec3(0.86, 0.83, 0.77);
			if (r2 < 0.55) {
				float beam = fi_pulse(q.y / 0.6, 0.0, 0.24, w / 0.6);
				alb = mix(alb * vec3(0.95, 0.85, 0.75), fi_wood(q.yx, 0.2, w), beam);
			}
			facing = 0.45;
			edge = min(min(h.x, R.x - h.x), R.z - h.z);
		}
		float ao = 0.5 + 0.5 * smoothstep(0.0, 0.6, edge);

		// The light: the day through the window, falling off into the room; a lamp under the ceiling.
		vec3 lp = vec3(R.x * 0.5, R.y - 0.75, R.z * 0.5);
		bool lamp = fi_h31(key + seed + 211.0) < lamp_share;
		vec3 lit = daylight_color.rgb * (daylight * mix(0.1, 1.0, exp(-h.z / (0.3 * R.z))) * facing * ao);
		if (lamp) {
			lit += lamp_color.rgb * lamp_energy / (1.0 + 1.2 * dot(h - lp, h - lp)) * mix(0.6, 1.0, min(facing, 1.0));
		}
		room = alb * lit;

		// Furniture: in front of the back wall (a sideboard with a lamp or a vase on it, a bed's head), then across the
		// middle of the room (a table and chairs, a sofa, an armchair and a side table, a desk), each over what is behind.
		float fkind = fi_h31(key + seed + 133.0);
		float bkind = fi_h31(key + seed + 257.0);
		float zm = R.z * (0.38 + 0.14 * r4);
		float zb = R.z - 0.42;
		for (int layer = 1; layer >= 0; layer--) {
			float zp = layer == 0 ? zm : zb;
			float tp = zp / dz;
			vec3 hp = vec3(local + vec2(dx, dy) * tp, zp);
			if (tp >= t || hp.x <= 0.0 || hp.x >= R.x || hp.y <= 0.0) {
				continue;
			}
			float wf = px * (dist + tp) / dist / max(rd.z, 0.25);
			float cx = layer == 0 ? R.x * (0.3 + 0.4 * r2) : R.x * (0.25 + 0.5 * r5);
			vec2 a = vec2(abs(hp.x - cx), hp.y);
			float f = 0.0;
			vec3 c = vec3(0.0);
			float k = layer == 0 ? fkind / max(furniture, 1e-3) : bkind / max(furniture, 1e-3);
			if (layer == 0) {
				if (k < 0.3) {
					float top = fi_box(hp.xy, vec2(cx - 0.65, 0.72), vec2(cx + 0.65, 0.79), wf);
					float legs = fi_box(a, vec2(0.52, 0.0), vec2(0.57, 0.75), wf);
					float chair = fi_box(a, vec2(0.78, 0.42), vec2(1.2, 0.48), wf) + fi_box(a, vec2(1.14, 0.0), vec2(1.2, 1.02), wf) + fi_box(a, vec2(0.78, 0.0), vec2(0.82, 0.45), wf);
					// A vase on the table.
					float vase = fi_step(length((hp.xy - vec2(cx + 0.2, 0.92)) / vec2(0.07, 0.13)), 1.0, wf / 0.08);
					f = min(top + legs + chair + vase, 1.0);
					c = mix(fi_wood(hp.xy, 0.4, wf), vec3(0.75, 0.70, 0.62), vase);
					c *= 1.0 + 0.4 * top;
				} else if (k < 0.52) {
					f = min(fi_box(a, vec2(0.0, 0.1), vec2(0.95, 0.86), wf) + fi_box(a, vec2(0.82, 0.0), vec2(0.87, 0.1), wf), 1.0);
					c = mix(vec3(0.42, 0.30, 0.20), vec3(0.22, 0.30, 0.24), step(0.5, r1));
					c = mix(c, vec3(0.45, 0.14, 0.12), step(0.8, r1));
					c *= 1.0 - 0.3 * (fi_box(a, vec2(0.0, 0.42), vec2(0.8, 0.47), wf) + fi_box(a, vec2(0.79, 0.1), vec2(0.82, 0.64), wf));
					c *= 1.0 + 0.25 * fi_step(0.47, hp.y, wf) * fi_step(hp.y, 0.6, wf) * fi_step(a.x, 0.79, wf);
				} else if (k < 0.68) {
					float chair = fi_box(hp.xy - vec2(cx - 0.4, 0.0), vec2(-0.4, 0.1), vec2(0.4, 1.0), wf);
					float tbl = fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.25, 0.55), vec2(0.25, 0.6), wf) + fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.03, 0.0), vec2(0.03, 0.58), wf);
					float lampshade = fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.13, 0.85), vec2(0.13, 1.05), wf) + fi_box(hp.xy - vec2(cx + 0.45, 0.0), vec2(-0.015, 0.6), vec2(0.015, 0.86), wf);
					f = min(chair + tbl + lampshade, 1.0);
					vec3 cc = mix(vec3(0.40, 0.12, 0.10), vec3(0.20, 0.28, 0.22), step(0.5, r6));
					c = mix(mix(fi_wood(hp.xy, 0.3, wf), vec3(0.85, 0.78, 0.6), min(lampshade, 1.0)), cc, min(chair, 1.0));
				} else if (k < 0.8) {
					float desk = fi_box(hp.xy, vec2(cx - 0.7, 0.74), vec2(cx + 0.7, 0.79), wf) + fi_box(a, vec2(0.6, 0.0), vec2(0.7, 0.76), wf);
					float chair = fi_box(hp.xy - vec2(cx, 0.0), vec2(-0.22, 0.44), vec2(0.22, 0.95), wf) * fi_step(0.48, hp.y, wf);
					f = min(desk + chair, 1.0);
					c = fi_wood(hp.xy, 0.25, wf);
				}
			} else {
				if (k < 0.35) {
					float body = fi_box(a, vec2(0.0, 0.1), vec2(0.75, 0.95), wf);
					float drawers = fi_pulse(hp.y / 0.28, 0.0, 0.04, wf / 0.28) * fi_step(hp.y, 0.95, wf);
					float feet = fi_box(a, vec2(0.68, 0.0), vec2(0.74, 0.1), wf);
					// On it a lamp or a vase.
					float lampshade = fi_box(hp.xy - vec2(cx - 0.35, 0.0), vec2(-0.14, 1.25), vec2(0.14, 1.45), wf) + fi_box(hp.xy - vec2(cx - 0.35, 0.0), vec2(-0.02, 0.95), vec2(0.02, 1.26), wf);
					float vase = fi_step(length((hp.xy - vec2(cx + 0.3, 1.08)) / vec2(0.08, 0.14)), 1.0, wf / 0.09);
					float on = step(0.5, r6);
					float piece = on > 0.5 ? lampshade : vase;
					f = min(body + feet + piece, 1.0);
					c = mix(vec3(0.82, 0.76, 0.6), fi_wood(hp.xy, 0.5, wf) * (1.0 - 0.5 * drawers), min(body + feet, 1.0));
				} else if (k < 0.5) {
					float head = fi_box(a, vec2(0.0, 0.0), vec2(0.8, 1.2), wf) * (1.0 - fi_step(1.05, hp.y, wf) * fi_step(0.7, a.x, wf));
					f = head;
					c = fi_wood(hp.xy, 0.35, wf);
				}
			}
			if (f > 0.0) {
				vec3 flit = daylight_color.rgb * (daylight * mix(0.1, 1.0, exp(-zp / (0.3 * R.z))) * 0.85 * (0.6 + 0.4 * smoothstep(0.0, 0.4, hp.y)));
				if (lamp) {
					flit += lamp_color.rgb * lamp_energy / (1.0 + 1.2 * dot(hp - lp, hp - lp)) * 0.7;
				}
				room = mix(room, c * flit, f);
			}
		}
		// The hanging lamp: a shade on a flex, lit or not.
		float tc = dot(lp - o, rd);
		float dl = length(o + rd * tc - lp);
		if (tc > 0.0 && tc < t) {
			vec3 pc = o + rd * tc;
			float wl = px * (dist + tc) / dist;
			float shade = fi_step(dl, 0.185, max(wl, 0.03)) * fi_step(pc.y, lp.y + 0.12, wl);
			float flex = fi_step(abs(pc.x - lp.x), 0.012, wl) * fi_step(lp.y, pc.y, wl) * smoothstep(0.3, 0.0, abs(pc.z - lp.z));
			vec3 sc = lamp ? lamp_color.rgb * lamp_energy * 2.5 : daylight_color.rgb * daylight * 0.35 * vec3(0.8, 0.7, 0.5);
			room = mix(room, sc, max(shade, flex * 0.8));
		}

		// Curtains just behind the glass: drapes drawn to either side, or a net across it.
		vec2 cw = win + vec2(dx, dy) * (0.1 / dz);
		float wc = px * (dist + 0.1 / dz) / dist;
		float ck = fi_h31(key + seed + 301.0);
		if (ck < curtain_share) {
			// Folds of uneven width (about 20 cm: flat beyond), softer near the floor where the cloth hangs straight.
			float wave = sin(cw.x * (26.0 + 12.0 * r5) + fi_noise(cw * vec2(4.0, 0.4)) * 6.0) * (1.0 - smoothstep(0.03, 0.09, wc));
			if (ck < curtain_share * 0.6) {
				// Drapes tied back: drawn furthest open a little above the sill.
				float open = (0.25 + 0.5 * r4) * 0.5 + 0.12 * smoothstep(0.7, 0.0, abs(cw.y - 1.55));
				float drape = smoothstep(open - 0.015 - wc, open + 0.015 + wc, abs(cw.x));
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
)";
}

void FellowInteriorMaterial3D::_param(const char *p_name, const Variant &p_value) {
	RS::get_singleton()->material_set_param(_get_material(), p_name, p_value);
}

void FellowInteriorMaterial3D::set_rooms_from_uv2(bool p_enable) {
	rooms_from_uv2 = p_enable;
	_param("rooms_from_uv2", p_enable);
}

void FellowInteriorMaterial3D::set_room_size(const Vector3 &p_size) {
	room_size = p_size.max(Vector3(0.5, 0.5, 0.2));
	_param("room_size", room_size);
}

void FellowInteriorMaterial3D::set_floor_height(float p_height) {
	floor_height = p_height;
	_param("floor_height", floor_height);
}

void FellowInteriorMaterial3D::set_seed(float p_seed) {
	seed = p_seed;
	_param("seed", seed);
}

void FellowInteriorMaterial3D::set_daylight(float p_energy) {
	daylight = MAX(p_energy, 0.0f);
	_param("daylight", daylight);
}

void FellowInteriorMaterial3D::set_daylight_color(const Color &p_color) {
	daylight_color = p_color;
	_param("daylight_color", daylight_color);
}

void FellowInteriorMaterial3D::set_lamp_share(float p_share) {
	lamp_share = CLAMP(p_share, 0.0f, 1.0f);
	_param("lamp_share", lamp_share);
}

void FellowInteriorMaterial3D::set_lamp_energy(float p_energy) {
	lamp_energy = MAX(p_energy, 0.0f);
	_param("lamp_energy", lamp_energy);
}

void FellowInteriorMaterial3D::set_lamp_color(const Color &p_color) {
	lamp_color = p_color;
	_param("lamp_color", lamp_color);
}

void FellowInteriorMaterial3D::set_curtain_share(float p_share) {
	curtain_share = CLAMP(p_share, 0.0f, 1.0f);
	_param("curtain_share", curtain_share);
}

void FellowInteriorMaterial3D::set_furniture(float p_share) {
	furniture = CLAMP(p_share, 0.0f, 1.0f);
	_param("furniture", furniture);
}

void FellowInteriorMaterial3D::set_glass_tint(const Color &p_color) {
	glass_tint = p_color;
	_param("glass_tint", glass_tint);
}

void FellowInteriorMaterial3D::set_glass_roughness(float p_roughness) {
	glass_roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	_param("glass_roughness", glass_roughness);
}

void FellowInteriorMaterial3D::set_grime(float p_grime) {
	grime = CLAMP(p_grime, 0.0f, 1.0f);
	_param("grime", grime);
}

void FellowInteriorMaterial3D::set_reflection(float p_reflection) {
	reflection = CLAMP(p_reflection, 0.0f, 1.0f);
	_param("reflection", reflection);
}

void FellowInteriorMaterial3D::set_pane_size(const Vector2 &p_size) {
	pane_size = p_size.max(Vector2());
	_param("pane_size", pane_size);
}

Shader::Mode FellowInteriorMaterial3D::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID FellowInteriorMaterial3D::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID FellowInteriorMaterial3D::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void FellowInteriorMaterial3D::_update_shader() {
	MutexLock shader_lock(shader_mutex);
	if (shader.is_null()) {
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, get_shader_code());
	}
}

void FellowInteriorMaterial3D::cleanup_shader() {
	if (shader.is_valid()) {
		ERR_FAIL_NULL(RenderingServer::get_singleton());
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

void FellowInteriorMaterial3D::fellow_bake_shader_codes(Vector<String> &r_codes) {
	r_codes.push_back(get_shader_code());
}

void FellowInteriorMaterial3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rooms_from_uv2", "enable"), &FellowInteriorMaterial3D::set_rooms_from_uv2);
	ClassDB::bind_method(D_METHOD("is_rooms_from_uv2"), &FellowInteriorMaterial3D::is_rooms_from_uv2);
	ClassDB::bind_method(D_METHOD("set_room_size", "size"), &FellowInteriorMaterial3D::set_room_size);
	ClassDB::bind_method(D_METHOD("get_room_size"), &FellowInteriorMaterial3D::get_room_size);
	ClassDB::bind_method(D_METHOD("set_floor_height", "height"), &FellowInteriorMaterial3D::set_floor_height);
	ClassDB::bind_method(D_METHOD("get_floor_height"), &FellowInteriorMaterial3D::get_floor_height);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowInteriorMaterial3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowInteriorMaterial3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_daylight", "energy"), &FellowInteriorMaterial3D::set_daylight);
	ClassDB::bind_method(D_METHOD("get_daylight"), &FellowInteriorMaterial3D::get_daylight);
	ClassDB::bind_method(D_METHOD("set_daylight_color", "color"), &FellowInteriorMaterial3D::set_daylight_color);
	ClassDB::bind_method(D_METHOD("get_daylight_color"), &FellowInteriorMaterial3D::get_daylight_color);
	ClassDB::bind_method(D_METHOD("set_lamp_share", "share"), &FellowInteriorMaterial3D::set_lamp_share);
	ClassDB::bind_method(D_METHOD("get_lamp_share"), &FellowInteriorMaterial3D::get_lamp_share);
	ClassDB::bind_method(D_METHOD("set_lamp_energy", "energy"), &FellowInteriorMaterial3D::set_lamp_energy);
	ClassDB::bind_method(D_METHOD("get_lamp_energy"), &FellowInteriorMaterial3D::get_lamp_energy);
	ClassDB::bind_method(D_METHOD("set_lamp_color", "color"), &FellowInteriorMaterial3D::set_lamp_color);
	ClassDB::bind_method(D_METHOD("get_lamp_color"), &FellowInteriorMaterial3D::get_lamp_color);
	ClassDB::bind_method(D_METHOD("set_curtain_share", "share"), &FellowInteriorMaterial3D::set_curtain_share);
	ClassDB::bind_method(D_METHOD("get_curtain_share"), &FellowInteriorMaterial3D::get_curtain_share);
	ClassDB::bind_method(D_METHOD("set_furniture", "share"), &FellowInteriorMaterial3D::set_furniture);
	ClassDB::bind_method(D_METHOD("get_furniture"), &FellowInteriorMaterial3D::get_furniture);
	ClassDB::bind_method(D_METHOD("set_glass_tint", "color"), &FellowInteriorMaterial3D::set_glass_tint);
	ClassDB::bind_method(D_METHOD("get_glass_tint"), &FellowInteriorMaterial3D::get_glass_tint);
	ClassDB::bind_method(D_METHOD("set_glass_roughness", "roughness"), &FellowInteriorMaterial3D::set_glass_roughness);
	ClassDB::bind_method(D_METHOD("get_glass_roughness"), &FellowInteriorMaterial3D::get_glass_roughness);
	ClassDB::bind_method(D_METHOD("set_grime", "grime"), &FellowInteriorMaterial3D::set_grime);
	ClassDB::bind_method(D_METHOD("get_grime"), &FellowInteriorMaterial3D::get_grime);
	ClassDB::bind_method(D_METHOD("set_reflection", "reflection"), &FellowInteriorMaterial3D::set_reflection);
	ClassDB::bind_method(D_METHOD("get_reflection"), &FellowInteriorMaterial3D::get_reflection);
	ClassDB::bind_method(D_METHOD("set_pane_size", "size"), &FellowInteriorMaterial3D::set_pane_size);
	ClassDB::bind_method(D_METHOD("get_pane_size"), &FellowInteriorMaterial3D::get_pane_size);

	ADD_GROUP("Rooms", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rooms_from_uv2"), "set_rooms_from_uv2", "is_rooms_from_uv2");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "room_size", PROPERTY_HINT_NONE, "suffix:m"), "set_room_size", "get_room_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "floor_height", PROPERTY_HINT_RANGE, "-10,10,0.01,or_greater,or_less,suffix:m"), "set_floor_height", "get_floor_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "curtain_share", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_curtain_share", "get_curtain_share");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "furniture", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_furniture", "get_furniture");
	ADD_GROUP("Light", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "daylight", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_daylight", "get_daylight");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "daylight_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_daylight_color", "get_daylight_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lamp_share", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_lamp_share", "get_lamp_share");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lamp_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_lamp_energy", "get_lamp_energy");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "lamp_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_lamp_color", "get_lamp_color");
	ADD_GROUP("Glass", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "glass_tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_glass_tint", "get_glass_tint");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glass_roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_glass_roughness", "get_glass_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grime", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_grime", "get_grime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reflection", "get_reflection");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pane_size", PROPERTY_HINT_NONE, "suffix:m"), "set_pane_size", "get_pane_size");
}

FellowInteriorMaterial3D::FellowInteriorMaterial3D() {
	_set_material(RS::get_singleton()->material_create());
	set_rooms_from_uv2(rooms_from_uv2);
	set_room_size(room_size);
	set_floor_height(floor_height);
	set_seed(seed);
	set_daylight(daylight);
	set_daylight_color(daylight_color);
	set_lamp_share(lamp_share);
	set_lamp_energy(lamp_energy);
	set_lamp_color(lamp_color);
	set_curtain_share(curtain_share);
	set_furniture(furniture);
	set_glass_tint(glass_tint);
	set_glass_roughness(glass_roughness);
	set_grime(grime);
	set_reflection(reflection);
	set_pane_size(pane_size);
}

FellowInteriorMaterial3D::~FellowInteriorMaterial3D() {
	RS::get_singleton()->material_set_shader(_get_material(), RID());
}
