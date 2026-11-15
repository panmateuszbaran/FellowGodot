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
		// Grey at or below its average, to red at 1 ms or more past it (orange if it never ran in an ordinary frame).
		Color c(0.45, 0.55, 0.65);
		if (average < 0.0) {
			c = Color(0.9, 0.6, 0.2);
		} else if (length > average) {
			c = c.lerp(Color(0.9, 0.2, 0.15), float(CLAMP((length - average) / 1000.0, 0.0, 1.0)));
		}
		draw_rect(r, c);
		const String name = pass[0];
		if (r.size.x > 40 * EDSCALE) {
			draw_string(font, Point2(r.position.x + 2 * EDSCALE, r.position.y + r.size.y * 0.75), name, HORIZONTAL_ALIGNMENT_LEFT, r.size.x - 4 * EDSCALE, font_size * 0.8, Color(1, 1, 1));
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
	tab.list->set_column_custom_minimum_width(1, 50 * EDSCALE);
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
	if (!tab || p_data.size() < 5) {
		return false;
	}
	const bool gpu = p_message == "fellow_hitch:gpu";
	if (!gpu && p_message != "fellow_hitch:cpu") {
		return false;
	}
	// [number, seconds in, milliseconds, where, the full text]
	TreeItem *item = tab->list->create_item(tab->list->get_root());
	item->set_text(0, vformat("%.2f s", double(p_data[1])));
	item->set_text(1, gpu ? String("GPU") : String("CPU"));
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
	if (gpu) {
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

void FellowHitchDebuggerPlugin::_clear(int p_session) {
	SessionTab *tab = tabs.getptr(p_session);
	if (!tab) {
		return;
	}
	tab->list->clear();
	tab->list->create_item();
	tab->details->set_text(String());
	tab->timeline->set_frame(Array(), -1.0);
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
}

void FellowHitchDebuggerPlugin::_update_summary(SessionTab &r_tab) {
	r_tab.summary->set_text(vformat(TTR("%d frame steps, %d GPU frames"), r_tab.cpu, r_tab.gpu));
}
