/**************************************************************************/
/*  fellow_virtual_shadow.h                                               */
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

#include "core/math/projection.h"
#include "core/templates/local_vector.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_virtual_shadow.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine virtual shadow maps (docs/decisions/0396-virtual-shadow-maps.md): the GPU side. The frame's header and
// page table (read by the scene shader's colour and virtual shadow passes, binding 57), the pages emptied before they
// are drawn again, and the pages the view's pixels look up, marked from the depth pre-pass and read back for the scene
// cull. The pool itself is the render buffers' (one per view).
class FellowVirtualShadow {
public:
	static constexpr int MAX_LEVELS = 16;
	static constexpr int WINDOW = 64;
	static constexpr int PAGE = 128;

	struct Header { // std430: FellowVSMHeader in scene_forward_clustered_inc.glsl and fellow_virtual_shadow.glsl
		uint32_t levels = 0;
		uint32_t light_index = 0;
		uint32_t pool_pages = 0;
		uint32_t flags = 0;
		float texel0 = 0.0f;
		float lod_bias = 0.0f;
		float footprint = 0.0f;
		float max_distance = 0.0f;
		int32_t h_top_fixed = 0;
		int32_t cam_h_fixed = 0;
		float cam_h_frac = 0.0f;
		float pad = 0.0f;
		float axis_x[4] = {};
		float axis_y[4] = {};
		float axis_z[4] = {};
		float origin[MAX_LEVELS][4] = {};
	};
	static constexpr uint32_t TABLE_ENTRIES = MAX_LEVELS * WINDOW * WINDOW;
	static constexpr uint32_t BUFFER_SIZE = sizeof(Header) + TABLE_ENTRIES * 4;
	static constexpr uint32_t REQUEST_WORDS = TABLE_ENTRIES / 32;

private:
	struct ClearPush { // std430
		uint32_t pool_pages;
		uint32_t count;
		uint32_t pad[2];
	};
	struct MarkPush { // std430
		float inv_projection[16];
		int32_t size[2];
		int32_t pad[2];
	};

	FellowVirtualShadowShaderRD shader;
	RID shader_version;
	RID pipelines[2]; // clear, mark
	bool compiled = false;
	RID buffer; // header + table
	RID request_buffer;
	RID clear_buffer;
	uint32_t clear_capacity = 0;
	RID dummy_pool; // 1 x 1, for the passes that do not read the pool
	RID empty_framebuffer; // the passes' (no attachments)

	void _compile();

public:
	RID get_buffer();
	RID get_dummy_pool();
	RID get_empty_framebuffer();
	// The frame's header and table (p_table: p_levels * WINDOW^2 entries).
	void upload(const Header &p_header, const uint32_t *p_table, uint32_t p_levels);
	// Empties the pool's physical pages p_pages (before they are drawn again).
	void clear_pages(RID p_pool, uint32_t p_pool_pages, const LocalVector<uint32_t> &p_pages);
	// Marks the pages the depth buffer's pixels look up (with this frame's header); the request bits go to p_callback (a
	// Callable taking a PackedByteArray first) once the GPU has done it.
	void mark(RID p_depth, const Size2i &p_size, const Projection &p_projection, const Callable &p_callback);

	FellowVirtualShadow();
	~FellowVirtualShadow();
};

} // namespace RendererRD
