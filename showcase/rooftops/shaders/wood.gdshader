// FellowEngine showcase (rooftops): weathered wood (beams, rafters, planks, doors) — grain along u, boards, grey where
// the sun has bleached it; painted (shutters: louvres, the paint worn to the wood at the edges) when `louvres` is on.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.38, 0.24, 0.14);
uniform float bleach = 0.25;
uniform float board_width = 0.0; // boards along u (0: none)
uniform bool louvres = false;
uniform vec3 paint : source_color = vec3(0.2, 0.32, 0.22);
uniform float wear = 0.3;
uniform float seed = 0.0;

void fragment() {
	vec2 uv = UV + vec2(seed * 3.3, seed * 11.1);
	float fade = rt_detail_fade(uv, 0.01);
	float board = board_width > 0.0 ? floor(uv.y / board_width) : 0.0;
	vec2 g = vec2(uv.x * 2.0 + rt_hash12(vec2(board, 3.0)) * 10.0, uv.y * 38.0 + rt_noise(uv * vec2(0.8, 4.0)) * 6.0);
	float grain = rt_fbm(g, 3);
	vec3 col = color * (0.75 + 0.5 * grain) * (0.9 + 0.2 * rt_hash12(vec2(board, 9.0)));
	col = mix(col, vec3(0.42, 0.4, 0.37), bleach * smoothstep(0.3, 0.8, rt_fbm(uv * 0.7, 3)));
	float h = 0.0015 * grain * fade;
	if (board_width > 0.0) {
		float e = fract(uv.y / board_width);
		float gap = 1.0 - smoothstep(0.0, 0.04, min(e, 1.0 - e));
		col *= 1.0 - 0.5 * gap;
		h -= 0.004 * gap;
	}
	float rough = 0.8;
	if (louvres) {
		float l = fract(uv.y / 0.065);
		h += 0.01 * l - 0.006 * smoothstep(0.85, 1.0, l);
		float worn = smoothstep(0.62, 0.7, rt_fbm(uv * 6.0 + 2.0, 4) * (1.0 - wear * 0.5) + wear * 0.35 * (1.0 - smoothstep(0.0, 0.15, l)));
		vec3 pc = paint * (0.85 + 0.25 * rt_fbm(uv * 2.0, 3));
		pc *= 0.8 + 0.25 * l;
		col = mix(pc, col, worn);
		rough = mix(0.6, 0.8, worn);
	}
	ALBEDO = col;
	ROUGHNESS = rough;
	SPECULAR = 0.35;
	NORMAL = rt_bump(NORMAL, VERTEX, h, 1.0);
}
