// FellowEngine showcase (rooftops): old window glass, the room behind it — dark, a curtain drawn in some, a lamp lit
// in a few; the panes' leading, the sky's reflection with the panes each a little out of true.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform float lit_share = 0.07;
uniform float curtain_share = 0.35;
uniform float lamp_energy = 0.5;
// Milestone 158: the panes' leading (old glass) — off where the windows have wooden casements (their bars the panes).
uniform float lead_width = 0.05;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 cell = floor(world_pos / vec3(1.3, 3.4, 1.3));
	float r = rt_hash13(cell + 0.5);
	float r2 = rt_hash13(cell + 7.5);
	vec2 uv = UV;
	vec3 col = vec3(0.025, 0.024, 0.022) * (0.6 + 0.8 * rt_fbm(uv * 2.0, 2));
	float emit = 0.0;
	if (r < curtain_share) {
		float folds = 0.5 + 0.5 * sin(uv.x * 55.0 + rt_noise(uv * vec2(4.0, 1.0)) * 4.0);
		vec3 cc = mix(vec3(0.75, 0.68, 0.55), vec3(0.55, 0.16, 0.12), step(0.5, r2));
		col = cc * (0.18 + 0.12 * folds);
	}
	if (r > 1.0 - lit_share) {
		emit = lamp_energy * (0.6 + 0.4 * rt_fbm(uv * 1.5, 2));
	}
	// The leading: panes 0.25 x 0.3.
	vec2 p = fract(uv / vec2(0.25, 0.3));
	float lead = lead_width > 0.0 ? 1.0 - smoothstep(0.0, lead_width, min(min(p.x, 1.0 - p.x), min(p.y, 1.0 - p.y))) : 0.0;
	vec2 pane = floor(uv / vec2(0.25, 0.3));
	vec2 wob = (rt_hash22(pane + cell.xz) - 0.5) * 0.025;
	NORMAL = normalize(NORMAL + (VIEW_MATRIX * vec4(wob.x, wob.y, 0.0, 0.0)).xyz);
	ALBEDO = mix(col, vec3(0.04), lead);
	EMISSION = vec3(1.0, 0.62, 0.3) * emit * (1.0 - lead) * (r < curtain_share ? 0.6 : 0.25);
	ROUGHNESS = mix(0.04, 0.6, lead);
	SPECULAR = mix(0.45, 0.2, lead);
	METALLIC = 0.0;
}
