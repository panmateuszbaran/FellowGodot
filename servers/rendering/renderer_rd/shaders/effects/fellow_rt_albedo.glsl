#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine ray tracing (docs/decisions/0083-ddgi.md, "texture albedo at hits"): the mean colour of a material's
// albedo texture, for the ray-traced effects' hits (they see a material's colour, not its texture). One workgroup per
// texture: 8 x 8 samples at a mip level of about 8 x 8 texels (sRGB view: linear values), weighed by their alpha (the
// colour of cut-out texels is not seen), averaged into the slot.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D albedo_texture;
layout(set = 0, binding = 1, std430) restrict writeonly buffer Averages {
	vec4 data[]; // rgb: the mean colour, a: the mean alpha
}
averages;

layout(push_constant, std430) uniform Params {
	uint index;
	float lod;
	uint pad0;
	uint pad1;
}
params;

shared vec4 partial[64];

void main() {
	uint i = gl_LocalInvocationIndex;
	vec2 uv = (vec2(gl_LocalInvocationID.xy) + 0.5) / 8.0;
	vec4 c = textureLod(albedo_texture, uv, params.lod);
	c = clamp(c, vec4(0.0), vec4(1.0));
	partial[i] = vec4(c.rgb * c.a, c.a);
	barrier();
	for (uint s = 32u; s > 0u; s >>= 1u) {
		if (i < s) {
			partial[i] += partial[i + s];
		}
		barrier();
	}
	if (i == 0u) {
		vec4 t = partial[0];
		averages.data[params.index] = t.a > 1e-4 ? vec4(t.rgb / t.a, t.a / 64.0) : vec4(1.0, 1.0, 1.0, 0.0);
	}
}
