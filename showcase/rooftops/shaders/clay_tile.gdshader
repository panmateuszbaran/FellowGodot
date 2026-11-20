// FellowEngine showcase (rooftops), milestone 163: a coppo's fired clay, smooth as the owner's reference's tiles — no
// photograph's grain on it. Each tile its own shade (its id in its UVs: ac_common.Builder.coppo writes u = 2 * id + the
// place across the arch, v along the tile); broad pale patches of weathering over the roof; the arch's sides darker
// where they dip into the valley, the upper end in the shade of the course above, the lower end's edge rounded over.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.66, 0.36, 0.24);
uniform float variation = 0.1; // each tile's brightness, +-
uniform float warmth = 0.5; // how far a tile's hue may lean warmer
uniform float weathering = 0.3; // broad pale patches (a dust of lime and lichen) over the roof
uniform float roughness = 0.5;
uniform float specular = 0.4;
uniform float end_round = 0.07; // the lower end's edge rounded over this much of the tile's length
uniform float seed = 0.0;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float tile = floor(UV.x * 0.5);
	float u = UV.x - 2.0 * tile; // 0..1 across the arch
	bool pan = tile >= 4096.0; // the pans in the valleys (concave), ids from 4096
	float along = 1.0 - UV.y; // 0 at the lower end, 1 at the upper (Godot's v is the glTF's, flipped); < 0 the end's face
	float h = rt_hash12(vec2(tile, seed + 1.0));
	float h2 = rt_hash12(vec2(tile + 17.0, seed + 3.1));
	vec3 col = color * (1.0 + variation * (h - 0.5) * 2.0);
	col = mix(col, col * vec3(1.07, 0.97, 0.88), h2 * warmth);
	float w = rt_fbm(world_pos.xz * 1.1 + vec2(seed * 3.7, seed * 1.3), 3);
	col = mix(col, col * vec3(1.1, 1.08, 1.03) + vec3(0.025, 0.022, 0.018), weathering * smoothstep(0.48, 0.78, w));
	// A faint mottling of the fired clay (a few centimetres), faded where a pixel covers more.
	col *= 1.0 + 0.05 * (rt_noise(world_pos.xz * 22.0 + h * 31.0) - 0.5) * rt_detail_fade(world_pos.xz * 22.0, 1.0);
	float crown = 1.0 - abs(u - 0.5) * 2.0;
	float ao;
	if (pan) {
		// In the valley: under the covers at its sides, its floor deep.
		ao = mix(0.35, 0.6, smoothstep(0.1, 0.6, crown));
	} else {
		// The arch's sides dip into the valley; the upper end lies under the course above.
		ao = mix(0.6, 1.0, smoothstep(0.0, 0.5, crown));
		ao *= mix(1.0, 0.55, smoothstep(0.8, 1.0, along));
		// The lower end's very edge: a darker crust, worn.
		col *= mix(0.84, 1.0, smoothstep(0.0, 0.05, along));
	}
	vec3 n = normalize(NORMAL);
	if (!pan && along < end_round) {
		// The lower end rounded over: the normal leaning down the tile.
		mat3 tbn = rt_cotangent_frame(n, VERTEX, UV);
		vec3 down = tbn[1] - n * dot(tbn[1], n);
		float k = 1.0 - clamp(along / end_round, 0.0, 1.0);
		n = normalize(n + normalize(down + vec3(1e-6)) * 1.4 * k * k);
	}
	ALBEDO = clamp(col, 0.0, 1.0);
	ROUGHNESS = clamp(roughness + 0.12 * (w - 0.5) + (h - 0.5) * 0.08, 0.2, 1.0);
	SPECULAR = specular;
	AO = ao;
	AO_LIGHT_AFFECT = 0.45;
	NORMAL = n;
}
