extends "res://scripts/terrain.gd"
# Milestone 70 (decision 0178): the terrain scene drawn from its virtual texture — its four layers baked into pages
# (the coarsest and nearest first, 16 a frame) and read through its page table.


func _ready() -> void:
	super()
	var t = get_child(get_child_count() - 1)
	t.virtual_texture = true
