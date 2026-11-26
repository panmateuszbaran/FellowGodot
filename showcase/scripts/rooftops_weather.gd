extends Node
## Milestone 211 (the owner: "can the showcase also show fire, smoke and the weather?"): the square's weather on the
## keys, through FellowWeather3D (its wind, rain or snow, lightning, clouds, the sun's share and the sky's light, the
## ground wet, snowy or frozen):
##   1 the scene's own fair weather (its towering cumulus, FAIR), 2 cloudy, 3 overcast, 4 rain, 5 a storm (lightning),
##   6 snow, 7 a blizzard; 0 the weather changing by itself (CYCLE, each held CYCLE_HOLD seconds); F1 the keys shown.
## The tools: --weather=<1-7> (at once), --weather_cycle=1; turn_to().
## The scene's depth fog closes in with the weather's (FellowWeather3D draws a depth fog in to the visibility its fog
## leaves, milestone 211: 2.5 km in the rain, 1.2 km in a storm, 300 m in a blizzard). The sky's own sun
## (rooftops.gd's SkySun, which lights the clouds and, through the sky, the square) dims with the weather's sunlight as
## the Sun does on the ground (sky_dim of it) and greys (the golden hour's light gone under the cloud); the square's
## lamps glow brighter as it darkens (LAMPS_DARK times their light under the darkest sky). The sky's light on the square
## (its radiance map) is not dimmed with the SkySun (milestone 214: the sky's shader's radiance_scale makes up for it):
## the weather's own ambient light dims it, and the engine turns it to an overcast sky's. The eye adapts to the
## weather's light (milestone 214): the exposure raised by EXPOSURE_ADAPT of the stops the square darkens by under each
## weather (DARKER), as the weather turns; the fair weather's exposure as it was. The rain and the snow drawn
## to be seen (wider streaks than the main showcase's, more and larger flakes in a smaller box when it snows); a weather
## turned to at once (--weather) leaves the ground as it would have — wet in the rain, white in the snow.

@export var weather_path: NodePath = ^"../Weather"
@export var transition := 10.0 # s
@export var cycle_hold := 25.0 # s
## How much of the weather's dimming of the sun the sky's own sun (the clouds' light) takes: 0 none, 1 all.
@export_range(0.0, 1.0) var sky_dim := 0.85

## The scene's fair weather (rooftops.gd's clouds; the Wind node's 3.5 m/s).
const FAIR := {"cloud_coverage": 0.45, "cloud_density": 2.6, "wind_speed": 3.5, "gustiness": 0.3, "sunlight": 1.0,
		"ambient_light": 1.0, "fog_density": 0.0, "temperature": 18.0, "precipitation": 0, "intensity": 0.0,
		"lightning_rate": 0.0}
const NAMES := ["fair", "cloudy", "overcast", "rain", "storm", "snow", "blizzard"]
const CYCLE := [1, 2, 4, 5, 4, 3, 1, 6, 7, 6, 1] # (keys) fair, cloudy, rain, a storm, rain, overcast, fair, snow...
const LAMPS_DARK := 2.4
## (milestone 214) How many stops darker the square is under each weather than in the fair one (keys 1-7: the RTX's
## frames from the square's corner, their mean luminance under the sky; the blizzard's between the snow's and the
## storm's), and the share of that the eye makes up for (a storm still darker than a fair evening).
const DARKER := [0.0, 0.27, 1.44, 2.74, 3.22, 1.3, 2.2]
const EXPOSURE_ADAPT := 0.55

var _weather: Node
var _key := 1
var _cycle := false
var _cycle_i := 0
var _cycle_t := 0.0
var _sky_sun: Light3D
var _sky_sun_energy := 0.0
var _sky_set := -1.0
var _lamps: Array = [] # [OmniLight3D, its energy]
var _sky_sun_color := Color(1, 1, 1)
var _sky_mat: ShaderMaterial
var _attrs: CameraAttributes
var _exposure_base := 1.0
var _stops := 0.0
var _stops_from := 0.0
var _stops_to := 0.0
var _stops_t := 0.0
var _stops_dur := 0.0
var _radiance_base := -1.0
var _radiance_set := -1.0
var _rain: Node
const SKY_GREY := Color(0.78, 0.82, 0.9)
var _label: Label
var _help := false
var _hint_t := 6.0


func _ready() -> void:
	_weather = get_node_or_null(weather_path)
	if _weather == null:
		return
	var start := 1
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--weather="):
			start = clampi(int(a.get_slice("=", 1)), 1, 7)
		elif a == "--weather_cycle=1":
			_cycle = true
	_rain = _weather.call("get_rain_node") if _weather.has_method("get_rain_node") else null
	turn_to(start, 0.0)
	var town := get_node_or_null(^"../Town")
	if town:
		for n in town.get_children():
			var light := n.get_node_or_null(^"Light") as OmniLight3D
			if light and String(n.name).begins_with("Lamp"):
				_lamps.append([light, light.light_energy])
	_rain = _weather.call("get_rain_node") if _weather.has_method("get_rain_node") else null
	if _rain:
		_rain.set("streak_width", 0.01)
		_rain.set("drop_color", Color(0.82, 0.85, 0.9, 0.5))
		_rain.set("flake_size", 0.03)
	var layer := CanvasLayer.new()
	layer.layer = 5
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(16, 12)
	_label.add_theme_font_size_override("font_size", 15)
	_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_label)
	_show()


## The weather of key p_key (1 the scene's fair weather) over p_seconds.
func turn_to(p_key: int, p_seconds: float) -> void:
	_key = p_key
	_stops_from = _stops
	_stops_to = float(DARKER[p_key - 1]) * EXPOSURE_ADAPT
	_stops_t = 0.0
	_stops_dur = p_seconds
	if p_seconds <= 0.0:
		_stops = _stops_to
		_apply_exposure()
	if p_key == 1:
		for k in FAIR:
			_weather.set(k, FAIR[k])
		_weather.call("set_weather", 7, p_seconds) # WEATHER_CUSTOM: to these values
	else:
		_weather.call("set_weather", p_key - 1, p_seconds) # WEATHER_CLOUDY .. WEATHER_BLIZZARD
	if _rain:
		var snowing := p_key >= 6
		_rain.set("max_drops", 60000 if snowing else 24000)
		_rain.set("area", 30.0 if snowing else 40.0)
		if p_seconds <= 0.0:
			# (at once: the ground as the weather would have left it)
			_rain.set("wetness", 1.0 if p_key == 4 or p_key == 5 else 0.0)
			_rain.set("snow", 1.0 if snowing else 0.0)
	_hint_t = 4.0
	_show()


func _unhandled_input(event: InputEvent) -> void:
	if _weather == null or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	var k: int = (event as InputEventKey).physical_keycode
	if k >= KEY_1 and k <= KEY_7:
		_cycle = false
		turn_to(k - KEY_0, transition)
	elif k == KEY_0:
		_cycle = not _cycle
		_cycle_t = cycle_hold
		_show()
	elif k == KEY_F1:
		_help = not _help
		_show()


func _process(delta: float) -> void:
	if _weather == null:
		return
	if _cycle:
		_cycle_t += delta
		if _cycle_t >= cycle_hold:
			_cycle_t = 0.0
			_cycle_i = (_cycle_i + 1) % CYCLE.size()
			turn_to(CYCLE[_cycle_i], transition)
	if _stops_t < _stops_dur:
		# (the eye adapting as the weather turns, eased as FellowWeather3D eases it)
		_stops_t = minf(_stops_t + delta, _stops_dur)
		_stops = lerpf(_stops_from, _stops_to, smoothstep(0.0, 1.0, _stops_t / _stops_dur))
		_apply_exposure()
	var state: Dictionary = _weather.call("get_current_state")
	var sun: float = state.get("sunlight", 1.0)
	for l in _lamps:
		(l[0] as OmniLight3D).light_energy = float(l[1]) * lerpf(1.0, LAMPS_DARK, clampf(1.0 - sun, 0.0, 1.0))
	if _sky_sun == null:
		_sky_sun = get_node_or_null(^"../SkySun") as Light3D
	if _sky_sun:
		# (rooftops.gd sets its energy from the Sun's when the Sun changes: what it set is the fair weather's)
		if _sky_set < 0.0 or not is_equal_approx(_sky_sun.light_energy, _sky_set):
			_sky_sun_energy = _sky_sun.light_energy
			_sky_sun_color = _sky_sun.light_color
		var dim := lerpf(1.0, sun, sky_dim)
		_sky_set = _sky_sun_energy * dim
		_sky_sun.light_energy = _sky_set
		_sky_sun.light_color = _sky_sun_color.lerp(SKY_GREY, clampf(1.0 - sun, 0.0, 1.0))
		# (milestone 214: the sky's light on the square not dimmed with the clouds' light — its radiance scaled back up)
		if _sky_mat == null:
			var we := get_node_or_null(^"../WorldEnvironment") as WorldEnvironment
			if we and we.environment and we.environment.sky and we.environment.sky.sky_material is ShaderMaterial:
				_sky_mat = we.environment.sky.sky_material as ShaderMaterial
				var base: Variant = _sky_mat.get_shader_parameter("radiance_scale")
				_radiance_base = float(base) if base != null else 1.0
		if _sky_mat:
			var rs := _radiance_base / maxf(dim, 0.05)
			if absf(rs - _radiance_set) > 0.001 * rs:
				_radiance_set = rs
				_sky_mat.set_shader_parameter("radiance_scale", rs)
	if _hint_t > 0.0:
		_hint_t -= delta
		if _hint_t <= 0.0:
			_show()


## (milestone 214) The scene's exposure: the fair weather's times 2 ^ the stops made up for.
func _apply_exposure() -> void:
	if _attrs == null:
		var we := get_node_or_null(^"../WorldEnvironment") as WorldEnvironment
		if we == null or we.camera_attributes == null:
			return
		_attrs = we.camera_attributes
		_exposure_base = _attrs.exposure_multiplier
	_attrs.exposure_multiplier = _exposure_base * pow(2.0, _stops)


func _show() -> void:
	if _label == null:
		return
	var text := "Weather: %s%s" % [NAMES[_key - 1], "  (changing by itself)" if _cycle else ""]
	if _help:
		text += "\n1 fair  2 cloudy  3 overcast  4 rain  5 storm  6 snow  7 blizzard\n0 the weather changing by itself   F1 hide"
	elif _hint_t > 0.0:
		text += "    (1-7 the weather, 0 by itself, F1 the keys)"
	_label.text = text
	_label.visible = _help or _hint_t > 0.0 or _cycle


## (tools) the key now and the weather's state.
func get_weather_key() -> int:
	return _key
