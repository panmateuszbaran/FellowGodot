#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine MultiMesh instance culling (decision 0034). A job is one MultiMesh instance (a MultiMeshInstance3D) in one
// pass (the camera's passes, or one shadow pass): its instances are tested one by one against the pass's view volume
// and the visible ones are copied, in their order, to the job's region of the compacted instance buffer, which the pass
// then draws instead of the MultiMesh's own buffer (indirect draws with the visible count). An instance whose box (the
// mesh's bounding box under the instance's and the node's transform) lies wholly outside one plane of the volume has no
// vertex inside it (the materials culled this way do not move vertices), so it can draw no fragment there.
//
// MODE_TEST (a workgroup of 64 per block of 64 instances of a job): each thread tests one instance; the block's
// visibility bits and visible count are stored.
// MODE_SCAN (a thread per job): the blocks' first output positions (prefix sums in order) and the job's visible count.
// MODE_COMPACT (a workgroup per block, then one thread per draw): each visible instance's data is copied to the job's
// region at its position; the threads after the blocks write the draws' arguments (index or vertex count, visible
// count).

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Job {
	vec4 xform[3]; // the node's transform, rows (world from the MultiMesh's space)
	vec4 aabb_center; // the mesh's box in its own space (xyz), w unused
	vec4 aabb_half; // half extents (xyz), w: margin added to every plane distance
	uint pool_offset; // first float of the MultiMesh's data in the pool
	uint stride; // floats per instance
	uint count; // instances drawn
	uint planes_offset; // first plane (vec4: normal, d; a point p is outside when dot(normal, p) > d)
	uint plane_count;
	uint out_offset; // the job's region of the compacted buffer, in instances of this job's stride
	uint first_block;
	uint block_count;
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
	uvec4 data[]; // per block: visibility bits (2), visible count, first output position within the job
}
block_data;

layout(set = 0, binding = 5, std430) buffer restrict JobVisible {
	uint data[];
}
job_visible;

layout(set = 0, binding = 6, std430) buffer restrict writeonly Compacted {
	vec4 data[];
}
compacted;

layout(set = 0, binding = 7, std430) buffer restrict readonly DrawInfo {
	uvec2 data[]; // per draw: job, index (or vertex) count
}
draw_info;

layout(set = 0, binding = 8, std430) buffer restrict writeonly DrawArgs {
	uint data[]; // per draw: count, instance count, first index / vertex, vertex offset / first instance, first instance
}
draw_args;

layout(set = 0, binding = 9, std430) buffer restrict Counters {
	uint tested;
	uint visible;
	uint pad0;
	uint pad1;
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
shared uint s_bits[2];
shared uint s_count;
#endif

void main() {
#ifdef MODE_TEST
	const uint b = gl_WorkGroupID.x;
	const uint lid = gl_LocalInvocationID.x;
	if (lid == 0) {
		s_bits[0] = 0;
		s_bits[1] = 0;
	}
	barrier();
	const uvec2 blk = blocks.data[b];
	const Job job = jobs.data[blk.x];
	const uint instance = blk.y + lid;
	bool visible = false;
	if (instance < job.count) {
		const uint base = (job.pool_offset + instance * job.stride) / 4;
		const vec4 r0 = pool.data[base + 0];
		const vec4 r1 = pool.data[base + 1];
		const vec4 r2 = pool.data[base + 2];
		// World transform = node * instance (3 x 4, rows).
		const mat3 ni = mat3(job.xform[0].xyz, job.xform[1].xyz, job.xform[2].xyz); // columns hold the node's rows
		const vec3 nt = vec3(job.xform[0].w, job.xform[1].w, job.xform[2].w);
		// Instance rows as a matrix acting on column vectors: row i of the instance is ri.xyz, translation ri.w.
		const mat3 ib = transpose(mat3(r0.xyz, r1.xyz, r2.xyz));
		const vec3 it = vec3(r0.w, r1.w, r2.w);
		const mat3 nb = transpose(ni);
		const mat3 wb = nb * ib; // world basis
		const vec3 center = nb * (ib * job.aabb_center.xyz + it) + nt;
		const vec3 half_extents = abs(wb[0]) * job.aabb_half.x + abs(wb[1]) * job.aabb_half.y + abs(wb[2]) * job.aabb_half.z;
		visible = true;
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
		if (visible) {
			atomicOr(s_bits[lid >> 5], 1u << (lid & 31));
		}
	}
	barrier();
	if (lid == 0) {
		const uint n = bitCount(s_bits[0]) + bitCount(s_bits[1]);
		block_data.data[b] = uvec4(s_bits[0], s_bits[1], n, 0);
		atomicAdd(counters.tested, min(64u, job.count - blk.y));
		atomicAdd(counters.visible, n);
	}
#endif

#ifdef MODE_SCAN
	const uint j = gl_GlobalInvocationID.x;
	if (j >= params.job_count) {
		return;
	}
	const uint first = jobs.data[j].first_block;
	const uint n = jobs.data[j].block_count;
	uint sum = 0;
	for (uint k = 0; k < n; k++) {
		const uint c = block_data.data[first + k].z;
		block_data.data[first + k].w = sum;
		sum += c;
	}
	job_visible.data[j] = sum;
#endif

#ifdef MODE_COMPACT
	const uint b = gl_WorkGroupID.x;
	const uint lid = gl_LocalInvocationID.x;
	if (b < params.block_count) {
		const uvec2 blk = blocks.data[b];
		const uvec4 bd = block_data.data[b];
		const uint word = lid >> 5;
		const uint bit = lid & 31;
		const uint bits = word == 0 ? bd.x : bd.y;
		if ((bits & (1u << bit)) == 0) {
			return;
		}
		const uint rank = (word == 0 ? 0 : bitCount(bd.x)) + bitCount(bits & ((1u << bit) - 1u));
		const Job job = jobs.data[blk.x];
		const uint src = (job.pool_offset + (blk.y + lid) * job.stride) / 4;
		const uint dst = ((job.out_offset + bd.w + rank) * job.stride) / 4;
		for (uint k = 0; k < job.stride / 4; k++) {
			compacted.data[dst + k] = pool.data[src + k];
		}
	} else {
		const uint d = (b - params.block_count) * 64 + lid;
		if (d >= params.draw_count) {
			return;
		}
		const uvec2 di = draw_info.data[d];
		draw_args.data[d * 5 + 0] = di.y;
		draw_args.data[d * 5 + 1] = job_visible.data[di.x];
		draw_args.data[d * 5 + 2] = 0;
		draw_args.data[d * 5 + 3] = 0;
		draw_args.data[d * 5 + 4] = 0;
	}
#endif
}
