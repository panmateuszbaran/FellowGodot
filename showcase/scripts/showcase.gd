extends Node3D
## FellowEngine Showcase: the valley's clock and weather, the village's lights, the campfire lit again once its log
## has burned, and the keys (the HUD lists them):
##   1-7  weather: clear, cloudy, overcast, rain, storm, snow, blizzard; 0: the weather changes by itself
##   T    the day's clock runs or stops; [ and ]: an hour back or forward
##   F    the campfire lit again; F1 the help, F2 the HUD, F3 one effect off at a time, F11 full screen, F12 a
##        screenshot (user://)
##   Esc  frees the mouse (a click captures it again)

const WEATHER_NAMES := ["Clear", "Cloudy", "Overcast", "Rain", "Storm", "Snow", "Blizzard"]
## F3: one effect off at a time, to see what each adds (or which one does something you do not like): a project
## setting (rendering/fellow/*, read every frame), the environment's ("env:") or the viewport's ("viewport:").
const EFFECTS := [
	["contact shadows", "rendering/fellow/post/contact_shadows/enabled", false],
	["light shafts", "rendering/fellow/post/light_shafts/intensity", 0.0],
	["bloom", "rendering/fellow/post/bloom/enabled", false],
	["clouds and their shadows", "rendering/fellow/post/clouds/enabled", false],
	["aerial perspective", "rendering/fellow/post/atmosphere/aerial_perspective", false],
	["ambient occlusion (SSAO)", "env:ssao_enabled", false],
	["temporal antialiasing (TAA)", "viewport:use_taa", false],
	["autotune", "rendering/fellow/autotune/enabled", false],
]
const AUTO_SEQUENCE := [0, 1, 3, 4, 2, 0, 1, 5, 6, 1]

@export var day_length := 720.0 ## seconds of play for a whole day
@export_range(0.0, 1.0) var time_of_day := 0.36 ## 0 midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset
@export var time_running := true
@export var weather_seconds := 120.0 ## how long each weather lasts when it changes by itself
@export var fire_relight_seconds := 20.0 ## after the log has burned out

@onready var sun: DirectionalLight3D = $Sun
@onready var moon: DirectionalLight3D = $Moon
@onready var world_env: WorldEnvironment = $WorldEnvironment
@onready var weather: Node = $Weather
@onready var campfire_log: Node = $Village/Campfire/Log/Burnable
@onready var player: Node3D = $Player
@onready var hud: CanvasLayer = $HUD

var weather_index := 0
var auto_weather := false
var _auto_t := 0.0
var _auto_i := 0
var _burned_out_t := 0.0
var _lamps: Array[OmniLight3D] = []
var _window_material: StandardMaterial3D
var _sun_energy := 1.0
var _moon_energy := 0.08
var _spin := 0.0
var _effect := -1 # the effect F3 turned off (-1: none)
var _effect_defaults := []
var _tilt := 0.0
var _tilt_t := 0.0


func _ready() -> void:
	_sun_energy = sun.light_energy
	_moon_energy = moon.light_energy
	for n in get_tree().get_nodes_in_group("showcase_lamps"):
		if n is OmniLight3D:
			_lamps.append(n)
	_window_material = load("res://world/window_glow.tres")
	_choose_character()
	var start_weather := 0
	# Options for pictures and tests (after -- on the command line): --showcase_time=<0..1>, --showcase_weather=<0..6>,
	# --showcase_stop (the clock stopped), --showcase_at=<x>,<z>,<heading°>[,<zoom>[,<pitch°>]] (the player and its
	# camera), --showcase_walk=<seconds> (walking forward), --showcase_run=1 (running), --showcase_spin=<°/s> (the camera
	# turning round the player), --showcase_tilt=<°/s> (its pitch swept from -70° to 35° and back), --showcase_jump=<s>,
	# --showcase_roll=<s> (a jump or a roll that many seconds in), --showcase_off=<n> (EFFECTS[n] off, as F3),
	# --showcase_autowalk=<heading°> (MotionBricks' character walking that way whatever the camera does).
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=", true, 1)
		match kv[0]:
			"showcase_time":
				time_of_day = float(kv[1])
			"showcase_weather":
				start_weather = int(kv[1])
			"showcase_stop":
				time_running = false
			"showcase_at":
				var v := kv[1].split(",")
				var x := float(v[0])
				var z := float(v[1])
				var terrain := get_node_or_null("Terrain")
				var y: float = terrain.call("get_height", Vector3(x, 0.0, z)) if terrain else 0.0
				player.call("place", Vector3(x, y + (0.3 if player is CharacterBody3D else 0.0), z), deg_to_rad(float(v[2])))
				if v.size() > 3:
					player.get_node("CameraPivot/SpringArm3D").spring_length = float(v[3])
					player.set("_zoom", float(v[3]))
				if v.size() > 4:
					player.get_node("CameraPivot/SpringArm3D").rotation.x = deg_to_rad(float(v[4]))
			"showcase_walk":
				Input.action_press("move_forward")
				get_tree().create_timer(float(kv[1])).timeout.connect(func() -> void: Input.action_release("move_forward"))
			"showcase_run":
				Input.action_press("run")
			"showcase_spin":
				_spin = float(kv[1])
			"showcase_autowalk":
				# Walking towards a heading (degrees, as the camera's) whatever the camera does.
				var h := deg_to_rad(float(kv[1]))
				player.set("autopilot", Vector3(-sin(h), 0.0, -cos(h)))
			"showcase_off":
				for i in int(kv[1]) + 1:
					_next_effect()
			"showcase_tilt":
				_tilt = float(kv[1])
			"showcase_jump":
				get_tree().create_timer(float(kv[1])).timeout.connect(func() -> void: _tap("jump"))
			"showcase_roll":
				get_tree().create_timer(float(kv[1])).timeout.connect(func() -> void: _tap("roll"))
	_set_weather(start_weather, 0.0)
	_apply_time()
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## MotionBricks' character when its files are in showcase/external (README.md), else the simple one;
## --showcase_character=simple keeps the simple one.
func _choose_character() -> void:
	if OS.get_cmdline_user_args().has("--showcase_character=simple") or not ClassDB.class_exists("FellowMotionCharacter3D"):
		return
	if not (FileAccess.file_exists("res://external/soma_f16.fmb") and FileAccess.file_exists("res://external/soma_mannequin.glb")):
		return
	var np: Node3D = load("res://neural_player.tscn").instantiate()
	np.set("start_yaw", player.get("start_yaw"))
	np.set("water_level", player.get("water_level"))
	var pos := player.global_position
	var terrain := get_node_or_null("Terrain")
	if terrain:
		var h: float = terrain.call("get_height", pos)
		if not is_nan(h):
			pos.y = h
	remove_child(player)
	player.queue_free()
	np.position = pos
	np.name = "Player"
	add_child(np)
	player = np


func get_character_text() -> String:
	return player.call("get_status") if player.has_method("get_status") else "simple character"


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
		get_viewport().set_input_as_handled()
		return
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	var k: int = event.physical_keycode
	if k >= KEY_1 and k <= KEY_7:
		auto_weather = false
		_set_weather(k - KEY_1, -1.0)
	elif k == KEY_0:
		auto_weather = true
		_auto_t = weather_seconds
	elif k == KEY_T:
		time_running = not time_running
	elif k == KEY_BRACKETLEFT:
		time_of_day = fposmod(time_of_day - 1.0 / 24.0, 1.0)
	elif k == KEY_BRACKETRIGHT:
		time_of_day = fposmod(time_of_day + 1.0 / 24.0, 1.0)
	elif k == KEY_F:
		_relight()
	elif k == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif k == KEY_F3:
		_next_effect()
	elif k == KEY_F11:
		var full := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if full else DisplayServer.WINDOW_MODE_FULLSCREEN)
	elif k == KEY_F12:
		var img := get_viewport().get_texture().get_image()
		var path := "user://showcase_%s.png" % Time.get_datetime_string_from_system().replace(":", "-")
		img.save_png(path)
		hud.call("flash", "Screenshot: " + ProjectSettings.globalize_path(path))


## An action pressed for a frame (the command line's jump and roll).
func _tap(p_action: String) -> void:
	var e := InputEventAction.new()
	e.action = p_action
	e.pressed = true
	Input.parse_input_event(e)
	await get_tree().process_frame
	await get_tree().process_frame
	var r := InputEventAction.new()
	r.action = p_action
	Input.parse_input_event(r)


func _process(delta: float) -> void:
	if _spin != 0.0:
		var pivot := player.get_node_or_null("CameraPivot") as Node3D
		if pivot:
			pivot.rotation.y += deg_to_rad(_spin) * delta
	if _tilt != 0.0:
		# The camera's pitch from -70° to 35° and back (the mouse's limits).
		var arm := player.get_node_or_null("CameraPivot/SpringArm3D") as Node3D
		if arm:
			_tilt_t += _tilt * delta
			arm.rotation.x = deg_to_rad(-70.0 + pingpong(_tilt_t, 105.0))
	if time_running:
		time_of_day = fposmod(time_of_day + delta / day_length, 1.0)
	_apply_time()
	if auto_weather:
		_auto_t += delta
		if _auto_t >= weather_seconds:
			_auto_t = 0.0
			_auto_i = (_auto_i + 1) % AUTO_SEQUENCE.size()
			_set_weather(AUTO_SEQUENCE[_auto_i], -1.0)
	# The campfire: lit again a while after its log has burned out.
	if campfire_log and campfire_log.call("is_burned_out"):
		_burned_out_t += delta
		if _burned_out_t > fire_relight_seconds:
			_relight()
	else:
		_burned_out_t = 0.0


func _effect_get(p_key: String) -> Variant:
	if p_key.begins_with("env:"):
		return world_env.environment.get(p_key.trim_prefix("env:"))
	if p_key.begins_with("viewport:"):
		return get_viewport().get(p_key.trim_prefix("viewport:"))
	return ProjectSettings.get_setting(p_key)


func _effect_set(p_key: String, p_value: Variant) -> void:
	if p_key.begins_with("env:"):
		world_env.environment.set(p_key.trim_prefix("env:"), p_value)
	elif p_key.begins_with("viewport:"):
		get_viewport().set(p_key.trim_prefix("viewport:"), p_value)
	else:
		ProjectSettings.set_setting(p_key, p_value)


## F3: the next effect off (the one before back on); after the last, all on again.
func _next_effect() -> void:
	if _effect_defaults.is_empty():
		for e in EFFECTS:
			_effect_defaults.append(_effect_get(e[1]))
	if _effect >= 0:
		_effect_set(EFFECTS[_effect][1], _effect_defaults[_effect])
	_effect += 1
	if _effect >= EFFECTS.size():
		_effect = -1
		hud.call("flash", "All effects on")
		return
	_effect_set(EFFECTS[_effect][1], EFFECTS[_effect][2])
	hud.call("flash", "Off: %s  (F3: the next one)" % EFFECTS[_effect][0])


func get_effect_text() -> String:
	return "" if _effect < 0 else "off: " + EFFECTS[_effect][0]


func _relight() -> void:
	_burned_out_t = 0.0
	if campfire_log:
		campfire_log.call("reset_burning")


func _set_weather(p_index: int, p_transition: float) -> void:
	weather_index = clampi(p_index, 0, WEATHER_NAMES.size() - 1)
	if weather:
		weather.call("set_weather", weather_index, p_transition)


## The sun and the moon across the sky; the village's lamps and windows from dusk to dawn.
func _apply_time() -> void:
	var phase := time_of_day
	var elev := sin((phase - 0.25) * TAU) * 64.0
	var azim := phase * 360.0 + 90.0
	sun.rotation_degrees = Vector3(-elev, azim, 0.0)
	sun.visible = elev > -4.0
	sun.light_energy = _sun_energy * clampf((elev + 4.0) / 10.0, 0.0, 1.0)
	moon.rotation_degrees = Vector3(-maxf(-elev, 8.0) * 0.8, azim + 180.0, 0.0)
	moon.visible = elev < 2.0
	moon.light_energy = _moon_energy * clampf((2.0 - elev) / 8.0, 0.0, 1.0)
	var night := clampf((6.0 - elev) / 12.0, 0.0, 1.0)
	for l in _lamps:
		l.light_energy = 2.2 * night
	if _window_material:
		_window_material.emission_energy_multiplier = 0.15 + 2.6 * night


func get_clock_text() -> String:
	var minutes := int(time_of_day * 24.0 * 60.0)
	return "%02d:%02d" % [minutes / 60, minutes % 60]


func get_weather_text() -> String:
	return WEATHER_NAMES[weather_index] + (" (changes by itself)" if auto_weather else "")
