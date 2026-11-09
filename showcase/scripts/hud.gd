extends CanvasLayer
## The showcase's HUD: the title, the clock, the weather and the frame rate, the keys (F1), a note while
## FellowPipelineWarmer3D compiles the pipelines around the camera, and short messages.

@export var showcase_path := NodePath("..")
@export var warmer_path := NodePath("../PipelineWarmer")

var _info: Label
var _help: Label
var _note: Label
var _flash: Label
var _flash_t := 0.0
var _root: Control
var _help_t := 25.0
var _fade: ColorRect
var _fade_t := 0.0

const HELP := """WASD  walk    Shift  run    JUMP    Mouse  look    Wheel  zoom
1-7  weather (clear, cloudy, overcast, rain, storm, snow, blizzard)    0  weather changes by itself
T  clock on/off    [ ]  an hour back/forward    F  light the fire again
F1  help    F2  HUD    F3  one effect off at a time    F11  full screen    F12  screenshot    Esc  free the mouse"""


func _label(p_size: int, p_alpha := 1.0) -> Label:
	var l := Label.new()
	var ls := LabelSettings.new()
	ls.font_size = p_size
	ls.font_color = Color(1, 1, 1, p_alpha)
	ls.outline_size = 4
	ls.outline_color = Color(0, 0, 0, 0.6)
	ls.shadow_size = 2
	ls.shadow_color = Color(0, 0, 0, 0.4)
	l.label_settings = ls
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


func _ready() -> void:
	_root = Control.new()
	_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	var title := _label(26)
	title.text = "FellowEngine Showcase"
	title.position = Vector2(24, 18)
	_root.add_child(title)
	_info = _label(16, 0.9)
	_info.position = Vector2(26, 56)
	_root.add_child(_info)
	_help = _label(13, 0.8)
	_help.text = HELP
	_help.anchor_top = 1.0
	_help.anchor_bottom = 1.0
	_help.offset_left = 26.0
	_help.offset_bottom = -20.0
	_help.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_root.add_child(_help)
	_note = _label(20)
	_note.set_anchors_preset(Control.PRESET_CENTER)
	_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_note.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_root.add_child(_note)
	_flash = _label(16)
	_flash.set_anchors_preset(Control.PRESET_CENTER_TOP)
	_flash.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_flash.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_flash.position.y = 24
	_root.add_child(_flash)
	_help.visible = true
	# The first frames fade in from black: the sky's light is ready over its first 8 frames (Sky's incremental mode).
	_fade = ColorRect.new()
	_fade.color = Color.BLACK
	_fade.set_anchors_preset(Control.PRESET_FULL_RECT)
	_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_fade)


func flash(p_text: String) -> void:
	_flash.text = p_text
	_flash_t = 4.0


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode == KEY_F1:
			_help.visible = not _help.visible
			_help_t = 0.0
		elif event.physical_keycode == KEY_F2:
			_root.visible = not _root.visible


func _process(delta: float) -> void:
	if _fade:
		_fade_t += delta
		# Black for 10 frames (at least), then 0.6 s to clear.
		if Engine.get_process_frames() > 10:
			_fade.color.a = clampf(1.0 - (_fade_t - 0.15) / 0.6, 0.0, 1.0)
		if _fade.color.a <= 0.0:
			_fade.queue_free()
			_fade = null
	var showcase := get_node_or_null(showcase_path)
	if _help.text == HELP and showcase and showcase.get("player"):
		# The MotionBricks character rolls too.
		_help.text = HELP.replace("JUMP    ", "Space  jump    C  roll    " if showcase.get("player").has_method("get_status") else "Space  jump    ")
	var parts := PackedStringArray()
	if showcase:
		parts.append(showcase.call("get_clock_text") + ("" if showcase.get("time_running") else " (stopped)"))
		parts.append(showcase.call("get_weather_text"))
		parts.append(showcase.call("get_character_text"))
		var off: String = showcase.call("get_effect_text")
		if off != "":
			parts.append(off)
	parts.append("%d fps" % Engine.get_frames_per_second())
	_info.text = "   ".join(parts)
	var warmer := get_node_or_null(warmer_path)
	if warmer and warmer.has_method("get_stats"):
		var st: Dictionary = warmer.get_stats()
		if st.get("running", false):
			_note.text = "Compiling the scene's shaders… %d pipelines" % int(st.get("pipelines", 0))
		else:
			_note.text = ""
	if _help_t > 0.0:
		_help_t -= delta
		if _help_t <= 0.0:
			_help.visible = false
	if _flash_t > 0.0:
		_flash_t -= delta
		if _flash_t <= 0.0:
			_flash.text = ""
