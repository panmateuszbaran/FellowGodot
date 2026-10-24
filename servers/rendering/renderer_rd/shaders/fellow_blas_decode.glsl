#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine ray tracing (docs/decisions/0080-raytracing-scene.md): a compressed surface's positions (R16G16B16A16_UNORM,
// relative to the surface's box) decoded to three floats each, for devices that cannot build acceleration structures
// from the compressed ones. The positions stay in the unit box; the instance transform maps them as the scene shader does.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) restrict readonly buffer Source {
	uint data[];
}
source;

layout(set = 0, binding = 1, std430) restrict writeonly buffer Dest {
	float data[];
}
dest;

layout(push_constant, std430) uniform Params {
	uint vertex_count;
	uint pad[3];
}
params;

void main() {
	uint i = gl_GlobalInvocationID.x;
	if (i >= params.vertex_count) {
		return;
	}
	vec2 xy = unpackUnorm2x16(source.data[i * 2u]);
	vec2 zw = unpackUnorm2x16(source.data[i * 2u + 1u]);
	dest.data[i * 3u + 0u] = xy.x;
	dest.data[i * 3u + 1u] = xy.y;
	dest.data[i * 3u + 2u] = zw.x;
}
