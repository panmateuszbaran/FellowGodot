/**************************************************************************/
/*  fellow_depth_range.h                                                  */
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
#include "servers/rendering/renderer_rd/shaders/effects/fellow_depth_range.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine depth-fitted sun shadows (docs/decisions/0388-directional-depth-fit.md): the depth pre-pass reduced to
// its nearest and farthest opaque surface, read back asynchronously (the scene cull narrows the directional light's
// cascades to that range a few frames later).
class FellowDepthRange {
public:
	struct Params {
		RID depth;
		Size2i size;
		Projection projection; // the (jittered) projection the depth buffer was rendered with
	};

private:
	struct Push { // std430
		float z_row[4];
		float w_row[4];
		int32_t size[2];
		int32_t pad[2];
	}; // 48 bytes

	FellowDepthRangeShaderRD shader;
	RID shader_version;
	RID pipeline;
	RID buffer; // 16 bytes: nearest, farthest (float bits), count, pad

public:
	// Records the reduction; the buffer's contents go to p_callback (a Callable taking a PackedByteArray first) once the
	// GPU has done it.
	void process(const Params &p_params, const Callable &p_callback);

	FellowDepthRange();
	~FellowDepthRange();
};

} // namespace RendererRD
