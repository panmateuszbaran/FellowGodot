/**************************************************************************/
/*  fellow_settings.cpp                                                   */
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


#include "fellow_settings.h"

#include "core/variant/dictionary.h"

void Fellow::register_settings() {
	GLOBAL_DEF("rendering/fellow/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_resolve_v2", false);
	GLOBAL_DEF("rendering/fellow/postfx/taa_history_pingpong", false);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_gamma", PROPERTY_HINT_RANGE, "0.5,4,0.01"), 1.25);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_feedback_min", PROPERTY_HINT_RANGE, "0.5,0.99,0.001"), 0.88);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_feedback_max", PROPERTY_HINT_RANGE, "0.5,0.99,0.001"), 0.9375);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_antiflicker", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.0);
	GLOBAL_DEF("rendering/fellow/fluid/enabled", true);
	GLOBAL_DEF("rendering/fellow/profiling/pass_timestamps", false);
	GLOBAL_DEF("rendering/fellow/shadows/enabled", true);
	GLOBAL_DEF("rendering/fellow/shadows/directional_cache", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_guard_texels", PROPERTY_HINT_RANGE, "16,1024,16"), 256);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_static_frames", PROPERTY_HINT_RANGE, "1,600,1"), 60);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shadows/directional_cache_lod_travel", PROPERTY_HINT_RANGE, "0,50,0.1,suffix:m"), 2.0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_max_rebuilds_per_frame", PROPERTY_HINT_RANGE, "1,16,1"), 1);
	GLOBAL_DEF("rendering/fellow/shadows/skip_backfacing_directional", false);
	GLOBAL_DEF("rendering/fellow/shadows/skip_unblended_cascade", false);
	GLOBAL_DEF("rendering/fellow/shadows/pcss_blend_band", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_debug_mode", PROPERTY_HINT_ENUM, "Normal,Lattice Only (No Cache),Rebuild Every Frame"), 0);
}

Fellow::Stats &Fellow::stats() {
	static Stats s;
	return s;
}

Dictionary Fellow::get_stats_dictionary() {
	Stats &s = stats();
	Dictionary d;
	d["shadow_cache_cascades_stock"] = s.shadow_cache_cascades_stock.get();
	d["shadow_cache_cascades_fallback"] = s.shadow_cache_cascades_fallback.get();
	d["shadow_cache_cascades_cached"] = s.shadow_cache_cascades_cached.get();
	d["shadow_cache_cascades_rebuilt"] = s.shadow_cache_cascades_rebuilt.get();
	d["shadow_cache_rebuilds_forced"] = s.shadow_cache_rebuilds_forced.get();
	d["shadow_cache_rebuilds_proactive"] = s.shadow_cache_rebuilds_proactive.get();
	d["shadow_cache_rebuilds_promotion"] = s.shadow_cache_rebuilds_promotion.get();
	d["shadow_cache_invalidations"] = s.shadow_cache_invalidations.get();
	d["shadow_cache_dynamic_casters"] = s.shadow_cache_dynamic_casters.get();
	d["shadow_cache_rebuild_casters"] = s.shadow_cache_rebuild_casters.get();
	d["shadow_cache_frames"] = s.shadow_cache_frames.get();
	return d;
}
