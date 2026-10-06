extends RefCounted
## Graphics presets for FellowBench.
##
## A preset is a flat Dictionary of namespaced keys:
##   vp.*   properties of the root Viewport (set directly)
##   rs.*   global RenderingServer quality knobs (set through RenderingServer API)
##   env.*  Environment properties applied AFTER the scene built its own artistic Environment
##   cam.*  CameraAttributesPractical properties
## Presets never differ between stock and FellowEngine runs: FellowEngine features are toggled
## exclusively through rendering/fellow/* project settings (see apply_fellow()).
## Everything that is applied is exported verbatim into the result JSON (anti-cheating rule).

const BASE := {
	# Viewport
	"vp.msaa_3d": 0,
	"vp.screen_space_aa": 0,
	"vp.use_taa": true,
	"vp.scaling_3d_mode": 0,  # bilinear
	"vp.scaling_3d_scale": 1.0,  # native 100 % render resolution
	"vp.fsr_sharpness": 0.2,
	"vp.texture_mipmap_bias": 0.0,
	"vp.mesh_lod_threshold": 1.0,
	"vp.anisotropic_filtering_level": 4,  # 16x
	"vp.positional_shadow_atlas_size": 4096,
	"vp.positional_shadow_atlas_16_bits": true,
	"vp.use_debanding": true,
	"vp.use_occlusion_culling": false,
	"vp.use_hdr_2d": false,
	# RenderingServer
	"rs.directional_shadow_size": 4096,
	"rs.directional_shadow_16_bits": true,
	"rs.directional_soft_shadow_quality": 3,  # SHADOW_QUALITY_SOFT_MEDIUM
	"rs.positional_soft_shadow_quality": 3,
	"rs.ssao_quality": 3,  # high
	"rs.ssao_half_size": false,
	"rs.ssao_adaptive_target": 0.5,
	"rs.ssao_blur_passes": 2,
	"rs.ssao_fadeout_from": 50.0,
	"rs.ssao_fadeout_to": 300.0,
	"rs.ssil_quality": 2,
	"rs.ssil_half_size": true,
	"rs.ssil_adaptive_target": 0.5,
	"rs.ssil_blur_passes": 4,
	"rs.ssil_fadeout_from": 50.0,
	"rs.ssil_fadeout_to": 300.0,
	"rs.volumetric_fog_size": 64,
	"rs.volumetric_fog_depth": 64,
	"rs.volumetric_fog_filter": true,
	"rs.glow_bicubic_upscale": true,
}

const PRESETS := {
	# Primary benchmark configuration: native resolution, TAA, high shadows / SSAO.
	"high": {},
	"high_noaa": {"vp.use_taa": false},
	"high_smaa": {"vp.use_taa": false, "vp.screen_space_aa": 2},
	"high_msaa4": {"vp.use_taa": false, "vp.msaa_3d": 2},
	# Offline-ish reference used for AA/temporal ground truth: 2x2 supersampling + 8x MSAA, no temporal filter.
	"reference_ssaa": {"vp.use_taa": false, "vp.msaa_3d": 3, "vp.scaling_3d_scale": 2.0},
	"medium": {
		"rs.directional_shadow_size": 2048, "rs.directional_soft_shadow_quality": 2,
		"rs.positional_soft_shadow_quality": 2, "vp.positional_shadow_atlas_size": 2048,
		"rs.ssao_quality": 2, "rs.ssao_half_size": true, "vp.anisotropic_filtering_level": 2,
	},
}


static func build(name: String, overrides: Dictionary) -> Dictionary:
	if not PRESETS.has(name):
		push_error("Unknown preset '%s'" % name)
		name = "high"
	var p := BASE.duplicate()
	p.merge(PRESETS[name], true)
	p.merge(overrides, true)
	p["_name"] = name
	p["_overrides"] = overrides
	return p


static func parse_overrides(s: String) -> Dictionary:
	# "vp.use_taa=false,env.glow_enabled=true,rs.ssao_quality=2"
	var out := {}
	if s.is_empty():
		return out
	for kv in s.split(",", false):
		var parts := kv.split("=", false, 1)
		if parts.size() == 2:
			out[parts[0].strip_edges()] = parse_value(parts[1].strip_edges())
	return out


static func parse_value(v: String) -> Variant:
	if v == "true":
		return true
	if v == "false":
		return false
	if v.is_valid_int():
		return v.to_int()
	if v.is_valid_float():
		return v.to_float()
	return v


static func apply(p: Dictionary, vp: Viewport, env: Environment, cam_attr: CameraAttributes) -> void:
	for k in p.keys():
		var key := str(k)
		if key.begins_with("vp."):
			vp.set(key.substr(3), p[k])
		elif key.begins_with("env.") and env:
			env.set(key.substr(4), p[k])
		elif key.begins_with("cam.") and cam_attr:
			cam_attr.set(key.substr(4), p[k])
	RenderingServer.directional_shadow_atlas_set_size(p["rs.directional_shadow_size"], p["rs.directional_shadow_16_bits"])
	RenderingServer.directional_soft_shadow_filter_set_quality(p["rs.directional_soft_shadow_quality"])
	RenderingServer.positional_soft_shadow_filter_set_quality(p["rs.positional_soft_shadow_quality"])
	RenderingServer.environment_set_ssao_quality(p["rs.ssao_quality"], p["rs.ssao_half_size"], p["rs.ssao_adaptive_target"], p["rs.ssao_blur_passes"], p["rs.ssao_fadeout_from"], p["rs.ssao_fadeout_to"])
	RenderingServer.environment_set_ssil_quality(p["rs.ssil_quality"], p["rs.ssil_half_size"], p["rs.ssil_adaptive_target"], p["rs.ssil_blur_passes"], p["rs.ssil_fadeout_from"], p["rs.ssil_fadeout_to"])
	RenderingServer.environment_set_volumetric_fog_volume_size(p["rs.volumetric_fog_size"], p["rs.volumetric_fog_depth"])
	RenderingServer.environment_set_volumetric_fog_filter_active(p["rs.volumetric_fog_filter"])
	RenderingServer.environment_glow_set_use_bicubic_upscale(p["rs.glow_bicubic_upscale"])


## Reads back what the engine actually uses, so the report shows effective values, not intentions.
static func effective(vp: Viewport, env: Environment, cam_attr: CameraAttributes) -> Dictionary:
	var out := {}
	for prop in ["msaa_3d", "screen_space_aa", "use_taa", "scaling_3d_mode", "scaling_3d_scale", "fsr_sharpness", "texture_mipmap_bias", "mesh_lod_threshold", "anisotropic_filtering_level", "positional_shadow_atlas_size", "positional_shadow_atlas_16_bits", "use_debanding", "use_occlusion_culling", "use_hdr_2d"]:
		out["vp." + prop] = vp.get(prop)
	out["vp.size"] = [vp.get_visible_rect().size.x, vp.get_visible_rect().size.y]
	if env:
		for prop in ["background_mode", "background_energy_multiplier", "ambient_light_source", "ambient_light_energy", "ambient_light_sky_contribution", "reflected_light_source",
				"tonemap_mode", "tonemap_exposure", "tonemap_white", "tonemap_agx_white", "tonemap_agx_contrast",
				"glow_enabled", "glow_intensity", "glow_strength", "glow_mix", "glow_bloom", "glow_blend_mode", "glow_hdr_threshold", "glow_hdr_scale", "glow_hdr_luminance_cap", "glow_normalized",
				"ssao_enabled", "ssao_radius", "ssao_intensity", "ssao_power", "ssil_enabled", "ssil_radius", "ssr_enabled", "ssr_max_steps",
				"sdfgi_enabled", "fog_enabled", "fog_density", "volumetric_fog_enabled", "volumetric_fog_density", "adjustment_enabled"]:
			out["env." + prop] = env.get(prop)
		var levels := []
		for i in range(7):
			levels.append(env.get_glow_level(i))
		out["env.glow_levels"] = levels
	if cam_attr:
		out["cam.exposure_multiplier"] = cam_attr.exposure_multiplier
		out["cam.auto_exposure_enabled"] = cam_attr.auto_exposure_enabled
	for s in ["rendering/textures/default_filters/anisotropic_filtering_level", "rendering/anti_aliasing/quality/use_taa",
			"rendering/lights_and_shadows/directional_shadow/size", "rendering/driver/threads/thread_model",
			"rendering/rendering_device/driver.windows", "rendering/renderer/rendering_method", "display/window/vsync/vsync_mode", "application/run/max_fps"]:
		out["ps." + s] = ProjectSettings.get_setting(s)
	out["vsync_mode_runtime"] = DisplayServer.window_get_vsync_mode()
	out["max_fps_runtime"] = Engine.max_fps
	return out


## FellowEngine runtime toggles. Stock Godot does not register rendering/fellow/*; there this is a no-op
## and the result is marked fellow.available=false.
static func apply_fellow(spec: String) -> Dictionary:
	var available := ProjectSettings.has_setting("rendering/fellow/enabled")
	var applied := {}
	if not spec.is_empty():
		for kv in spec.split(",", false):
			var parts := kv.split("=", false, 1)
			if parts.size() != 2:
				continue
			# Keys are relative to rendering/fellow/; a full path (rendering/...) is taken as is.
			var k := parts[0].strip_edges()
			var key := k if k.begins_with("rendering/") else "rendering/fellow/" + k
			var val = parse_value(parts[1].strip_edges())
			if available:
				ProjectSettings.set_setting(key, val)
			applied[key] = val
	var state := {}
	if available:
		for prop in ProjectSettings.get_property_list():
			var n: String = prop["name"]
			if n.begins_with("rendering/fellow/"):
				state[n] = ProjectSettings.get_setting(n)
	return {"available": available, "requested": applied, "state": state}
