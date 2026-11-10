extends "res://scripts/terrain.gd"
# Milestone 64 (decision 0172): the terrain scene with holes — a round one and a trench cut with paint_holes(), and a
# rectangle stamped with blit_hole_region() — looked at from above their rims: the patches over them drawn with the
# material that discards their triangles, the rest with the terrain's own.


func _ready() -> void:
	super()
	var t = get_child(get_child_count() - 1)
	# In the terrain's local metres (its origin at (-128, 0, -200)).
	t.paint_holes(t.to_global(Vector3(128, 0, 150)), 9.0, true)
	for i in 12:
		t.paint_holes(t.to_global(Vector3(90 + i * 3.0, 0, 175)), 2.5, true)
	var stamp := Image.create(10, 6, false, Image.FORMAT_L8)
	stamp.fill(Color(1, 1, 1))
	t.blit_hole_region(stamp, Vector2i(160, 170))
