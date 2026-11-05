extends Node3D
## A runtime error at _ready (calling a method on null): the check reports it with this file and line.


func _ready() -> void:
	var target: Node3D = get_node_or_null("DoesNotExist")
	target.rotate_y(1.0)
