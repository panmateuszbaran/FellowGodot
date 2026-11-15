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
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/scene_string_names.h"

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
	tab.details = memnew(TextEdit);
	tab.details->set_editable(false);
	tab.details->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tab.details->set_line_wrapping_mode(TextEdit::LINE_WRAPPING_NONE);
	tab.details->set_placeholder(TTR("Select a hitch to see where its time went."));
	split->add_child(tab.details);

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
}

void FellowHitchDebuggerPlugin::_update_summary(SessionTab &r_tab) {
	r_tab.summary->set_text(vformat(TTR("%d frame steps, %d GPU frames"), r_tab.cpu, r_tab.gpu));
}
