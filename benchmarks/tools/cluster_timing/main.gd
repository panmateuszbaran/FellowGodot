extends Node3D
# Cluster build timing: a large procedural mesh (N x N grid with a displaced height field), time from adding it to
# the scene to the first drawn frame, and the fellow cluster stats.
var t0 := 0
func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	var n := 1000
	for a in args:
		if a.begins_with("--n="):
			n = int(a.substr(4))
		if a.begins_with("--fellow="):
			for kv in a.substr(9).split(",", false):
				var p := kv.split("=")
				ProjectSettings.set_setting("rendering/fellow/" + p[0], p[1] == "true" if p[1] in ["true", "false"] else int(p[1]))
	var verts := PackedVector3Array()
	verts.resize(n * n)
	for y in n:
		for x in n:
			verts[y * n + x] = Vector3(x * 0.05, sin(x * 0.07) * cos(y * 0.05) * 0.5, y * 0.05)
	var idx := PackedInt32Array()
	idx.resize((n - 1) * (n - 1) * 6)
	var k := 0
	for y in n - 1:
		for x in n - 1:
			var a := y * n + x
			idx[k] = a; idx[k + 1] = a + 1; idx[k + 2] = a + n
			idx[k + 3] = a + 1; idx[k + 4] = a + n + 1; idx[k + 5] = a + n
			k += 6
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = verts
	arr[Mesh.ARRAY_INDEX] = idx
	var cam := Camera3D.new()
	add_child(cam)
	cam.position = Vector3(n * 0.025, 5, -5)
	cam.look_at(Vector3(n * 0.025, 0, n * 0.025))
	await get_tree().process_frame
	t0 = Time.get_ticks_usec()
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var t1 := Time.get_ticks_usec()
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	add_child(mi)
	await RenderingServer.frame_post_draw
	var t2 := Time.get_ticks_usec()
	await RenderingServer.frame_post_draw
	var t3 := Time.get_ticks_usec()
	var st := {}
	if RenderingServer.has_method("fellow_get_stats"):
		st = RenderingServer.call("fellow_get_stats")
	print("TIMING tris=%d add_surface_ms=%.1f first_frame_ms=%.1f build_ms=%.1f join_wait_ms=%.1f active=%s" % [idx.size() / 3, (t1 - t0) / 1000.0, (t2 - t1) / 1000.0, st.get("geometry_cluster_build_usec", 0) / 1000.0, st.get("geometry_cluster_join_wait_usec", 0) / 1000.0, st.get("geometry_cluster_surfaces_active", -1)])
	if "--update" in args:
		var data := PackedByteArray()
		data.resize(12 * 10)
		RenderingServer.mesh_surface_update_vertex_region(mesh.get_rid(), 0, 0, data)
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		st = RenderingServer.call("fellow_get_stats")
		print("AFTER_UPDATE dropped=%s draws=%s" % [st.get("geometry_cluster_surfaces_dropped"), st.get("geometry_cluster_draws")])
	get_tree().quit()
