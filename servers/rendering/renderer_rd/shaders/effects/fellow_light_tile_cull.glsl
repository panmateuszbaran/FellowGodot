#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine (rendering/fellow/lights/tile_cull, docs/decisions/0049-light-tile-cull.md): after the depth pre-pass, for
// every 8 x 8 pixel tile, the omni and spot lights of its cluster tile whose range can reach a point of the tile's
// geometry. The tile's geometry lies between the nearest and the farthest depth its pixels hold (pixels without
// geometry, at the cleared depth, are left out): the part of the tile's frustum between those depths, whose view-space
// bounding box is widened by a margin. A light whose sphere (its range, widened by a margin) misses that box is out of
// range of every fragment the opaque colour pass shades in the tile (its fragments are at the depth pre-pass's depths:
// equal depth test), so the light loop would skip it at its range test (lights/zero_attenuation_skip). The light loop
// of the opaque colour pass then visits only the lights whose bit is set here.
//
// Output, per tile: 2 x words masks (omni lights, then spot lights), words = the cluster's words per element type.

#include "../light_data_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D depth_buffer;

layout(set = 0, binding = 1, std430) restrict readonly buffer ClusterData {
	uint data[];
}
cluster;

layout(set = 0, binding = 2, std430) restrict readonly buffer OmniLights {
	LightData data[];
}
omni_lights;

layout(set = 0, binding = 3, std430) restrict readonly buffer SpotLights {
	LightData data[];
}
spot_lights;

layout(set = 0, binding = 4, std430) restrict writeonly buffer TileMasks {
	uint data[];
}
tile_masks;

layout(push_constant, std430) uniform Params {
	mat4 inv_projection; // NDC (x, y in -1..1, depth as stored) to view space, as the colour pass projects
	uvec2 screen_size;
	uint tiles_x;
	uint words; // the cluster's words per element type
	uint cluster_shift;
	uint cluster_width;
	uint cluster_type_size;
	uint pad;
}
params;

#define MAX_WORDS 128

shared uint s_depth_min;
shared uint s_depth_max;
shared uint s_mask[2 * MAX_WORDS];

void main() {
	uvec2 tile = gl_WorkGroupID.xy;
	uint lid = gl_LocalInvocationIndex;
	uint total_words = 2u * params.words;

	if (lid == 0u) {
		s_depth_min = 0x7F800000u; // +infinity
		s_depth_max = 0u;
	}
	for (uint w = lid; w < total_words; w += 64u) {
		s_mask[w] = 0u;
	}
	barrier();

	// The depths of the tile's pixels (non-negative floats: their bits order as the values).
	uvec2 pixel = tile * 8u + gl_LocalInvocationID.xy;
	if (all(lessThan(pixel, params.screen_size))) {
		float depth = texelFetch(depth_buffer, ivec2(pixel), 0).r;
		if (depth > 0.0) { // reverse Z: 0 is the cleared depth (no geometry)
			atomicMin(s_depth_min, floatBitsToUint(depth));
			atomicMax(s_depth_max, floatBitsToUint(depth));
		}
	}
	barrier();

	if (s_depth_max != 0u) {
		float depths[2] = float[2](uintBitsToFloat(s_depth_min), uintBitsToFloat(s_depth_max));
		// The tile's pixel rectangle (edges), in NDC.
		vec2 ndc_from = vec2(tile * 8u) / vec2(params.screen_size) * 2.0 - 1.0;
		vec2 ndc_to = vec2(min(tile * 8u + 8u, params.screen_size)) / vec2(params.screen_size) * 2.0 - 1.0;
		vec3 box_min = vec3(3.0e38);
		vec3 box_max = vec3(-3.0e38);
		for (int c = 0; c < 8; c++) {
			vec4 p = params.inv_projection * vec4((c & 1) != 0 ? ndc_to.x : ndc_from.x, (c & 2) != 0 ? ndc_to.y : ndc_from.y, depths[c >> 2], 1.0);
			p.xyz /= p.w;
			box_min = min(box_min, p.xyz);
			box_max = max(box_max, p.xyz);
		}
		// Margin: 1/1000 of the box's largest coordinate (far above the difference between a fragment's interpolated
		// position and the one its depth gives) plus 1/10000.
		vec3 extent = max(abs(box_min), abs(box_max));
		float margin = max(extent.x, max(extent.y, extent.z)) * 1e-3 + 1e-4;
		box_min -= vec3(margin);
		box_max += vec3(margin);

		// The candidates: the lights of the cluster tile that holds this tile (any depth). One word per 32 threads.
		uvec2 cluster_pos = (tile * 8u) >> params.cluster_shift;
		uint cluster_offset = (params.cluster_width * cluster_pos.y + cluster_pos.x) * (params.words + 32u);
		uint bit = lid & 31u;
		for (uint w = lid >> 5u; w < total_words; w += 2u) {
			uint type = w / params.words;
			uint word = w - type * params.words;
			uint bits = cluster.data[cluster_offset + type * params.cluster_type_size + word];
			if ((bits & (1u << bit)) == 0u) {
				continue;
			}
			uint index = word * 32u + bit;
			vec3 position;
			float inv_radius;
			if (type == 0u) {
				position = omni_lights.data[index].position;
				inv_radius = omni_lights.data[index].inv_radius;
			} else {
				position = spot_lights.data[index].position;
				inv_radius = spot_lights.data[index].inv_radius;
			}
			// Distance from the light to the box; the range widened by 1/1000 plus 1/10000.
			vec3 d = position - clamp(position, box_min, box_max);
			float range = inv_radius > 0.0 ? 1.0 / inv_radius : 3.0e38;
			float reach = range * 1.001 + 1e-4;
			if (dot(d, d) <= reach * reach) {
				atomicOr(s_mask[w], 1u << bit);
			}
		}
	}
	barrier();

	uint base = (tile.y * params.tiles_x + tile.x) * total_words;
	for (uint w = lid; w < total_words; w += 64u) {
		tile_masks.data[base + w] = s_mask[w];
	}
}
