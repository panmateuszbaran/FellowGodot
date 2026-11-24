/**************************************************************************/
/*  fellow_weathering_3d.cpp                                              */
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
#include "fellow_weathering_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/main/viewport.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/rendering_server.h"

FellowWeathering3D *FellowWeathering3D::active = nullptr;

static GPUParticlesCollisionHeightField3D::Resolution weathering_resolution(int p_res) {
	switch (p_res) {
		case 256:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_256;
		case 512:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_512;
		case 1024:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_1024;
		case 4096:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_4096;
		case 8192:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_8192;
		default:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_2048;
	}
}

void FellowWeathering3D::_build_layers() {
	if (layers[0]) {
		return;
	}
	for (int i = 0; i < MAX_LAYERS; i++) {
		GPUParticlesCollisionHeightField3D *hf = memnew(GPUParticlesCollisionHeightField3D);
		hf->set_name(vformat("WeatheringLayer%d", i));
		hf->set_as_top_level(true);
		// Seen by no particles (the weathering's, not a collider: GPUParticles3D would bounce off it).
		hf->set_cull_mask(0);
		add_child(hf, false, INTERNAL_MODE_FRONT);
		layers[i] = hf;
	}
	_update_layers();
}

void FellowWeathering3D::_update_layers() {
	if (!layers[0]) {
		return;
	}
	const float ov = _layer_overlap();
	for (int i = 0; i < MAX_LAYERS; i++) {
		GPUParticlesCollisionHeightField3D *hf = layers[i];
		const bool used = i < heightfield_layers;
		hf->set_visible(used);
		// Each layer's box: its height and an overlap below it (a floor on the boundary is seen by the layer above).
		hf->set_size(Vector3(heightfield_size, heightfield_layer_height + ov, heightfield_size));
		hf->set_resolution(weathering_resolution(heightfield_resolution));
		hf->set_heightfield_mask(cull_mask);
		// Moved with the camera by this node in whole sixteenths of its size (whole texels: the ledges' edges do not
		// shift between renders), as FellowRain3D moves its shelter.
		hf->set_follow_camera_enabled(false);
		hf->set_update_mode(heightfield_always_update ? GPUParticlesCollisionHeightField3D::UPDATE_MODE_ALWAYS : GPUParticlesCollisionHeightField3D::UPDATE_MODE_WHEN_MOVED);
	}
	if (is_inside_tree()) {
		_follow_camera(true);
		for (int i = 0; i < heightfield_layers; i++) {
			RS::get_singleton()->particles_collision_height_field_update(layers[i]->get_base()); // drawn again
		}
	}
}

bool FellowWeathering3D::_place_layer(int p_layer, const Vector2 &p_xz) {
	const float ov = _layer_overlap();
	const float base = get_global_position().y;
	const Vector3 at(p_xz.x, base + p_layer * heightfield_layer_height + 0.5f * (heightfield_layer_height - ov), p_xz.y);
	GPUParticlesCollisionHeightField3D *hf = layers[p_layer];
	if (hf->get_global_transform().origin.is_equal_approx(at) && hf->get_global_basis() == Basis()) {
		return false;
	}
	hf->set_global_transform(Transform3D(Basis(), at));
	return true;
}

void FellowWeathering3D::_follow_camera(bool p_all) {
	if (!layers[0]) {
		return;
	}
	Vector2 at(get_global_position().x, get_global_position().z);
	Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
	if (cam) {
		const Vector3 c = cam->get_global_position();
		const float snap = heightfield_size / 16.0f;
		at.x = Math::floor(c.x / snap + 0.5f) * snap;
		at.y = Math::floor(c.z / snap + 0.5f) * snap;
	}
	// One layer a frame while following the camera (each is drawn again where it moves: all of them in one frame, a
	// depth pass each over the town, would hitch); each keeps its own centre meanwhile. All at once when the camera
	// has jumped (a cut: a quarter of the layers' width away).
	for (int i = 0; i < heightfield_layers && !p_all; i++) {
		const Vector3 c = layers[i]->get_global_position();
		p_all = Vector2(c.x, c.z).distance_to(at) > heightfield_size * 0.25f;
	}
	for (int i = 0; i < heightfield_layers; i++) {
		if (_place_layer(i, at) && !p_all) {
			break;
		}
	}
}

void FellowWeathering3D::_push_state(bool p_enabled) {
	RendererRD::FellowWeather::WeatheringState st;
	st.enabled = p_enabled;
	st.strength = strength;
	st.streaks = streaks;
	st.streak_length = streak_length;
	st.damp = damp;
	st.damp_height = damp_height;
	st.patina = patina;
	st.seed = seed;
	st.stain_color = stain_color;
	st.damp_color = damp_color;
	st.cull_mask = cull_mask;
	if (layers[0] && layers[0]->is_inside_tree()) {
		st.layer_count = uint32_t(CLAMP(heightfield_layers, 1, MAX_LAYERS));
		for (uint32_t i = 0; i < st.layer_count; i++) {
			st.layers[i] = layers[i]->get_base();
			const Vector3 at = layers[i]->get_global_position();
			st.layer_center[i][0] = at.x;
			st.layer_center[i][1] = at.z;
		}
		st.base = get_global_position().y;
		st.size_x = heightfield_size;
		st.size_z = heightfield_size;
		st.layer_height = heightfield_layer_height;
		st.layer_overlap = _layer_overlap();
		st.resolution = uint32_t(heightfield_resolution);
	}
	PackedByteArray state;
	state.resize(sizeof(st));
	memcpy(state.ptrw(), &st, sizeof(st));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowWeathering3D::_rt_set).bind(state));
}

void FellowWeathering3D::_rt_set(PackedByteArray p_state) {
	if (p_state.size() != int64_t(sizeof(RendererRD::FellowWeather::WeatheringState))) {
		return;
	}
	RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton();
	if (!w) {
		return;
	}
	RendererRD::FellowWeather::WeatheringState st;
	memcpy(&st, p_state.ptr(), sizeof(st));
	if (st.enabled) {
		w->fellow_note_wetting_node(); // decision 0161: materials keep the weather's code from now on
	}
	w->set_weathering_state(st);
}

PackedStringArray FellowWeathering3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (is_inside_tree() && active != this) {
		warnings.push_back(RTR("Another FellowWeathering3D weathers the scene: one works at a time (the last to enter the tree)."));
	}
	if (cull_mask == 0) {
		warnings.push_back(RTR("The cull mask has no layer: nothing is weathered."));
	}
	return warnings;
}

void FellowWeathering3D::set_cull_mask(uint32_t p_mask) {
	cull_mask = p_mask & 0xFFFFFu;
	_update_layers(); // the layers see the weathered instances only
	update_configuration_warnings();
}

void FellowWeathering3D::set_heightfield_size(float p_size) {
	heightfield_size = MAX(1.0f, p_size);
	_update_layers();
}

void FellowWeathering3D::set_heightfield_layers(int p_layers) {
	heightfield_layers = CLAMP(p_layers, 1, MAX_LAYERS);
	_update_layers();
}

void FellowWeathering3D::set_heightfield_layer_height(float p_height) {
	heightfield_layer_height = MAX(0.25f, p_height);
	_update_layers();
}

void FellowWeathering3D::set_heightfield_resolution(int p_resolution) {
	heightfield_resolution = p_resolution;
	_update_layers();
}

void FellowWeathering3D::set_heightfield_always_update(bool p_always) {
	heightfield_always_update = p_always;
	_update_layers();
}

void FellowWeathering3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (active && active != this) {
				FellowWeathering3D *previous = active;
				active = this;
				previous->update_configuration_warnings(); // no longer the one
			}
			active = this;
			layers_placed = false;
			_build_layers();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (active == this) {
				_follow_camera(!layers_placed); // all at once the first time
				layers_placed = true;
				_push_state(is_visible_in_tree() && Fellow::fluid_enabled());
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (active == this) {
				active = nullptr;
				_push_state(false);
			}
		} break;
	}
}

void FellowWeathering3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &FellowWeathering3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &FellowWeathering3D::get_strength);
	ClassDB::bind_method(D_METHOD("set_streaks", "amount"), &FellowWeathering3D::set_streaks);
	ClassDB::bind_method(D_METHOD("get_streaks"), &FellowWeathering3D::get_streaks);
	ClassDB::bind_method(D_METHOD("set_streak_length", "metres"), &FellowWeathering3D::set_streak_length);
	ClassDB::bind_method(D_METHOD("get_streak_length"), &FellowWeathering3D::get_streak_length);
	ClassDB::bind_method(D_METHOD("set_stain_color", "color"), &FellowWeathering3D::set_stain_color);
	ClassDB::bind_method(D_METHOD("get_stain_color"), &FellowWeathering3D::get_stain_color);
	ClassDB::bind_method(D_METHOD("set_damp", "amount"), &FellowWeathering3D::set_damp);
	ClassDB::bind_method(D_METHOD("get_damp"), &FellowWeathering3D::get_damp);
	ClassDB::bind_method(D_METHOD("set_damp_height", "metres"), &FellowWeathering3D::set_damp_height);
	ClassDB::bind_method(D_METHOD("get_damp_height"), &FellowWeathering3D::get_damp_height);
	ClassDB::bind_method(D_METHOD("set_damp_color", "color"), &FellowWeathering3D::set_damp_color);
	ClassDB::bind_method(D_METHOD("get_damp_color"), &FellowWeathering3D::get_damp_color);
	ClassDB::bind_method(D_METHOD("set_patina", "amount"), &FellowWeathering3D::set_patina);
	ClassDB::bind_method(D_METHOD("get_patina"), &FellowWeathering3D::get_patina);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowWeathering3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowWeathering3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_cull_mask", "mask"), &FellowWeathering3D::set_cull_mask);
	ClassDB::bind_method(D_METHOD("get_cull_mask"), &FellowWeathering3D::get_cull_mask);
	ClassDB::bind_method(D_METHOD("set_heightfield_size", "metres"), &FellowWeathering3D::set_heightfield_size);
	ClassDB::bind_method(D_METHOD("get_heightfield_size"), &FellowWeathering3D::get_heightfield_size);
	ClassDB::bind_method(D_METHOD("set_heightfield_layers", "layers"), &FellowWeathering3D::set_heightfield_layers);
	ClassDB::bind_method(D_METHOD("get_heightfield_layers"), &FellowWeathering3D::get_heightfield_layers);
	ClassDB::bind_method(D_METHOD("set_heightfield_layer_height", "metres"), &FellowWeathering3D::set_heightfield_layer_height);
	ClassDB::bind_method(D_METHOD("get_heightfield_layer_height"), &FellowWeathering3D::get_heightfield_layer_height);
	ClassDB::bind_method(D_METHOD("set_heightfield_resolution", "texels"), &FellowWeathering3D::set_heightfield_resolution);
	ClassDB::bind_method(D_METHOD("get_heightfield_resolution"), &FellowWeathering3D::get_heightfield_resolution);
	ClassDB::bind_method(D_METHOD("set_heightfield_always_update", "always"), &FellowWeathering3D::set_heightfield_always_update);
	ClassDB::bind_method(D_METHOD("is_heightfield_always_update"), &FellowWeathering3D::is_heightfield_always_update);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
	ADD_GROUP("Streaks", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streaks", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_streaks", "get_streaks");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streak_length", PROPERTY_HINT_RANGE, "0.05,10,0.01,or_greater,suffix:m"), "set_streak_length", "get_streak_length");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "stain_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_stain_color", "get_stain_color");
	ADD_GROUP("Damp", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damp", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_damp", "get_damp");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damp_height", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater,suffix:m"), "set_damp_height", "get_damp_height");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "damp_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_damp_color", "get_damp_color");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "patina", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_patina", "get_patina");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_cull_mask", "get_cull_mask");
	ADD_GROUP("Heightfield", "heightfield_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heightfield_size", PROPERTY_HINT_RANGE, "1,1024,0.1,or_greater,suffix:m"), "set_heightfield_size", "get_heightfield_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "heightfield_layers", PROPERTY_HINT_RANGE, "1,16,1"), "set_heightfield_layers", "get_heightfield_layers");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heightfield_layer_height", PROPERTY_HINT_RANGE, "0.25,20,0.01,or_greater,suffix:m"), "set_heightfield_layer_height", "get_heightfield_layer_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "heightfield_resolution", PROPERTY_HINT_ENUM, "256:256,512:512,1024:1024,2048:2048,4096:4096,8192:8192"), "set_heightfield_resolution", "get_heightfield_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "heightfield_always_update"), "set_heightfield_always_update", "is_heightfield_always_update");
}

FellowWeathering3D::FellowWeathering3D() {
}

FellowWeathering3D::~FellowWeathering3D() {
	if (active == this) {
		active = nullptr;
	}
}
