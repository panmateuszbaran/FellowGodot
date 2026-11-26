extends Node3D
## Milestone 211 (the owner: "can the showcase also show fire, smoke and the weather?"): a fire on the square or smoke
## from a chimney, made at run time on this node, FellowEngine's gas (FellowGasVolume3D: fire and smoke simulated on the
## GPU, blown by the scene's wind; asleep, frozen, beyond SLEEP metres from the camera).
##  * kind "brazier": an iron brazier on three legs, three logs burning in it (FellowBurnable3D: they char), its flames
##    lighting the square (the gas's fire light, with shadows); it does not take the player through it (a collider).
##    The rain or the snow on it damps it — less fuel, more smoke — as FellowRain3D's rain says.
##  * kind "chimney": a hearth's smoke from a chimney's top (this node there): warm, grey, carried off by the wind.
## Each box is moved downwind of its source (the scene's Wind): the plume carried off has room to go before the box's
## edge fades it. The wind given to the gas is the scene's (its gusts too) times wind_scale: the square is sheltered by
## its houses (the brazier's flames lean, are not blown out of the box: at the scene's whole 3.5 m/s the fuel left the
## box before it burnt, the fire's power 0.4 for some 20 still); the chimneys' plumes are bent, not torn off. The
## scene's Rain and Wind are found under its root (the owner).

@export_enum("brazier", "chimney") var kind := "brazier"
@export var resolution := 56
@export var sleep := 70.0 # m
@export var wind_scale := -1.0 # (-1: the kind's own: 0.2 the brazier, 0.6 a chimney)
@export var wind_max := 2.5 # m/s: the most wind given to the gas (a blizzard's 14 m/s laid the brazier's flames flat)

const LAYER_WORLD := 1

var _volume: Node3D
var _emitter: Node3D
var _fuel := 0.0
var _smoke := 0.0
var _rain: Node
var _logs: Array[Node] = []
var _restock_t := 0.0
var _wind: Node


func _ready() -> void:
	if not ClassDB.class_exists(&"FellowGasVolume3D"):
		return
	var scene: Node = owner if owner else get_tree().current_scene
	_rain = scene.get_node_or_null("Rain") if scene else null
	var wind: Node = scene.get_node_or_null("Wind") if scene else null
	var downwind := Vector3()
	if wind and wind.has_method("get_direction"):
		_wind = wind
		downwind = global_basis.inverse() * (wind.call("get_direction") as Vector3)
		downwind.y = 0.0
	if wind_scale < 0.0:
		wind_scale = 0.2 if kind == "brazier" else 0.6
	if kind == "brazier":
		_brazier(downwind)
	else:
		_chimney(downwind)
	# (tools: --gas_set_<kind>=name:value,... and --emit_set_<kind>=... set the gas's or the emitter's properties)
	for a in OS.get_cmdline_user_args():
		for pre in ["--gas_set_%s=" % kind, "--emit_set_%s=" % kind]:
			if a.begins_with(pre):
				var target: Node = _volume if pre.begins_with("--gas") else _emitter
				for kv in a.substr(pre.length()).split(","):
					var key := kv.get_slice(":", 0)
					var val := kv.get_slice(":", 1)
					target.set(key, Color(val) if val.begins_with("#") else float(val))
					if pre.begins_with("--emit") and key == "smoke":
						_smoke = float(val)


## A gas volume p_size, its floor p_y above this node, moved p_shift downwind (the emitter kept over the source:
## _source()).
func _gas(p_size: Vector3, p_y: float, p_shift: Vector3) -> Node3D:
	var v: Node3D = ClassDB.instantiate(&"FellowGasVolume3D")
	v.name = "Gas"
	v.set("size", p_size)
	v.set("resolution", resolution)
	v.set("wind_from_scene", false) # (the scene's, scaled: _process)
	v.set("sleep_distance", sleep)
	v.set("edge_fade", 0.7)
	v.set("prewarm_steps", 240)
	v.set("physics_obstacles", false)
	v.position = Vector3(p_shift.x, p_y, p_shift.z)
	add_child(v)
	return v


## An emitter in _volume over this node's origin, p_y above the volume's floor.
func _source(p_y: float) -> Node3D:
	var e: Node3D = ClassDB.instantiate(&"FellowGasEmitter3D")
	e.name = "Source"
	e.position = Vector3(-_volume.position.x, p_y, -_volume.position.z)
	_volume.add_child(e)
	return e


func _brazier(p_downwind: Vector3) -> void:
	var iron := StandardMaterial3D.new()
	iron.albedo_color = Color(0.16, 0.14, 0.13)
	iron.metallic = 0.65
	iron.roughness = 0.55
	var bowl := CylinderMesh.new()
	bowl.top_radius = 0.44
	bowl.bottom_radius = 0.26
	bowl.height = 0.32
	bowl.radial_segments = 20
	bowl.cap_top = false
	_mesh(bowl, Transform3D(Basis(), Vector3(0.0, 0.86, 0.0)), iron)
	var rim := TorusMesh.new()
	rim.inner_radius = 0.42
	rim.outer_radius = 0.47
	rim.rings = 24
	_mesh(rim, Transform3D(Basis(), Vector3(0.0, 1.02, 0.0)), iron)
	var leg := CylinderMesh.new()
	leg.top_radius = 0.025
	leg.bottom_radius = 0.03
	leg.height = 0.95
	leg.radial_segments = 6
	for i in 3:
		var a := TAU * i / 3.0
		var foot := Vector3(sin(a) * 0.48, 0.0, cos(a) * 0.48)
		var top := Vector3(sin(a) * 0.27, 0.82, cos(a) * 0.27)
		var up := (top - foot).normalized()
		var side := up.cross(Vector3(cos(a), 0.0, -sin(a))).normalized()
		_mesh(leg, Transform3D(Basis(side, up, side.cross(up)).orthonormalized(), (foot + top) * 0.5), iron)
	# The embers' bed and the logs crossed in it.
	var bed := CylinderMesh.new()
	bed.top_radius = 0.36
	bed.bottom_radius = 0.3
	bed.height = 0.08
	var ember := StandardMaterial3D.new()
	ember.albedo_color = Color(0.08, 0.06, 0.05)
	ember.emission_enabled = true
	ember.emission = Color(1.0, 0.32, 0.06)
	ember.emission_energy_multiplier = 1.6
	ember.roughness = 0.95
	_mesh(bed, Transform3D(Basis(), Vector3(0.0, 0.92, 0.0)), ember)
	# The gas: its floor at the bowl's, tall enough for the smoke to thin out before the top.
	_volume = _gas(Vector3(3.0, 5.5, 3.0), 0.9, p_downwind * 0.6)
	_volume.set("smoke_dissipation", 0.25)
	_volume.set("turbulence", 4.0)
	_volume.set("vorticity", 0.3)
	_volume.set("fire_intensity", 16.0)
	_volume.set("closed_faces", 4) # the floor
	_volume.set("fire_light_shadows", true)
	_volume.set("fire_light_range", 9.0)
	_volume.set("fire_light_smoothing", 0.15)
	_emitter = _source(0.1)
	_emitter.set("shape", 2) # a cylinder
	_emitter.set("radius", 0.22)
	_emitter.set("height", 0.12)
	_fuel = 2.2
	_smoke = 0.12
	_emitter.set("fuel", _fuel)
	_emitter.set("temperature", 900.0)
	_emitter.set("smoke", _smoke)
	_emitter.set("noise", 0.75)
	_emitter.set("noise_scale", 0.15)
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.27, 0.18, 0.11)
	wood.roughness = 0.9
	var log_mesh := CylinderMesh.new()
	log_mesh.top_radius = 0.06
	log_mesh.bottom_radius = 0.065
	log_mesh.height = 0.62
	log_mesh.radial_segments = 12
	for i in 3:
		var a := TAU * i / 3.0 + 0.4
		var b := Basis(Vector3.UP, a) * Basis(Vector3.FORWARD, PI * 0.5 - 0.35)
		var mi := _mesh(log_mesh, Transform3D(b, Vector3(0.0, 1.0, 0.0)), wood)
		var burn: Node = ClassDB.instantiate(&"FellowBurnable3D")
		burn.set("resolution", 24)
		burn.set("pyrolysis_rate", 0.015) # (a third of the default's: a log lasts)
		mi.add_child(burn)
		# (its gas named: milestone 211's engines found none round the logs — their voxel box's middle, reaching down to the
		# bowl for the ash heap, was under the volume's floor; fixed in FellowBurnable3D since)
		burn.set("gas_volume_path", burn.get_path_to(_volume))
		_logs.append(burn)
	# Not walked through.
	var body := StaticBody3D.new()
	body.collision_layer = LAYER_WORLD
	var cs := CollisionShape3D.new()
	var cyl := CylinderShape3D.new()
	cyl.radius = 0.5
	cyl.height = 1.1
	cs.shape = cyl
	cs.position = Vector3(0.0, 0.55, 0.0)
	body.add_child(cs)
	add_child(body)


func _chimney(p_downwind: Vector3) -> void:
	# A plume (Fire Lab's chimney, benchmarks/project/scenes/fire_lab.gd): from the flue, warm enough to rise, pushed
	# up out of it, lasting long enough to be carried off. A wood fire's smoke, pale blue-grey: it scatters most of the
	# light it takes (Fire Lab's grey soot, 0.5, was a black plume in the bell tower's shade against the sky).
	_volume = _gas(Vector3(6.0, 8.0, 6.0), -0.2, p_downwind * 1.5)
	_volume.set("smoke_dissipation", 0.06)
	_volume.set("smoke_density", 1.6)
	_volume.set("self_shadow", 0.6)
	_volume.set("cooling", 0.6)
	_volume.set("vorticity", 0.25)
	_volume.set("fire_light", false)
	_volume.set("closed_faces", 0)
	_volume.set("wind_rate", 0.3)
	_volume.set("smoke_color", Color(0.82, 0.82, 0.84))
	_emitter = _source(0.25) # (in the flue, under the cap: the smoke comes out from under it)
	_emitter.set("shape", 0) # a sphere in the flue
	_emitter.set("radius", 0.22)
	_fuel = 0.0
	_smoke = 3.0
	_emitter.set("fuel", 0.0)
	_emitter.set("temperature", 160.0)
	_emitter.set("smoke", _smoke)
	_emitter.set("noise", 0.4)
	_emitter.set("noise_scale", 0.4)
	_emitter.set("velocity", Vector3(0.0, 1.0, 0.0))
	_emitter.set("velocity_strength", 0.3)


func _mesh(p_mesh: Mesh, p_xform: Transform3D, p_mat: Material) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = p_mesh
	mi.material_override = p_mat
	mi.transform = p_xform
	add_child(mi)
	return mi


func _process(delta: float) -> void:
	if _volume == null:
		return
	if _wind and is_instance_valid(_wind):
		_volume.set("wind", ((_wind.call("get_velocity_at", _volume.global_position) as Vector3) * wind_scale).limit_length(wind_max))
	if kind != "brazier" or _emitter == null:
		return
	if _rain and is_instance_valid(_rain):
		# The rain or snow falling on it: less fuel burning, more smoke (and steam).
		var falling: float = _rain.get("rain") # (its rain or its snow: the weather's intensity)
		_emitter.set("fuel", _fuel * (1.0 - 0.7 * falling))
		_emitter.set("smoke", _smoke + 0.45 * falling)
	# A log burned through is a new one put on (someone keeps the fire).
	_restock_t += delta
	if _restock_t >= 3.0:
		_restock_t = 0.0
		for b in _logs:
			if b.call("is_burned_out") or float(b.call("get_ash_fraction")) > 0.5:
				b.call("reset_burning")
				break


## (tools) the gas volume.
func get_volume() -> Node3D:
	return _volume


## (tools) the fire's power, the gas's steps and wind, the emitter's fuel, the logs' burning voxels.
func get_stats() -> Dictionary:
	if _volume == null:
		return {}
	var burning := 0
	var hottest := 0.0
	for b in _logs:
		burning += int(b.call("get_burning_voxels"))
		hottest = maxf(hottest, float(b.call("get_max_temperature")))
	return {"power": snappedf(float(_volume.call("get_fire_power")), 0.01), "steps": _volume.call("get_steps_simulated"),
			"sleeping": _volume.call("is_sleeping"), "wind": (_volume.get("wind") as Vector3).snapped(Vector3(0.01, 0.01, 0.01)),
			"fuel": snappedf(float(_emitter.get("fuel")), 0.01), "smoke": snappedf(float(_emitter.get("smoke")), 0.01), "logs_burning": burning,
			"logs_hottest": snappedf(hottest, 1.0)}
