// FellowEngine showcase (rooftops): a roof of coppi — terracotta half-tubes in rows down the slope, the channels laid
// concave, the covers convex over them, each tile overlapping the one below; each tile its own fired tone, some
// darker, lichen in yellow-grey spots, the channels' shade. The UVs: u along the eave, v up the slope (metres).
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.68, 0.34, 0.2);
uniform float variation = 0.18;
uniform float lichen = 0.35;
uniform float pitch = 0.26; // the rows' spacing along the eave (m): a channel and a cover
uniform float tile_length = 0.42;
uniform float seed = 0.0;

void fragment() {
	vec2 uv = UV + vec2(seed * 7.7, seed * 2.3);
	float fade = rt_detail_fade(uv, 0.02);
	float s = -uv.y; // up the slope (the glTF's v runs down it)
	float col_i = floor(uv.x / pitch * 2.0);
	float x = fract(uv.x / pitch * 2.0); // 0..1 across half a pitch: a channel then a cover
	bool cover = mod(col_i, 2.0) > 0.5;
	float row_shift = cover ? 0.5 : 0.0;
	float row = floor(s / tile_length + row_shift + rt_hash12(vec2(col_i, 1.0)) * 0.2);
	float along = fract(s / tile_length + row_shift + rt_hash12(vec2(col_i, 1.0)) * 0.2); // 0 at the tile's low end
	float arc = sin(3.14159 * x);
	float h;
	if (cover) {
		h = 0.05 * pow(arc, 0.7);
	} else {
		h = -0.022 * arc;
	}
	// Each tile lies a little lower up the slope, its low end lifted over the tile below.
	h += 0.014 * (1.0 - along) + 0.006 * smoothstep(0.0, 0.03, along) - 0.006;
	vec2 tid = vec2(col_i, row);
	float tone = rt_hash12(tid + seed);
	vec3 col = color * (1.0 + variation * (tone - 0.5) * 2.0);
	col = mix(col, col * vec3(0.7, 0.62, 0.58), step(0.9, tone) * 0.7);
	col = mix(col, col * vec3(1.12, 1.02, 0.86), step(tone, 0.12) * 0.6);
	col *= 0.9 + 0.2 * rt_fbm(uv * 3.0 + tid, 3);
	// Shade: the channels, the low ends' shadow on the tile below.
	float shade = cover ? 1.0 : 0.72 + 0.28 * (1.0 - arc);
	shade *= mix(0.65, 1.0, smoothstep(0.0, 0.08, along));
	col *= mix(1.0, shade, fade);
	// Lichen and soot.
	float l = smoothstep(0.62, 0.7, rt_fbm(uv * 1.1 + 31.0, 4) + 0.15 * rt_noise(uv * 9.0)) * lichen;
	col = mix(col, vec3(0.62, 0.6, 0.45), l * 0.75);
	float soot = smoothstep(0.55, 0.85, rt_fbm(uv * 0.25 + 3.0, 3));
	col *= 1.0 - 0.25 * soot;
	ALBEDO = col;
	ROUGHNESS = 0.72 + 0.15 * l + 0.1 * (1.0 - tone);
	SPECULAR = 0.45;
	AO = mix(1.0, shade, 0.6);
	NORMAL = rt_bump(NORMAL, VERTEX, (h + 0.001 * rt_noise(uv * 40.0)) * mix(0.25, 1.0, fade), 1.0);
}
