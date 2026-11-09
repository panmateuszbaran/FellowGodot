/**************************************************************************/
/*  fellow_pipeline_record.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fellow_pipeline_record.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/version.h"
#include "servers/rendering/fellow_settings.h"

namespace {

// The file's layout; a file of another layout is not read.
constexpr int RECORD_FORMAT = 1;
// Seconds between the first new pipeline and the file's write (a burst of compilations is written once).
constexpr uint64_t WRITE_DELAY_USEC = 3000000;

struct PortableKey {
	int vertex = -1; // index into the file's vertex formats
	int framebuffer = -1; // index into its framebuffer formats
	uint32_t fields[FellowPipelineRecord::FIELD_MAX] = {};
};

struct State {
	Mutex mutex;
	bool recording = false;
	bool replaying = false;
	String path; // read from
	String write_path; // written to (user:// when res:// is not writable)
	String signature; // the scene shader's source and defines
	// The file's tables, with what this run recorded added: descriptions as JSON values, their texts as keys.
	Array vertex_formats;
	Array framebuffer_formats;
	HashMap<String, int> vertex_by_text;
	HashMap<String, int> framebuffer_by_text;
	HashMap<uint64_t, LocalVector<PortableKey>> shaders;
	HashSet<String> keys;
	// This run's format IDs for the tables' entries, both ways (RD::INVALID_ID: not made yet; -2: could not be made).
	LocalVector<RD::VertexFormatID> vertex_ids;
	LocalVector<RD::FramebufferFormatID> framebuffer_ids;
	HashMap<RD::VertexFormatID, int> vertex_index;
	HashMap<RD::FramebufferFormatID, int> framebuffer_index;
	// Recorded since the last write (this run's IDs).
	LocalVector<Pair<uint64_t, FellowPipelineRecord::Entry>> pending;
	uint64_t pending_since_usec = 0;
	bool dirty = false;
	bool write_failed = false;
};

State *state = nullptr;

String _hex64(uint64_t p_value) {
	return String::num_uint64(p_value, 16).lpad(16, "0");
}

bool _parse_hex64(const String &p_text, uint64_t &r_value) {
	if (p_text.is_empty() || p_text.length() > 16) {
		return false;
	}
	uint64_t v = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		uint64_t d;
		if (c >= '0' && c <= '9') {
			d = c - '0';
		} else if (c >= 'a' && c <= 'f') {
			d = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			d = c - 'A' + 10;
		} else {
			return false;
		}
		v = (v << 4) | d;
	}
	r_value = v;
	return true;
}

// A vertex format as a JSON value: [[binding, location, offset, format, stride, frequency], ...].
Array _vertex_to_json(const Vector<RD::VertexAttribute> &p_attributes) {
	Array a;
	for (const RD::VertexAttribute &attr : p_attributes) {
		Array e;
		e.push_back(int64_t(attr.binding));
		e.push_back(int64_t(attr.location));
		e.push_back(int64_t(attr.offset));
		e.push_back(int64_t(attr.format));
		e.push_back(int64_t(attr.stride));
		e.push_back(int64_t(attr.frequency));
		a.push_back(e);
	}
	return a;
}

bool _vertex_from_json(const Variant &p_value, Vector<RD::VertexAttribute> &r_attributes) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array a = p_value;
	for (int i = 0; i < a.size(); i++) {
		if (a[i].get_type() != Variant::ARRAY || Array(a[i]).size() != 6) {
			return false;
		}
		const Array e = a[i];
		RD::VertexAttribute attr;
		attr.binding = uint32_t(int64_t(e[0]));
		attr.location = uint32_t(int64_t(e[1]));
		attr.offset = uint32_t(int64_t(e[2]));
		attr.format = RD::DataFormat(int64_t(e[3]));
		attr.stride = uint32_t(int64_t(e[4]));
		attr.frequency = RD::VertexFrequency(int64_t(e[5]));
		if (attr.format >= RD::DATA_FORMAT_MAX) {
			return false;
		}
		r_attributes.push_back(attr);
	}
	return true;
}

Array _ints_to_json(const Vector<int32_t> &p_values) {
	Array a;
	for (int32_t v : p_values) {
		a.push_back(int64_t(v));
	}
	return a;
}

bool _ints_from_json(const Variant &p_value, Vector<int32_t> &r_values) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array a = p_value;
	for (int i = 0; i < a.size(); i++) {
		r_values.push_back(int32_t(int64_t(a[i])));
	}
	return true;
}

// A framebuffer format: [[[format, samples, usage], ...], [[colour, input, resolve, preserve, depth, depth resolve], ...],
// view count, VRS attachment].
Array _framebuffer_to_json(const Vector<RD::AttachmentFormat> &p_attachments, const Vector<RD::FramebufferPass> &p_passes, uint32_t p_view_count, int32_t p_vrs_attachment) {
	Array attachments;
	for (const RD::AttachmentFormat &af : p_attachments) {
		Array e;
		e.push_back(int64_t(af.format));
		e.push_back(int64_t(af.samples));
		e.push_back(int64_t(af.usage_flags));
		attachments.push_back(e);
	}
	Array passes;
	for (const RD::FramebufferPass &pass : p_passes) {
		Array e;
		e.push_back(_ints_to_json(pass.color_attachments));
		e.push_back(_ints_to_json(pass.input_attachments));
		e.push_back(_ints_to_json(pass.resolve_attachments));
		e.push_back(_ints_to_json(pass.preserve_attachments));
		e.push_back(int64_t(pass.depth_attachment));
		e.push_back(int64_t(pass.depth_resolve_attachment));
		passes.push_back(e);
	}
	Array a;
	a.push_back(attachments);
	a.push_back(passes);
	a.push_back(int64_t(p_view_count));
	a.push_back(int64_t(p_vrs_attachment));
	return a;
}

bool _framebuffer_from_json(const Variant &p_value, Vector<RD::AttachmentFormat> &r_attachments, Vector<RD::FramebufferPass> &r_passes, uint32_t &r_view_count, int32_t &r_vrs_attachment) {
	if (p_value.get_type() != Variant::ARRAY || Array(p_value).size() != 4) {
		return false;
	}
	const Array a = p_value;
	if (a[0].get_type() != Variant::ARRAY || a[1].get_type() != Variant::ARRAY) {
		return false;
	}
	const Array attachments = a[0];
	for (int i = 0; i < attachments.size(); i++) {
		if (attachments[i].get_type() != Variant::ARRAY || Array(attachments[i]).size() != 3) {
			return false;
		}
		const Array e = attachments[i];
		RD::AttachmentFormat af;
		af.format = RD::DataFormat(int64_t(e[0]));
		af.samples = RD::TextureSamples(int64_t(e[1]));
		af.usage_flags = uint32_t(int64_t(e[2]));
		if (af.format >= RD::DATA_FORMAT_MAX || af.samples >= RD::TEXTURE_SAMPLES_MAX) {
			return false;
		}
		r_attachments.push_back(af);
	}
	const Array passes = a[1];
	for (int i = 0; i < passes.size(); i++) {
		if (passes[i].get_type() != Variant::ARRAY || Array(passes[i]).size() != 6) {
			return false;
		}
		const Array e = passes[i];
		RD::FramebufferPass pass;
		if (!_ints_from_json(e[0], pass.color_attachments) || !_ints_from_json(e[1], pass.input_attachments) || !_ints_from_json(e[2], pass.resolve_attachments) || !_ints_from_json(e[3], pass.preserve_attachments)) {
			return false;
		}
		pass.depth_attachment = int32_t(int64_t(e[4]));
		pass.depth_resolve_attachment = int32_t(int64_t(e[5]));
		r_passes.push_back(pass);
	}
	r_view_count = uint32_t(int64_t(a[2]));
	r_vrs_attachment = int32_t(int64_t(a[3]));
	return true;
}

String _key_text(uint64_t p_shader, const PortableKey &p_key) {
	String t = _hex64(p_shader) + ":" + itos(p_key.vertex) + ":" + itos(p_key.framebuffer);
	for (int i = 0; i < FellowPipelineRecord::FIELD_MAX; i++) {
		t += ":" + itos(p_key.fields[i]);
	}
	return t;
}

void _clear_tables() {
	state->vertex_formats.clear();
	state->framebuffer_formats.clear();
	state->vertex_by_text.clear();
	state->framebuffer_by_text.clear();
	state->shaders.clear();
	state->keys.clear();
	state->vertex_ids.clear();
	state->framebuffer_ids.clear();
	state->vertex_index.clear();
	state->framebuffer_index.clear();
}

// The file at state->path into the tables; false (the tables empty) if it is missing, unreadable or another
// layout's, build's or settings'.
bool _load() {
	if (!FileAccess::exists(state->path)) {
		return false;
	}
	const String text = FileAccess::get_file_as_string(state->path);
	const Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		WARN_PRINT(vformat("FellowEngine recorded pipelines: \"%s\" is not a pipelines file; not replayed.", state->path));
		return false;
	}
	const Dictionary root = parsed;
	if (int64_t(root.get("format", 0)) != RECORD_FORMAT || String(root.get("scene_shader", "")) != state->signature) {
		WARN_PRINT(vformat("FellowEngine recorded pipelines: \"%s\" was recorded with another build of the engine or other rendering settings; not replayed (record again).", state->path));
		return false;
	}
	const Array vertex_formats = root.get("vertex_formats", Array());
	const Array framebuffer_formats = root.get("framebuffer_formats", Array());
	const Dictionary shaders = root.get("shaders", Dictionary());
	bool valid = true;
	// The descriptions written again as this run writes them (JSON's numbers are read as floats), so that their texts
	// match the ones this run records.
	for (int i = 0; i < vertex_formats.size() && valid; i++) {
		Vector<RD::VertexAttribute> attributes;
		valid = _vertex_from_json(vertex_formats[i], attributes);
		const Array json = _vertex_to_json(attributes);
		state->vertex_formats.push_back(json);
		state->vertex_by_text[JSON::stringify(json)] = i;
		state->vertex_ids.push_back(RD::INVALID_ID);
	}
	for (int i = 0; i < framebuffer_formats.size() && valid; i++) {
		Vector<RD::AttachmentFormat> attachments;
		Vector<RD::FramebufferPass> passes;
		uint32_t view_count = 1;
		int32_t vrs_attachment = -1;
		valid = _framebuffer_from_json(framebuffer_formats[i], attachments, passes, view_count, vrs_attachment);
		const Array json = _framebuffer_to_json(attachments, passes, view_count, vrs_attachment);
		state->framebuffer_formats.push_back(json);
		state->framebuffer_by_text[JSON::stringify(json)] = i;
		state->framebuffer_ids.push_back(RD::INVALID_ID);
	}
	for (const KeyValue<Variant, Variant> &kv : shaders) {
		if (!valid) {
			break;
		}
		uint64_t shader = 0;
		if (!_parse_hex64(String(kv.key), shader) || kv.value.get_type() != Variant::ARRAY) {
			valid = false;
			break;
		}
		const Array list = kv.value;
		for (int i = 0; i < list.size(); i++) {
			if (list[i].get_type() != Variant::ARRAY || Array(list[i]).size() != 2 + FellowPipelineRecord::FIELD_MAX) {
				valid = false;
				break;
			}
			const Array e = list[i];
			PortableKey key;
			key.vertex = int(int64_t(e[0]));
			key.framebuffer = int(int64_t(e[1]));
			if (key.vertex < 0 || key.vertex >= vertex_formats.size() || key.framebuffer < 0 || key.framebuffer >= framebuffer_formats.size()) {
				valid = false;
				break;
			}
			for (int f = 0; f < FellowPipelineRecord::FIELD_MAX; f++) {
				key.fields[f] = uint32_t(int64_t(e[2 + f]));
			}
			const String key_text = _key_text(shader, key);
			if (!state->keys.has(key_text)) {
				state->keys.insert(key_text);
				state->shaders[shader].push_back(key);
			}
		}
	}
	if (!valid) {
		WARN_PRINT(vformat("FellowEngine recorded pipelines: \"%s\" is damaged; not replayed.", state->path));
		_clear_tables();
		return false;
	}
	return true;
}

// This run's vertex format as an index into the tables (added if new); -1 if RD does not know it.
int _vertex_index(RD::VertexFormatID p_id) {
	if (const int *found = state->vertex_index.getptr(p_id)) {
		return *found;
	}
	const Vector<RD::VertexAttribute> attributes = RD::get_singleton()->fellow_vertex_format_get_attributes(p_id);
	if (attributes.is_empty()) {
		return -1;
	}
	const Array json = _vertex_to_json(attributes);
	const String text = JSON::stringify(json);
	int index;
	if (const int *existing = state->vertex_by_text.getptr(text)) {
		index = *existing;
	} else {
		index = state->vertex_formats.size();
		state->vertex_formats.push_back(json);
		state->vertex_by_text[text] = index;
		state->vertex_ids.push_back(p_id);
	}
	state->vertex_ids[index] = p_id;
	state->vertex_index[p_id] = index;
	return index;
}

int _framebuffer_index(RD::FramebufferFormatID p_id) {
	if (const int *found = state->framebuffer_index.getptr(p_id)) {
		return *found;
	}
	Vector<RD::AttachmentFormat> attachments;
	Vector<RD::FramebufferPass> passes;
	uint32_t view_count = 1;
	int32_t vrs_attachment = -1;
	// An empty format (no attachments) is not kept: its samples are not part of its key, and making it ahead could
	// give a later request other samples than a run without the file.
	if (!RD::get_singleton()->fellow_framebuffer_format_get_description(p_id, attachments, passes, view_count, vrs_attachment) || attachments.is_empty()) {
		return -1;
	}
	const Array json = _framebuffer_to_json(attachments, passes, view_count, vrs_attachment);
	const String text = JSON::stringify(json);
	int index;
	if (const int *existing = state->framebuffer_by_text.getptr(text)) {
		index = *existing;
	} else {
		index = state->framebuffer_formats.size();
		state->framebuffer_formats.push_back(json);
		state->framebuffer_by_text[text] = index;
		state->framebuffer_ids.push_back(p_id);
	}
	state->framebuffer_ids[index] = p_id;
	state->framebuffer_index[p_id] = index;
	return index;
}

// The pipelines recorded since the last write into the tables (the mutex held).
void _take_pending() {
	for (const Pair<uint64_t, FellowPipelineRecord::Entry> &p : state->pending) {
		PortableKey key;
		key.vertex = _vertex_index(p.second.vertex_format);
		key.framebuffer = _framebuffer_index(p.second.framebuffer_format);
		if (key.vertex < 0 || key.framebuffer < 0) {
			static const bool debug = OS::get_singleton()->has_environment("FELLOW_PIPELINES_DEBUG");
			if (debug) {
				print_line(vformat("FellowEngine recorded pipelines: not kept, shader %s vertex format %d (%d) framebuffer format %d (%d) version %d", _hex64(p.first), int64_t(p.second.vertex_format), key.vertex, int64_t(p.second.framebuffer_format), key.framebuffer, p.second.fields[FellowPipelineRecord::FIELD_VERSION]));
			}
			continue;
		}
		memcpy(key.fields, p.second.fields, sizeof(key.fields));
		const String key_text = _key_text(p.first, key);
		if (state->keys.has(key_text)) {
			continue;
		}
		state->keys.insert(key_text);
		state->shaders[p.first].push_back(key);
		state->dirty = true;
		Fellow::stats().pipelines_recorded.increment();
	}
	state->pending.clear();
	state->pending_since_usec = 0;
}

void _write() {
	if (!state->dirty || state->write_failed) {
		return;
	}
	Dictionary root;
	root["format"] = RECORD_FORMAT;
	root["engine"] = String(GODOT_VERSION_FULL_BUILD) + " " + String(GODOT_VERSION_HASH);
	root["scene_shader"] = state->signature;
	root["vertex_formats"] = state->vertex_formats;
	root["framebuffer_formats"] = state->framebuffer_formats;
	Dictionary shaders;
	uint64_t count = 0;
	for (const KeyValue<uint64_t, LocalVector<PortableKey>> &kv : state->shaders) {
		Array list;
		for (const PortableKey &key : kv.value) {
			Array e;
			e.push_back(key.vertex);
			e.push_back(key.framebuffer);
			for (int f = 0; f < FellowPipelineRecord::FIELD_MAX; f++) {
				e.push_back(int64_t(key.fields[f]));
			}
			list.push_back(e);
			count++;
		}
		shaders[_hex64(kv.key)] = list;
	}
	root["shaders"] = shaders;
	root["pipelines"] = int64_t(count);
	const String text = JSON::stringify(root, "", true);

	Ref<FileAccess> f = FileAccess::open(state->write_path, FileAccess::WRITE);
	if (f.is_null() && state->write_path.begins_with("res://")) {
		// An exported game cannot write into its own files.
		const String fallback = "user://" + state->write_path.get_file();
		WARN_PRINT(vformat("FellowEngine recorded pipelines: cannot write \"%s\"; recording into \"%s\".", state->write_path, fallback));
		state->write_path = fallback;
		f = FileAccess::open(state->write_path, FileAccess::WRITE);
	}
	if (f.is_null()) {
		ERR_PRINT(vformat("FellowEngine recorded pipelines: cannot write \"%s\"; recording stopped.", state->write_path));
		state->write_failed = true;
		return;
	}
	f->store_string(text);
	state->dirty = false;
}

} // namespace

void FellowPipelineRecord::initialize(const String &p_scene_shader_hash) {
	if (state != nullptr || !Fellow::enabled() || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// Test hooks: FELLOW_PIPELINES_RECORD, FELLOW_PIPELINES_REPLAY (0 or 1) and FELLOW_PIPELINES_PATH override the settings.
	OS *os = OS::get_singleton();
	bool record = GLOBAL_GET("rendering/fellow/pipelines/record");
	bool replay = GLOBAL_GET("rendering/fellow/pipelines/replay");
	String path = GLOBAL_GET("rendering/fellow/pipelines/path");
	if (os->has_environment("FELLOW_PIPELINES_RECORD")) {
		record = os->get_environment("FELLOW_PIPELINES_RECORD") == "1";
	}
	if (os->has_environment("FELLOW_PIPELINES_REPLAY")) {
		replay = os->get_environment("FELLOW_PIPELINES_REPLAY") == "1";
	}
	if (os->has_environment("FELLOW_PIPELINES_PATH")) {
		path = os->get_environment("FELLOW_PIPELINES_PATH");
	}
	if ((!record && !replay) || path.is_empty()) {
		return;
	}
	state = memnew(State);
	state->path = path;
	state->write_path = path;
	state->signature = p_scene_shader_hash;
	const bool loaded = _load();
	state->replaying = replay && loaded;
	state->recording = record;
	if (state->recording && !loaded && FileAccess::exists(path)) {
		// Another build's or settings' file: recording starts it again.
		state->dirty = true;
	}
	if (!state->recording && !state->replaying) {
		memdelete(state);
		state = nullptr;
		return;
	}
	if (state->replaying) {
		uint64_t count = 0;
		for (const KeyValue<uint64_t, LocalVector<PortableKey>> &kv : state->shaders) {
			count += kv.value.size();
		}
		print_verbose(vformat("FellowEngine recorded pipelines: %d pipelines of %d shaders to replay from \"%s\".", count, state->shaders.size(), path));
	}
}

void FellowPipelineRecord::process() {
	if (state == nullptr || !state->recording) {
		return;
	}
	MutexLock lock(state->mutex);
	if (state->pending.is_empty() && !state->dirty) {
		return;
	}
	if (!state->pending.is_empty() && OS::get_singleton()->get_ticks_usec() - state->pending_since_usec < WRITE_DELAY_USEC) {
		return;
	}
	_take_pending();
	_write();
}

void FellowPipelineRecord::finalize() {
	if (state == nullptr) {
		return;
	}
	if (state->recording) {
		MutexLock lock(state->mutex);
		_take_pending();
		_write();
	}
	memdelete(state);
	state = nullptr;
}

bool FellowPipelineRecord::is_recording() {
	return state != nullptr && state->recording;
}

bool FellowPipelineRecord::is_replaying() {
	return state != nullptr && state->replaying;
}

void FellowPipelineRecord::record(uint64_t p_shader_hash, const Entry &p_entry) {
	if (state == nullptr || !state->recording) {
		return;
	}
	MutexLock lock(state->mutex);
	if (state->pending.is_empty()) {
		state->pending_since_usec = OS::get_singleton()->get_ticks_usec();
	}
	state->pending.push_back({ p_shader_hash, p_entry });
}

bool FellowPipelineRecord::get_replay(uint64_t p_shader_hash, LocalVector<Entry> &r_entries) {
	if (state == nullptr || !state->replaying) {
		return false;
	}
	MutexLock lock(state->mutex);
	const LocalVector<PortableKey> *keys = state->shaders.getptr(p_shader_hash);
	if (keys == nullptr) {
		return false;
	}
	RD *rd = RD::get_singleton();
	for (const PortableKey &key : *keys) {
		RD::VertexFormatID &vertex = state->vertex_ids[key.vertex];
		if (vertex == RD::INVALID_ID) {
			Vector<RD::VertexAttribute> attributes;
			_vertex_from_json(state->vertex_formats[key.vertex], attributes);
			vertex = rd->vertex_format_create(attributes);
			if (vertex == RD::INVALID_ID) {
				vertex = -2;
			} else {
				state->vertex_index[vertex] = key.vertex;
			}
		}
		RD::FramebufferFormatID &framebuffer = state->framebuffer_ids[key.framebuffer];
		if (framebuffer == RD::INVALID_ID) {
			Vector<RD::AttachmentFormat> attachments;
			Vector<RD::FramebufferPass> passes;
			uint32_t view_count = 1;
			int32_t vrs_attachment = -1;
			_framebuffer_from_json(state->framebuffer_formats[key.framebuffer], attachments, passes, view_count, vrs_attachment);
			framebuffer = rd->framebuffer_format_create_multipass(attachments, passes, view_count, vrs_attachment);
			if (framebuffer == RD::INVALID_ID) {
				framebuffer = -2;
			} else {
				state->framebuffer_index[framebuffer] = key.framebuffer;
			}
		}
		if (vertex < 0 || framebuffer < 0) {
			continue;
		}
		Entry e;
		e.vertex_format = vertex;
		e.framebuffer_format = framebuffer;
		memcpy(e.fields, key.fields, sizeof(e.fields));
		r_entries.push_back(e);
	}
	Fellow::stats().pipelines_replayed.add(r_entries.size());
	return !r_entries.is_empty();
}
