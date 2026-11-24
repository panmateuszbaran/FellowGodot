extends CompositorEffect
## Milestone 191: each frame's motion vectors (the scene's velocity buffer: the previous frame's UV minus this frame's,
## per pixel) read back after the transparent pass, for play_look.gd --velocity=1 to save beside the frame. Stalls the
## GPU each frame: a measuring tool.

var bytes := PackedByteArray()
var size := Vector2i()

var _rd: RenderingDevice
var _shader := RID()
var _pipeline := RID()
var _copy := RID()


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	needs_motion_vectors = true
	_rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(_make)


func _make() -> void:
	var f: RDShaderFile = load("res://tools/velocity_dump.glsl")
	_shader = _rd.shader_create_from_spirv(f.get_spirv())
	_pipeline = _rd.compute_pipeline_create(_shader)


func _render_callback(_type: int, p_data: RenderData) -> void:
	var buffers := p_data.get_render_scene_buffers() as RenderSceneBuffersRD
	if buffers == null or not _pipeline.is_valid():
		return
	var s := buffers.get_internal_size()
	var velocity := buffers.get_velocity_texture()
	if not velocity.is_valid():
		return
	if s != size or not _copy.is_valid():
		if _copy.is_valid():
			_rd.free_rid(_copy)
		var tf := RDTextureFormat.new()
		tf.format = RenderingDevice.DATA_FORMAT_R16G16_SFLOAT
		tf.width = s.x
		tf.height = s.y
		tf.usage_bits = RenderingDevice.TEXTURE_USAGE_STORAGE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT | RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
		_copy = _rd.texture_create(tf, RDTextureView.new())
		size = s
	var u0 := RDUniform.new()
	u0.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u0.binding = 0
	u0.add_id(velocity)
	var u1 := RDUniform.new()
	u1.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u1.binding = 1
	u1.add_id(_copy)
	var set := _rd.uniform_set_create([u0, u1], _shader, 0)
	var pc := PackedInt32Array([s.x, s.y, 0, 0]).to_byte_array()
	var list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(list, _pipeline)
	_rd.compute_list_bind_uniform_set(list, set, 0)
	_rd.compute_list_set_push_constant(list, pc, pc.size())
	_rd.compute_list_dispatch(list, (s.x + 7) / 8, (s.y + 7) / 8, 1)
	_rd.compute_list_end()
	bytes = _rd.texture_get_data(_copy, 0)
	_rd.free_rid(set)
