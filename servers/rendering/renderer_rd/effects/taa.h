/**************************************************************************/
/*  taa.h                                                                 */
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

#include "servers/rendering/renderer_rd/shaders/effects/taa_resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

class TAA {
public:
	TAA();
	~TAA();

	void process(Ref<RenderSceneBuffersRD> p_render_buffers, RD::DataFormat p_format, float p_z_near, float p_z_far);

private:
	struct TAAResolvePushConstant {
		float resolution_width;
		float resolution_height;
		float disocclusion_threshold;
		float variance_dynamic;
		// FellowEngine resolve v2 tunables (unused by the stock resolve).
		float fellow_gamma;
		float fellow_feedback_min;
		float fellow_feedback_max;
		float fellow_antiflicker;
	};

	// Shader variants: bit 0 = FellowEngine resolve v2, bit 1 = fused velocity write (history ping-pong).
	enum {
		VARIANT_FELLOW_V2 = 1,
		VARIANT_FELLOW_WRITE_VELOCITY = 2,
		VARIANT_MAX = 4,
	};

	TaaResolveShaderRD taa_shader;
	RID shader_version;
	RID pipelines[VARIANT_MAX];

	// Per-render-buffers state for the FellowEngine history ping-pong path.
	class FellowPingPongState : public RenderBufferCustomDataRD {
		GDSOFTCLASS(FellowPingPongState, RenderBufferCustomDataRD);

	public:
		uint32_t parity = 0;
		bool valid = false;
		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override { valid = false; }
		virtual void free_data() override { valid = false; }
	};

	void resolve(RID p_frame, RID p_temp, RID p_depth, RID p_velocity, RID p_prev_velocity, RID p_history, Size2 p_resolution, float p_z_near, float p_z_far, uint32_t p_variant = 0, RID p_velocity_out = RID());
	void process_pingpong(Ref<RenderSceneBuffersRD> p_render_buffers, RD::DataFormat p_format, float p_z_near, float p_z_far, uint32_t p_variant);
};

} // namespace RendererRD
