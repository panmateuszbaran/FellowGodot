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

#include "fellow_burnable_3d.h"
#include "fellow_cloth_3d.h"
#include "fellow_gas_emitter_3d.h"
#include "fellow_gas_volume_3d.h"
#include "fellow_lightning_3d.h"
#include "fellow_rain_3d.h"
#include "fellow_shallow_water_3d.h"
#include "fellow_shared_shaders.h"
#include "fellow_snow_tracker_3d.h"
#include "fellow_water_3d.h"
#include "fellow_weather_3d.h"
#include "fellow_wind_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "main/performance.h"
#include "servers/rendering/fellow_bake_shaders.h"

// Milestone 73: the cloths' time a frame on the main thread (preparing, waiting for the worker threads, uploading) in
// the debugger's Monitors tab, averaged since the debugger's previous sample.
static const char *FELLOW_CLOTH_MONITOR = "FellowEngine/Cloth time";

static Variant _fellow_cloth_monitor() {
	static uint64_t last_frame = 0;
	static int64_t last_usec = 0;
	static double per_frame = 0.0;
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	if (frame != last_frame) {
		const int64_t usec = FellowCloth3D::get_global_stats().get("frame_usec", 0);
		per_frame = usec >= last_usec ? (usec - last_usec) / double(frame - last_frame) / 1e6 : 0.0;
		last_frame = frame;
		last_usec = usec;
	}
	return per_frame;
}

void initialize_fellow_fluid_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FellowShallowWater3D);
		GDREGISTER_CLASS(FellowWater3D);
		GDREGISTER_CLASS(FellowRain3D);
		GDREGISTER_CLASS(FellowWind3D);
		GDREGISTER_CLASS(FellowLightning3D);
		GDREGISTER_CLASS(FellowSnowTracker3D);
		GDREGISTER_CLASS(FellowWeather3D);
		GDREGISTER_CLASS(FellowGasVolume3D);
		GDREGISTER_CLASS(FellowGasEmitter3D);
		GDREGISTER_CLASS(FellowBurnable3D);
		GDREGISTER_CLASS(FellowCloth3D);
		GDREGISTER_INTERNAL_CLASS(FellowGasReadback);
		// Milestone 61: the nodes' code shaders for Godot's shader baker.
		FellowBakeShaders::register_provider(&FellowWater3D::fellow_bake_shader_codes);
		FellowBakeShaders::register_provider(&FellowRain3D::fellow_bake_shader_codes);
		FellowBakeShaders::register_provider(&FellowLightning3D::fellow_bake_shader_codes);
		FellowBakeShaders::register_provider(&FellowShallowWater3D::fellow_bake_shader_codes);
		FellowBakeShaders::register_provider(&FellowBurnable3D::fellow_bake_shader_codes);
		FellowBakeShaders::register_object_provider(&FellowBurnable3D::fellow_bake_object_codes); // milestone 63
		Performance *performance = Performance::get_singleton();
		if (performance && !performance->has_custom_monitor(FELLOW_CLOTH_MONITOR)) {
			performance->add_custom_monitor(FELLOW_CLOTH_MONITOR, callable_mp_static(&_fellow_cloth_monitor), Vector<Variant>(), Performance::MONITOR_TYPE_TIME);
		}
	}
}

void uninitialize_fellow_fluid_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		FellowSharedShaders::clear(); // milestone 53: while the RenderingServer lives
		Performance *performance = Performance::get_singleton();
		if (performance && performance->has_custom_monitor(FELLOW_CLOTH_MONITOR)) {
			performance->remove_custom_monitor(FELLOW_CLOTH_MONITOR);
		}
	}
}
