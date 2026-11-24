#[compute]
#version 450

// Milestone 191: the scene's motion vectors copied into a texture the CPU can read (velocity_dump.gd).

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rg16f, set = 0, binding = 0) uniform restrict readonly image2D velocity;
layout(rg16f, set = 0, binding = 1) uniform restrict writeonly image2D copy;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	ivec2 pad;
}
params;

void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, params.size))) {
		return;
	}
	imageStore(copy, p, vec4(imageLoad(velocity, p).xy, 0.0, 0.0));
}
