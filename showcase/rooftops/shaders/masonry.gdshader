// FellowEngine showcase (rooftops): stone and brick laid in courses — ashlar, rusticated blocks with their chamfered
// joints and rough faces, bricks, the square's paving slabs each tipped a little: each block its own tone, its joints
// recessed and darker, its face pitted; grime gathered low.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.72, 0.68, 0.6);
uniform vec3 joint_color : source_color = vec3(0.55, 0.52, 0.47);
uniform float variation = 0.12;
uniform float hue_variation = 0.04;
uniform vec2 block_size = vec2(0.7, 0.36);
uniform float row_offset = 0.5;
uniform float joint_width = 0.006;
uniform float joint_depth = 0.008;
uniform float chamfer = 0.01; // the bevel at a block's edges (m)
uniform float face_rough = 0.002; // the face's relief (m)
uniform float slab_tilt = 0.0; // each block tipped by up to this (m across it): paving
uniform float grime = 0.3;
uniform float ground_dirt = 1.0;
uniform float roughness = 0.82;
uniform float seed = 0.0;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 uv = UV + vec2(seed * 13.7, seed * 3.9);
	float fade = rt_detail_fade(uv, joint_width * 2.0);
	vec2 id;
	vec4 b = rt_blocks(uv, block_size, row_offset, id);
	float tone = b.w;
	vec3 col = color * (1.0 + variation * (tone - 0.5) * 2.0);
	col *= vec3(1.0 + hue_variation * (rt_hash12(id + 2.0) - 0.5) * 2.0, 1.0, 1.0 - hue_variation * (rt_hash12(id + 9.0) - 0.5) * 2.0);
	float face = rt_fbm(uv * (1.2 / max(face_rough * 40.0, 0.05)) + id * 3.1, 4);
	col *= 0.92 + 0.16 * rt_fbm(uv * 2.5 + id, 3);
	// The joints: recessed, chamfered.
	float j = 1.0 - smoothstep(joint_width * 0.5, joint_width * 0.5 + max(chamfer, 1e-4), b.z);
	float jcore = 1.0 - smoothstep(joint_width * 0.35, joint_width * 0.5 + 0.0015, b.z);
	float h = -joint_depth * j + face_rough * (face - 0.5) * 2.0 * (1.0 - j);
	vec2 tilt = rt_hash22(id + 4.0) - 0.5;
	h += slab_tilt * dot(b.xy - 0.5, tilt) * (1.0 - j);
	col = mix(col, joint_color * (0.85 + 0.3 * rt_noise(uv * 20.0)), jcore * fade);
	col *= 1.0 - 0.25 * j * (1.0 - jcore);
	// Grime: streaks down each block and dirt at the foot.
	float g = smoothstep(0.35, 0.85, rt_fbm(vec2(uv.x * 1.5, world_pos.y * 0.25), 3)) * grime;
	col *= 1.0 - 0.25 * g;
	float dirt = (1.0 - smoothstep(0.0, ground_dirt, world_pos.y)) * 0.5;
	col = mix(col, col * vec3(0.7, 0.68, 0.64), dirt);
	ALBEDO = col;
	ROUGHNESS = clamp(roughness + 0.1 * (face - 0.5) + 0.08 * jcore, 0.0, 1.0);
	SPECULAR = 0.4;
	AO = 1.0 - 0.4 * jcore;
	AO_LIGHT_AFFECT = 0.3;
	NORMAL = rt_bump(NORMAL, VERTEX, h * mix(0.3, 1.0, fade), 1.0);
}
