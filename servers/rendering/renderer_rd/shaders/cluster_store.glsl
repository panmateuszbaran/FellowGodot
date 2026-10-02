#[compute]

#version 450

#VERSION_DEFINES

#ifdef FELLOW_PARALLEL
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
	uint pad1;
	uint pad2;
}
params;

layout(set = 0, binding = 1, std430) buffer restrict readonly ClusterRender {
	uint data[];
}
cluster_render;

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

#ifdef FELLOW_PARALLEL

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

#endif // FELLOW_PARALLEL
