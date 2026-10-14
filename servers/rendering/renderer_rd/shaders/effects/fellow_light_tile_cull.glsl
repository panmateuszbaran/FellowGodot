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
// of the opaque colour pass then visits only the lights whose bit is set here; the masks are subsets of the cluster
// tile's (the candidates come from it), so they replace its words.
//
// A workgroup takes a 16 x 16 pixel block (four tiles; the block lies in one cluster tile: cluster tiles of 16 pixels or
// more). Output, per tile: 2 x words masks (omni lights, then spot lights), words = the cluster's words per element type.

#include "../light_data_inc.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

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
	uint tiles_y;
	uint flags; // 1: diagnostics (debug/tile_cull_passthrough): every candidate kept (the masks equal the cluster tile's)
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

#define MAX_WORDS 32 // max_clustered_elements up to 1024 (the renderer does not run the pass above)

shared uint s_depth_min[4];
shared uint s_depth_max[4];
shared vec3 s_corner[32];
shared vec3 s_box_min[4];
shared vec3 s_box_max[4];
shared uint s_cluster[2 * MAX_WORDS];
shared uint s_mask[4 * 2 * MAX_WORDS];

void main() {
	uint lid = gl_LocalInvocationIndex;
	uvec2 block = gl_WorkGroupID.xy;
	uint total_words = 2u * params.words;
	uvec2 cluster_pos = (block * 16u) >> params.cluster_shift;
	uint cluster_offset = (params.cluster_width * cluster_pos.y + cluster_pos.x) * (params.words + 32u);

	if (lid < 4u) {
		s_depth_min[lid] = 0x7F800000u; // +infinity
		s_depth_max[lid] = 0u;
	}
	for (uint w = lid; w < total_words; w += 64u) {
		uint type = w / params.words;
		s_cluster[w] = cluster.data[cluster_offset + type * params.cluster_type_size + (w - type * params.words)];
	}
	for (uint w = lid; w < 4u * total_words; w += 64u) {
		s_mask[w] = 0u;
	}
	barrier();

	// Depths: thread lid takes 2 x 2 pixels of tile lid / 16 (non-negative floats: their bits order as the values).
	uint tile = lid >> 4u;
	uvec2 tile_offset = uvec2(tile & 1u, tile >> 1u) * 8u;
	uvec2 quad = uvec2(lid & 3u, (lid >> 2u) & 3u) * 2u;
	uvec2 base = block * 16u + tile_offset + quad;
	float local_min = 3.0e38;
	float local_max = 0.0;
	for (uint q = 0u; q < 4u; q++) {
		uvec2 pixel = base + uvec2(q & 1u, q >> 1u);
		if (all(lessThan(pixel, params.screen_size))) {
			float depth = texelFetch(depth_buffer, ivec2(pixel), 0).r;
			if (depth > 0.0) { // reverse Z: 0 is the cleared depth (no geometry)
				local_min = min(local_min, depth);
				local_max = max(local_max, depth);
			}
		}
	}
	if (local_max > 0.0) {
		atomicMin(s_depth_min[tile], floatBitsToUint(local_min));
		atomicMax(s_depth_max[tile], floatBitsToUint(local_max));
	}
	barrier();

	// The corners of each tile's frustum between its depths (threads 0-31: tile lid / 8, corner lid % 8).
	if (lid < 32u) {
		uint t = lid >> 3u;
		uint c = lid & 7u;
		if (s_depth_max[t] != 0u) {
			uvec2 t_from = block * 16u + uvec2(t & 1u, t >> 1u) * 8u;
			uvec2 corner_px = t_from + uvec2((c & 1u) != 0u ? 8u : 0u, (c & 2u) != 0u ? 8u : 0u);
			vec2 ndc = vec2(min(corner_px, params.screen_size)) / vec2(params.screen_size) * 2.0 - 1.0;
			float depth = uintBitsToFloat((c & 4u) != 0u ? s_depth_max[t] : s_depth_min[t]);
			vec4 p = params.inv_projection * vec4(ndc, depth, 1.0);
			s_corner[lid] = p.xyz / p.w;
		}
	}
	barrier();
	if (lid < 4u) {
		vec3 box_min = s_corner[lid * 8u];
		vec3 box_max = box_min;
		for (uint c = 1u; c < 8u; c++) {
			box_min = min(box_min, s_corner[lid * 8u + c]);
			box_max = max(box_max, s_corner[lid * 8u + c]);
		}
		// Margin: 1/1000 of the box's largest coordinate (far above the difference between a fragment's interpolated
		// position and the one its depth gives) plus 1/10000.
		vec3 extent = max(abs(box_min), abs(box_max));
		float margin = max(extent.x, max(extent.y, extent.z)) * 1e-3 + 1e-4;
		s_box_min[lid] = box_min - vec3(margin);
		s_box_max[lid] = box_max + vec3(margin);
	}
	barrier();

	// The candidates: the cluster tile's lights (any depth), one word per 32 threads; each against the four tiles.
	uint bit = lid & 31u;
	for (uint w = lid >> 5u; w < total_words; w += 2u) {
		uint bits = s_cluster[w];
		if ((bits & (1u << bit)) == 0u) {
			continue;
		}
		uint type = w / params.words;
		uint index = (w - type * params.words) * 32u + bit;
		vec3 position;
		float inv_radius;
		if (type == 0u) {
			position = omni_lights.data[index].position;
			inv_radius = omni_lights.data[index].inv_radius;
		} else {
			position = spot_lights.data[index].position;
			inv_radius = spot_lights.data[index].inv_radius;
		}
		// The range widened by 1/1000 plus 1/10000.
		float range = inv_radius > 0.0 ? 1.0 / inv_radius : 3.0e38;
		float reach = range * 1.001 + 1e-4;
		float reach2 = reach * reach;
		for (uint t = 0u; t < 4u; t++) {
			if (s_depth_max[t] == 0u) {
				continue; // no geometry in this tile
			}
			vec3 d = position - clamp(position, s_box_min[t], s_box_max[t]);
			if (dot(d, d) <= reach2 || (params.flags & 1u) != 0u) {
				atomicOr(s_mask[t * total_words + w], 1u << bit);
			}
		}
	}
	barrier();

	for (uint i = lid; i < 4u * total_words; i += 64u) {
		uint t = i / total_words;
		uvec2 tile_pos = block * 2u + uvec2(t & 1u, t >> 1u);
		if (tile_pos.x < params.tiles_x && tile_pos.y < params.tiles_y) {
			tile_masks.data[(tile_pos.y * params.tiles_x + tile_pos.x) * total_words + (i - t * total_words)] = s_mask[i];
		}
	}
}
