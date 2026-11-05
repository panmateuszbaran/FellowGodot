/**************************************************************************/
/*  fellow_weather_3d.h                                                   */
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

#include "scene/main/node.h"
#include "scene/resources/environment.h"

class FellowWind3D;
class FellowRain3D;
class FellowLightning3D;
class DirectionalLight3D;

// FellowEngine weather controller (docs/decisions/0132-weather-controller.md): one node that sets the weather — clear,
// cloudy, overcast, rain, a thunderstorm, snow, a blizzard, or a state of its own — and changes it over time. It drives
// the scene's FellowWind3D, FellowRain3D and FellowLightning3D (finding them, or creating them where the weather needs
// them), the volumetric clouds' coverage and density, the sun on the scene's surfaces (dimmed by the clouds over it), the
// environment's fog and ambient light, and the ground: wet in the rain, drying after it, covered in falling snow that
// melts when it is warmer than freezing. Only while the game runs (not in the editor).
class FellowWeather3D : public Node {
	GDCLASS(FellowWeather3D, Node);

public:
	enum Weather {
		WEATHER_CLEAR,
		WEATHER_CLOUDY,
		WEATHER_OVERCAST,
		WEATHER_RAIN,
		WEATHER_STORM,
		WEATHER_SNOW,
		WEATHER_BLIZZARD,
		WEATHER_CUSTOM,
	};
	enum Precipitation {
		PRECIPITATION_NONE,
		PRECIPITATION_RAIN,
		PRECIPITATION_SNOW,
	};
	// What the weather drives (`controls`).
	enum Control {
		CONTROL_WIND = 1,
		CONTROL_PRECIPITATION = 2,
		CONTROL_GROUND = 4,
		CONTROL_LIGHTNING = 8,
		CONTROL_CLOUDS = 16,
		CONTROL_SUN = 32,
		CONTROL_FOG = 64,
		CONTROL_AMBIENT = 128,
		CONTROL_ALL = 255,
	};

	struct State {
		float wind_speed = 2.0f; // m/s
		float gustiness = 0.3f; // 0..1
		Precipitation precipitation = PRECIPITATION_NONE;
		float intensity = 0.0f; // 0..1: how hard it rains or snows
		float lightning = 0.0f; // strikes per minute
		float clouds = 0.2f; // 0..1: the volumetric clouds' coverage
		float cloud_density = 1.0f;
		float sunlight = 1.0f; // 0..1: the sun's share that reaches the scene through the clouds
		float ambient = 1.0f; // the sky's light (its ambient and reflections, the environment's background energy), relative to its own
		float fog = 0.0f; // 1/m: exponential fog added to the environment's
		float temperature = 18.0f; // degrees C: snow melts above 0
	};

private:
	static FellowWeather3D *active;

	Weather weather = WEATHER_CLEAR;
	State target;
	State from; // when the transition began
	State current;
	float transition_time = 30.0f; // s, when the weather is set without one
	double transition_length = 0.0;
	double transition_elapsed = 0.0;
	bool transitioning = false;
	bool started = false; // at ready: before it, the weather is taken at once

	uint32_t controls = CONTROL_ALL;
	bool settle_ground = true;
	float ground_speed = 1.0f;
	bool create_missing = true;
	bool prepare_nodes = false;
	Color fog_color = Color(0.52f, 0.55f, 0.6f);

	NodePath wind_path;
	NodePath rain_path;
	NodePath lightning_path;
	NodePath sun_path;
	NodePath environment_path;
	ObjectID wind_id;
	ObjectID rain_id;
	ObjectID lightning_id;
	ObjectID sun_id;
	bool sun_searched = false;
	bool lightning_searched = false;
	bool rain_owned = false; // the ground's follow modes turned off on the rain node once
	ObjectID rain_created; // the rain node this controller created (it sets its drops for rain or snow)

	// The environment's own values (its base) and the ones last written, to see a project change them meanwhile.
	struct EnvValues {
		bool fog_enabled = false;
		float fog_density = 0.0f;
		Color fog_light_color;
		float fog_sky_affect = 1.0f;
		float sky_energy = 1.0f; // background_energy_multiplier: the sky, its ambient light and reflections
	};
	Ref<Environment> env;
	EnvValues env_base;
	EnvValues env_applied;
	bool env_captured = false;

	struct SkyPushed {
		bool clouds = false;
		float coverage = -1.0f;
		float density = -1.0f;
		RID sun;
		float sun_factor = -1.0f;
	};
	SkyPushed sky_pushed;
	bool sky_pushed_any = false;

	static State _preset(Weather p_weather);
	static State _blend(const State &p_from, const State &p_to, float p_t);
	void _begin_transition(double p_seconds);
	void _target_changed();

	Node *_scene_root() const;
	FellowWind3D *_get_wind(bool p_create);
	FellowRain3D *_get_rain(bool p_create);
	FellowLightning3D *_get_lightning(bool p_create);
	DirectionalLight3D *_get_sun();
	Ref<Environment> _find_environment() const;

	void _apply(double p_delta);
	void _apply_ground(FellowRain3D *p_rain, double p_delta);
	void _apply_environment();
	void _restore_environment();
	void _push_sky(bool p_on);
	// Static: it may run on the render thread after the node is gone (its state is in the argument).
	static void _rt_set_sky(PackedByteArray p_state);

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	static FellowWeather3D *get_active() { return active; }

	void set_weather(Weather p_weather, float p_transition = -1.0f);
	// The property's setter (one argument: a setter with two was refused by debug builds' ClassDB, and the editor had
	// no "weather" property; milestone 50).
	void _set_weather_property(Weather p_weather) { set_weather(p_weather); }
	Weather get_weather() const { return weather; }
	bool is_transitioning() const { return transitioning; }
	float get_transition_progress() const;
	Dictionary get_current_state() const;
	static Dictionary get_preset_state(Weather p_weather);

	void set_transition_time(float p_seconds) { transition_time = MAX(0.0f, p_seconds); }
	float get_transition_time() const { return transition_time; }

	void set_wind_speed(float p_speed);
	float get_wind_speed() const { return target.wind_speed; }
	void set_gustiness(float p_gustiness);
	float get_gustiness() const { return target.gustiness; }
	void set_precipitation(Precipitation p_precipitation);
	Precipitation get_precipitation() const { return target.precipitation; }
	void set_intensity(float p_intensity);
	float get_intensity() const { return target.intensity; }
	void set_lightning_rate(float p_rate);
	float get_lightning_rate() const { return target.lightning; }
	void set_cloud_coverage(float p_coverage);
	float get_cloud_coverage() const { return target.clouds; }
	void set_cloud_density(float p_density);
	float get_cloud_density() const { return target.cloud_density; }
	void set_sunlight(float p_sunlight);
	float get_sunlight() const { return target.sunlight; }
	void set_ambient_light(float p_ambient);
	float get_ambient_light() const { return target.ambient; }
	void set_fog_density(float p_fog);
	float get_fog_density() const { return target.fog; }
	void set_temperature(float p_temperature);
	float get_temperature() const { return target.temperature; }

	void set_controls(BitField<Control> p_controls);
	BitField<Control> get_controls() const { return BitField<Control>(controls); }
	bool has_control(Control p_control) const { return (controls & p_control) != 0; }
	void set_settle_ground(bool p_enable) { settle_ground = p_enable; }
	bool get_settle_ground() const { return settle_ground; }
	void set_ground_speed(float p_speed) { ground_speed = MAX(0.0f, p_speed); }
	float get_ground_speed() const { return ground_speed; }
	void set_create_missing(bool p_enable) { create_missing = p_enable; }
	bool get_create_missing() const { return create_missing; }
	void set_prepare_nodes(bool p_enable) { prepare_nodes = p_enable; }
	bool get_prepare_nodes() const { return prepare_nodes; }
	void set_fog_color(const Color &p_color) { fog_color = p_color; }
	Color get_fog_color() const { return fog_color; }

	void set_wind_path(const NodePath &p_path);
	NodePath get_wind_path() const { return wind_path; }
	void set_rain_path(const NodePath &p_path);
	NodePath get_rain_path() const { return rain_path; }
	void set_lightning_path(const NodePath &p_path);
	NodePath get_lightning_path() const { return lightning_path; }
	void set_sun_path(const NodePath &p_path);
	NodePath get_sun_path() const { return sun_path; }
	void set_environment_path(const NodePath &p_path);
	NodePath get_environment_path() const { return environment_path; }

	FellowWind3D *get_wind_node() const;
	FellowRain3D *get_rain_node() const;
	FellowLightning3D *get_lightning_node() const;
	DirectionalLight3D *get_sun_node() const;

	FellowWeather3D();
};

VARIANT_ENUM_CAST(FellowWeather3D::Weather);
VARIANT_ENUM_CAST(FellowWeather3D::Precipitation);
VARIANT_BITFIELD_CAST(FellowWeather3D::Control);
