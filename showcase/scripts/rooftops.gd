extends Node3D
## The rooftops scene: a Renaissance square at the golden hour (built by tools/build_rooftops.gd). Its sky's settings
## (few clouds, high and thin, so that the low sun reaches the square), and the mouse: captured for looking round, Esc
## frees it, a click takes it again.

@export var cloud_coverage := 0.15
@export var cloud_density := 0.7


func _ready() -> void:
	ProjectSettings.set_setting("rendering/fellow/post/clouds/coverage", cloud_coverage)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/density", cloud_density)
	if not OS.get_cmdline_user_args().has("--demo") and DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_F11:
		var fs := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if fs else DisplayServer.WINDOW_MODE_FULLSCREEN)
