extends Node3D
## The rooftops scene: a Renaissance square at the golden hour (built by tools/build_rooftops.gd). Its sky's settings:
## few clouds, high and thin, so that the low sun reaches the square.

@export var cloud_coverage := 0.15
@export var cloud_density := 0.7


func _ready() -> void:
	ProjectSettings.set_setting("rendering/fellow/post/clouds/coverage", cloud_coverage)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/density", cloud_density)
