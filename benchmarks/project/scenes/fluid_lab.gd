extends "res://bench/scene_base.gd"
## BENCHMARK D - Fluid Lab: runtime GPU shallow-water simulation (FellowEngine module fellow_fluid).
## Stock Godot has no GPU fluid system: on stock builds this scene reports "unsupported" and exits.

func build(p_args: Dictionary) -> void:
	args = p_args
	bench_name = "fluid"
	if not ClassDB.class_exists("FellowShallowWater3D"):
		load_error = "unsupported: FellowShallowWater3D not available (stock Godot has no GPU fluid module)"
		return
	_setup_common()
	var impl = load("res://scenes/fluid_lab_impl.gd")
	if impl == null:
		load_error = "fluid_lab_impl.gd missing"
		return
	await impl.build_into(self, args)
