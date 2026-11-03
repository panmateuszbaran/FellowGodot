/**************************************************************************/
/*  fellow_weather_3d.cpp                                                 */
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

#include "fellow_weather_3d.h"

#include "fellow_lightning_3d.h"
#include "fellow_rain_3d.h"
#include "fellow_wind_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/world_environment.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/rendering_server.h"

FellowWeather3D *FellowWeather3D::active = nullptr;

// ------------------------------------------------------------------ the weathers

FellowWeather3D::State FellowWeather3D::_preset(Weather p_weather) {
	State s;
	switch (p_weather) {
		case WEATHER_CLEAR:
		case WEATHER_CUSTOM: {
			s.wind_speed = 2.0f;
			s.gustiness = 0.3f;
			s.clouds = 0.15f;
			s.temperature = 18.0f;
		} break;
		case WEATHER_CLOUDY: {
			s.wind_speed = 4.0f;
			s.gustiness = 0.4f;
			s.clouds = 0.5f;
			s.sunlight = 0.75f;
			s.temperature = 15.0f;
		} break;
		case WEATHER_OVERCAST: {
			s.wind_speed = 5.0f;
			s.gustiness = 0.45f;
			s.clouds = 0.9f;
			s.cloud_density = 1.4f;
			s.sunlight = 0.2f;
			s.ambient = 0.8f;
			s.fog = 0.0004f;
			s.temperature = 12.0f;
		} break;
		case WEATHER_RAIN: {
			s.wind_speed = 6.0f;
			s.gustiness = 0.5f;
			s.precipitation = PRECIPITATION_RAIN;
			s.intensity = 0.6f;
			s.clouds = 0.97f;
			s.cloud_density = 1.8f;
			s.sunlight = 0.1f;
			s.ambient = 0.7f;
			s.fog = 0.0012f;
			s.temperature = 10.0f;
		} break;
		case WEATHER_STORM: {
			s.wind_speed = 12.0f;
			s.gustiness = 0.7f;
			s.precipitation = PRECIPITATION_RAIN;
			s.intensity = 1.0f;
			s.lightning = 5.0f;
			s.clouds = 1.0f;
			s.cloud_density = 2.6f;
			s.sunlight = 0.05f;
			s.ambient = 0.55f;
			s.fog = 0.0025f;
			s.temperature = 14.0f;
		} break;
		case WEATHER_SNOW: {
			s.wind_speed = 3.0f;
			s.gustiness = 0.35f;
			s.precipitation = PRECIPITATION_SNOW;
			s.intensity = 0.6f;
			s.clouds = 0.95f;
			s.cloud_density = 1.5f;
			s.sunlight = 0.12f;
			s.ambient = 0.85f;
			s.fog = 0.0015f;
			s.temperature = -3.0f;
		} break;
		case WEATHER_BLIZZARD: {
			s.wind_speed = 14.0f;
			s.gustiness = 0.65f;
			s.precipitation = PRECIPITATION_SNOW;
			s.intensity = 1.0f;
			s.clouds = 1.0f;
			s.cloud_density = 2.2f;
			s.sunlight = 0.06f;
			s.ambient = 0.7f;
			s.fog = 0.01f;
			s.temperature = -10.0f;
		} break;
	}
	return s;
}

// From one state to another at t (0..1, eased). Rain turning to snow (or back) eases off to nothing over the first half
// and the other sets in over the second.
FellowWeather3D::State FellowWeather3D::_blend(const State &p_from, const State &p_to, float p_t) {
	const float t = CLAMP(p_t, 0.0f, 1.0f);
	const float e = t * t * (3.0f - 2.0f * t);
	State s;
	s.wind_speed = Math::lerp(p_from.wind_speed, p_to.wind_speed, e);
	s.gustiness = Math::lerp(p_from.gustiness, p_to.gustiness, e);
	s.lightning = Math::lerp(p_from.lightning, p_to.lightning, e);
	s.clouds = Math::lerp(p_from.clouds, p_to.clouds, e);
	s.cloud_density = Math::lerp(p_from.cloud_density, p_to.cloud_density, e);
	s.sunlight = Math::lerp(p_from.sunlight, p_to.sunlight, e);
	s.ambient = Math::lerp(p_from.ambient, p_to.ambient, e);
	s.fog = Math::lerp(p_from.fog, p_to.fog, e);
	s.temperature = Math::lerp(p_from.temperature, p_to.temperature, e);

	const float a = p_from.precipitation == PRECIPITATION_NONE ? 0.0f : p_from.intensity;
	const float b = p_to.precipitation == PRECIPITATION_NONE ? 0.0f : p_to.intensity;
	if (p_from.precipitation != PRECIPITATION_NONE && p_to.precipitation != PRECIPITATION_NONE && p_from.precipitation != p_to.precipitation && a > 0.0f) {
		if (t < 0.5f) {
			const float h = t * 2.0f;
			s.precipitation = p_from.precipitation;
			s.intensity = a * (1.0f - h * h * (3.0f - 2.0f * h));
		} else {
			const float h = t * 2.0f - 1.0f;
			s.precipitation = p_to.precipitation;
			s.intensity = b * h * h * (3.0f - 2.0f * h);
		}
	} else {
		s.precipitation = p_to.precipitation != PRECIPITATION_NONE ? p_to.precipitation : p_from.precipitation;
		s.intensity = Math::lerp(a, b, e);
		if (t >= 1.0f) {
			s.precipitation = p_to.precipitation;
		}
	}
	if (s.intensity <= 0.0f && t >= 1.0f) {
		s.precipitation = p_to.precipitation;
	}
	return s;
}

void FellowWeather3D::_begin_transition(double p_seconds) {
	if (!started || p_seconds <= 0.0) {
		current = target;
		from = target;
		transitioning = false;
		transition_elapsed = 0.0;
		transition_length = 0.0;
		return;
	}
	from = current;
	transition_length = p_seconds;
	transition_elapsed = 0.0;
	transitioning = true;
}

void FellowWeather3D::set_weather(Weather p_weather, float p_transition) {
	ERR_FAIL_INDEX(int(p_weather), int(WEATHER_CUSTOM) + 1);
	weather = p_weather;
	if (weather != WEATHER_CUSTOM) {
		target = _preset(weather);
	}
	_begin_transition(p_transition < 0.0f ? double(transition_time) : double(p_transition));
	notify_property_list_changed();
	if (started) {
		emit_signal(SNAME("weather_changed"), int(weather));
	}
}

// A target value set on its own: the weather becomes a custom one (unless it is the preset's value) and turns to it.
void FellowWeather3D::_target_changed() {
	if (weather != WEATHER_CUSTOM) {
		const State p = _preset(weather);
		if (p.wind_speed != target.wind_speed || p.gustiness != target.gustiness || p.precipitation != target.precipitation ||
				p.intensity != target.intensity || p.lightning != target.lightning || p.clouds != target.clouds ||
				p.cloud_density != target.cloud_density || p.sunlight != target.sunlight || p.ambient != target.ambient ||
				p.fog != target.fog || p.temperature != target.temperature) {
			weather = WEATHER_CUSTOM;
			notify_property_list_changed();
		}
	}
	_begin_transition(transition_time);
}

#define WEATHER_TARGET_SETTER(m_func, m_type, m_member, m_expr) \
	void FellowWeather3D::m_func(m_type p_value) { \
		if (target.m_member == (m_expr)) { \
			return; \
		} \
		target.m_member = (m_expr); \
		_target_changed(); \
	}

WEATHER_TARGET_SETTER(set_wind_speed, float, wind_speed, MAX(0.0f, p_value))
WEATHER_TARGET_SETTER(set_gustiness, float, gustiness, CLAMP(p_value, 0.0f, 1.0f))
WEATHER_TARGET_SETTER(set_precipitation, Precipitation, precipitation, p_value)
WEATHER_TARGET_SETTER(set_intensity, float, intensity, CLAMP(p_value, 0.0f, 1.0f))
WEATHER_TARGET_SETTER(set_lightning_rate, float, lightning, MAX(0.0f, p_value))
WEATHER_TARGET_SETTER(set_cloud_coverage, float, clouds, CLAMP(p_value, 0.0f, 1.0f))
WEATHER_TARGET_SETTER(set_cloud_density, float, cloud_density, MAX(0.0f, p_value))
WEATHER_TARGET_SETTER(set_sunlight, float, sunlight, CLAMP(p_value, 0.0f, 1.0f))
WEATHER_TARGET_SETTER(set_ambient_light, float, ambient, MAX(0.0f, p_value))
WEATHER_TARGET_SETTER(set_fog_density, float, fog, MAX(0.0f, p_value))
WEATHER_TARGET_SETTER(set_temperature, float, temperature, CLAMP(p_value, -60.0f, 60.0f))

float FellowWeather3D::get_transition_progress() const {
	if (!transitioning || transition_length <= 0.0) {
		return 1.0f;
	}
	return float(CLAMP(transition_elapsed / transition_length, 0.0, 1.0));
}

static Dictionary weather_state_dict(const FellowWeather3D::State &p_s) {
	Dictionary d;
	d["wind_speed"] = p_s.wind_speed;
	d["gustiness"] = p_s.gustiness;
	d["precipitation"] = int(p_s.precipitation);
	d["intensity"] = p_s.intensity;
	d["lightning_rate"] = p_s.lightning;
	d["cloud_coverage"] = p_s.clouds;
	d["cloud_density"] = p_s.cloud_density;
	d["sunlight"] = p_s.sunlight;
	d["ambient_light"] = p_s.ambient;
	d["fog_density"] = p_s.fog;
	d["temperature"] = p_s.temperature;
	return d;
}

Dictionary FellowWeather3D::get_current_state() const {
	return weather_state_dict(current);
}

Dictionary FellowWeather3D::get_preset_state(Weather p_weather) {
	return weather_state_dict(_preset(p_weather));
}

void FellowWeather3D::set_controls(BitField<Control> p_controls) {
	const uint32_t was = controls;
	controls = uint32_t(int64_t(p_controls)) & CONTROL_ALL;
	if ((was & (CONTROL_FOG | CONTROL_AMBIENT)) != (controls & (CONTROL_FOG | CONTROL_AMBIENT))) {
		_restore_environment(); // taken again with what is still driven
	}
}

// ------------------------------------------------------------------ the scene's nodes

Node *FellowWeather3D::_scene_root() const {
	if (!is_inside_tree()) {
		return nullptr;
	}
	Node *root = get_tree()->get_current_scene();
	if (!root) {
		root = get_tree()->get_root();
	}
	return root;
}

#define WEATHER_PATH_SETTER(m_func, m_member, m_id) \
	void FellowWeather3D::m_func(const NodePath &p_path) { \
		m_member = p_path; \
		m_id = ObjectID(); \
	}

WEATHER_PATH_SETTER(set_wind_path, wind_path, wind_id)
WEATHER_PATH_SETTER(set_rain_path, rain_path, rain_id)
WEATHER_PATH_SETTER(set_lightning_path, lightning_path, lightning_id)

void FellowWeather3D::set_sun_path(const NodePath &p_path) {
	sun_path = p_path;
	sun_id = ObjectID();
	sun_searched = false;
}

void FellowWeather3D::set_environment_path(const NodePath &p_path) {
	environment_path = p_path;
	_restore_environment();
}

FellowWind3D *FellowWeather3D::get_wind_node() const {
	return ObjectDB::get_instance<FellowWind3D>(wind_id);
}

FellowRain3D *FellowWeather3D::get_rain_node() const {
	return ObjectDB::get_instance<FellowRain3D>(rain_id);
}

FellowLightning3D *FellowWeather3D::get_lightning_node() const {
	return ObjectDB::get_instance<FellowLightning3D>(lightning_id);
}

DirectionalLight3D *FellowWeather3D::get_sun_node() const {
	return ObjectDB::get_instance<DirectionalLight3D>(sun_id);
}

FellowWind3D *FellowWeather3D::_get_wind(bool p_create) {
	FellowWind3D *w = get_wind_node();
	if (w && w->is_inside_tree()) {
		return w;
	}
	w = nullptr;
	if (!wind_path.is_empty()) {
		w = Object::cast_to<FellowWind3D>(get_node_or_null(wind_path));
	}
	if (!w) {
		w = FellowWind3D::get_active();
	}
	if (!w && p_create) {
		w = memnew(FellowWind3D);
		w->set_name("Wind");
		add_child(w, false, INTERNAL_MODE_BACK);
	}
	wind_id = w ? w->get_instance_id() : ObjectID();
	return w;
}

FellowRain3D *FellowWeather3D::_get_rain(bool p_create) {
	FellowRain3D *r = get_rain_node();
	if (r && r->is_inside_tree()) {
		return r;
	}
	r = nullptr;
	if (!rain_path.is_empty()) {
		r = Object::cast_to<FellowRain3D>(get_node_or_null(rain_path));
	}
	if (!r) {
		r = FellowRain3D::get_active();
	}
	if (!r && p_create) {
		r = memnew(FellowRain3D);
		r->set_name("Rain");
		// Dry, without snow, not raining yet: the weather sets it in.
		r->set_rain(0.0f);
		r->set_wetness(0.0f);
		r->set_snow(0.0f);
		add_child(r, false, INTERNAL_MODE_BACK);
		rain_created = r->get_instance_id();
	}
	if (r && r->get_instance_id() != rain_id) {
		rain_owned = false;
	}
	rain_id = r ? r->get_instance_id() : ObjectID();
	return r;
}

FellowLightning3D *FellowWeather3D::_get_lightning(bool p_create) {
	FellowLightning3D *l = get_lightning_node();
	if (l && l->is_inside_tree()) {
		return l;
	}
	l = nullptr;
	if (!lightning_path.is_empty()) {
		l = Object::cast_to<FellowLightning3D>(get_node_or_null(lightning_path));
	}
	if (!l && !lightning_searched) {
		// Searched for once (and again when the weather would create one).
		lightning_searched = !p_create;
		if (Node *root = _scene_root()) {
			TypedArray<Node> found = root->find_children("*", "FellowLightning3D", true, false);
			if (found.size() > 0) {
				l = Object::cast_to<FellowLightning3D>(found[0]);
			}
		}
	}
	if (!l && p_create) {
		l = memnew(FellowLightning3D);
		l->set_name("Lightning");
		l->set_strikes_per_minute(0.0f);
		add_child(l, false, INTERNAL_MODE_BACK);
	}
	lightning_id = l ? l->get_instance_id() : ObjectID();
	return l;
}

DirectionalLight3D *FellowWeather3D::_get_sun() {
	DirectionalLight3D *s = get_sun_node();
	if (s && s->is_inside_tree()) {
		return s;
	}
	s = nullptr;
	if (!sun_path.is_empty()) {
		s = Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_path));
	} else if (!sun_searched) {
		// The scene's first directional light that lights it (not a lightning's flash).
		sun_searched = true;
		if (Node *root = _scene_root()) {
			TypedArray<Node> found = root->find_children("*", "DirectionalLight3D", true, false);
			for (int i = 0; i < found.size(); i++) {
				DirectionalLight3D *d = Object::cast_to<DirectionalLight3D>(found[i]);
				if (d && !d->has_meta(SNAME("_fellow_lightning")) && d->get_sky_mode() != DirectionalLight3D::SKY_MODE_SKY_ONLY && d->is_visible_in_tree()) {
					s = d;
					break;
				}
			}
		}
	}
	sun_id = s ? s->get_instance_id() : ObjectID();
	return s;
}

Ref<Environment> FellowWeather3D::_find_environment() const {
	if (!environment_path.is_empty()) {
		WorldEnvironment *we = Object::cast_to<WorldEnvironment>(get_node_or_null(environment_path));
		return we ? we->get_environment() : Ref<Environment>();
	}
	Viewport *vp = get_viewport();
	if (!vp) {
		return Ref<Environment>();
	}
	if (Camera3D *cam = vp->get_camera_3d()) {
		if (cam->get_environment().is_valid()) {
			return cam->get_environment();
		}
	}
	Ref<World3D> world = vp->find_world_3d();
	return world.is_valid() ? world->get_environment() : Ref<Environment>();
}

// ------------------------------------------------------------------ applying it

void FellowWeather3D::_apply_ground(FellowRain3D *p_rain, double p_delta) {
	if (!rain_owned) {
		// The weather wets and covers the ground itself.
		p_rain->set_wetness_follows_rain(false);
		p_rain->set_snow_follows_precipitation(false);
		rain_owned = true;
	}
	const float dt = float(p_delta) * ground_speed;
	if (dt <= 0.0f) {
		return;
	}
	// What falls here: under showers (decision 0137) as the clouds above let it.
	const float rain = current.precipitation == PRECIPITATION_RAIN ? current.intensity * p_rain->get_rain_here() : 0.0f;
	const float snowfall = current.precipitation == PRECIPITATION_SNOW ? current.intensity * p_rain->get_rain_here() : 0.0f;
	const float t = current.temperature;
	float wet = p_rain->get_wetness();
	float snow = p_rain->get_snow();
	// Snow settles where it is cold enough and melts above freezing: in melting_time at 5 degrees, three times as fast
	// in the rain.
	if (snowfall > 0.0f && t <= 1.0f) {
		snow = MIN(1.0f, snow + dt * snowfall / MAX(p_rain->get_snowing_time(), 0.01f));
	}
	float melt = 0.0f;
	if (t > 0.0f && snow > 0.0f) {
		melt = MIN(snow, dt * (t / 5.0f) * (1.0f + 2.0f * rain) / MAX(p_rain->get_melting_time(), 0.01f));
		snow -= melt;
	}
	// The rain wets the ground, melting snow too; it dries when neither does, slowly below freezing.
	if (rain > 0.0f) {
		wet = MIN(1.0f, wet + dt * rain / MAX(p_rain->get_wetting_time(), 0.01f));
	}
	if (melt > 0.0f) {
		wet = MIN(1.0f, wet + melt * 3.0f);
	}
	if (rain <= 0.0f && melt <= 0.0f) {
		wet = MAX(0.0f, wet - dt * (t > 0.0f ? 1.0f : 0.25f) / MAX(p_rain->get_drying_time(), 0.01f));
	}
	// Ice (decision 0136): below freezing the water on the ground freezes, in freezing_time at 5 degrees below (rain
	// falling below freezing glazes it as it lands); above freezing it thaws twice as fast. Hoarfrost forms on what
	// faces the sky in the cold when nothing falls — in freezing_time at 5 degrees below under a clear sky, a fifth as
	// fast under a full deck (the clouds keep the ground warm) — and goes above freezing, at once in the rain.
	const float ft = MAX(p_rain->get_freezing_time(), 0.01f);
	float freeze = p_rain->get_freeze();
	float frost = p_rain->get_frost();
	if (t < 0.0f) {
		freeze = MIN(1.0f, freeze + dt * MIN(-t / 5.0f, 2.0f) / ft);
		const float falling = rain + snowfall;
		frost = MIN(1.0f, frost + dt * MIN(-t / 5.0f, 2.0f) * (1.0f - 0.8f * current.clouds) * (1.0f - falling) / ft);
	} else {
		freeze = MAX(0.0f, freeze - dt * 2.0f * MAX(t / 5.0f, 0.2f) / ft);
		frost = MAX(0.0f, frost - dt * (4.0f * MAX(t / 5.0f, 0.2f) + 20.0f * rain) / ft);
	}
	p_rain->set_wetness(wet);
	p_rain->set_snow(snow);
	p_rain->set_freeze(freeze);
	p_rain->set_frost(frost);
}

void FellowWeather3D::_restore_environment() {
	if (env.is_valid() && env_captured) {
		if (env->get_fog_density() == env_applied.fog_density) {
			env->set_fog_density(env_base.fog_density);
		}
		if (env->is_fog_enabled() == env_applied.fog_enabled) {
			env->set_fog_enabled(env_base.fog_enabled);
		}
		if (env->get_fog_light_color() == env_applied.fog_light_color) {
			env->set_fog_light_color(env_base.fog_light_color);
		}
		if (env->get_fog_sky_affect() == env_applied.fog_sky_affect) {
			env->set_fog_sky_affect(env_base.fog_sky_affect);
		}
		if (env->get_bg_energy_multiplier() == env_applied.sky_energy) {
			env->set_bg_energy_multiplier(env_base.sky_energy);
		}
	}
	env.unref();
	env_captured = false;
}

void FellowWeather3D::_apply_environment() {
	const bool fog = has_control(CONTROL_FOG);
	const bool ambient = has_control(CONTROL_AMBIENT);
	Ref<Environment> e = (fog || ambient) ? _find_environment() : Ref<Environment>();
	if (e != env) {
		_restore_environment();
		env = e;
	}
	if (env.is_null()) {
		return;
	}
	if (!env_captured) {
		env_base.fog_enabled = env->is_fog_enabled();
		env_base.fog_density = env->get_fog_density();
		env_base.fog_light_color = env->get_fog_light_color();
		env_base.fog_sky_affect = env->get_fog_sky_affect();
		env_base.sky_energy = env->get_bg_energy_multiplier();
		env_applied = env_base;
		env_captured = true;
	} else {
		// What the project changed meanwhile is its new base.
		if (env->is_fog_enabled() != env_applied.fog_enabled) {
			env_base.fog_enabled = env->is_fog_enabled();
		}
		if (env->get_fog_density() != env_applied.fog_density) {
			env_base.fog_density = env->get_fog_density();
		}
		if (env->get_fog_light_color() != env_applied.fog_light_color) {
			env_base.fog_light_color = env->get_fog_light_color();
		}
		if (env->get_fog_sky_affect() != env_applied.fog_sky_affect) {
			env_base.fog_sky_affect = env->get_fog_sky_affect();
		}
		if (env->get_bg_energy_multiplier() != env_applied.sky_energy) {
			env_base.sky_energy = env->get_bg_energy_multiplier();
		}
	}
	EnvValues v = env_base;
	if (fog && env->get_fog_mode() == Environment::FOG_MODE_EXPONENTIAL) {
		const float add = current.fog;
		if (add > 0.0f) {
			const float base = env_base.fog_enabled ? env_base.fog_density : 0.0f;
			v.fog_enabled = true;
			v.fog_density = base + add;
			// The weather's share of the fog takes its colour, as bright as the sky (dark at night: the environment's
			// background energy); the sky greys as the visibility drops (a few km).
			const Color weather_fog = fog_color * (env_base.sky_energy * current.ambient);
			v.fog_light_color = env_base.fog_enabled ? env_base.fog_light_color.lerp(weather_fog, add / (base + add)) : weather_fog;
			v.fog_light_color.a = 1.0f;
			const float sky = 1.0f - Math::exp(-add * 1500.0f);
			v.fog_sky_affect = env_base.fog_enabled ? Math::lerp(env_base.fog_sky_affect, 1.0f, sky) : sky;
		}
	}
	if (ambient) {
		v.sky_energy = env_base.sky_energy * current.ambient;
	}
	if (v.fog_enabled != env->is_fog_enabled()) {
		env->set_fog_enabled(v.fog_enabled);
	}
	if (v.fog_density != env->get_fog_density()) {
		env->set_fog_density(v.fog_density);
	}
	if (v.fog_light_color != env->get_fog_light_color()) {
		env->set_fog_light_color(v.fog_light_color);
	}
	if (v.fog_sky_affect != env->get_fog_sky_affect()) {
		env->set_fog_sky_affect(v.fog_sky_affect);
	}
	if (v.sky_energy != env->get_bg_energy_multiplier()) {
		env->set_bg_energy_multiplier(v.sky_energy);
	}
	// Read back: the setters may round (the colour), so that the next frame does not take them for the project's.
	env_applied.fog_enabled = env->is_fog_enabled();
	env_applied.fog_density = env->get_fog_density();
	env_applied.fog_light_color = env->get_fog_light_color();
	env_applied.fog_sky_affect = env->get_fog_sky_affect();
	env_applied.sky_energy = env->get_bg_energy_multiplier();
}

void FellowWeather3D::_rt_set_sky(PackedByteArray p_state) {
	if (p_state.size() != int64_t(sizeof(RendererRD::FellowWeather::SkyState))) {
		return;
	}
	RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton();
	if (w) {
		RendererRD::FellowWeather::SkyState sky;
		memcpy(&sky, p_state.ptr(), sizeof(sky));
		w->set_sky_state(sky);
	}
}

void FellowWeather3D::_push_sky(bool p_on) {
	SkyPushed s;
	if (p_on) {
		s.clouds = has_control(CONTROL_CLOUDS);
		s.coverage = current.clouds;
		s.density = current.cloud_density;
		if (has_control(CONTROL_SUN)) {
			if (DirectionalLight3D *sun = _get_sun()) {
				s.sun = sun->get_light_rid();
				s.sun_factor = current.sunlight;
			}
		}
	}
	if (sky_pushed_any && s.clouds == sky_pushed.clouds && s.coverage == sky_pushed.coverage && s.density == sky_pushed.density && s.sun == sky_pushed.sun && s.sun_factor == sky_pushed.sun_factor) {
		return;
	}
	if (!p_on && !sky_pushed_any) {
		return; // nothing to turn off
	}
	sky_pushed = s;
	sky_pushed_any = p_on;
	RendererRD::FellowWeather::SkyState sky;
	sky.active = s.clouds;
	sky.coverage = s.coverage;
	sky.density = s.density;
	sky.sun_light = s.sun;
	sky.sun_factor = s.sun.is_valid() ? s.sun_factor : 1.0f;
	PackedByteArray state;
	state.resize(sizeof(sky));
	memcpy(state.ptrw(), &sky, sizeof(sky));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowWeather3D::_rt_set_sky).bind(state));
}

void FellowWeather3D::_apply(double p_delta) {
	const bool create = create_missing;
	if (has_control(CONTROL_WIND)) {
		if (FellowWind3D *w = _get_wind(create && (target.wind_speed > 0.0f || prepare_nodes))) {
			w->set_speed(current.wind_speed);
			w->set_gustiness(current.gustiness);
		}
	}
	const bool precipitation = has_control(CONTROL_PRECIPITATION);
	const bool ground = has_control(CONTROL_GROUND);
	if (precipitation || ground) {
		// Below freezing the ground model needs the rain node too: its ice and frost (decision 0136).
		const bool needed = (current.precipitation != PRECIPITATION_NONE && current.intensity > 0.0f) || (target.precipitation != PRECIPITATION_NONE && target.intensity > 0.0f) || (ground && (current.temperature < 0.0f || target.temperature < 0.0f));
		if (FellowRain3D *r = _get_rain(create && precipitation && (needed || prepare_nodes))) {
			if (precipitation) {
				const bool owned = r->get_instance_id() == rain_created;
				if (current.precipitation == PRECIPITATION_RAIN && r->get_precipitation() != FellowRain3D::PRECIPITATION_RAIN) {
					r->set_precipitation(FellowRain3D::PRECIPITATION_RAIN);
					if (owned) {
						r->set_max_drops(24000);
						r->set_area(40.0f);
					}
				} else if (current.precipitation == PRECIPITATION_SNOW && r->get_precipitation() != FellowRain3D::PRECIPITATION_SNOW) {
					// Flakes fall slowly: more of them, in a smaller box (decision 0128's showcase).
					r->set_precipitation(FellowRain3D::PRECIPITATION_SNOW);
					if (owned) {
						r->set_max_drops(60000);
						r->set_area(30.0f);
					}
				}
				const float amount = current.precipitation == PRECIPITATION_NONE ? 0.0f : current.intensity;
				if (r->get_rain() != amount) {
					r->set_rain(amount);
				}
			}
			if (ground) {
				_apply_ground(r, p_delta);
			}
		}
	}
	if (has_control(CONTROL_LIGHTNING)) {
		if (FellowLightning3D *l = _get_lightning(create && (current.lightning > 0.0f || target.lightning > 0.0f || prepare_nodes))) {
			l->set_strikes_per_minute(current.lightning);
		}
	}
	_push_sky(has_control(CONTROL_CLOUDS) || has_control(CONTROL_SUN));
	_apply_environment();
}

void FellowWeather3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			active = this;
			set_process_internal(true);
		} break;
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			started = true;
			if (!transitioning) {
				current = target;
				from = target;
			}
			// The ground as the weather leaves it: wet in the rain, covered in the snow (a project's own wet or snowy
			// ground is kept).
			if (settle_ground && has_control(CONTROL_GROUND)) {
				const bool needed = (current.precipitation != PRECIPITATION_NONE && current.intensity > 0.0f) || current.temperature < 0.0f;
				if (FellowRain3D *r = _get_rain(create_missing && has_control(CONTROL_PRECIPITATION) && (needed || prepare_nodes))) {
					if (current.precipitation == PRECIPITATION_RAIN && current.intensity > 0.0f) {
						r->set_wetness(1.0f);
					} else if (current.precipitation == PRECIPITATION_SNOW && current.intensity > 0.0f && current.temperature <= 1.0f) {
						r->set_snow(1.0f);
					}
					// Below freezing its water is ice, and under a clear sky without snow the ground is frosted.
					if (current.temperature < 0.0f) {
						r->set_freeze(1.0f);
						if (current.intensity <= 0.0f && current.clouds < 0.6f) {
							r->set_frost(1.0f);
						}
					}
				}
			}
			_apply(0.0);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			const double dt = get_process_delta_time();
			if (transitioning) {
				transition_elapsed += dt;
				const float t = transition_length > 0.0 ? float(transition_elapsed / transition_length) : 1.0f;
				current = _blend(from, target, t);
				if (t >= 1.0f) {
					current = target;
					transitioning = false;
					emit_signal(SNAME("transition_finished"), int(weather));
				}
			}
			if (active == this) {
				_apply(dt);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			_push_sky(false);
			_restore_environment();
			if (active == this) {
				active = nullptr;
			}
			sun_searched = false;
			lightning_searched = false;
		} break;
	}
}

void FellowWeather3D::_validate_property(PropertyInfo &p_property) const {
	// A preset's values are its own: stored only for a custom weather.
	static const char *targets[] = { "wind_speed", "gustiness", "precipitation", "intensity", "lightning_rate", "cloud_coverage",
		"cloud_density", "sunlight", "ambient_light", "fog_density", "temperature" };
	if (weather == WEATHER_CUSTOM) {
		return;
	}
	for (const char *t : targets) {
		if (p_property.name == t) {
			p_property.usage &= ~PROPERTY_USAGE_STORAGE;
			return;
		}
	}
}

void FellowWeather3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_weather", "weather", "transition"), &FellowWeather3D::set_weather, DEFVAL(-1.0f));
	ClassDB::bind_method(D_METHOD("get_weather"), &FellowWeather3D::get_weather);
	ClassDB::bind_method(D_METHOD("is_transitioning"), &FellowWeather3D::is_transitioning);
	ClassDB::bind_method(D_METHOD("get_transition_progress"), &FellowWeather3D::get_transition_progress);
	ClassDB::bind_method(D_METHOD("get_current_state"), &FellowWeather3D::get_current_state);
	ClassDB::bind_static_method("FellowWeather3D", D_METHOD("get_preset_state", "weather"), &FellowWeather3D::get_preset_state);
	ClassDB::bind_method(D_METHOD("set_transition_time", "seconds"), &FellowWeather3D::set_transition_time);
	ClassDB::bind_method(D_METHOD("get_transition_time"), &FellowWeather3D::get_transition_time);

	ClassDB::bind_method(D_METHOD("set_wind_speed", "speed"), &FellowWeather3D::set_wind_speed);
	ClassDB::bind_method(D_METHOD("get_wind_speed"), &FellowWeather3D::get_wind_speed);
	ClassDB::bind_method(D_METHOD("set_gustiness", "gustiness"), &FellowWeather3D::set_gustiness);
	ClassDB::bind_method(D_METHOD("get_gustiness"), &FellowWeather3D::get_gustiness);
	ClassDB::bind_method(D_METHOD("set_precipitation", "precipitation"), &FellowWeather3D::set_precipitation);
	ClassDB::bind_method(D_METHOD("get_precipitation"), &FellowWeather3D::get_precipitation);
	ClassDB::bind_method(D_METHOD("set_intensity", "intensity"), &FellowWeather3D::set_intensity);
	ClassDB::bind_method(D_METHOD("get_intensity"), &FellowWeather3D::get_intensity);
	ClassDB::bind_method(D_METHOD("set_lightning_rate", "rate"), &FellowWeather3D::set_lightning_rate);
	ClassDB::bind_method(D_METHOD("get_lightning_rate"), &FellowWeather3D::get_lightning_rate);
	ClassDB::bind_method(D_METHOD("set_cloud_coverage", "coverage"), &FellowWeather3D::set_cloud_coverage);
	ClassDB::bind_method(D_METHOD("get_cloud_coverage"), &FellowWeather3D::get_cloud_coverage);
	ClassDB::bind_method(D_METHOD("set_cloud_density", "density"), &FellowWeather3D::set_cloud_density);
	ClassDB::bind_method(D_METHOD("get_cloud_density"), &FellowWeather3D::get_cloud_density);
	ClassDB::bind_method(D_METHOD("set_sunlight", "sunlight"), &FellowWeather3D::set_sunlight);
	ClassDB::bind_method(D_METHOD("get_sunlight"), &FellowWeather3D::get_sunlight);
	ClassDB::bind_method(D_METHOD("set_ambient_light", "ambient"), &FellowWeather3D::set_ambient_light);
	ClassDB::bind_method(D_METHOD("get_ambient_light"), &FellowWeather3D::get_ambient_light);
	ClassDB::bind_method(D_METHOD("set_fog_density", "density"), &FellowWeather3D::set_fog_density);
	ClassDB::bind_method(D_METHOD("get_fog_density"), &FellowWeather3D::get_fog_density);
	ClassDB::bind_method(D_METHOD("set_temperature", "temperature"), &FellowWeather3D::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &FellowWeather3D::get_temperature);

	ClassDB::bind_method(D_METHOD("set_controls", "controls"), &FellowWeather3D::set_controls);
	ClassDB::bind_method(D_METHOD("get_controls"), &FellowWeather3D::get_controls);
	ClassDB::bind_method(D_METHOD("has_control", "control"), &FellowWeather3D::has_control);
	ClassDB::bind_method(D_METHOD("set_settle_ground", "enable"), &FellowWeather3D::set_settle_ground);
	ClassDB::bind_method(D_METHOD("get_settle_ground"), &FellowWeather3D::get_settle_ground);
	ClassDB::bind_method(D_METHOD("set_ground_speed", "speed"), &FellowWeather3D::set_ground_speed);
	ClassDB::bind_method(D_METHOD("get_ground_speed"), &FellowWeather3D::get_ground_speed);
	ClassDB::bind_method(D_METHOD("set_create_missing", "enable"), &FellowWeather3D::set_create_missing);
	ClassDB::bind_method(D_METHOD("get_create_missing"), &FellowWeather3D::get_create_missing);
	ClassDB::bind_method(D_METHOD("set_prepare_nodes", "enable"), &FellowWeather3D::set_prepare_nodes);
	ClassDB::bind_method(D_METHOD("get_prepare_nodes"), &FellowWeather3D::get_prepare_nodes);
	ClassDB::bind_method(D_METHOD("set_fog_color", "color"), &FellowWeather3D::set_fog_color);
	ClassDB::bind_method(D_METHOD("get_fog_color"), &FellowWeather3D::get_fog_color);

	ClassDB::bind_method(D_METHOD("set_wind_path", "path"), &FellowWeather3D::set_wind_path);
	ClassDB::bind_method(D_METHOD("get_wind_path"), &FellowWeather3D::get_wind_path);
	ClassDB::bind_method(D_METHOD("set_rain_path", "path"), &FellowWeather3D::set_rain_path);
	ClassDB::bind_method(D_METHOD("get_rain_path"), &FellowWeather3D::get_rain_path);
	ClassDB::bind_method(D_METHOD("set_lightning_path", "path"), &FellowWeather3D::set_lightning_path);
	ClassDB::bind_method(D_METHOD("get_lightning_path"), &FellowWeather3D::get_lightning_path);
	ClassDB::bind_method(D_METHOD("set_sun_path", "path"), &FellowWeather3D::set_sun_path);
	ClassDB::bind_method(D_METHOD("get_sun_path"), &FellowWeather3D::get_sun_path);
	ClassDB::bind_method(D_METHOD("set_environment_path", "path"), &FellowWeather3D::set_environment_path);
	ClassDB::bind_method(D_METHOD("get_environment_path"), &FellowWeather3D::get_environment_path);
	ClassDB::bind_method(D_METHOD("get_wind_node"), &FellowWeather3D::get_wind_node);
	ClassDB::bind_method(D_METHOD("get_rain_node"), &FellowWeather3D::get_rain_node);
	ClassDB::bind_method(D_METHOD("get_lightning_node"), &FellowWeather3D::get_lightning_node);
	ClassDB::bind_method(D_METHOD("get_sun_node"), &FellowWeather3D::get_sun_node);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "weather", PROPERTY_HINT_ENUM, "Clear,Cloudy,Overcast,Rain,Storm,Snow,Blizzard,Custom"), "set_weather", "get_weather");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transition_time", PROPERTY_HINT_RANGE, "0,600,0.1,or_greater,suffix:s"), "set_transition_time", "get_transition_time");

	ADD_GROUP("State", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_speed", PROPERTY_HINT_RANGE, "0,40,0.1,or_greater,suffix:m/s"), "set_wind_speed", "get_wind_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gustiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_gustiness", "get_gustiness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "precipitation", PROPERTY_HINT_ENUM, "None,Rain,Snow"), "set_precipitation", "get_precipitation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_intensity", "get_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lightning_rate", PROPERTY_HINT_RANGE, "0,60,0.1,or_greater,suffix:/min"), "set_lightning_rate", "get_lightning_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_coverage", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_cloud_coverage", "get_cloud_coverage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_density", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_cloud_density", "get_cloud_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sunlight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_sunlight", "get_sunlight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ambient_light", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_ambient_light", "get_ambient_light");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog_density", PROPERTY_HINT_RANGE, "0,0.05,0.0001,or_greater,suffix:1/m"), "set_fog_density", "get_fog_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature", PROPERTY_HINT_RANGE, "-40,40,0.1,or_less,or_greater,suffix:°C"), "set_temperature", "get_temperature");

	ADD_GROUP("Control", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "controls", PROPERTY_HINT_FLAGS, "Wind,Precipitation,Ground,Lightning,Clouds,Sun,Fog,Ambient Light"), "set_controls", "get_controls");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "settle_ground"), "set_settle_ground", "get_settle_ground");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_speed", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_ground_speed", "get_ground_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "create_missing"), "set_create_missing", "get_create_missing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "prepare_nodes"), "set_prepare_nodes", "get_prepare_nodes");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "fog_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_fog_color", "get_fog_color");

	ADD_GROUP("Nodes", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "wind_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowWind3D"), "set_wind_path", "get_wind_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "rain_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowRain3D"), "set_rain_path", "get_rain_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "lightning_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowLightning3D"), "set_lightning_path", "get_lightning_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun_path", "get_sun_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "environment_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "WorldEnvironment"), "set_environment_path", "get_environment_path");

	ADD_SIGNAL(MethodInfo("weather_changed", PropertyInfo(Variant::INT, "weather")));
	ADD_SIGNAL(MethodInfo("transition_finished", PropertyInfo(Variant::INT, "weather")));

	BIND_ENUM_CONSTANT(WEATHER_CLEAR);
	BIND_ENUM_CONSTANT(WEATHER_CLOUDY);
	BIND_ENUM_CONSTANT(WEATHER_OVERCAST);
	BIND_ENUM_CONSTANT(WEATHER_RAIN);
	BIND_ENUM_CONSTANT(WEATHER_STORM);
	BIND_ENUM_CONSTANT(WEATHER_SNOW);
	BIND_ENUM_CONSTANT(WEATHER_BLIZZARD);
	BIND_ENUM_CONSTANT(WEATHER_CUSTOM);
	BIND_ENUM_CONSTANT(PRECIPITATION_NONE);
	BIND_ENUM_CONSTANT(PRECIPITATION_RAIN);
	BIND_ENUM_CONSTANT(PRECIPITATION_SNOW);
	BIND_BITFIELD_FLAG(CONTROL_WIND);
	BIND_BITFIELD_FLAG(CONTROL_PRECIPITATION);
	BIND_BITFIELD_FLAG(CONTROL_GROUND);
	BIND_BITFIELD_FLAG(CONTROL_LIGHTNING);
	BIND_BITFIELD_FLAG(CONTROL_CLOUDS);
	BIND_BITFIELD_FLAG(CONTROL_SUN);
	BIND_BITFIELD_FLAG(CONTROL_FOG);
	BIND_BITFIELD_FLAG(CONTROL_AMBIENT);
	BIND_BITFIELD_FLAG(CONTROL_ALL);
}

FellowWeather3D::FellowWeather3D() {
	target = _preset(WEATHER_CLEAR);
	from = target;
	current = target;
}
