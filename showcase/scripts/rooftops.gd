extends Node3D
## The rooftops scene: a Renaissance square at the golden hour (built by tools/build_rooftops.gd). Its sky (milestone
## 168, beside the owner's reference: a layer of stratocumulus, the owner's choice, thin and dense enough for crisp
## edges, seen as streaks towards the horizon; the air clearer; the clouds' shadows off, so that the low sun reaches the
## square and the climb's walls; a sky lit by a sun 10 degrees up over the square lit from higher, brighter than the
## sun alone would make it, the square's light through the same air); its photographic materials (Poly Haven's,
## tools/fetch_textures.py) put in place of the procedural ones when their textures have been fetched and imported; and
## the mouse: captured for looking round, Esc frees it, a click takes it again.

## Milestone 193 (the owner: a sky more epic, as the Witcher 3's): towering cumulus — their bases at 1.5 km, 2.2 km tall,
## fewer (milestone 168's stratocumulus: 0.6 coverage, 2.5 km up, 0.6 km thick).
@export var cloud_coverage := 0.45
@export var cloud_density := 2.6
@export var cloud_altitude := 1.5 # km
@export var cloud_thickness := 2.2 # km
@export var cloud_shadows := 0.0
@export var atmosphere_mie := 0.5 # the haze's aerosols: half the default's, the sky bluer, the horizon clearer
## Milestone 168: the sky's and the clouds' own sun (SkySun, made here: a light that only the sky sees), this many
## degrees up in the Sun's direction when the Sun is higher: the reference's golden-hour sky, its clouds lit from the
## side, over a square lit from higher (with the Sun itself at 10 degrees the climb's wall falls into its neighbours'
## shade). 0: the Sun lights the sky too.
@export var sky_sun_elevation := 10.0
## Milestone 168: the sky seen and its clouds this much brighter than the Sun's energy makes them (the SkySun's energy),
## the sky's light on the scene (its radiance map: the ambient light, the reflections), the sun's disk and the light
## shafts as they were. Beside the reference our sky had been a dim grey under bright walls, its clouds hardly brighter
## than it (the clouds' multiple scattering approximated by four octaves: their sides lit by a low sun darker than a
## white wall's).
@export var sky_brightness := 3.0
## Milestone 168: the Sun's light coloured as the SkySun's is, by the air a sun sky_sun_elevation degrees up shines
## through (Rayleigh's, the haze's and ozone's optical depths over the two suns' air masses; the light's luminance
## kept): the reference's golden light with the sky's. 1: all of it; 0: the Sun's own colour. Beside the reference our
## sunlit stone under the clearer air had been paler still (red / green 1.09-1.12, 1.13-1.17 with this, against its
## 1.23-1.27).
@export var sun_air := 1.0
## Milestone 193 (the owner: a sky more epic, as the Witcher 3's): the SkySun's own colour — the light the atmosphere
## reddens through its air for the sky and the clouds, so white as the sun's is above the air. The Sun's golden colour
## (the square's light, already the air's) had been reddened twice for the sky: its blue in-scattering by 0.57, the sky
## grey. Its energy keeps the sky's luminance as it was.
@export var sky_sun_color := Color(1.0, 1.0, 1.0)
## Milestone 193: the sky seen this much more saturated than the atmosphere makes it (its shader's sky_saturation; the
## scene's light from the sky unchanged).
@export var sky_saturation := 1.6

const SHAFTS := "rendering/fellow/post/light_shafts/"

var _sun: DirectionalLight3D
var _sky_sun: DirectionalLight3D
var _sun_basis := Basis()
var _sun_energy := 0.0
var _sun_color := Color(1.0, 1.0, 1.0) # the Sun's own (sRGB), before sun_air
var _shafts := Vector2(-1.0, -1.0) # the light shafts' threshold and intensity before this scene changed them
var _air_share := 1.0 # milestone 216: how much of sun_air the Sun's light takes (none from the moon)
var _sky_energy := -1.0 # milestone 216: the SkySun's energy from this, not the Sun's, when set (the sunset's sky)


func _enter_tree() -> void:
	# Milestone 154: the player drawn between the physics ticks (the scene's root has interpolation off, the player on):
	# at a display's 120-165 Hz the body had moved in 60 Hz steps and the camera following it shook.
	get_tree().physics_interpolation = true


func _exit_tree() -> void:
	get_tree().physics_interpolation = ProjectSettings.get_setting("physics/common/physics_interpolation", false)
	if _shafts.x >= 0.0:
		ProjectSettings.set_setting(SHAFTS + "threshold", _shafts.x)
		ProjectSettings.set_setting(SHAFTS + "intensity", _shafts.y)


func _ready() -> void:
	ProjectSettings.set_setting("rendering/fellow/post/clouds/coverage", cloud_coverage)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/density", cloud_density)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/altitude", cloud_altitude)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/thickness", cloud_thickness)
	ProjectSettings.set_setting("rendering/fellow/post/clouds/shadows", cloud_shadows)
	ProjectSettings.set_setting("rendering/fellow/post/atmosphere/mie_scale", atmosphere_mie)
	_photo_materials()
	_sun = get_node_or_null("Sun") as DirectionalLight3D
	if _sun and sky_sun_elevation > 0.0:
		_sky_sun = DirectionalLight3D.new()
		_sky_sun.name = "SkySun"
		_sky_sun.sky_mode = DirectionalLight3D.SKY_MODE_SKY_ONLY
		add_child(_sky_sun)
		_sun.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_ONLY
		_sun_color = _sun.light_color
		var we := get_node_or_null("WorldEnvironment") as WorldEnvironment
		if we and we.environment and we.environment.sky and we.environment.sky.sky_material is ShaderMaterial:
			var sky_mat := we.environment.sky.sky_material as ShaderMaterial
			sky_mat.set_shader_parameter("radiance_scale", 1.0 / sky_brightness)
			sky_mat.set_shader_parameter("sun_disk_energy", 1.0 / sky_brightness)
			sky_mat.set_shader_parameter("sky_saturation", sky_saturation)
		# The light shafts (decision 0116: the sky's pixels brighter than a threshold, blurred towards the sun) as before
		# with the brighter sky: its excess over a threshold that much higher, that much weaker.
		_shafts = Vector2(ProjectSettings.get_setting(SHAFTS + "threshold", 0.5), ProjectSettings.get_setting(SHAFTS + "intensity", 0.0))
		ProjectSettings.set_setting(SHAFTS + "threshold", _shafts.x * sky_brightness)
		ProjectSettings.set_setting(SHAFTS + "intensity", _shafts.y / sky_brightness)
		_follow_sun()
	if not OS.get_cmdline_user_args().has("--demo") and DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _process(_delta: float) -> void:
	if _sky_sun and (_sun.global_basis != _sun_basis or _sun.light_energy != _sun_energy):
		_follow_sun()


## Milestone 216 (rooftops_night.gd): the Sun's own colour (the moon's at night), how much of the air's reddening it
## takes (sun_air's share), and the energy the SkySun follows instead of the Sun's (negative: the Sun's) — the sky keeps
## its light through the sunset while the Sun's on the square fades at the horizon.
func set_sun_own_color(p_color: Color, p_air: float, p_sky_energy := -1.0) -> void:
	_sun_color = p_color
	_air_share = p_air
	_sky_energy = p_sky_energy
	_sun_energy = -1.0 # (followed next frame)


## The SkySun in the Sun's direction (seen from above), at most sky_sun_elevation degrees up; its colour the Sun's own,
## its energy the Sun's times sky_brightness (the sky's and the clouds' light scale with it). The Sun's colour through
## the SkySun's air (sun_air).
func _follow_sun() -> void:
	_sun_basis = _sun.global_basis
	_sun_energy = _sun.light_energy
	var to_sun := _sun_basis.z.normalized()
	var flat := Vector2(to_sun.x, to_sun.z)
	flat = flat.normalized() if flat.length() > 1e-4 else Vector2(0.0, 1.0)
	var sun_up := rad_to_deg(asin(clampf(to_sun.y, -1.0, 1.0)))
	var sky_up := minf(sun_up, sky_sun_elevation)
	var e := deg_to_rad(sky_up)
	var d := Vector3(flat.x * cos(e), sin(e), flat.y * cos(e))
	_sky_sun.global_basis = Basis.looking_at(-d, Vector3.UP)
	_sky_sun.light_color = sky_sun_color
	const Y := Vector3(0.2126, 0.7152, 0.0722)
	var own := _sun_color.srgb_to_linear()
	var sky := sky_sun_color.srgb_to_linear()
	var keep := Vector3(own.r, own.g, own.b).dot(Y) / maxf(Vector3(sky.r, sky.g, sky.b).dot(Y), 1e-4)
	_sky_sun.light_energy = (_sun.light_energy if _sky_energy < 0.0 else _sky_energy) * sky_brightness * keep
	_sky_sun.light_angular_distance = _sun.light_angular_distance
	_sun.light_color = _sun_color
	if sun_air * _air_share > 0.0 and sky_up < sun_up and sky_up > 0.0:
		# Zenith optical depths (FellowEngine's atmosphere, decision 0108: Rayleigh's sRGB coefficients over its 8 km
		# scale height, the haze's extinction over 1.2 km, ozone's absorption over its 30 km tent's 15) and Kasten and
		# Young's air mass.
		var rayleigh: float = ProjectSettings.get_setting("rendering/fellow/post/atmosphere/rayleigh_scale", 1.0)
		var mie: float = ProjectSettings.get_setting("rendering/fellow/post/atmosphere/mie_scale", 1.0)
		var tau := Vector3(7.190e-3, 13.763e-3, 32.519e-3) * 8.0 * rayleigh + Vector3.ONE * 4.44e-3 * 1.2 * mie + Vector3(0.650e-3, 1.881e-3, 0.085e-3) * 15.0
		var extra := _air_mass(sky_up) - _air_mass(sun_up)
		var t := Vector3.ONE.lerp(Vector3(exp(-tau.x * extra), exp(-tau.y * extra), exp(-tau.z * extra)), sun_air * _air_share)
		var c := _sun_color.srgb_to_linear()
		var lit := Vector3(c.r * t.x, c.g * t.y, c.b * t.z)
		const LUMA := Vector3(0.2126, 0.7152, 0.0722)
		lit *= Vector3(c.r, c.g, c.b).dot(LUMA) / maxf(lit.dot(LUMA), 1e-4)
		_sun.light_color = Color(lit.x, lit.y, lit.z).linear_to_srgb()


func _air_mass(p_degrees: float) -> float:
	return 1.0 / (sin(deg_to_rad(p_degrees)) + 0.50572 * pow(p_degrees + 6.07995, -1.6364))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and event.pressed and Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	elif event is InputEventKey and event.pressed and event.physical_keycode == KEY_F11:
		var fs := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if fs else DisplayServer.WINDOW_MODE_FULLSCREEN)


## The photographic materials (rooftops/materials_photo/<name>) in place of the procedural ones of the same name
## (rooftops/materials/<name>) on every mesh, if the textures are there (fetched by tools/fetch_textures.py, imported).
func _photo_materials() -> void:
	const PHOTO := "res://rooftops/materials_photo/"
	if not ResourceLoader.exists("res://rooftops/textures/plastered_wall_diff_2k.jpg") or not DirAccess.dir_exists_absolute(PHOTO):
		return
	var swap := {}
	for f in DirAccess.get_files_at(PHOTO):
		var file := f.trim_suffix(".remap")
		if file.ends_with(".tres") or file.ends_with(".res"):
			var m := load(PHOTO + file) as Material
			if m:
				swap["res://rooftops/materials/" + file] = m
	var stack: Array[Node] = [self]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		var mmi := n as MultiMeshInstance3D
		if mmi and mmi.multimesh and mmi.multimesh.mesh:
			# Milestone 153: the city's houses (instances): their mesh's own materials swapped (it is theirs alone).
			var mesh := mmi.multimesh.mesh
			for i in mesh.get_surface_count():
				var m := mesh.surface_get_material(i)
				if m and swap.has(m.resource_path):
					mesh.surface_set_material(i, swap[m.resource_path])
			continue
		var mi := n as MeshInstance3D
		if mi == null or mi.mesh == null:
			continue
		for i in mi.mesh.get_surface_count():
			var m := mi.mesh.surface_get_material(i)
			if m and swap.has(m.resource_path):
				mi.set_surface_override_material(i, swap[m.resource_path])
