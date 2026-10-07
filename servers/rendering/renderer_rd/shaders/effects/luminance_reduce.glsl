#[compute]

#version 450

#VERSION_DEFINES

#define BLOCK_SIZE 8

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

shared float tmp_data[BLOCK_SIZE * BLOCK_SIZE];
#ifdef FELLOW_FAST_REDUCE
shared float fellow_partial[8];
#endif

#ifdef READ_TEXTURE

//use for main texture
layout(set = 0, binding = 0) uniform sampler2D source_texture;

#else

//use for intermediate textures
layout(r32f, set = 0, binding = 0) uniform restrict readonly image2D source_luminance;

#endif

layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_luminance;

#ifdef WRITE_LUMINANCE
layout(set = 2, binding = 0) uniform sampler2D prev_luminance;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	float max_luminance;
	float min_luminance;
	float exposure_adjust;
	float pad[3];
}
params;

void main() {
	uint t = gl_LocalInvocationID.y * BLOCK_SIZE + gl_LocalInvocationID.x;
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	if (any(lessThan(pos, params.source_size))) {
#ifdef READ_TEXTURE
		vec3 v = texelFetch(source_texture, pos, 0).rgb;
		tmp_data[t] = max(v.r, max(v.g, v.b));
#else
		tmp_data[t] = imageLoad(source_luminance, pos).r;
#endif
	} else {
		tmp_data[t] = 0.0;
	}

	groupMemoryBarrier();
	barrier();

#ifdef FELLOW_FAST_REDUCE
	// FellowEngine (postfx/fast_luminance_reduce, docs/decisions/0025-luminance-reduce.md): the tree below (index t
	// += index t + size, size 32, 16, ..., 1), the same additions on the same operands, with two barriers instead of
	// six: eight threads compute what the tree holds at indices 0-7 after its steps 32, 16 and 8, the first thread its
	// steps 4, 2 and 1.
	if (t < 8u) {
		const float lo = (tmp_data[t] + tmp_data[t + 32u]) + (tmp_data[t + 16u] + tmp_data[t + 48u]);
		const float hi = (tmp_data[t + 8u] + tmp_data[t + 40u]) + (tmp_data[t + 24u] + tmp_data[t + 56u]);
		fellow_partial[t] = lo + hi;
	}
	groupMemoryBarrier();
	barrier();
	if (t == 0) {
		tmp_data[0] = ((fellow_partial[0] + fellow_partial[4]) + (fellow_partial[2] + fellow_partial[6])) + ((fellow_partial[1] + fellow_partial[5]) + (fellow_partial[3] + fellow_partial[7]));
	}
#else
	uint size = (BLOCK_SIZE * BLOCK_SIZE) >> 1;

	do {
		if (t < size) {
			tmp_data[t] += tmp_data[t + size];
		}
		groupMemoryBarrier();
		barrier();

		size >>= 1;
	} while (size >= 1);
#endif

	if (t == 0) {
		//compute rect size
		ivec2 rect_size = min(params.source_size - pos, ivec2(BLOCK_SIZE));
		float avg = tmp_data[0] / float(rect_size.x * rect_size.y);
		//float avg = tmp_data[0] / float(BLOCK_SIZE*BLOCK_SIZE);
		pos /= ivec2(BLOCK_SIZE);
#ifdef WRITE_LUMINANCE
		float prev_lum = texelFetch(prev_luminance, ivec2(0, 0), 0).r; //1 pixel previous exposure
		avg = clamp(prev_lum + (avg - prev_lum) * params.exposure_adjust, params.min_luminance, params.max_luminance);
#endif
		imageStore(dest_luminance, pos, vec4(avg));
	}
}
