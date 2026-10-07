#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine persistent instance data (docs/decisions/0022-persistent-instance-data.md): copies the surface entries
// that changed (records of a slot index and the entry's 44 words, stock's InstanceData) into the persistent buffer. One
// invocation per word.

#define ENTRY_WORDS 44
#define RECORD_WORDS 48 // slot, 3 words of padding, the entry

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) buffer restrict readonly Records {
	uint data[];
}
records;

layout(set = 0, binding = 1, std430) buffer restrict writeonly Slots {
	uint data[];
}
slots;

layout(push_constant, std430) uniform Params {
	uint count;
	uint pad[3];
}
params;

void main() {
	uint index = gl_GlobalInvocationID.x;
	uint record = index / ENTRY_WORDS;
	if (record >= params.count) {
		return;
	}
	uint word = index - record * ENTRY_WORDS;
	uint slot = records.data[record * RECORD_WORDS];
	slots.data[slot * ENTRY_WORDS + word] = records.data[record * RECORD_WORDS + 4 + word];
}
