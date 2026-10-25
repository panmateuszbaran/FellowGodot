#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine contact shadows (docs/decisions/0107-contact-shadows.md): per pixel of the depth pre-pass, a march of
// `steps` steps over `length` metres towards the first directional light, in view space, each step projected to the
// screen and compared with the depth buffer: a surface in front of the march by more than a small bias and less than
// two steps' change of depth (at least `thickness`) shadows the pixel. The start along the march is dithered per pixel
// and frame (TAA averages it).
// Output: x 1 lit, 0 in contact shadow; y 1 (valid).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // NDC (x, y, depth * 2 - 1) to view
	mat4 projection; // view to clip (the same correction)
	vec4 light_direction; // xyz: view space, towards the light; w: length in metres
	ivec2 size;
	uint frame;
	uint steps;
	float thickness;
	float pad0;
	float pad1;
	float pad2;
}
params;

layout(set = 0, binding = 1) uniform sampler2D depth_buffer;
layout(rg8, set = 0, binding = 2) uniform restrict writeonly image2D dest;

vec3 view_position(vec2 p_uv, float p_depth) {
	vec4 v = params.inv_projection * vec4(p_uv * 2.0 - 1.0, p_depth * 2.0 - 1.0, 1.0);
	return v.xyz / v.w;
}

// Interleaved gradient noise (Jimenez 2014), shifted per frame.
float ign(vec2 p_pos, uint p_frame) {
	p_pos += 5.588238 * float(p_frame & 63u);
	return fract(52.9829189 * fract(dot(p_pos, vec2(0.06711056, 0.00583715))));
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.size))) {
		return;
	}
	float depth = texelFetch(depth_buffer, pos, 0).r;
	if (depth == 0.0) {
		imageStore(dest, pos, vec4(1.0, 1.0, 0.0, 0.0)); // the sky
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / vec2(params.size);
	vec3 vp = view_position(uv, depth);
	vec3 l = params.light_direction.xyz;
	float len = params.light_direction.w;
	uint steps = params.steps;
	float step_len = len / float(steps);
	float jitter = ign(vec2(pos), params.frame);
	// Self-occlusion margin: grows with the distance (the depth buffer's precision, the TAA jitter's half pixel).
	float bias = 0.01 + 0.002 * abs(vp.z);
	// A surface occludes when the march has just passed behind it: within two steps' change of depth (as Unreal's
	// contact shadows), at least `thickness`. A surface further in front was passed behind, not hit (the march goes
	// behind objects seen from the camera: they are not solid all the way back).
	float tolerance = max(abs(l.z) * step_len * 2.0, params.thickness);
	float shadow = 1.0;
	for (uint i = 0u; i < steps; i++) {
		float t = (float(i) + jitter) * step_len + step_len * 0.5;
		vec3 p = vp + l * t;
		vec4 clip = params.projection * vec4(p, 1.0);
		if (clip.w <= 0.0) {
			break;
		}
		vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
		if (any(lessThan(suv, vec2(0.0))) || any(greaterThanEqual(suv, vec2(1.0)))) {
			break;
		}
		ivec2 sp = ivec2(suv * vec2(params.size));
		float sd = texelFetch(depth_buffer, sp, 0).r;
		if (sd == 0.0) {
			continue; // the sky
		}
		float zs = view_position((vec2(sp) + 0.5) / vec2(params.size), sd).z;
		float delta = zs - p.z; // > 0: the surface seen there is in front of the march
		if (delta > bias && delta < tolerance + bias) {
			shadow = 0.0;
			break;
		}
	}
	imageStore(dest, pos, vec4(shadow, 1.0, 0.0, 0.0));
}
