#[compute]

#version 450

#VERSION_DEFINES

#if defined(FELLOW_PACK_ELEMENTS)
// FellowEngine (rendering/fellow/lights/cluster_pack_elements, decision 0051): one workgroup per cluster tile, one
// invocation per element of a chunk of 64 (the tile's element bits two words at a time), then one per depth slice.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#elif defined(FELLOW_PARALLEL)
// FellowEngine (rendering/fellow/lights/cluster_pack_parallel, decision 0014): one workgroup per cluster tile, one
// invocation per depth slice. Stock gives each tile to a single invocation, which walks the tile's elements and, for
// each, updates every slice it covers in memory (read-modify-write chains, 2-4 k invocations at 1080p).
layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

layout(push_constant, std430) uniform Params {
	uint cluster_render_data_size; // how much data for a single cluster takes
	uint max_render_element_count_div_32; //divided by 32
	uvec2 cluster_screen_size;
	uint render_element_count_div_32; //divided by 32

	uint max_cluster_element_count_div_32; //divided by 32
	uint pad1; // FellowEngine: bit 0, the negative control of debug/cluster_verify (FELLOW_PACK_ELEMENTS)
	uint pad2;
}
params;

#ifdef FELLOW_PACK_ELEMENTS
// Read and left cleared (zero) for the next frame: the words this pass reads are the only ones the render pass sets.
layout(set = 0, binding = 1, std430) buffer restrict ClusterRender {
	uint data[];
}
cluster_render;
#else
layout(set = 0, binding = 1, std430) buffer restrict readonly ClusterRender {
	uint data[];
}
cluster_render;
#endif

layout(set = 0, binding = 2, std430) buffer restrict ClusterStore {
	uint data[];
}
cluster_store;

struct RenderElement {
	uint type; //0-4
	bool touches_near;
	bool touches_far;
	uint original_index;
	mat3x4 transform_inv;
	vec3 scale;
	uint pad;
};

layout(set = 0, binding = 3, std430) buffer restrict readonly RenderElements {
	RenderElement data[];
}
render_elements;

#if defined(FELLOW_PACK_ELEMENTS)

#define FELLOW_TYPE_COUNT 5

// The elements of the current chunk that cover at least one slice: type | from_z << 8 | to_z << 16, and their index of
// their type. Two counters, alternating between chunks, so that one is reset while the other is in use.
shared uint s_entry[64];
shared uint s_orig[64];
shared uint s_entry_count[2];
shared uint s_minmax[FELLOW_TYPE_COUNT * 32];

void main() {
	uint tile = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
	uint tile_count = params.cluster_screen_size.x * params.cluster_screen_size.y;
	if (tile >= tile_count) {
		return;
	}
	uint lid = gl_LocalInvocationID.x;
	uint lane = lid & 31u; // the element's bit in phase 1, the depth slice in phase 2
	uint half_index = lid >> 5u;
	uint src_offset = tile * params.cluster_render_data_size;
	uint dst_stride = params.max_cluster_element_count_div_32 + 32u;

	// This invocation's slice (lane), per element type, in stock's encoding: 0 = no element, otherwise
	// min index | (max index + 1) << 16, over the entries of its half of each chunk. min/max and the bit OR below do not
	// depend on the order of the elements, so the stored words are the ones stock's loop stores.
	uint minmax[FELLOW_TYPE_COUNT] = uint[](0u, 0u, 0u, 0u, 0u);

	if (lid < 2u) {
		s_entry_count[lid] = 0u;
	}
	barrier();

	uint parity = 0u;
	for (uint w0 = 0u; w0 < params.render_element_count_div_32; w0 += 2u) {
		// Phase 1: invocation lid takes bit `lane` of word w0 + half_index.
		uint w = w0 + half_index;
		uint bits = 0u;
		if (w < params.render_element_count_div_32) {
			bits = cluster_render.data[src_offset + w];
		}
		if ((params.pad1 & 1u) != 0u && lane == 31u) {
			bits &= 0x7FFFFFFFu; // Negative control of debug/cluster_verify (2): bit 31 of every word dropped.
		}
		if ((bits & (1u << lane)) != 0u) {
			uint index = w * 32u + lane;
			uint z_range_offset = src_offset + params.max_render_element_count_div_32 + index;
			uint z_range = cluster_render.data[z_range_offset];
			cluster_render.data[z_range_offset] = 0u; // Only this invocation reads this word.

			if (z_range != 0u) {
				uint type = render_elements.data[index].type;
				uint from_z = findLSB(z_range);
				uint to_z = findMSB(z_range) + 1;

				if (render_elements.data[index].touches_near) {
					from_z = 0;
				}

				if (render_elements.data[index].touches_far) {
					to_z = 32;
				}

				uint fellow_range = render_elements.data[index].pad;
				if ((fellow_range & 2u) != 0u) {
					from_z = (fellow_range >> 8) & 0xFFu;
					to_z = (fellow_range >> 16) & 0xFFu;
				}

				uint orig_index = render_elements.data[index].original_index;

				uint dst_offset = (tile + type * tile_count) * dst_stride;
				atomicOr(cluster_store.data[dst_offset + (orig_index >> 5)], 1u << (orig_index & 0x1F));

				if (from_z < to_z) {
					uint slot = atomicAdd(s_entry_count[parity], 1u);
					s_entry[slot] = type | (from_z << 8) | (to_z << 16);
					s_orig[slot] = orig_index;
				}
			}
		}
		barrier();

		// Every invocation has read the chunk's words: clear them for the next frame.
		if (lane == 0u && bits != 0u) {
			cluster_render.data[src_offset + w] = 0u;
		}
		if (lid == 0u) {
			s_entry_count[parity ^ 1u] = 0u; // Last read before the previous barrier; used by the next chunk.
		}

		// Phase 2: invocation lid takes slice `lane` for the entries half_index, half_index + 2, ...
		uint count = s_entry_count[parity];
		for (uint e = half_index; e < count; e += 2u) {
			uint entry = s_entry[e];
			uint from_z = (entry >> 8) & 0xFFu;
			uint to_z = entry >> 16;
			if (lane >= from_z && lane < to_z) {
				uint type = entry & 0xFFu;
				uint orig_index = s_orig[e];
				for (uint t = 0; t < FELLOW_TYPE_COUNT; t++) {
					if (t == type) {
						uint mm = minmax[t];
						if (mm == 0) {
							mm = 0xFFFF; //min 0, max 0xFFFF
						}
						uint elem_min = min(orig_index, mm & 0xFFFF);
						uint elem_max = max(orig_index + 1, mm >> 16);
						minmax[t] = elem_min | (elem_max << 16);
					}
				}
			}
		}
		barrier();
		parity ^= 1u;
	}

	// The two halves' ranges, combined (min of the minima, max of the maxima).
	if (half_index == 1u) {
		for (uint t = 0; t < FELLOW_TYPE_COUNT; t++) {
			s_minmax[t * 32u + lane] = minmax[t];
		}
	}
	barrier();
	if (half_index == 0u) {
		for (uint t = 0; t < FELLOW_TYPE_COUNT; t++) {
			uint a = minmax[t];
			uint b = s_minmax[t * 32u + lane];
			uint mm = a;
			if (b != 0u) {
				mm = a == 0u ? b : (min(a & 0xFFFFu, b & 0xFFFFu) | (max(a >> 16, b >> 16) << 16));
			}
			if (mm != 0u) {
				uint dst_offset = (tile + t * tile_count) * dst_stride;
				cluster_store.data[dst_offset + params.max_cluster_element_count_div_32 + lane] = mm;
			}
		}
	}
}

#elif defined(FELLOW_PARALLEL)

#define FELLOW_TYPE_COUNT 5

void main() {
	uint tile = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
	if (tile >= params.cluster_screen_size.x * params.cluster_screen_size.y) {
		return;
	}
	uint slice = gl_LocalInvocationID.x;

	uint base_offset = tile;
	uint src_offset = base_offset * params.cluster_render_data_size;

	// This invocation's slice, per element type, in stock's encoding: 0 = no element, otherwise
	// min index | (max index + 1) << 16. min/max and the bit OR below do not depend on the order of the elements, so
	// the stored words are the ones stock's loop stores.
	uint minmax[FELLOW_TYPE_COUNT] = uint[](0u, 0u, 0u, 0u, 0u);

	uint render_element_offset = 0;
	while (render_element_offset < params.render_element_count_div_32) {
		uint bits = cluster_render.data[src_offset + render_element_offset];
		while (bits != 0) {
			uint index_bit = findLSB(bits);
			uint index = render_element_offset * 32 + index_bit;
			uint type = render_elements.data[index].type;

			uint z_range_offset = src_offset + params.max_render_element_count_div_32 + index;
			uint z_range = cluster_render.data[z_range_offset];

			if (z_range != 0) {
				uint from_z = findLSB(z_range);
				uint to_z = findMSB(z_range) + 1;

				if (render_elements.data[index].touches_near) {
					from_z = 0;
				}

				if (render_elements.data[index].touches_far) {
					to_z = 32;
				}

				uint fellow_range = render_elements.data[index].pad;
				if ((fellow_range & 2u) != 0u) {
					from_z = (fellow_range >> 8) & 0xFFu;
					to_z = (fellow_range >> 16) & 0xFFu;
				}

				uint orig_index = render_elements.data[index].original_index;

				if (slice >= from_z && slice < to_z) {
					for (uint t = 0; t < FELLOW_TYPE_COUNT; t++) {
						if (t == type) {
							uint mm = minmax[t];
							if (mm == 0) {
								mm = 0xFFFF; //min 0, max 0xFFFF
							}
							uint elem_min = min(orig_index, mm & 0xFFFF);
							uint elem_max = max(orig_index + 1, mm >> 16);
							minmax[t] = elem_min | (elem_max << 16);
						}
					}
				}

				// The element's bit, set by one invocation (spread over the 32 by the element index).
				if ((index & 31u) == slice) {
					uint dst_offset = (base_offset + type * (params.cluster_screen_size.x * params.cluster_screen_size.y)) * (params.max_cluster_element_count_div_32 + 32);
					atomicOr(cluster_store.data[dst_offset + (orig_index >> 5)], 1u << (orig_index & 0x1F));
				}
			}

			bits &= ~(1u << index_bit);
		}

		render_element_offset++;
	}

	for (uint t = 0; t < FELLOW_TYPE_COUNT; t++) {
		if (minmax[t] != 0) {
			uint dst_offset = (base_offset + t * (params.cluster_screen_size.x * params.cluster_screen_size.y)) * (params.max_cluster_element_count_div_32 + 32);
			cluster_store.data[dst_offset + params.max_cluster_element_count_div_32 + slice] = minmax[t];
		}
	}
}

#else

void main() {
	uvec2 pos = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pos, params.cluster_screen_size))) {
		return;
	}

	//counter for each type of render_element

	//base offset for this cluster
	uint base_offset = (pos.x + params.cluster_screen_size.x * pos.y);
	uint src_offset = base_offset * params.cluster_render_data_size;

	uint render_element_offset = 0;

	//check all render_elements and see which one was written to
	while (render_element_offset < params.render_element_count_div_32) {
		uint bits = cluster_render.data[src_offset + render_element_offset];
		while (bits != 0) {
			//if bits exist, check the render_element
			uint index_bit = findLSB(bits);
			uint index = render_element_offset * 32 + index_bit;
			uint type = render_elements.data[index].type;

			uint z_range_offset = src_offset + params.max_render_element_count_div_32 + index;
			uint z_range = cluster_render.data[z_range_offset];

			//if object was written, z was written, but check just in case
			if (z_range != 0) { //should always be > 0

				uint from_z = findLSB(z_range);
				uint to_z = findMSB(z_range) + 1;

				if (render_elements.data[index].touches_near) {
					from_z = 0;
				}

				if (render_elements.data[index].touches_far) {
					to_z = 32;
				}

				uint fellow_range = render_elements.data[index].pad;
				if ((fellow_range & 2u) != 0u) {
					// FellowEngine depth slices (cluster_builder_rd.h): the slices are logarithmic in view depth and the
					// element covers the slices of its conservative view-depth range (computed on the CPU), which replaces
					// the range rasterised above: that one is sampled (it can miss depths the element reaches, which the
					// coarse linear slices hide) and is in linear units.
					from_z = (fellow_range >> 8) & 0xFFu;
					to_z = (fellow_range >> 16) & 0xFFu;
				}

				// find cluster offset in the buffer used for indexing in the renderer
				uint dst_offset = (base_offset + type * (params.cluster_screen_size.x * params.cluster_screen_size.y)) * (params.max_cluster_element_count_div_32 + 32);

				uint orig_index = render_elements.data[index].original_index;
				//store this index in the Z slices by setting the relevant bit
				for (uint i = from_z; i < to_z; i++) {
					uint slice_ofs = dst_offset + params.max_cluster_element_count_div_32 + i;

					uint minmax = cluster_store.data[slice_ofs];

					if (minmax == 0) {
						minmax = 0xFFFF; //min 0, max 0xFFFF
					}

					uint elem_min = min(orig_index, minmax & 0xFFFF);
					uint elem_max = max(orig_index + 1, minmax >> 16); //always store plus one, so zero means range is empty when not written to

					minmax = elem_min | (elem_max << 16);
					cluster_store.data[slice_ofs] = minmax;
				}

				uint store_word = orig_index >> 5;
				uint store_bit = orig_index & 0x1F;

				//store the actual render_element index at the end, so the rendering code can reference it
				cluster_store.data[dst_offset + store_word] |= 1 << store_bit;
			}

			bits &= ~(1 << index_bit); //clear the bit to continue iterating
		}

		render_element_offset++;
	}
}

#endif // FELLOW_PACK_ELEMENTS, FELLOW_PARALLEL
