#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine MultiMesh instance culling (decision 0034) and per-instance LOD (decision 0035). A job is one MultiMesh
// instance (a MultiMeshInstance3D) in one pass (the camera's passes, or one shadow pass) — or, with per-instance LOD, one
// of its surfaces in the camera's passes: its instances are tested one by one against the pass's view volume and the
// visible ones are copied, in their order, to the job's region of the compacted instance buffer, which the pass then
// draws instead of the MultiMesh's own buffer (indirect draws with the visible count). An instance whose box (the mesh's
// bounding box under the instance's and the node's transform) lies wholly outside one plane of the volume has no vertex
// inside it (the materials culled this way do not move vertices), so it can draw no fragment there.
//
// With contribution culling (decision 0121; lod_params.z or .w > 0) an instance is also left out when its bounding sphere
// (radius: half the diagonal of its box in world space) is too small for the pass: r^2 < a * z^2 + b, z the depth of its
// center along the pass's view direction (contribution: plane, z = dot(xyz, p) + w), a and b from the pass's threshold.
// Thinning (contribution_mode.x = 1) keeps the coverage of many small instances (a far meadow): of the instances below
// the threshold the share r^2 / (a z^2 + b) is kept (by a hash of the instance's index, the same in every pass and frame),
// grown about its origin to the threshold's size, so the far grass is fewer, larger clumps instead of bare ground.
//
// With per-instance LOD (lod_count > 0) every visible instance also gets the detail level stock Godot's mesh LOD rule
// (RendererRD::MeshStorage::mesh_surface_get_lod) would give it if it were a node of its own: the distance from the
// camera to its box, its scale; the instances of each level form a bucket (in order), drawn with that level's indices.
//
// MODE_TEST (a workgroup of 64 per block of 64 instances of a job): each thread tests one instance; the block's bucket
// per instance (a byte, 0xFF: not drawn) and its count per bucket are stored.
// MODE_SCAN (a workgroup per job): per bucket, the blocks' first output positions (prefix sums in order); the job's
// count and first position per bucket (buckets one after the other in the job's region).
// MODE_COMPACT (a workgroup per block, then one thread per draw): each drawn instance's data is copied to its position;
// the threads after the blocks write the draws' arguments (per bucket: index count, instance count, first instance).

#define BUCKETS 8
#define BLOCK_UINTS 32 // per block: 16 uints of bucket bytes, 8 counts, 8 first positions

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Job {
	vec4 xform[3]; // the node's transform, rows (world from the MultiMesh's space)
	vec4 aabb_center; // the mesh's box in its own space (xyz), w: the node's extra cull margin (in the node's space)
	vec4 aabb_half; // half extents (xyz), w: margin added to every plane distance
	uint pool_offset; // first float of the MultiMesh's data in the pool
	uint stride; // floats per instance
	uint count; // instances drawn
	uint planes_offset; // first plane (vec4: normal, d; a point p is outside when dot(normal, p) > d)
	uint plane_count;
	uint out_offset; // the job's region of the compacted buffer, in instances of this job's stride
	uint first_block;
	uint block_count;
	vec4 lod_camera; // xyz: the camera's position; w: the mesh LOD threshold
	vec4 lod_params; // x: the LOD distance multiplier, y: the node's LOD bias; contribution culling: z a, w b
	uint lod_count; // levels after level 0 (0: no per-instance LOD)
	uint prev_pool_offset; // first float of the MultiMesh's previous-frame data in the pool (motion vectors)
	uint out_prev_offset; // the job's region for the previous-frame data, in instances of this job's stride
	uint has_previous; // 1: the MultiMesh's motion vectors read two halves this frame; the previous one is compacted too
	vec4 lod_edges[2]; // edge length of levels 1..7
	vec4 contribution; // the pass's view direction as a plane (depth z = dot(xyz, p) + w)
	vec4 instance_range; // HLOD proxy (decision 0122): xyz the camera's position, w the squared distance drawn from
	vec4 contribution_mode; // x: 0 the small instances culled, 1 thinned (decision 0121)
};

layout(set = 0, binding = 0, std430) buffer restrict readonly Jobs {
	Job data[];
}
jobs;

layout(set = 0, binding = 1, std430) buffer restrict readonly Blocks {
	uvec2 data[]; // per block: job, first instance
}
blocks;

layout(set = 0, binding = 2, std430) buffer restrict readonly Planes {
	vec4 data[];
}
planes;

layout(set = 0, binding = 3, std430) buffer restrict readonly Pool {
	vec4 data[]; // read as floats through vec4 (strides are multiples of 4 floats)
}
pool;

layout(set = 0, binding = 4, std430) buffer restrict BlockData {
	uint data[]; // BLOCK_UINTS per block
}
block_data;

layout(set = 0, binding = 5, std430) buffer restrict JobVisible {
	uint data[]; // per job: count per bucket (8), first position per bucket (8)
}
job_visible;

layout(set = 0, binding = 6, std430) buffer restrict writeonly Compacted {
	vec4 data[];
}
compacted;

layout(set = 0, binding = 7, std430) buffer restrict readonly DrawInfo {
	uint data[]; // per draw: job, buckets drawn, index (or vertex) count per bucket (8)
}
draw_info;

layout(set = 0, binding = 8, std430) buffer restrict writeonly DrawArgs {
	uint data[]; // per draw and bucket: count, instance count, first index / vertex, vertex offset / first instance, first instance
}
draw_args;

layout(set = 0, binding = 9, std430) buffer restrict Counters {
	uint tested;
	uint visible;
	uint coarse; // drawn at a level above 0
	uint small; // left out as too small (contribution culling)
}
counters;

layout(push_constant, std430) uniform Params {
	uint block_count;
	uint job_count;
	uint draw_count;
	uint pad;
}
params;

#ifdef MODE_TEST
shared uint s_bytes[16];
shared uint s_counts[BUCKETS];
shared uint s_small;
#endif

#ifdef MODE_SCAN
shared uint s_scan[BUCKETS][64];
#endif

#ifdef MODE_COMPACT
shared uint s_bytes[16];
#endif

float job_lod_edge(Job job, uint i) {
	const vec4 v = job.lod_edges[i >> 2];
	const uint k = i & 3;
	return k == 0 ? v.x : (k == 1 ? v.y : (k == 2 ? v.z : v.w));
}

// The instance's box in world space (center, half extents): the mesh's box under the instance's and the node's
// transform, grown by the node's margin on every axis.
void instance_box(Job job, vec4 r0, vec4 r1, vec4 r2, out vec3 center, out vec3 half_extents, out mat3 wb) {
	// World transform = node * instance (3 x 4, rows).
	const mat3 nb = transpose(mat3(job.xform[0].xyz, job.xform[1].xyz, job.xform[2].xyz));
	const vec3 nt = vec3(job.xform[0].w, job.xform[1].w, job.xform[2].w);
	const mat3 ib = transpose(mat3(r0.xyz, r1.xyz, r2.xyz));
	const vec3 it = vec3(r0.w, r1.w, r2.w);
	wb = nb * ib; // world basis
	center = nb * (ib * job.aabb_center.xyz + it) + nt;
	half_extents = abs(wb[0]) * job.aabb_half.x + abs(wb[1]) * job.aabb_half.y + abs(wb[2]) * job.aabb_half.z + (abs(nb[0]) + abs(nb[1]) + abs(nb[2])) * job.aabb_center.w;
}

// Contribution culling (decision 0121): 1 for an instance drawn as it is, 0 for one left out, above 1 for a thinned
// survivor's growth.
float contribution_scale(Job job, vec3 center, vec3 half_extents, uint instance) {
	if (!(job.lod_params.z > 0.0 || job.lod_params.w > 0.0)) {
		return 1.0;
	}
	const float z = dot(job.contribution.xyz, center) + job.contribution.w;
	const float r2 = dot(half_extents, half_extents);
	const float min_r2 = job.lod_params.z * z * z + job.lod_params.w;
	if (r2 >= min_r2) {
		return 1.0;
	}
	if (job.contribution_mode.x < 0.5 || r2 <= 0.0) {
		return 0.0;
	}
	uint h = instance * 0x9E3779B1u;
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	const float u = float(h >> 8) * (1.0 / 16777216.0);
	// Kept with the probability of its area against the threshold's, grown to the threshold's size: the expected
	// coverage stays the same.
	if (u >= r2 / min_r2) {
		return 0.0;
	}
	return sqrt(min_r2 / r2);
}

void main() {
#ifdef MODE_TEST
	const uint b = gl_WorkGroupID.x;
	const uint lid = gl_LocalInvocationID.x;
	if (lid < 16) {
		s_bytes[lid] = 0xFFFFFFFFu;
	}
	if (lid < BUCKETS) {
		s_counts[lid] = 0;
	}
	if (lid == 0) {
		s_small = 0;
	}
	barrier();
	const uvec2 blk = blocks.data[b];
	const Job job = jobs.data[blk.x];
	const uint instance = blk.y + lid;
	if (instance < job.count) {
		const uint base = (job.pool_offset + instance * job.stride) / 4;
		const vec4 r0 = pool.data[base + 0];
		const vec4 r1 = pool.data[base + 1];
		const vec4 r2 = pool.data[base + 2];
		vec3 center, half_extents;
		mat3 wb;
		instance_box(job, r0, r1, r2, center, half_extents, wb);
		bool visible = true;
		for (uint p = 0; p < job.plane_count; p++) {
			const vec4 plane = planes.data[job.planes_offset + p];
			const float dist = dot(plane.xyz, center) - plane.w;
			float reach = dot(abs(plane.xyz), half_extents);
			// Margin for the rounding of this transform against the vertex shader's: relative to the magnitudes
			// involved, plus an absolute one (aabb_half.w).
			reach += 1e-4 * (abs(dot(plane.xyz, center)) + abs(plane.w) + reach) + job.aabb_half.w;
			if (dist - reach > 0.0) {
				visible = false;
				break;
			}
		}
		if (visible && job.instance_range.w > 0.0) {
			// An HLOD proxy's instance (decision 0122) is drawn from the distance on, by its box's center, as the scene
			// cull judges the node it replaces (closer, that node is drawn).
			const vec3 to_camera = center - job.instance_range.xyz;
			if (dot(to_camera, to_camera) < job.instance_range.w) {
				visible = false;
			}
		}
		if (visible && contribution_scale(job, center, half_extents, instance) == 0.0) {
			visible = false;
			atomicAdd(s_small, 1);
		}
		if (visible) {
			uint bucket = 0;
			if (job.lod_count > 0) {
				// Stock's rule for a node: the distance from the camera to the box, the largest scale of the transform.
				const vec3 box_min = center - half_extents;
				const vec3 box_max = center + half_extents;
				const vec3 cam = job.lod_camera.xyz;
				const float distance = length(max(vec3(0.0), max(box_min - cam, cam - box_max))) * job.lod_params.x;
				const float scale = max(length(wb[0]), max(length(wb[1]), length(wb[2]))) * job.lod_params.y;
				for (uint i = 0; i < job.lod_count; i++) {
					const float screen_size = job_lod_edge(job, i) * scale / distance;
					if (screen_size > job.lod_camera.w) {
						break;
					}
					bucket = i + 1;
				}
			}
			atomicAdd(s_counts[bucket], 1);
			const uint shift = (lid & 3) * 8;
			atomicAnd(s_bytes[lid >> 2], ~(0xFFu << shift) | (bucket << shift));
		}
	}
	barrier();
	if (lid < 16) {
		block_data.data[b * BLOCK_UINTS + lid] = s_bytes[lid];
	}
	if (lid < BUCKETS) {
		block_data.data[b * BLOCK_UINTS + 16 + lid] = s_counts[lid];
	}
	if (lid == 0) {
		uint n = 0;
		for (uint l = 0; l < BUCKETS; l++) {
			n += s_counts[l];
		}
		atomicAdd(counters.tested, min(64u, job.count - blk.y));
		atomicAdd(counters.visible, n);
		atomicAdd(counters.coarse, n - s_counts[0]);
		if (s_small > 0) {
			atomicAdd(counters.small, s_small);
		}
	}
#endif

#ifdef MODE_SCAN
	const uint j = gl_WorkGroupID.x;
	const uint lid = gl_LocalInvocationID.x;
	const uint first = jobs.data[j].first_block;
	const uint n = jobs.data[j].block_count;
	const uint per = (n + 63) / 64;
	const uint from = min(n, lid * per);
	const uint to = min(n, from + per);
	uint local_sum[BUCKETS];
	for (uint l = 0; l < BUCKETS; l++) {
		local_sum[l] = 0;
	}
	for (uint k = from; k < to; k++) {
		for (uint l = 0; l < BUCKETS; l++) {
			local_sum[l] += block_data.data[(first + k) * BLOCK_UINTS + 16 + l];
		}
	}
	for (uint l = 0; l < BUCKETS; l++) {
		s_scan[l][lid] = local_sum[l];
	}
	barrier();
	// Inclusive scans of the 64 sums per bucket (Hillis-Steele).
	for (uint step = 1; step < 64; step <<= 1) {
		uint add[BUCKETS];
		for (uint l = 0; l < BUCKETS; l++) {
			add[l] = lid >= step ? s_scan[l][lid - step] : 0;
		}
		barrier();
		for (uint l = 0; l < BUCKETS; l++) {
			s_scan[l][lid] += add[l];
		}
		barrier();
	}
	// The buckets follow each other in the job's region.
	uint bucket_first[BUCKETS];
	uint total = 0;
	for (uint l = 0; l < BUCKETS; l++) {
		bucket_first[l] = total;
		total += s_scan[l][63];
	}
	uint position[BUCKETS];
	for (uint l = 0; l < BUCKETS; l++) {
		position[l] = bucket_first[l] + s_scan[l][lid] - local_sum[l];
	}
	for (uint k = from; k < to; k++) {
		for (uint l = 0; l < BUCKETS; l++) {
			const uint c = block_data.data[(first + k) * BLOCK_UINTS + 16 + l];
			block_data.data[(first + k) * BLOCK_UINTS + 24 + l] = position[l];
			position[l] += c;
		}
	}
	if (lid < BUCKETS) {
		job_visible.data[j * 16 + lid] = s_scan[lid][63];
		job_visible.data[j * 16 + 8 + lid] = bucket_first[lid];
	}
#endif

#ifdef MODE_COMPACT
	const uint b = gl_WorkGroupID.x;
	const uint lid = gl_LocalInvocationID.x;
	if (b < params.block_count) {
		if (lid < 16) {
			s_bytes[lid] = block_data.data[b * BLOCK_UINTS + lid];
		}
		barrier();
		const uint bucket = (s_bytes[lid >> 2] >> ((lid & 3) * 8)) & 0xFFu;
		if (bucket == 0xFFu) {
			return;
		}
		uint rank = 0;
		for (uint k = 0; k < lid; k++) {
			if (((s_bytes[k >> 2] >> ((k & 3) * 8)) & 0xFFu) == bucket) {
				rank++;
			}
		}
		const uvec2 blk = blocks.data[b];
		const Job job = jobs.data[blk.x];
		const uint position = block_data.data[b * BLOCK_UINTS + 24 + bucket] + rank;
		const uint src = (job.pool_offset + (blk.y + lid) * job.stride) / 4;
		const uint dst = ((job.out_offset + position) * job.stride) / 4;
		// A thinned survivor (decision 0121) is grown about its origin: the basis (xyz of the three rows) scaled.
		float grow = 1.0;
		if (job.contribution_mode.x > 0.5 && (job.lod_params.z > 0.0 || job.lod_params.w > 0.0)) {
			vec3 center, half_extents;
			mat3 wb;
			instance_box(job, pool.data[src + 0], pool.data[src + 1], pool.data[src + 2], center, half_extents, wb);
			grow = contribution_scale(job, center, half_extents, blk.y + lid);
		}
		for (uint k = 0; k < job.stride / 4; k++) {
			const vec4 v = pool.data[src + k];
			compacted.data[dst + k] = (k < 3 && grow != 1.0) ? vec4(v.xyz * grow, v.w) : v;
		}
		if (job.has_previous != 0) {
			// The same instance's previous-frame data at the same position of the previous region (the vertex shader
			// reads it for motion vectors at the same instance index).
			const uint psrc = (job.prev_pool_offset + (blk.y + lid) * job.stride) / 4;
			const uint pdst = ((job.out_prev_offset + position) * job.stride) / 4;
			for (uint k = 0; k < job.stride / 4; k++) {
				const vec4 v = pool.data[psrc + k];
				compacted.data[pdst + k] = (k < 3 && grow != 1.0) ? vec4(v.xyz * grow, v.w) : v;
			}
		}
	} else {
		const uint d = (b - params.block_count) * 64 + lid;
		if (d >= params.draw_count) {
			return;
		}
		const uint job = draw_info.data[d * 10 + 0];
		const uint levels = draw_info.data[d * 10 + 1];
		for (uint l = 0; l < BUCKETS; l++) {
			const uint a = (d * BUCKETS + l) * 5;
			draw_args.data[a + 0] = draw_info.data[d * 10 + 2 + l];
			draw_args.data[a + 1] = l < levels ? job_visible.data[job * 16 + l] : 0;
			draw_args.data[a + 2] = 0;
			draw_args.data[a + 3] = 0;
			draw_args.data[a + 4] = job_visible.data[job * 16 + 8 + l];
		}
	}
#endif
}
