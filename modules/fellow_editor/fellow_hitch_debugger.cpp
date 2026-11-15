/**************************************************************************/
/*  fellow_hitch_debugger.cpp                                             */
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

#include "fellow_hitch_debugger.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/font.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/scene_string_names.h"

// Milestone 120 (decision 0242): the time line.
static const float TIMELINE_ROW = 22.0f;
static const float TIMELINE_LABEL = 70.0f;

void FellowGpuTimeline::set_frame(const Array &p_passes, double p_gap_usec) {
	passes = p_passes;
	gap_usec = p_gap_usec;
	span_usec = 0.0;
	for (int i = 0; i < passes.size(); i++) {
		const Array pass = passes[i];
		if (pass.size() >= 4) {
			span_usec = MAX(span_usec, double(pass[2]) + double(pass[3]));
		}
	}
	update_minimum_size();
	queue_redraw();
}

Size2 FellowGpuTimeline::get_minimum_size() const {
	return passes.is_empty() ? Size2() : Size2(200, TIMELINE_ROW * 2 + 18) * EDSCALE;
}

Rect2 FellowGpuTimeline::_pass_rect(int p_index) const {
	const Array pass = passes[p_index];
	const float label = TIMELINE_LABEL * EDSCALE;
	const float width = MAX(1.0f, get_size().x - label - 4 * EDSCALE);
	const float row = TIMELINE_ROW * EDSCALE;
	const double start = double(pass[2]);
	const double length = double(pass[3]);
	const float x = label + float(start / MAX(1.0, span_usec)) * width;
	const float w = MAX(1.0f, float(length / MAX(1.0, span_usec)) * width);
	return Rect2(x, 16 * EDSCALE + int(pass[1]) * row, w, row - 3 * EDSCALE);
}

int FellowGpuTimeline::_pass_at(const Point2 &p_pos) const {
	for (int i = passes.size() - 1; i >= 0; i--) {
		if (_pass_rect(i).has_point(p_pos)) {
			return i;
		}
	}
	return -1;
}

String FellowGpuTimeline::get_tooltip(const Point2 &p_pos) const {
	const int i = _pass_at(p_pos);
	if (i < 0) {
		return Control::get_tooltip(p_pos);
	}
	const Array pass = passes[i];
	const double average = pass[4];
	return vformat("%s%s\n%.2f ms from %.2f ms%s", String(pass[0]), int(pass[1]) ? TTR(" (async queue)") : String(), double(pass[3]) / 1000.0, double(pass[2]) / 1000.0,
			average >= 0.0 ? vformat(TTR(" (average %.2f ms)"), average / 1000.0) : String());
}

void FellowGpuTimeline::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW || passes.is_empty()) {
		return;
	}
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	const Color text = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const float label = TIMELINE_LABEL * EDSCALE;
	const float row = TIMELINE_ROW * EDSCALE;
	// The scale: labelled ticks at least 60 px apart (1, 2 or 5 times a power of ten milliseconds), five finer ones
	// between them where they are 8 px apart.
	const float width = MAX(1.0f, get_size().x - label - 4 * EDSCALE);
	const double px_per_ms = width / MAX(0.001, span_usec / 1000.0);
	double step_ms = 0.1;
	for (int k = 0; k < 12 && step_ms * px_per_ms < 60.0 * EDSCALE; k++) {
		static const double factors[3] = { 2.0, 2.5, 2.0 }; // 0.1, 0.2, 0.5, 1, 2, 5, 10 ...
		step_ms *= factors[k % 3];
	}
	const double minor_ms = step_ms / 5.0;
	const bool minor = minor_ms * px_per_ms >= 8.0 * EDSCALE;
	for (int i = 0; minor_ms * i * 1000.0 <= span_usec; i++) {
		const bool major = i % 5 == 0;
		if (!major && !minor) {
			continue;
		}
		const double ms = minor_ms * i;
		const float x = label + float(ms * px_per_ms);
		draw_line(Point2(x, major ? 12 * EDSCALE : 14 * EDSCALE), Point2(x, 16 * EDSCALE + 2 * row), Color(text, major ? 0.18 : 0.07));
		if (major) {
			draw_string(font, Point2(x + 2 * EDSCALE, 11 * EDSCALE), step_ms < 1.0 ? vformat("%.1f ms", ms) : vformat("%d ms", int(Math::round(ms))), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size * 0.8, Color(text, 0.7));
		}
	}
	draw_string(font, Point2(2 * EDSCALE, 16 * EDSCALE + row * 0.7), TTR("main"), HORIZONTAL_ALIGNMENT_LEFT, label - 4 * EDSCALE, font_size, text);
	draw_string(font, Point2(2 * EDSCALE, 16 * EDSCALE + row * 1.7), TTR("async"), HORIZONTAL_ALIGNMENT_LEFT, label - 4 * EDSCALE, font_size, text);
	for (int i = 0; i < passes.size(); i++) {
		const Array pass = passes[i];
		const Rect2 r = _pass_rect(i);
		const double length = pass[3];
		const double average = pass[4];
		// Grey at or below its average, to red at 1 ms or more past it (orange if it never ran in an ordinary frame;
		// decision 0243: two greys in turn when the frame was captured without the sampler's averages).
		Color c(0.45, 0.55, 0.65);
		if (average <= -2.0) {
			c = (i % 2) ? Color(0.38, 0.48, 0.6) : Color(0.5, 0.6, 0.7);
		} else if (average < 0.0) {
			c = Color(0.9, 0.6, 0.2);
		} else if (length > average) {
			c = c.lerp(Color(0.9, 0.2, 0.15), float(CLAMP((length - average) / 1000.0, 0.0, 1.0)));
		}
		draw_rect(r, c);
		if (r.size.x >= 3 * EDSCALE) {
			draw_line(r.position + Vector2(r.size.x - 0.5f, 0), r.position + Vector2(r.size.x - 0.5f, r.size.y), Color(0, 0, 0, 0.35)); // where the next begins
		}
		const String name = pass[0];
		if (r.size.x > 40 * EDSCALE) {
			draw_string(font, Point2(r.position.x + 2 * EDSCALE, r.position.y + r.size.y * 0.75), name, HORIZONTAL_ALIGNMENT_LEFT, r.size.x - 4 * EDSCALE, font_size * 0.8, Color(1, 1, 1));
		}
	}
}

// Milestone 121 (decision 0244): a recording's frames.
void FellowFrameGraph::_bind_methods() {
	ADD_SIGNAL(MethodInfo("frame_selected", PropertyInfo(Variant::INT, "index")));
}

void FellowFrameGraph::set_frames(const Array &p_frames) {
	frames = p_frames;
	selected = -1;
	// The scale: to the slowest frame, but at most three times the 90th percentile's (and at least 33.3 ms) — a spike
	// does not flatten the rest; bars past it are clipped and marked.
	LocalVector<double> times;
	for (int i = 0; i < frames.size(); i++) {
		const Array frame = frames[i];
		times.push_back(MAX(double(frame[2]), double(frame[5])));
	}
	times.sort();
	const double slowest = times.is_empty() ? 0.0 : times[times.size() - 1];
	const double p90 = times.is_empty() ? 0.0 : times[uint32_t(0.9 * (times.size() - 1))];
	scale_ms = 1.1 * MAX(16.7, MIN(slowest, MAX(33.3, 3.0 * p90)));
	set_visible(!frames.is_empty());
	update_minimum_size();
	queue_redraw();
}

void FellowFrameGraph::select_frame(int p_index) {
	selected = (p_index >= 0 && p_index < frames.size()) ? p_index : -1;
	queue_redraw();
}

void FellowFrameGraph::_select(int p_index) {
	if (p_index < 0 || p_index == selected) {
		return;
	}
	select_frame(p_index);
	emit_signal(SNAME("frame_selected"), selected);
}

int FellowFrameGraph::_frame_at(float p_x) const {
	if (frames.is_empty()) {
		return -1;
	}
	const float label = TIMELINE_LABEL * EDSCALE;
	const float width = MAX(1.0f, get_size().x - label - 4 * EDSCALE);
	return CLAMP(int((p_x - label) / width * frames.size()), 0, frames.size() - 1);
}

void FellowFrameGraph::gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->is_pressed() && button->get_button_index() == MouseButton::LEFT) {
		_select(_frame_at(button->get_position().x));
		accept_event();
		return;
	}
	const Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && motion->get_button_mask().has_flag(MouseButtonMask::LEFT)) {
		_select(_frame_at(motion->get_position().x));
		accept_event();
	}
}

String FellowFrameGraph::get_tooltip(const Point2 &p_pos) const {
	const int i = p_pos.x >= TIMELINE_LABEL * EDSCALE ? _frame_at(p_pos.x) : -1;
	if (i < 0) {
		return Control::get_tooltip(p_pos);
	}
	const Array frame = frames[i];
	const double interval = frame[2];
	return vformat(TTR("Frame %d, %.2f s in\nThe GPU's passes %.2f ms\n%s\nThe CPU waited %.2f ms for the GPU"), int64_t(frame[0]), double(frame[1]), double(frame[5]),
			interval >= 0.0 ? vformat(TTR("%.2f ms after the previous frame"), interval) : TTR("The first frame recorded"), double(frame[4]));
}

Size2 FellowFrameGraph::get_minimum_size() const {
	return frames.is_empty() ? Size2() : Size2(200, 84) * EDSCALE;
}

void FellowFrameGraph::_notification(int p_what) {
	if (p_what != NOTIFICATION_DRAW || frames.is_empty()) {
		return;
	}
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	const Color text = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const float label = TIMELINE_LABEL * EDSCALE;
	const float width = MAX(1.0f, get_size().x - label - 4 * EDSCALE);
	const float top = 4 * EDSCALE;
	const float height = MAX(1.0f, get_size().y - top - 2 * EDSCALE);
	const float bottom = top + height;
	const double px_per_ms = height / MAX(0.001, scale_ms);
	const float bar = width / frames.size();
	draw_rect(Rect2(label, top, width, height), Color(text, 0.04));
	// Each frame: the interval since the previous one (grey, behind), the GPU's passes (blue, in front); the selected
	// one marked.
	for (int i = 0; i < frames.size(); i++) {
		const Array frame = frames[i];
		const float x = label + bar * i;
		const float w = MAX(1.0f, bar - (bar >= 4 * EDSCALE ? 1.0f : 0.0f));
		const double interval = frame[2];
		const double gpu = frame[5];
		if (interval > 0.0) {
			const float h = MIN(height, float(interval * px_per_ms));
			draw_rect(Rect2(x, bottom - h, w, h), Color(text, 0.25));
		}
		const float h = MIN(height, float(gpu * px_per_ms));
		draw_rect(Rect2(x + w * 0.2f, bottom - h, MAX(1.0f, w * 0.6f), h), i == selected ? Color(1.0, 0.85, 0.3) : Color(0.35, 0.6, 0.9));
		if (MAX(interval, gpu) > scale_ms) {
			draw_rect(Rect2(x, top, w, 3 * EDSCALE), Color(0.95, 0.35, 0.3)); // past the scale (its time in the tooltip)
		}
	}
	if (selected >= 0) {
		const float x = label + bar * selected + MAX(1.0f, bar) * 0.5f;
		draw_line(Point2(x, top), Point2(x, bottom), Color(1.0, 0.85, 0.3, 0.6));
	}
	// 60 and 30 frames a second; a label only where it does not run into another.
	draw_string(font, Point2(2 * EDSCALE, top + font_size * 0.8f), vformat("%.0f ms", scale_ms), HORIZONTAL_ALIGNMENT_LEFT, label - 4 * EDSCALE, font_size * 0.8, Color(text, 0.6));
	float last_label_y = top;
	static const double marks[2] = { 33.3, 16.7 };
	for (double mark : marks) {
		if (mark >= scale_ms) {
			continue;
		}
		const float y = bottom - float(mark * px_per_ms);
		draw_line(Point2(label, y), Point2(label + width, y), Color(0.9, 0.4, 0.3, 0.6));
		if (y - last_label_y >= font_size * 0.9f) {
			draw_string(font, Point2(2 * EDSCALE, y + font_size * 0.3f), vformat("%.1f ms", mark), HORIZONTAL_ALIGNMENT_LEFT, label - 4 * EDSCALE, font_size * 0.8, Color(text, 0.8));
			last_label_y = y;
		}
	}
}

void FellowHitchDebuggerPlugin::setup_session(int p_session) {
	Ref<EditorDebuggerSession> session = get_session(p_session);
	ERR_FAIL_COND(session.is_null());
	SessionTab tab;
	tab.root = memnew(VBoxContainer);
	tab.root->set_name(TTR("FellowEngine Hitches"));

	HBoxContainer *bar = memnew(HBoxContainer);
	tab.root->add_child(bar);
	tab.record = memnew(CheckBox);
	tab.record->set_text(TTR("Record"));
	tab.record->set_tooltip_text(TTR("Start FellowEngine's hitch sampler in the running game (rendering/fellow/debug/hitch_sampler): frame steps and GPU frames longer than the threshold are listed here as they happen."));
	bar->add_child(tab.record);
	Label *over = memnew(Label);
	over->set_text(TTR("Longer than:"));
	bar->add_child(over);
	tab.threshold = memnew(SpinBox);
	tab.threshold->set_min(1.0);
	tab.threshold->set_max(1000.0);
	tab.threshold->set_step(0.1);
	tab.threshold->set_suffix("ms");
	tab.threshold->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	tab.threshold->set_value(double(GLOBAL_GET("rendering/fellow/debug/hitch_sampler_ms")));
	bar->add_child(tab.threshold);
	Button *clear = memnew(Button);
	clear->set_text(TTR("Clear"));
	bar->add_child(clear);
	// Decision 0243: any frame's GPU passes on the time line, when asked.
	Button *capture_frame = memnew(Button);
	capture_frame->set_text(TTR("Capture GPU Frame"));
	capture_frame->set_tooltip_text(TTR("The running game's next frame: its GPU passes, a row per queue, listed and drawn on the time line."));
	bar->add_child(capture_frame);
	// Decision 0244: every frame of a span, as a graph of their times.
	Button *record_frames = memnew(Button);
	record_frames->set_text(TTR("Record GPU Frames"));
	record_frames->set_tooltip_text(TTR("Every frame of the running game for the seconds beside: a bar each (the GPU's passes, and the time since the frame before), any of them selected to see its passes on the time line."));
	bar->add_child(record_frames);
	tab.record_seconds = memnew(SpinBox);
	tab.record_seconds->set_min(0.5);
	tab.record_seconds->set_max(30.0);
	tab.record_seconds->set_step(0.5);
	tab.record_seconds->set_value(2.0);
	tab.record_seconds->set_suffix("s");
	tab.record_seconds->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	bar->add_child(tab.record_seconds);
	tab.summary = memnew(Label);
	tab.summary->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tab.summary->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	bar->add_child(tab.summary);

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tab.root->add_child(split);
	tab.list = memnew(Tree);
	tab.list->set_columns(4);
	tab.list->set_column_titles_visible(true);
	tab.list->set_column_title(0, TTR("At"));
	tab.list->set_column_title(1, TTR("Kind"));
	tab.list->set_column_title(2, TTR("Length"));
	tab.list->set_column_title(3, TTR("Where"));
	tab.list->set_column_expand(0, false);
	tab.list->set_column_custom_minimum_width(0, 70 * EDSCALE);
	tab.list->set_column_expand(1, false);
	tab.list->set_column_custom_minimum_width(1, 64 * EDSCALE);
	tab.list->set_column_expand(2, false);
	tab.list->set_column_custom_minimum_width(2, 80 * EDSCALE);
	tab.list->set_hide_root(true);
	tab.list->set_select_mode(Tree::SELECT_ROW);
	tab.list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tab.list->set_custom_minimum_size(Size2(360, 0) * EDSCALE);
	tab.list->create_item();
	split->add_child(tab.list);
	VBoxContainer *right = memnew(VBoxContainer);
	right->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(right);
	tab.graph = memnew(FellowFrameGraph);
	tab.graph->set_tooltip_text(" "); // the frames' own tooltips (get_tooltip)
	tab.graph->set_visible(false);
	right->add_child(tab.graph);
	tab.timeline = memnew(FellowGpuTimeline);
	tab.timeline->set_tooltip_text(" "); // the passes' own tooltips (get_tooltip)
	right->add_child(tab.timeline);
	tab.details = memnew(TextEdit);
	tab.details->set_editable(false);
	tab.details->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tab.details->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_NONE);
	tab.details->set_placeholder(TTR("Select a hitch to see where its time went."));
	right->add_child(tab.details);

	tab.record->connect(SceneStringName(toggled), callable_mp(this, &FellowHitchDebuggerPlugin::_record_toggled).bind(p_session));
	clear->connect(SceneStringName(pressed), callable_mp(this, &FellowHitchDebuggerPlugin::_clear).bind(p_session));
	capture_frame->connect(SceneStringName(pressed), callable_mp(this, &FellowHitchDebuggerPlugin::_capture_frame).bind(p_session));
	record_frames->connect(SceneStringName(pressed), callable_mp(this, &FellowHitchDebuggerPlugin::_record_frames).bind(p_session));
	tab.graph->connect("frame_selected", callable_mp(this, &FellowHitchDebuggerPlugin::_graph_frame_selected).bind(p_session));
	tab.list->connect(SceneStringName(item_selected), callable_mp(this, &FellowHitchDebuggerPlugin::_selected).bind(p_session));
	session->connect("started", callable_mp(this, &FellowHitchDebuggerPlugin::_session_started).bind(p_session));

	tabs.insert(p_session, tab);
	_update_summary(tabs[p_session]);
	session->add_session_tab(tab.root);
}

bool FellowHitchDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == "fellow_hitch";
}

bool FellowHitchDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	if (!tab) {
		return false;
	}
	if (p_message == "fellow_hitch:frames") {
		// Decision 0244: a batch of a recording's frames [frames, the last batch].
		if (p_data.size() < 2) {
			return false;
		}
		if (!tab->recording) {
			tab->recording = tab->list->create_item(tab->list->get_root());
			tab->recording->set_text(1, TTR("Frames"));
			tab->recording->set_metadata(2, Array());
		}
		Array frames = tab->recording->get_metadata(2);
		frames.append_array(Array(p_data[0]));
		tab->recording->set_metadata(2, frames);
		double slowest = 0.0;
		double gpu_sum = 0.0;
		for (int i = 0; i < frames.size(); i++) {
			const Array frame = frames[i];
			slowest = MAX(slowest, MAX(double(frame[2]), double(frame[5])));
			gpu_sum += double(frame[5]);
		}
		if (!frames.is_empty()) {
			tab->recording->set_text(0, vformat("%.2f s", double(Array(frames[0])[1])));
		}
		tab->recording->set_text(2, vformat("%.1f ms", slowest));
		const String where = vformat(TTR("%d frames, the GPU's passes %.2f ms on average"), frames.size(), frames.is_empty() ? 0.0 : gpu_sum / frames.size());
		tab->recording->set_text(3, where);
		tab->recording->set_tooltip_text(3, where);
		tab->recording->set_metadata(0, vformat(TTR("Recorded: %s; the slowest %.2f ms (the time since the frame before, or the GPU's passes). Select a frame in the graph above."), where, slowest));
		if (bool(p_data[1])) {
			// The last batch: the recording shown, its slowest frame selected (the user asked for it).
			TreeItem *item = tab->recording;
			tab->recording = nullptr;
			item->select(0);
			tab->list->scroll_to_item(item);
			_selected(p_session);
			int slowest_index = -1;
			double slowest_ms = -1.0;
			for (int i = 0; i < frames.size(); i++) {
				const Array frame = frames[i];
				const double ms = MAX(double(frame[2]), double(frame[5]));
				if (ms > slowest_ms) {
					slowest_ms = ms;
					slowest_index = i;
				}
			}
			tab->graph->select_frame(slowest_index);
			_graph_frame_selected(slowest_index, p_session);
		}
		return true;
	}
	if (p_data.size() < 5) {
		return false;
	}
	const bool frame_capture = p_message == "fellow_hitch:frame"; // decision 0243
	const bool gpu = p_message == "fellow_hitch:gpu" || frame_capture;
	if (!gpu && p_message != "fellow_hitch:cpu") {
		return false;
	}
	// [number, seconds in, milliseconds, where, the full text]
	TreeItem *item = tab->list->create_item(tab->list->get_root());
	item->set_text(0, vformat("%.2f s", double(p_data[1])));
	item->set_text(1, frame_capture ? TTR("Frame") : (gpu ? String("GPU") : String("CPU")));
	item->set_text(2, vformat("%.1f ms", double(p_data[2])));
	item->set_text(3, String(p_data[3]));
	item->set_tooltip_text(3, String(p_data[3]));
	item->set_metadata(0, String(p_data[4]));
	if (gpu && p_data.size() >= 7) {
		Array frame;
		frame.push_back(p_data[5]); // the passes (milestone 120)
		frame.push_back(p_data[6]); // the gap before the frame
		item->set_metadata(1, frame);
	}
	if (frame_capture) {
		// Shown at once: the user asked for it.
		item->select(0);
		tab->list->scroll_to_item(item);
		_selected(p_session);
	} else if (gpu) {
		tab->gpu++;
	} else {
		tab->cpu++;
	}
	_update_summary(*tab);
	return true;
}

void FellowHitchDebuggerPlugin::_session_started(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	if (!tab) {
		return;
	}
	_clear(p_session);
	if (tab->record->is_pressed()) {
		_send_start(p_session);
	}
}

void FellowHitchDebuggerPlugin::_record_toggled(bool p_on, int p_session) {
	if (p_on) {
		_send_start(p_session);
	}
}

void FellowHitchDebuggerPlugin::_send_start(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (!tab || session.is_null() || !session->is_active()) {
		return;
	}
	Array args;
	args.push_back(tab->threshold->get_value());
	session->send_message("fellow_hitch:start", args);
}

void FellowHitchDebuggerPlugin::_capture_frame(int p_session) {
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (session.is_valid() && session->is_active()) {
		session->send_message("fellow_hitch:capture", Array());
	}
}

void FellowHitchDebuggerPlugin::_record_frames(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (tab && session.is_valid() && session->is_active() && !tab->recording) {
		Array args;
		args.push_back(tab->record_seconds->get_value());
		session->send_message("fellow_hitch:record", args);
	}
}

String FellowHitchDebuggerPlugin::_frame_text(const Array &p_frame) {
	if (p_frame.size() < 7) {
		return String();
	}
	// The passes summed by name and queue, longest first; the asynchronous queue's part.
	const Array timeline = p_frame[6];
	HashMap<String, double> passes;
	double async_first = -1.0;
	double async_last = 0.0;
	for (int i = 0; i < timeline.size(); i++) {
		const Array pass = timeline[i];
		const bool async = int(pass[1]) != 0;
		const String name = async ? String(pass[0]) + TTR(" (async queue)") : String(pass[0]);
		double *found = passes.getptr(name);
		if (found) {
			*found += double(pass[3]);
		} else {
			passes.insert(name, double(pass[3]));
		}
		if (async) {
			async_first = async_first < 0.0 ? double(pass[2]) : MIN(async_first, double(pass[2]));
			async_last = MAX(async_last, double(pass[2]) + double(pass[3]));
		}
	}
	LocalVector<Pair<double, String>> longest;
	for (const KeyValue<String, double> &kv : passes) {
		longest.push_back(Pair<double, String>(kv.value, kv.key));
	}
	for (uint32_t i = 0; i < longest.size(); i++) {
		for (uint32_t j = i + 1; j < longest.size(); j++) {
			if (longest[j].first > longest[i].first) {
				SWAP(longest[i], longest[j]);
			}
		}
	}
	const double interval = p_frame[2];
	String text = vformat(TTR("Frame %d, %.2f s in: %s; the GPU's passes %.2f ms; the CPU recorded them over %.2f ms and waited %.2f ms for the GPU before it%s. The longest passes:"),
			int64_t(p_frame[0]), double(p_frame[1]), interval >= 0.0 ? vformat(TTR("%.2f ms after the previous frame"), interval) : TTR("the first frame recorded"),
			double(p_frame[5]), double(p_frame[3]), double(p_frame[4]),
			async_first >= 0.0 ? vformat(TTR("; the asynchronous compute queue's part began %.2f ms into it and took %.2f ms"), async_first / 1000.0, (async_last - async_first) / 1000.0) : String());
	for (uint32_t i = 0; i < longest.size() && i < 16; i++) {
		text += vformat("\n    \"%s\" %.3f ms", longest[i].second, longest[i].first / 1000.0);
	}
	return text;
}

void FellowHitchDebuggerPlugin::_graph_frame_selected(int p_index, int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	TreeItem *item = tab ? tab->list->get_selected() : nullptr;
	if (!item) {
		return;
	}
	const Array frames = item->get_metadata(2);
	if (p_index < 0 || p_index >= frames.size()) {
		return;
	}
	const Array frame = frames[p_index];
	tab->timeline->set_frame(Array(frame[6]), -1.0);
	tab->details->set_text(_frame_text(frame));
}

void FellowHitchDebuggerPlugin::_clear(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	if (!tab) {
		return;
	}
	tab->list->clear();
	tab->list->create_item();
	tab->details->set_text(String());
	tab->timeline->set_frame(Array(), -1.0);
	tab->graph->set_frames(Array());
	tab->recording = nullptr;
	tab->cpu = 0;
	tab->gpu = 0;
	_update_summary(*tab);
}

void FellowHitchDebuggerPlugin::_selected(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	if (!tab) {
		return;
	}
	TreeItem *item = tab->list->get_selected();
	tab->details->set_text(item ? String(item->get_metadata(0)) : String());
	const Array frame = item ? Array(item->get_metadata(1)) : Array();
	tab->timeline->set_frame(frame.size() >= 2 ? Array(frame[0]) : Array(), frame.size() >= 2 ? double(frame[1]) : -1.0);
	// Decision 0244: a recording's frames on the graph (its first selected until another is).
	const Array frames = item ? Array(item->get_metadata(2)) : Array();
	tab->graph->set_frames(frames);
	if (!frames.is_empty()) {
		tab->graph->select_frame(0);
		_graph_frame_selected(0, p_session);
	}
}

void FellowHitchDebuggerPlugin::_update_summary(SessionTab &r_tab) {
	r_tab.summary->set_text(vformat(TTR("%d frame steps, %d GPU frames"), r_tab.cpu, r_tab.gpu));
}
