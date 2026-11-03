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
#include "fellow_gas_emitter_3d.h"
#include "fellow_gas_volume_3d.h"
#include "fellow_lightning_3d.h"
#include "fellow_rain_3d.h"
#include "fellow_wind_3d.h"
#include "fellow_shallow_water_3d.h"
#include "fellow_snow_tracker_3d.h"
#include "fellow_water_3d.h"
#include "fellow_weather_3d.h"

#include "core/object/class_db.h"

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
		GDREGISTER_INTERNAL_CLASS(FellowGasReadback);
	}
}

void uninitialize_fellow_fluid_module(ModuleInitializationLevel p_level) {
}
