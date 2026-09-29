extends RefCounted
## Deterministic camera path: keyframes of (time, position, look-at target, fov).
## Positions and targets are interpolated with uniform Catmull-Rom splines, fov linearly.
## Sampling depends only on the virtual time argument, never on wall-clock time,
## so every build renders exactly the same camera sequence.

var keys: Array = []  # Array of Dictionary {t, pos, target, fov}


static func from_array(arr: Array):
	var p = load("res://bench/camera_path.gd").new()
	for k in arr:
		p.add_key(k[0], k[1], k[2], k[3] if k.size() > 3 else 60.0)
	return p


func add_key(t: float, pos: Vector3, target: Vector3, fov: float = 60.0) -> void:
	keys.append({"t": t, "pos": pos, "target": target, "fov": fov})


func duration() -> float:
	return keys[keys.size() - 1]["t"] if keys.size() > 0 else 0.0


func _cr(p0: Vector3, p1: Vector3, p2: Vector3, p3: Vector3, u: float) -> Vector3:
	var u2 := u * u
	var u3 := u2 * u
	return 0.5 * ((2.0 * p1) + (-p0 + p2) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3)


func sample(t: float) -> Dictionary:
	var n := keys.size()
	if n == 0:
		return {"xform": Transform3D.IDENTITY, "fov": 60.0}
	if n == 1 or t <= keys[0]["t"]:
		return _make(keys[0]["pos"], keys[0]["target"], keys[0]["fov"])
	if t >= keys[n - 1]["t"]:
		return _make(keys[n - 1]["pos"], keys[n - 1]["target"], keys[n - 1]["fov"])
	var i := 0
	while i < n - 1 and keys[i + 1]["t"] < t:
		i += 1
	var k1 = keys[i]
	var k2 = keys[i + 1]
	var k0 = keys[max(i - 1, 0)]
	var k3 = keys[min(i + 2, n - 1)]
	var u: float = (t - k1["t"]) / max(k2["t"] - k1["t"], 1e-6)
	var pos := _cr(k0["pos"], k1["pos"], k2["pos"], k3["pos"], u)
	var tgt := _cr(k0["target"], k1["target"], k2["target"], k3["target"], u)
	var fov: float = lerp(float(k1["fov"]), float(k2["fov"]), u)
	return _make(pos, tgt, fov)


func _make(pos: Vector3, target: Vector3, fov: float) -> Dictionary:
	var up := Vector3.UP
	var dir := (target - pos).normalized()
	if abs(dir.dot(up)) > 0.999:
		up = Vector3.FORWARD
	var xf := Transform3D(Basis.IDENTITY, pos).looking_at(target, up)
	return {"xform": xf, "fov": fov}


func to_json() -> Array:
	var out := []
	for k in keys:
		out.append({"t": k["t"], "pos": [k["pos"].x, k["pos"].y, k["pos"].z], "target": [k["target"].x, k["target"].y, k["target"].z], "fov": k["fov"]})
	return out
