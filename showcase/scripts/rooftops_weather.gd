extends Node
## Milestone 211 (the owner: "can the showcase also show fire, smoke and the weather?"): the square's weather on the
## keys, through FellowWeather3D (its wind, rain or snow, lightning, clouds, the sun's share and the sky's light, the
## ground wet, snowy or frozen):
##   1 the scene's own fair weather (its towering cumulus, FAIR), 2 cloudy, 3 overcast, 4 rain, 5 a storm (lightning),
##   6 snow, 7 a blizzard; 0 the weather changing by itself (CYCLE, each held CYCLE_HOLD seconds); F1 the keys shown.
## The tools: --weather=<1-7> (at once), --weather_cycle=1; turn_to().
## The scene's depth fog (FellowWeather3D adds its fog to an exponential fog only) follows the weather's: it closes in to
## the visibility the weather's fog would leave (3 / its density: 2.5 km in the rain, 1.2 km in a storm, 300 m in a
## blizzard), greys from the golden hour's haze as the sun goes, and veils the sky in the thickest. The sky's own sun
## (rooftops.gd's SkySun, which lights the clouds and, through the sky, the square) dims with the weather's sunlight as
## the Sun does on the ground (sky_dim of it) and greys (the golden hour's light gone under the cloud); the square's
## lamps glow brighter as it darkens (LAMPS_DARK times their light under the darkest sky). The rain and the snow drawn
## to be seen (wider streaks than the main showcase's, more and larger flakes in a smaller box when it snows); a weather
## turned to at once (--weather) leaves the ground as it would have — wet in the rain, white in the snow.

@export var weather_path: NodePath = ^"../Weather"
@export var environment_path: NodePath = ^"../WorldEnvironment"
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
const FOG_NEAREST := 260.0 # m: the depth fog's end at the thickest
const FOG_GREY := Color(0.55, 0.58, 0.62)
const LAMPS_DARK := 2.4

var _weather: Node
var _env: Environment
var _key := 1
var _cycle := false
var _cycle_i := 0
var _cycle_t := 0.0
var _fog_end := 0.0
var _fog_begin := 0.0
var _fog_color := Color()
var _fog_sky := 0.0
var _sky_sun: Light3D
var _sky_sun_energy := 0.0
var _sky_set := -1.0
var _lamps: Array = [] # [OmniLight3D, its energy]
var _sky_sun_color := Color(1, 1, 1)
var _rain: Node
const SKY_GREY := Color(0.78, 0.82, 0.9)
var _label: Label
var _help := false
var _hint_t := 6.0


func _ready() -> void:
	_weather = get_node_or_null(weather_path)
	var we := get_node_or_null(environment_path) as WorldEnvironment
	if we:
		_env = we.environment
		_fog_end = _env.fog_depth_end
		_fog_begin = _env.fog_depth_begin
		_fog_color = _env.fog_light_color
		_fog_sky = _env.fog_sky_affect
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
	var state: Dictionary = _weather.call("get_current_state")
	var sun: float = state.get("sunlight", 1.0)
	if _env and _fog_end > 0.0 and _env.fog_mode == Environment.FOG_MODE_DEPTH:
		var fog: float = state.get("fog_density", 0.0)
		var end := clampf(3.0 / maxf(fog, 1e-6), FOG_NEAREST, _fog_end)
		_env.fog_depth_end = end
		_env.fog_depth_begin = minf(_fog_begin, end * 0.05)
		var grey := FOG_GREY * (0.55 + 0.45 * float(state.get("ambient_light", 1.0)))
		grey.a = 1.0
		_env.fog_light_color = _fog_color.lerp(grey, clampf(1.0 - sun, 0.0, 1.0))
		_env.fog_sky_affect = lerpf(_fog_sky, 0.8, clampf(fog / 0.01, 0.0, 1.0))
	for l in _lamps:
		(l[0] as OmniLight3D).light_energy = float(l[1]) * lerpf(1.0, LAMPS_DARK, clampf(1.0 - sun, 0.0, 1.0))
	if _sky_sun == null:
		_sky_sun = get_node_or_null(^"../SkySun") as Light3D
	if _sky_sun:
		# (rooftops.gd sets its energy from the Sun's when the Sun changes: what it set is the fair weather's)
		if _sky_set < 0.0 or not is_equal_approx(_sky_sun.light_energy, _sky_set):
			_sky_sun_energy = _sky_sun.light_energy
			_sky_sun_color = _sky_sun.light_color
		_sky_set = _sky_sun_energy * lerpf(1.0, sun, sky_dim)
		_sky_sun.light_energy = _sky_set
		_sky_sun.light_color = _sky_sun_color.lerp(SKY_GREY, clampf(1.0 - sun, 0.0, 1.0))
	if _hint_t > 0.0:
		_hint_t -= delta
		if _hint_t <= 0.0:
			_show()


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
