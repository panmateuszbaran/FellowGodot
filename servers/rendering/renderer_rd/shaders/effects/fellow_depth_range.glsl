#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine depth-fitted sun shadows (docs/decisions/0388-directional-depth-fit.md): the nearest and the farthest
// opaque surface of the depth pre-pass, as distances along the camera's axis (view space -z), into two words of a
// buffer (positive floats order as their bits do). Sky (reverse Z: depth 0) is not a surface. Each thread takes 4 x 4
// pixels, each workgroup reduces in shared memory first, then one atomic per workgroup and word.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D depth_buffer;

layout(set = 0, binding = 1, std430) buffer restrict Range {
	uint nearest; // float bits; 0x7f7fffff: none yet
	uint farthest; // float bits; 0: none yet
	uint count; // pixels with a surface
	uint pad;
}
range;

layout(push_constant, std430) uniform Params {
	vec4 z_row; // the inverse projection's z row: view z = dot(z_row, ndc) / dot(w_row, ndc)
	vec4 w_row;
	ivec2 size;
	ivec2 pad;
}
params;

shared uint s_nearest;
shared uint s_farthest;
shared uint s_count;

void main() {
	if (gl_LocalInvocationIndex == 0) {
		s_nearest = 0x7f7fffffu;
		s_farthest = 0u;
		s_count = 0u;
	}
	barrier();
	ivec2 base = ivec2(gl_GlobalInvocationID.xy) * 4;
	float nearest = 3.4e38;
	float farthest = 0.0;
	uint count = 0u;
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			ivec2 p = base + ivec2(x, y);
			if (any(greaterThanEqual(p, params.size))) {
				continue;
			}
			float d = texelFetch(depth_buffer, p, 0).r;
			if (d <= 0.0) {
				continue; // sky
			}
			vec2 ndc_xy = (vec2(p) + 0.5) / vec2(params.size) * 2.0 - 1.0;
			vec4 ndc = vec4(ndc_xy, d * 2.0 - 1.0, 1.0);
			float z = -dot(params.z_row, ndc) / dot(params.w_row, ndc);
			if (z > 0.0) {
				nearest = min(nearest, z);
				farthest = max(farthest, z);
				count++;
			}
		}
	}
	if (count > 0u) {
		atomicMin(s_nearest, floatBitsToUint(nearest));
		atomicMax(s_farthest, floatBitsToUint(farthest));
		atomicAdd(s_count, count);
	}
	barrier();
	if (gl_LocalInvocationIndex == 0 && s_count > 0u) {
		atomicMin(range.nearest, s_nearest);
		atomicMax(range.farthest, s_farthest);
		atomicAdd(range.count, s_count);
	}
}
