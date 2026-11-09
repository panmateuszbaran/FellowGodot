/**************************************************************************/
/*  fellow_pipeline_record.h                                              */
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

#pragma once

#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device.h"

// FellowEngine recorded pipelines (rendering/fellow/pipelines/*, decision 0171). A game run with `record` on writes
// the pipelines its materials' shaders compiled — each shader known by a hash of its code, each pipeline by its key
// with the vertex and framebuffer formats described rather than numbered — to `path` (res://fellow_pipelines.json by
// default, merged with what is there). A run with `replay` on (the default) and that file compiles a shader's recorded
// pipelines in the background as soon as the shader is created at load, before a draw asks for them: the pipelines a
// play-through met later (another weather, a light, autotune's variants) are ready at their first frame.
//
// The keys are the scene shader's (Forward+); the file is tied to the engine's scene shader (its source and defines'
// hash): one recorded with another build or other rendering settings is not replayed, and recording starts it again.
class FellowPipelineRecord {
public:
	// A pipeline of a scene shader: its formats (this run's IDs) and the rest of its key, as numbers.
	enum {
		FIELD_CULL,
		FIELD_PRIMITIVE,
		FIELD_VERSION,
		FIELD_COLOR_PASS_FLAGS,
		FIELD_SPECIALIZATION_0,
		FIELD_SPECIALIZATION_1,
		FIELD_SPECIALIZATION_2,
		FIELD_WIREFRAME,
		FIELD_UBERSHADER,
		FIELD_MAX
	};

	struct Entry {
		RD::VertexFormatID vertex_format = RD::INVALID_ID;
		RD::FramebufferFormatID framebuffer_format = RD::INVALID_ID;
		uint32_t fields[FIELD_MAX] = {};
	};

	// Called by the scene shader once it is initialized (its source's hash ties the file to it): reads the settings and,
	// to replay or to add to it, the file.
	static void initialize(const String &p_scene_shader_hash);
	// Every frame (the render thread): writes the file a few seconds after something new was recorded (a game stopped
	// from the editor is killed, not quit).
	static void process();
	// At the renderer's end: writes what is left.
	static void finalize();

	static bool is_recording();
	static bool is_replaying();
	// A pipeline compiled (any thread).
	static void record(uint64_t p_shader_hash, const Entry &p_entry);
	// The recorded pipelines of a shader, their formats made in this run (the render thread or the one creating the
	// shader); false if there are none.
	static bool get_replay(uint64_t p_shader_hash, LocalVector<Entry> &r_entries);
};
