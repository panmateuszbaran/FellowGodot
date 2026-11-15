/**************************************************************************/
/*  fellow_hitch_debugger.h                                               */
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

#include "core/templates/hash_map.h"
#include "editor/debugger/editor_debugger_plugin.h"

class Button;
class CheckBox;
class Label;
class SpinBox;
class TextEdit;
class Tree;
class TreeItem;
class VBoxContainer;

// Milestone 120 (docs/decisions/0242-gpu-timeline.md): a long GPU frame's passes drawn on a time line, a row per queue,
// each pass coloured by how far past its average it ran.
class FellowGpuTimeline : public Control {
	GDCLASS(FellowGpuTimeline, Control);

	Array passes; // [name, queue, start µs, length µs, average µs or -1]
	double span_usec = 0.0;
	double gap_usec = -1.0;
	int _pass_at(const Point2 &p_pos) const;
	Rect2 _pass_rect(int p_index) const;

protected:
	void _notification(int p_what);

public:
	void set_frame(const Array &p_passes, double p_gap_usec);
	virtual String get_tooltip(const Point2 &p_pos) const override;
	virtual Size2 get_minimum_size() const override;
};

// Milestone 121 (docs/decisions/0244-frame-recording.md): a recording's frames as bars — the interval since the previous
// frame behind, the GPU's passes in front — one selected by clicking or dragging over them.
class FellowFrameGraph : public Control {
	GDCLASS(FellowFrameGraph, Control);

	Array frames; // [frame, seconds in, interval ms or -1, CPU recording ms, CPU wait ms, GPU passes ms, time line]
	int selected = -1;
	double scale_ms = 16.7;
	int _frame_at(float p_x) const;
	void _select(int p_index);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_frames(const Array &p_frames);
	void select_frame(int p_index);
	int get_selected() const { return selected; }
	virtual void gui_input(const Ref<InputEvent> &p_event) override;
	virtual String get_tooltip(const Point2 &p_pos) const override;
	virtual Size2 get_minimum_size() const override;
};

// FellowEngine milestone 119 (docs/decisions/0238-hitches-in-the-debugger.md): the debugger's "FellowEngine Hitches" tab.
// A game run from the editor with rendering/fellow/debug/hitch_sampler on (or started from the tab) sends each hitch as
// it happens: the main thread's or the draw's long frame steps, named (FellowHitchSampler), and the GPU's long frames
// with the passes longest past their averages. The tab lists them (when, what, how long, where) and shows the one
// selected in full.
class FellowHitchDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(FellowHitchDebuggerPlugin, EditorDebuggerPlugin);

	struct SessionTab {
		VBoxContainer *root = nullptr;
		CheckBox *record = nullptr;
		SpinBox *threshold = nullptr;
		Label *summary = nullptr;
		Tree *list = nullptr;
		TextEdit *details = nullptr;
		FellowGpuTimeline *timeline = nullptr; // milestone 120
		FellowFrameGraph *graph = nullptr; // milestone 121: a recording's frames
		SpinBox *record_seconds = nullptr;
		TreeItem *recording = nullptr; // the recording whose frames are arriving
		int cpu = 0;
		int gpu = 0;
	};
	HashMap<int, SessionTab> tabs;

	void _session_started(int p_session);
	void _record_toggled(bool p_on, int p_session);
	void _clear(int p_session);
	void _selected(int p_session);
	void _send_start(int p_session);
	void _capture_frame(int p_session); // decision 0243
	void _record_frames(int p_session); // decision 0244
	void _graph_frame_selected(int p_index, int p_session);
	static String _frame_text(const Array &p_frame);
	void _update_summary(SessionTab &r_tab);

public:
	virtual void setup_session(int p_session) override;
	virtual bool has_capture(const String &p_capture) const override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session) override;
};
