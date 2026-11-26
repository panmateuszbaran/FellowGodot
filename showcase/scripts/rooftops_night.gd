extends Node
## Milestone 216: night falls on the square (N; N again: the day comes back). The sun sets — down its azimuth to
## SUNSET_UNDER degrees under the horizon, its light reddening and gone at the horizon, the sky through its sunset and
## twilight (FellowEngine's atmosphere draws them from the sun's elevation) — then the moon rises over the roofs: the
## Sun's light turned to the moon's (MOON_COLOR, moon_energy, from MOON_UP degrees up, MOON_TURN degrees round from where
## the sun set, a smaller disk), the sky's light dimmed (sky_night) so that its stars show, the houses' windows lit (their
## rooms' lamps: glass.tres, FellowInteriorMaterial3D); the weather keys (rooftops_weather.gd) take get_night() for the
## square's lamps and the eye's adapting. The tools: --night=1 (night at once), --night_at=<s> (night falling then).

@export var duration := 14.0 # s, either way

const SUNSET_UNDER := 10.0 # degrees
const SUNSET_SHARE := 0.55 # of the way: the sun setting; the rest the moon rising
const MOON_UP := 28.0 # degrees
const MOON_TURN := 80.0 # degrees round from the sunset's azimuth (up over the square's west side, seen from its corner)
const MOON_COLOR := Color(0.62, 0.72, 1.0)
const MOON_SIZE := 1.2 # degrees across (the disk drawn and the shadows' softness)
## The moon's light on the square (the sun's 4), the share of it the sky and the clouds are lit by (their sun's), the
## sky's intensity under it (its stars show where it is dark). Tools: --night_set=moon:<e>,sky_share:<s>,sky:<i>.
var moon_energy := 0.15
var moon_sky_share := 0.1
var sky_night := 0.12
const SUNSET_COLOR := Color(1.0, 0.52, 0.3) # the sun's light at the horizon
const WINDOW_LAMPS := 0.55 # the share of the rooms with a lamp on at night
const WINDOW_DAYLIGHT := 0.05 # the moonlight through the windows

var _t := 0.0 # 0 the day, 1 the night
var _to := 0.0
var _at := -1.0
var _clock := 0.0
var _sun: DirectionalLight3D
var _root: Node
var _sun_flat := Vector2(0.0, 1.0) # the sun's azimuth (towards it, seen from above)
var _sun_up := 24.0 # its elevation by day (degrees)
var _sun_energy := 4.0
var _sun_color := Color(1, 1, 1)
var _sun_size := 2.5
var _sky_mat: ShaderMaterial
var _sky_intensity := 1.0
var _disk := -1.0 # the sky's sun_disk_energy by day
const MOON_DISK := 0.1 # the moon's disk this much of the sun's (its glow at night's exposure)
var _glass: Resource
var _glass_lamps := 0.0
var _glass_daylight := 1.0
var _applied := -1.0
var _sky_up := 10.0 # rooftops.gd's sky_sun_elevation by day


func _ready() -> void:
	_root = get_parent()
	_sun = _root.get_node_or_null(^"Sun") as DirectionalLight3D
	if _sun == null:
		return
	var to_sun := _sun.global_basis.z.normalized()
	var flat := Vector2(to_sun.x, to_sun.z)
	_sun_flat = flat.normalized() if flat.length() > 1e-4 else Vector2(0.0, 1.0)
	_sun_up = rad_to_deg(asin(clampf(to_sun.y, -1.0, 1.0)))
	_sun_energy = _sun.light_energy
	_sun_color = _sun.light_color
	_sun_size = _sun.light_angular_distance
	if "sky_sun_elevation" in _root:
		_sky_up = float(_root.get("sky_sun_elevation"))
	var we := _root.get_node_or_null(^"WorldEnvironment") as WorldEnvironment
	if we and we.environment and we.environment.sky and we.environment.sky.sky_material is ShaderMaterial:
		_sky_mat = we.environment.sky.sky_material as ShaderMaterial
		var v: Variant = _sky_mat.get_shader_parameter("intensity")
		_sky_intensity = float(v) if v != null else 1.0
	if ResourceLoader.exists("res://rooftops/materials/glass.tres"):
		_glass = load("res://rooftops/materials/glass.tres")
		if _glass and "lamp_share" in _glass:
			_glass_lamps = float(_glass.get("lamp_share"))
			_glass_daylight = float(_glass.get("daylight"))
		else:
			_glass = null
	for a in OS.get_cmdline_user_args():
		if a == "--night=1":
			_t = 1.0
			_to = 1.0
		elif a.begins_with("--night_at="):
			_at = float(a.get_slice("=", 1))
		elif a.begins_with("--night_set="):
			for kv in a.get_slice("=", 1).split(","):
				var v := float(kv.get_slice(":", 1))
				match kv.get_slice(":", 0):
					"moon":
						moon_energy = v
					"sky_share":
						moon_sky_share = v
					"sky":
						sky_night = v
	# (applied from the first frame: rooftops.gd sets the sky's disk after this node is ready)


func _unhandled_input(event: InputEvent) -> void:
	if _sun == null or not (event is InputEventKey) or not event.pressed or event.echo:
		return
	if (event as InputEventKey).physical_keycode == KEY_N:
		toggle()


## Night falls, or the day comes back.
func toggle() -> void:
	_to = 0.0 if _to > 0.5 else 1.0


## 0 by day, 1 with the moon up: how far the night has fallen (the lamps lit from the sunset on).
func get_night() -> float:
	return smoothstep(0.3, 0.9, _t)


func is_night() -> bool:
	return _to > 0.5


func _process(delta: float) -> void:
	if _sun == null:
		return
	_clock += delta
	if _at >= 0.0 and _clock >= _at:
		_at = -1.0
		_to = 1.0
	if _t != _to:
		_t = move_toward(_t, _to, delta / maxf(duration, 0.01))
	if _t != _applied:
		_apply()


func _apply() -> void:
	_applied = _t
	var elevation: float
	var flat := _sun_flat
	var energy: float
	var color: Color
	var size := _sun_size
	if _t <= SUNSET_SHARE:
		# The sun setting: down its azimuth, its light reddening to the horizon and gone under it.
		var u := smoothstep(0.0, 1.0, _t / SUNSET_SHARE)
		elevation = lerpf(_sun_up, -SUNSET_UNDER, u)
		energy = _sun_energy * smoothstep(-1.5, 6.0, elevation)
		color = _sun_color.lerp(SUNSET_COLOR, smoothstep(14.0, 0.0, elevation) * u)
	else:
		# The moon rising (its light from MOON_UP, where the sky's light had gone).
		var v := smoothstep(0.0, 1.0, (_t - SUNSET_SHARE) / (1.0 - SUNSET_SHARE))
		elevation = MOON_UP
		flat = _sun_flat.rotated(deg_to_rad(MOON_TURN))
		energy = moon_energy * v
		color = MOON_COLOR
		size = MOON_SIZE
	if "sky_sun_elevation" in _root:
		# (the sky's light, and the disk it draws, as high as the moon: seen over the roofs, rising as it brightens)
		var moon_sky := lerpf(_sky_up, MOON_UP, smoothstep(SUNSET_SHARE, 1.0, _t))
		_root.set("sky_sun_elevation", moon_sky)
	var e := deg_to_rad(elevation)
	var d := Vector3(flat.x * cos(e), sin(e), flat.y * cos(e))
	_sun.global_basis = Basis.looking_at(-d, Vector3.UP)
	_sun.light_energy = energy
	_sun.light_angular_distance = size
	if _root.has_method("set_sun_own_color"):
		# (the sky's light through the sunset the day's, the atmosphere darkening it as the sun goes under; under the
		# moon the moon's)
		_root.call("set_sun_own_color", color, 0.0 if _t > SUNSET_SHARE else 1.0, energy * moon_sky_share if _t > SUNSET_SHARE else _sun_energy)
	else:
		_sun.light_color = color
	var night := get_night()
	if _sky_mat:
		var moon := smoothstep(SUNSET_SHARE, 1.0, _t)
		_sky_mat.set_shader_parameter("intensity", lerpf(_sky_intensity, _sky_intensity * sky_night, moon))
		if _disk < 0.0:
			var v: Variant = _sky_mat.get_shader_parameter("sun_disk_energy")
			_disk = float(v) if v != null else 1.0
		_sky_mat.set_shader_parameter("sun_disk_energy", _disk * (MOON_DISK if _t > SUNSET_SHARE else 1.0))
	if _glass:
		_glass.set("lamp_share", lerpf(_glass_lamps, WINDOW_LAMPS, night))
		_glass.set("daylight", lerpf(_glass_daylight, WINDOW_DAYLIGHT, night))


## (tools) where the night is.
func get_stats() -> Dictionary:
	return {"t": snappedf(_t, 0.001), "night": snappedf(get_night(), 0.001), "sun_up": snappedf(rad_to_deg(asin(clampf(_sun.global_basis.z.y, -1.0, 1.0))), 0.1) if _sun else 0.0,
		"energy": snappedf(_sun.light_energy, 0.001) if _sun else 0.0}
