extends Node3D
## Turns its node; fine.

@export var speed := 1.0


func _process(delta: float) -> void:
	rotate_y(speed * delta)
