/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "fellow_hlod_3d.h"
#include "fellow_world_streamer_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "main/performance.h"
#include "servers/rendering/rendering_server.h"

// Milestone 50 (tools): the streaming systems in the debugger's Monitors tab, under "FellowEngine" (and to
// Performance.get_custom_monitor() in a game). Read when the debugger samples them, about once a second.
static const char *FELLOW_WORLD_MONITORS[] = {
	"FellowEngine/Texture streaming resident",
	"FellowEngine/Texture streaming wanted",
	"FellowEngine/Texture streaming pending",
	"FellowEngine/World cells loaded",
	"FellowEngine/World cells loading",
};

static Variant _fellow_world_monitor(int p_which) {
	if (p_which <= 2) {
		RenderingServer *rs = RenderingServer::get_singleton();
		if (!rs) {
			return 0;
		}
		const Dictionary d = rs->fellow_get_texture_streaming_info();
		static const char *keys[3] = { "resident_bytes", "wanted_bytes", "pending" };
		return d.get(keys[p_which], 0);
	}
	int cells, loaded, loading;
	FellowWorldStreamer3D::get_totals(cells, loaded, loading);
	return p_which == 3 ? loaded : loading;
}

void initialize_fellow_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FellowHLOD3D);
		GDREGISTER_CLASS(FellowStreamingCell3D);
		GDREGISTER_CLASS(FellowWorldStreamer3D);

		Performance *performance = Performance::get_singleton();
		if (performance) {
			static const Performance::MonitorType types[5] = { Performance::MONITOR_TYPE_MEMORY, Performance::MONITOR_TYPE_MEMORY, Performance::MONITOR_TYPE_QUANTITY, Performance::MONITOR_TYPE_QUANTITY, Performance::MONITOR_TYPE_QUANTITY };
			for (int i = 0; i < 5; i++) {
				if (!performance->has_custom_monitor(FELLOW_WORLD_MONITORS[i])) {
					performance->add_custom_monitor(FELLOW_WORLD_MONITORS[i], callable_mp_static(&_fellow_world_monitor).bind(i), Vector<Variant>(), types[i]);
				}
			}
		}
	}
}

void uninitialize_fellow_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		Performance *performance = Performance::get_singleton();
		if (performance) {
			for (const char *id : FELLOW_WORLD_MONITORS) {
				if (performance->has_custom_monitor(id)) {
					performance->remove_custom_monitor(id);
				}
			}
		}
	}
}
