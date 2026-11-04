/**************************************************************************/
/*  fsr3.h                                                                */
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
#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/templates/rid.h"

// FellowEngine (milestone 47, decision 0148): AMD FidelityFX Super Resolution 3.1 upscaler on RenderingDevice.
//
// The FidelityFX SDK's types share their names with FSR 2.2's (thirdparty/amd-fsr2) and differ from them, so the SDK
// is only included by fsr3.cpp; this header is opaque.
namespace RendererRD {

class FSR3Context;

class FSR3Effect {
public:
	struct Parameters {
		FSR3Context *context = nullptr;
		Size2i internal_size;
		Size2i target_size;
		RID color;
		RID depth;
		RID velocity;
		RID reactive;
		RID exposure;
		RID output;
		float z_near = 0.0f;
		float z_far = 0.0f;
		float fovy = 0.0f;
		Vector2 jitter;
		float delta_time = 0.0f; // Seconds.
		float sharpness = 0.0f; // 0 none, 1 the most.
		bool reset_accumulation = false;
		Projection reprojection;
	};

	// FSR 3.1's tunables (ffxFsr3UpscalerSetConstant); defaults are the SDK's.
	struct Tuning {
		float velocity_factor = 1.0f;
		float reactiveness_scale = 1.0f;
		float shading_change_scale = 1.0f;
		float accumulation_added_per_frame = 1.0f / 3.0f;
		float min_disocclusion_accumulation = -1.0f / 3.0f;
		bool operator!=(const Tuning &p_other) const {
			return velocity_factor != p_other.velocity_factor || reactiveness_scale != p_other.reactiveness_scale || shading_change_scale != p_other.shading_change_scale || accumulation_added_per_frame != p_other.accumulation_added_per_frame || min_disocclusion_accumulation != p_other.min_disocclusion_accumulation;
		}
	};

	struct Device;

	FSR3Effect();
	~FSR3Effect();

	FSR3Context *create_context(Size2i p_internal_size, Size2i p_target_size);
	static void free_context(FSR3Context *p_context);
	void upscale(const Parameters &p_params, const Tuning &p_tuning, bool p_debug_view = false);

private:
	Device *device = nullptr;
};

} // namespace RendererRD
