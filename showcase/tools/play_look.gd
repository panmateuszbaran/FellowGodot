extends SceneTree
## The rooftops as they play (milestone 191: the owner saw the buildings' shadows flicker while running): one of the
## demo's shots (rooftops_demo.gd: the character sprinting, the camera following it as a player's does), every frame of
## a stretch of it saved (<dir>/f%04d.png) for flicker_metrics.py, with one feature off. The frame rate fixed (pass
## --fixed-fps 60): the same frames every run, whatever is off.
##
##   godot --path showcase --resolution 1920x1080 --fixed-fps 60 -s res://tools/play_look.gd -- --demo \
##       --demo_only=run --out=<dir> [--skip=120] [--frames=180] [--config=base] [--hide_player=1] [--set=<setting>=<value>]...
##
## --config: base (nothing off), no_taa, taa_m188 (TAA v2 as before milestone 189), no_shadow (the sun's shadows),
## no_soft (its soft filter), no_contact (contact shadows), no_cache (the directional shadow cache), no_ssao (SSAO and
## SSIL), no_sdfgi, no_gather (SDFGI's screen gather), no_weathering (FellowWeathering3D), no_relief (the facades'
## parallax), no_people (the crowd), no_lod (every mesh at its finest level of detail), no_shadow_lod (the shadows'
## casters at theirs: the directional shadow cache's texel LOD off). --hide_player=1 hides the character's mesh (the camera follows it all the same).
## --velocity=1 also saves each frame's motion vectors (<dir>/v%04d.bin: width and height as two int32, then RG16F per
## pixel; velocity_dump.gd) for reproj_flicker.py.

var _out := "user://play_look"
var _skip := 120
var _frames := 180
var _config := "base"
var _hide_player := false
var _velocity: CompositorEffect = null


func _init() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.get_slice("=", 1)
		elif a.begins_with("--skip="):
			_skip = int(a.get_slice("=", 1))
		elif a.begins_with("--frames="):
			_frames = int(a.get_slice("=", 1))
		elif a.begins_with("--config="):
			_config = a.get_slice("=", 1)
		elif a == "--hide_player=1":
			_hide_player = true
		elif a == "--velocity=1":
			_velocity = (load("res://tools/velocity_dump.gd") as GDScript).new()
		elif a.begins_with("--set="):
			var kv := a.substr(6)
			var value = str_to_var(kv.get_slice("=", 1))
			ProjectSettings.set_setting(kv.get_slice("=", 0), value if value != null else kv.get_slice("=", 1))
	DirAccess.make_dir_recursive_absolute(_out)
	var sc: Node = (load("res://rooftops/rooftops.tscn") as PackedScene).instantiate()
	match _config:
		"no_weathering":
			var w := sc.get_node_or_null("Weathering") as Node3D
			if w:
				w.visible = false
		"no_people":
			for n in ["Crowd", "Birds"]:
				var c := sc.get_node_or_null(n)
				if c:
					sc.remove_child(c)
					c.free()
	root.add_child(sc)
	await process_frame
	var env: Environment = null
	for n in sc.find_children("*", "WorldEnvironment", true, false):
		env = (n as WorldEnvironment).environment
	_configure(sc, env)
	if _velocity:
		for n in sc.find_children("*", "WorldEnvironment", true, false):
			var we := n as WorldEnvironment
			var comp: Compositor = we.compositor if we.compositor else Compositor.new()
			var effects := comp.compositor_effects
			effects.append(_velocity)
			comp.compositor_effects = effects
			we.compositor = comp
	if _hide_player:
		var model := sc.get_node_or_null("Player/Model") as Node3D
		if model:
			model.visible = false
	for i in _skip + _frames:
		await process_frame
		if i >= _skip:
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png("%s/f%04d.png" % [_out, i - _skip])
			if _velocity:
				var f := FileAccess.open("%s/v%04d.bin" % [_out, i - _skip], FileAccess.WRITE)
				var sz: Vector2i = _velocity.get("size")
				f.store_32(sz.x)
				f.store_32(sz.y)
				f.store_buffer(_velocity.get("bytes"))
				f.close()
	print("play_look: %s %d frames" % [_config, _frames])
	quit()


func _configure(p_scene: Node, p_env: Environment) -> void:
	match _config:
		"no_taa":
			root.use_taa = false
		"taa_m188":
			ProjectSettings.set_setting("rendering/fellow/postfx/taa_v2_stability", 0.0)
			ProjectSettings.set_setting("rendering/fellow/postfx/taa_v2_soften_untrusted", 0.0)
		"no_shadow":
			var sun := p_scene.get_node_or_null("Sun") as DirectionalLight3D
			if sun:
				sun.shadow_enabled = false
		"no_soft":
			RenderingServer.directional_soft_shadow_filter_set_quality(RenderingServer.SHADOW_QUALITY_HARD)
		"no_contact":
			ProjectSettings.set_setting("rendering/fellow/post/contact_shadows/enabled", false)
		"no_cache":
			ProjectSettings.set_setting("rendering/fellow/shadows/directional_cache", false)
		"no_ssao":
			if p_env:
				p_env.ssao_enabled = false
				p_env.ssil_enabled = false
		"no_sdfgi":
			if p_env:
				p_env.sdfgi_enabled = false
		"no_gather":
			ProjectSettings.set_setting("rendering/fellow/sdfgi/screen_gather", false)
		"no_lod":
			root.mesh_lod_threshold = 0.0
		"no_shadow_lod":
			ProjectSettings.set_setting("rendering/fellow/shadows/directional_cache_texel_lod", 0.0)
		"no_relief":
			_set_shader_param(p_scene, "facade_pbr", "parallax_depth", 0.0)


## Every ShaderMaterial of the scene's meshes (and their overrides) whose shader is <shader>.gdshader given a value.
func _set_shader_param(p_scene: Node, p_shader: String, p_param: String, p_value: Variant) -> void:
	var stack: Array[Node] = [p_scene]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		stack.append_array(n.get_children())
		var mats: Array[Material] = []
		var mi := n as MeshInstance3D
		if mi and mi.mesh:
			for i in mi.mesh.get_surface_count():
				mats.append(mi.get_surface_override_material(i) if mi.get_surface_override_material(i) else mi.mesh.surface_get_material(i))
		for mat in mats:
			var sm := mat as ShaderMaterial
			if sm and sm.shader and sm.shader.resource_path.get_file() == p_shader + ".gdshader":
				sm.set_shader_parameter(p_param, p_value)
