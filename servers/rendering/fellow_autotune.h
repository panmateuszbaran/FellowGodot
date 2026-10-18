/**************************************************************************/
/*  fellow_autotune.h                                                     */
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

#include "core/string/ustring.h"
#include "core/typedefs.h"

// FellowEngine autotune (docs/decisions/0020-autotune.md): exact optimizations whose cost and benefit depend on the
// scene are chosen by measurement. Such a toggle changes no pixel whichever way it is set, so the render thread can
// run it on and off in alternating blocks of frames, compare the GPU time of those frames (two timestamps around the
// frame's viewports) or, for optimizations that move work between the render thread and the GPU, the frames' wall time,
// and keep the faster choice, checking again now and then. Render thread only.
namespace Fellow {

enum Tunable {
	TUNE_DIRECTIONAL_PYRAMID, // shadows/directional_max_mip (decision 0005)
	TUNE_POSITIONAL_PYRAMID, // shadows/positional_max_mip (decision 0005)
	TUNE_MULTIMESH_CULLING, // geometry/multimesh_culling (decision 0034), while MultiMeshes it can cull are drawn
	// Not exact, but visually lossless (decision 0027): adaptive shading rate pays only where its shading savings exceed
	// its passes' cost; switched like the exact ones (both ways look the same within the measured limits).
	TUNE_ADAPTIVE_VRS, // shading/adaptive_vrs_threshold > 0 (the attachment is created at start-up)
	TUNE_LIGHT_TILE_CULL, // lights/tile_cull (decision 0049), while the opaque colour pass can use it
	TUNE_SSR_MAIN, // frame/async_ss_split 3: SSR on the main queue beside the async SSAO too (decision 0060), while SSIL and SSR run with async compute
	// Measured by the frame's wall time instead (they trade render-thread time against GPU time):
	TUNE_ASYNC_COMPUTE, // frame/async_compute (decision 0015; the device created its second queue at start-up)
	TUNE_FRAME_PLAN, // frame/replay_frame_plan (decision 0019)
	TUNE_PERSISTENT_INSTANCES, // cpu/persistent_instances (decision 0022)
	TUNE_MAX,
};

// The choice for the frame being rendered (true: the optimization runs). Always true while autotune is off.
bool autotune_allows(Tunable p_tunable);

// The renderer found MultiMesh elements MultiMesh culling can take in this frame (TUNE_MULTIMESH_CULLING applies only
// while it does).
void autotune_note_multimesh_candidates();

// The renderer could run light tile culling in this frame (omni/spot lights in view, a depth pre-pass, ...):
// TUNE_LIGHT_TILE_CULL applies only while it can.
void autotune_note_light_tile_cull_candidates();

// The screen-space effects ran split between the queues with SSIL and SSR in this frame: TUNE_SSR_MAIN applies only
// while they do (decision 0060).
void autotune_note_ss_split_candidates();

// Called by RenderingServerDefault::_draw: before the viewports are drawn (decides this frame's choices and captures
// the start timestamp), after them (end timestamp), and for each captured timestamp read back.
void autotune_frame_begin();
void autotune_frame_end();
void autotune_timestamp(const String &p_name, uint64_t p_gpu_time);

} // namespace Fellow
