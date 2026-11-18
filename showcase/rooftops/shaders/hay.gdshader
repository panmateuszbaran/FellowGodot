// FellowEngine showcase (rooftops): a cart's hay — straws every way, gold to grey-green, deep shade between them.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "rooftops_common.gdshaderinc"

uniform vec3 color : source_color = vec3(0.8, 0.64, 0.3);

varying vec3 local_pos;

void vertex() {
	local_pos = VERTEX;
}

void fragment() {
	vec3 p = local_pos * 9.0;
	float s1 = rt_fbm(vec2(p.x * 0.4 + p.z * 2.0, p.y * 3.5), 3);
	float s2 = rt_fbm(vec2(p.z * 3.0, p.x * 3.0 + p.y * 0.5) + 5.0, 3);
	float s3 = rt_fbm(vec2(p.x * 3.2 - p.y, p.z * 0.4), 3);
	float straw = max(max(s1, s2), s3);
	vec3 col = color * (0.55 + 0.7 * straw);
	col = mix(col, vec3(0.5, 0.5, 0.36), smoothstep(0.6, 0.8, rt_fbm(p.xz * 0.2, 3)) * 0.5);
	float shade = smoothstep(0.35, 0.65, straw);
	ALBEDO = col * (0.5 + 0.5 * shade);
	ROUGHNESS = 0.95;
	SPECULAR = 0.3;
	BACKLIGHT = col * 0.25;
	NORMAL = rt_bump(NORMAL, VERTEX, 0.012 * straw, 1.0);
}
