#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine rainbows (decision 0134): the bows' colour by angle from the point opposite the sun (a table), added to
// the colour buffer where rain stands between the camera and what it sees (the sky: all of it; a surface: by its
// distance), in the sunlight the rain gets (the clouds' shadow at the camera).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_view_projection; // NDC (x, y, depth) to the position relative to the camera (decision 0212)
	vec4 camera_position; // world (m); w: ln 2 / the distance at which half the bow shows (1/m)
	vec4 anti_sun; // the point opposite the sun (unit)
	vec4 light; // the bow's brightness (rgb)
	vec4 cloud_shadow; // the clouds' shadow map: origin x, z (m), 1 / extent (1/m), strength (0: none)
	vec4 cloud_shadow2; // the sun's direction x / y, z / y
	ivec4 size;
}
params;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(set = 0, binding = 2) uniform sampler2D bow_table;
layout(set = 0, binding = 3) uniform sampler2D cloud_shadow;
layout(rgba16f, set = 1, binding = 0) uniform restrict image2D color_image;

#define TABLE_MIN 30.0
#define TABLE_MAX 60.0

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size.xy))) {
		return;
	}
	vec2 ndc = (vec2(pos) + 0.5) / vec2(params.size.xy) * 2.0 - 1.0;
	float depth = texelFetch(depth_buffer, pos, 0).r;
	vec3 cam = params.camera_position.xyz;
	vec3 d;
	float rain = 1.0;
	if (depth > 0.0) {
		vec4 wp = params.inv_view_projection * vec4(ndc, depth, 1.0);
		vec3 v = wp.xyz / wp.w;
		float dist = length(v);
		d = v / max(dist, 1e-4);
		rain = 1.0 - exp(-dist * params.camera_position.w);
	} else {
		vec4 wp = params.inv_view_projection * vec4(ndc, 0.5, 1.0);
		d = normalize(wp.xyz / wp.w);
	}
	float theta = degrees(acos(clamp(dot(d, params.anti_sun.xyz), -1.0, 1.0)));
	if (theta <= TABLE_MIN || theta >= TABLE_MAX) {
		return;
	}
	vec3 bow = textureLod(bow_table, vec2((theta - TABLE_MIN) / (TABLE_MAX - TABLE_MIN), 0.5), 0.0).rgb;
	// Faded out at the table's ends (the primary's inner glow, the secondary's outer).
	bow *= smoothstep(TABLE_MIN, TABLE_MIN + 4.0, theta) * smoothstep(TABLE_MAX, TABLE_MAX - 4.0, theta);
	float sun = 1.0;
	if (params.cloud_shadow.w > 0.0) {
		vec2 g = cam.xz - params.cloud_shadow2.xy * cam.y;
		vec2 cuv = (g - params.cloud_shadow.xy) * params.cloud_shadow.z + 0.5;
		sun = mix(1.0, textureLod(cloud_shadow, cuv, 0.0).r, params.cloud_shadow.w);
	}
	vec4 c = imageLoad(color_image, pos);
	c.rgb += bow * params.light.rgb * (rain * sun);
	imageStore(color_image, pos, c);
}
