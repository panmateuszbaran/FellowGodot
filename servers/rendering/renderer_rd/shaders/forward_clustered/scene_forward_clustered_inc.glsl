#define M_PI 3.14159265359
#define M_TAU 6.28318530718
#define ROUGHNESS_MAX_LOD 5

#define MAX_VOXEL_GI_INSTANCES 8
#define MAX_VIEWS 2

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_quad : enable

#include "../cluster_data_inc.glsl"
#include "../decal_data_inc.glsl"
#include "../oct_inc.glsl"
#include "../fellow_ddgi_inc.glsl"
#include "../scene_data_inc.glsl"

#if !defined(MODE_RENDER_DEPTH) || defined(MODE_RENDER_MATERIAL) || defined(MODE_RENDER_SDF) || defined(MODE_RENDER_NORMAL_ROUGHNESS) || defined(MODE_RENDER_VOXEL_GI) || defined(TANGENT_USED) || defined(NORMAL_MAP_USED) || defined(BENT_NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED)
#ifndef NORMAL_USED
#define NORMAL_USED
#endif
#endif

#if !defined(TANGENT_USED) && (defined(NORMAL_MAP_USED) || defined(BENT_NORMAL_MAP_USED) || defined(LIGHT_ANISOTROPY_USED))
#define TANGENT_USED
#endif

layout(push_constant, std430) uniform DrawCall {
	uint instance_index;
	uint uv_offset;
	uint multimesh_motion_vectors_current_offset;
	uint multimesh_motion_vectors_previous_offset;
#ifdef UBERSHADER
	uint sc_packed_0;
	uint sc_packed_1;
	uint sc_packed_2;
	uint uc_packed_0;
#endif
}
draw_call;

/* Specialization Constants */

#ifdef UBERSHADER

#define POLYGON_CULL_DISABLED 0
#define POLYGON_CULL_FRONT 1
#define POLYGON_CULL_BACK 2

// Pull the constants from the draw call's push constants.
uint sc_packed_0() {
	return draw_call.sc_packed_0;
}

uint sc_packed_1() {
	return draw_call.sc_packed_1;
}

uint sc_packed_2() {
	return draw_call.sc_packed_2;
}

uint uc_cull_mode() {
	return (draw_call.uc_packed_0 >> 0) & 3U;
}

#else

// Pull the constants from the pipeline's specialization constants.
layout(constant_id = 0) const uint pso_sc_packed_0 = 0;
layout(constant_id = 1) const uint pso_sc_packed_1 = 0;
layout(constant_id = 3) const uint pso_sc_packed_2 = 0;

uint sc_packed_0() {
	return pso_sc_packed_0;
}

uint sc_packed_1() {
	return pso_sc_packed_1;
}

uint sc_packed_2() {
	return pso_sc_packed_2;
}

#endif

bool sc_use_forward_gi() {
	return ((sc_packed_0() >> 0) & 1U) != 0;
}

bool sc_use_light_projector() {
	return ((sc_packed_0() >> 1) & 1U) != 0;
}

bool sc_use_light_soft_shadows() {
	return ((sc_packed_0() >> 2) & 1U) != 0;
}

bool sc_use_directional_soft_shadows() {
	return ((sc_packed_0() >> 3) & 1U) != 0;
}

bool sc_decal_use_mipmaps() {
	return ((sc_packed_0() >> 4) & 1U) != 0;
}

bool sc_projector_use_mipmaps() {
	return ((sc_packed_0() >> 5) & 1U) != 0;
}

bool sc_use_depth_fog() {
	return ((sc_packed_0() >> 6) & 1U) != 0;
}

bool sc_use_lightmap_bicubic_filter() {
	return ((sc_packed_0() >> 7) & 1U) != 0;
}

uint sc_soft_shadow_samples() {
	return (sc_packed_0() >> 8) & 63U;
}

uint sc_penumbra_shadow_samples() {
	return (sc_packed_0() >> 14) & 63U;
}

uint sc_directional_soft_shadow_samples() {
	return (sc_packed_0() >> 20) & 63U;
}

uint sc_directional_penumbra_shadow_samples() {
	return (sc_packed_0() >> 26) & 63U;
}

bool sc_multimesh() {
	return ((sc_packed_1() >> 0) & 1U) != 0;
}

bool sc_multimesh_format_2d() {
	return ((sc_packed_1() >> 1) & 1U) != 0;
}

bool sc_multimesh_has_color() {
	return ((sc_packed_1() >> 2) & 1U) != 0;
}

bool sc_multimesh_has_custom_data() {
	return ((sc_packed_1() >> 3) & 1U) != 0;
}

bool sc_fog_use_legacy_blending() {
	return ((sc_packed_1() >> 4) & 1U) != 0;
}

bool sc_cluster_has_area_light() {
	return ((sc_packed_1() >> 5) & 1U) != 0;
}

// FellowEngine features whose code a specialized pipeline drops when their toggle is off (the runtime scene flags
// still decide per frame when it is on): fewer live registers in the light loops for the configurations without them.
bool sc_fellow_pcss_blend_band() {
	return ((sc_packed_1() >> 6) & 1U) != 0;
}

bool sc_fellow_directional_max_mip() {
	return ((sc_packed_1() >> 7) & 1U) != 0;
}

bool sc_fellow_positional_max_mip() {
	return ((sc_packed_1() >> 8) & 1U) != 0;
}

bool sc_fellow_zero_attenuation_skip() {
	return ((sc_packed_1() >> 9) & 1U) != 0;
}

bool sc_fellow_skip_backfacing_positional() {
	return ((sc_packed_1() >> 10) & 1U) != 0;
}

bool sc_fellow_skip_backfacing_directional() {
	return ((sc_packed_1() >> 11) & 1U) != 0;
}

bool sc_fellow_skip_unblended_cascade() {
	return ((sc_packed_1() >> 12) & 1U) != 0;
}

bool sc_fellow_cluster_log_depth() {
	return ((sc_packed_1() >> 13) & 1U) != 0;
}

bool sc_fellow_blended_alpha_zero_skip() {
	return ((sc_packed_1() >> 14) & 1U) != 0;
}

bool sc_fellow_persistent_instances() {
	return ((sc_packed_1() >> 15) & 1U) != 0;
}

bool sc_fellow_pcss_filter_early_out() {
	return ((sc_packed_1() >> 16) & 1U) != 0;
}

bool sc_fellow_shadow_tap_batch() {
	return ((sc_packed_1() >> 17) & 1U) != 0;
}

bool sc_fellow_shadow_tap_batch_positional() {
	return ((sc_packed_1() >> 18) & 1U) != 0;
}

bool sc_fellow_pcss_quad_search() {
	return ((sc_packed_1() >> 19) & 1U) != 0;
}
#define FELLOW_HAS_PCSS_QUAD_SEARCH

bool sc_fellow_light_fields_early() {
	return ((sc_packed_1() >> 20) & 1U) != 0;
}
#define FELLOW_HAS_LIGHT_FIELDS_EARLY

bool sc_fellow_light_prefetch_next() {
	return ((sc_packed_1() >> 21) & 1U) != 0;
}

bool sc_fellow_light_prefetch_all() {
	return ((sc_packed_1() >> 22) & 1U) != 0;
}

bool sc_fellow_light_prefetch_words() {
	return ((sc_packed_1() >> 23) & 1U) != 0;
}

bool sc_fellow_light_hot() {
	return ((sc_packed_1() >> 24) & 1U) != 0;
}

bool sc_fellow_light_hot_ubo() {
	return ((sc_packed_1() >> 25) & 1U) != 0;
}

bool sc_fellow_light_tile_cull() {
	return ((sc_packed_1() >> 26) & 1U) != 0;
}
#define FELLOW_HAS_LIGHT_TILE_CULL

// FellowEngine (shading/absent_features_specialized, decision 0057): what this frame's colour passes do not have, as
// specialization constants: their loops would run no iteration, and are not compiled.
bool sc_fellow_no_decals() {
	return ((sc_packed_1() >> 27) & 1U) != 0;
}

bool sc_fellow_no_reflection_probes() {
	return ((sc_packed_1() >> 28) & 1U) != 0;
}

bool sc_fellow_no_directional_lights() {
	return ((sc_packed_1() >> 29) & 1U) != 0;
}

bool sc_fellow_no_omni_lights() {
	return ((sc_packed_1() >> 30) & 1U) != 0;
}

bool sc_fellow_no_spot_lights() {
	return ((sc_packed_1() >> 31) & 1U) != 0;
}

bool sc_fellow_no_positional_shadows() {
	return ((sc_packed_2() >> 0) & 1U) != 0;
}
#define FELLOW_HAS_NO_POSITIONAL_SHADOWS

// FellowEngine ray-traced sun shadows (decision 0082): the opaque colour passes read the first directional light's
// shadow from the traced mask (fellow_rt_shadow_mask) where it is valid.
bool sc_fellow_rt_sun_shadow() {
	return ((sc_packed_2() >> 1) & 1U) != 0;
}
#define FELLOW_HAS_RT_SUN_SHADOW

// FellowEngine contact shadows (decision 0107): the first directional light's shadow multiplied by the contact mask
// (bound where the traced mask is, fellow_rt_shadow_mask; the two are never on together).
bool sc_fellow_contact_shadow() {
	return ((sc_packed_2() >> 5) & 1U) != 0;
}

// FellowEngine weather (decision 0124): the depth pre-pass and opaque colour passes wet the materials (FellowRain3D).
bool sc_fellow_wetness() {
	return ((sc_packed_2() >> 6) & 1U) != 0;
}

// FellowEngine cloud shadows (decision 0133): the colour passes multiply the sun's shadow by the clouds' shadow map.
bool sc_fellow_cloud_shadow() {
	return ((sc_packed_2() >> 7) & 1U) != 0;
}

// FellowEngine DDGI (decision 0083): the colour passes take the ambient light from the probe volume.
bool sc_fellow_ddgi() {
	return ((sc_packed_2() >> 2) & 1U) != 0;
}

// FellowEngine ray-traced reflections (decision 0084): the opaque colour passes read the traced reflections.
bool sc_fellow_rt_reflections() {
	return ((sc_packed_2() >> 3) & 1U) != 0;
}

// FellowEngine DDGI's screen pass (decision 0083): the opaque colour passes read the volume's irradiance per pixel
// (fellow_ddgi_screen) instead of looking the probes up.
bool sc_fellow_ddgi_screen() {
	return ((sc_packed_2() >> 4) & 1U) != 0;
}
#define FELLOW_OMNI_HOT(m_idx, m_field) (sc_fellow_light_hot() ? (sc_fellow_light_hot_ubo() ? fellow_omni_hot_ubo.data[m_idx].m_field : fellow_omni_hot_ssbo.data[m_idx].m_field) : omni_lights.data[m_idx].m_field)
#define FELLOW_SPOT_HOT(m_idx, m_field) (sc_fellow_light_hot() ? (sc_fellow_light_hot_ubo() ? fellow_spot_hot_ubo.data[m_idx].m_field : fellow_spot_hot_ssbo.data[m_idx].m_field) : spot_lights.data[m_idx].m_field)
#define FELLOW_HAS_LIGHT_PREFETCH_NEXT
#define FELLOW_HAS_SHADOW_TAP_BATCH

float sc_luminance_multiplier() {
	// Not used in clustered renderer but we share some code with the mobile renderer that requires this.
	return 1.0;
}

layout(constant_id = 2) const bool sc_emulate_point_size = false;

#ifdef POINT_SIZE_USED

#define VERTEX_INDEX (sc_emulate_point_size ? gl_InstanceIndex : gl_VertexIndex)
#define INSTANCE_INDEX (sc_emulate_point_size ? (gl_VertexIndex / 6) : gl_InstanceIndex)

#else

#define VERTEX_INDEX gl_VertexIndex
#define INSTANCE_INDEX gl_InstanceIndex

#endif

#define REFLECTION_MULTIPLIER 1.0

#define SDFGI_MAX_CASCADES 8

/* Set 0: Base Pass (never changes) */

#include "../light_data_inc.glsl"

layout(set = 0, binding = 2) uniform sampler shadow_sampler;

// FellowEngine automatic HLOD (decision 0122): a MultiMesh proxy's instances transform their normals by the inverse
// transpose of their basis (stock: the basis), as the nodes they stand in for do under a non-uniform scale.
#define INSTANCE_FLAGS_FELLOW_INSTANCE_NORMALS (1 << 1)
#define INSTANCE_FLAGS_DYNAMIC (1 << 3)
#define INSTANCE_FLAGS_NON_UNIFORM_SCALE (1 << 4)
#define INSTANCE_FLAGS_USE_GI_BUFFERS (1 << 5)
#define INSTANCE_FLAGS_USE_SDFGI (1 << 6)
#define INSTANCE_FLAGS_USE_LIGHTMAP_CAPTURE (1 << 7)
#define INSTANCE_FLAGS_USE_LIGHTMAP (1 << 8)
#define INSTANCE_FLAGS_USE_SH_LIGHTMAP (1 << 9)
#define INSTANCE_FLAGS_USE_VOXEL_GI (1 << 10)
#define INSTANCE_FLAGS_PARTICLES (1 << 11)
#define INSTANCE_FLAGS_PARTICLE_TRAIL_SHIFT 16
#define INSTANCE_FLAGS_FADE_SHIFT 24
//3 bits of stride
#define INSTANCE_FLAGS_PARTICLE_TRAIL_MASK 0xFF

#define SCREEN_SPACE_EFFECTS_FLAGS_USE_SSAO (1 << 0)
#define SCREEN_SPACE_EFFECTS_FLAGS_USE_SSIL (1 << 1)
#define SCREEN_SPACE_EFFECTS_FLAGS_USE_SSR (1 << 2)
#define SCREEN_SPACE_EFFECTS_FLAGS_RESOLVE_SSR (1 << 3)

layout(set = 0, binding = 3, std430) restrict readonly buffer OmniLights {
	LightData data[];
}
omni_lights;

layout(set = 0, binding = 4, std430) restrict readonly buffer SpotLights {
	LightData data[];
}
spot_lights;

layout(set = 0, binding = 5, std430) restrict readonly buffer AreaLights {
	LightData data[];
}
area_lights;

// FellowEngine (rendering/fellow/lights/hot_fields_buffer, decision 0047): compact copies of the fields the clustered
// light loop reads for every omni/spot light it visits (the same bits as LightData's), in storage buffers (1) or uniform
// buffers (2; the loop's light index is the same for the whole subgroup).
struct FellowLightHot {
	vec3 position;
	float inv_radius;
	vec3 color;
	float attenuation;
	vec3 direction;
	float cone_angle;
	float size;
	float specular_amount;
	float shadow_opacity;
	float cone_attenuation;
	uint mask;
	uint bake_mode;
	uint pad0;
	uint pad1;
};

layout(set = 0, binding = 21, std430) restrict readonly buffer FellowOmniHot {
	FellowLightHot data[];
}
fellow_omni_hot_ssbo;

layout(set = 0, binding = 22, std430) restrict readonly buffer FellowSpotHot {
	FellowLightHot data[];
}
fellow_spot_hot_ssbo;

layout(set = 0, binding = 23, std140) uniform FellowOmniHotU {
	FellowLightHot data[FELLOW_LIGHT_HOT_UBO_SIZE];
}
fellow_omni_hot_ubo;

layout(set = 0, binding = 24, std140) uniform FellowSpotHotU {
	FellowLightHot data[FELLOW_LIGHT_HOT_UBO_SIZE];
}
fellow_spot_hot_ubo;

layout(set = 0, binding = 6, std430) restrict readonly buffer ReflectionProbeData {
	ReflectionData data[];
}
reflections;

layout(set = 0, binding = 7, std140) uniform DirectionalLights {
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
}
directional_lights;

#define LIGHTMAP_FLAG_USE_DIRECTION 1
#define LIGHTMAP_FLAG_USE_SPECULAR_DIRECTION 2

#define LIGHTMAP_SHADOWMASK_MODE_NONE 0
#define LIGHTMAP_SHADOWMASK_MODE_REPLACE 1
#define LIGHTMAP_SHADOWMASK_MODE_OVERLAY 2
#define LIGHTMAP_SHADOWMASK_MODE_ONLY 3

struct Lightmap {
	mat3 normal_xform;
	vec2 light_texture_size;
	float exposure_normalization;
	uint flags;
};

layout(set = 0, binding = 8, std140) restrict readonly buffer Lightmaps {
	Lightmap data[];
}
lightmaps;

struct LightmapCapture {
	vec4 sh[9];
};

layout(set = 0, binding = 9, std140) restrict readonly buffer LightmapCaptures {
	LightmapCapture data[];
}
lightmap_captures;

layout(set = 0, binding = 10) uniform texture2D decal_atlas;
layout(set = 0, binding = 11) uniform texture2D decal_atlas_srgb;

layout(set = 0, binding = 12, std430) restrict readonly buffer Decals {
	DecalData data[];
}
decals;

layout(set = 0, binding = 13, std430) restrict readonly buffer GlobalShaderUniformData {
	vec4 data[];
}
global_shader_uniforms;

struct SDFVoxelGICascadeData {
	vec3 position;
	float to_probe;
	ivec3 probe_world_offset;
	float to_cell; // 1/bounds * grid_size
	vec3 pad;
	float exposure_normalization;
};

layout(set = 0, binding = 14, std140) uniform SDFGI {
	vec3 grid_size;
	uint max_cascades;

	bool use_occlusion;
	int probe_axis_size;
	float probe_to_uvw;
	float normal_bias;

	vec3 lightprobe_tex_pixel_size;
	float energy;

	vec3 lightprobe_uv_offset;
	float y_mult;

	vec3 occlusion_clamp;
	uint pad3;

	vec3 occlusion_renormalize;
	uint pad4;

	vec3 cascade_probe_size;
	uint pad5;

	SDFVoxelGICascadeData cascades[SDFGI_MAX_CASCADES];
}
sdfgi;

layout(set = 0, binding = 15) uniform sampler DEFAULT_SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;

layout(set = 0, binding = 16) uniform texture2D best_fit_normal_texture;

layout(set = 0, binding = 17) uniform texture2D dfg;

layout(set = 0, binding = 18) uniform sampler2D ltc_lut1;

layout(set = 0, binding = 19) uniform sampler2D ltc_lut2;

layout(set = 0, binding = 20) uniform texture2D area_light_atlas;
/* Set 1: Render Pass (changes per render pass) */

layout(set = 1, binding = 0, std140) uniform SceneDataBlock {
	SceneData data;
	SceneData prev_data;
}
scene_data_block;

// Depth slice of the light / decal / reflection probe clusters for a view depth. FellowEngine depth slices
// (SCENE_DATA_FLAGS_FELLOW_CLUSTER_LOG_DEPTH, cluster_builder_rd.h): 32 slices logarithmic between the near and far
// planes, computed exactly like cluster_store.glsl; otherwise stock's 32 linear slices over [0, far].
uint fellow_cluster_z(float p_depth, float p_z_near, float p_z_far, uint p_flags) {
	if (sc_fellow_cluster_log_depth() && bool(p_flags & SCENE_DATA_FLAGS_FELLOW_CLUSTER_LOG_DEPTH)) {
		return p_depth > p_z_near ? min(uint(log2(p_depth / p_z_near) * (32.0 / log2(p_z_far / p_z_near))), 31u) : 0u;
	}
	return uint(clamp((p_depth / p_z_far) * 32.0, 0.0, 31.0));
}

struct ImplementationData {
	uint cluster_shift;
	uint cluster_width;
	uint cluster_type_size;
	uint max_cluster_element_count_div_32;

	uint ss_effects_flags;
	float ssao_light_affect;
	float ssao_ao_affect;
	uint fellow_tile_cull_width; // FellowEngine light tile culling (decision 0049): 8 x 8 tiles per row

	mat4 sdf_to_bounds;

	ivec3 sdf_offset;
	uint fellow_tile_cull_count; // FellowEngine light tile culling (decision 0049): tiles in all

	ivec3 sdf_size;
	bool gi_upscale_for_msaa;

	bool volumetric_fog_enabled;
	float volumetric_fog_inv_length;
	float volumetric_fog_detail_spread;
	uint fellow_zero; // was volumetric_fog_pad: always 0 (decision 0057, fellow_keep)
};

layout(set = 1, binding = 1, std140) uniform ImplementationDataBlock {
	ImplementationData data;
}
implementation_data_block;

#define implementation_data implementation_data_block.data

// FellowEngine decision 0057: where a specialization constant leaves out a block of code, the values that block could
// have changed are passed through this identity (an XOR with a zero the compiler cannot see), as they pass through the
// block's phi in stock's pipeline. The compiler then cannot fuse the multiply that computed such a value with the add
// that uses it after the block (a contracted multiply-add rounds once instead of twice), where stock's compiler could
// not either: the values that reach the code after the block are computed with stock's rounding.
float fellow_keep(float v) {
	return uintBitsToFloat(floatBitsToUint(v) ^ implementation_data.fellow_zero);
}
vec3 fellow_keep(vec3 v) {
	return uintBitsToFloat(floatBitsToUint(v) ^ uvec3(implementation_data.fellow_zero));
}
vec4 fellow_keep(vec4 v) {
	return uintBitsToFloat(floatBitsToUint(v) ^ uvec4(implementation_data.fellow_zero));
}
#ifdef EXPLICIT_FP16
float16_t fellow_keep(float16_t v) {
	return float16_t(fellow_keep(float(v)));
}
f16vec3 fellow_keep(f16vec3 v) {
	return f16vec3(fellow_keep(vec3(v)));
}
#endif

struct InstanceData {
	mat3x4 transform;
	vec4 compressed_aabb_position_pad; // Only .xyz is used. .w is padding.
	vec4 compressed_aabb_size_pad; // Only .xyz is used. .w is padding.
	vec4 uv_scale;
	uint flags;
	uint instance_uniforms_ofs; //base offset in global buffer for instance variables
	uint gi_offset; //GI information when using lightmapping (VCT or lightmap index)
	uint layer_mask;
	mat3x4 prev_transform;
	vec4 lightmap_uv_scale;
#ifdef USE_DOUBLE_PRECISION
	vec4 model_precision;
	vec4 prev_model_precision;
#endif
};

layout(set = 1, binding = 2, std430) buffer restrict readonly InstanceDataBuffer {
	InstanceData data[];
}
instances;

// FellowEngine persistent instance data (cpu/persistent_instances, docs/decisions/0022-persistent-instance-data.md):
// the per-frame buffer then holds one 16-byte element per list element (the surface's slot, the flags and the GI offset
// its pass gave it); the rest of the instance data is in a buffer kept from frame to frame, one entry per surface and
// rewritten only when it changes. Both bindings are always provided.
layout(set = 1, binding = 39, std430) buffer restrict readonly FellowElementBuffer {
	uvec4 data[];
}
fellow_elements;

layout(set = 1, binding = 40, std430) buffer restrict readonly FellowSlotBuffer {
	InstanceData data[];
}
fellow_slots;

// FellowEngine occlusion culling (docs/decisions/0026-occlusion-culling.md): the opaque pass's elements of the runs the
// GPU culled, the visible ones of each run compacted in order from its first element. A draw whose element index has
// its top bit set reads its elements here.
// FellowEngine light tile culling (rendering/fellow/lights/tile_cull, decision 0049): per 8 x 8 pixel tile, the omni
// then the spot lights (max_cluster_element_count_div_32 words each) that can reach the tile's geometry.
layout(set = 1, binding = 42, std430) buffer restrict readonly FellowLightTileMasks {
	uint data[];
}
fellow_light_tile_masks;

layout(set = 1, binding = 41, std430) buffer restrict readonly FellowCompactedElementBuffer {
	uvec4 data[];
}
fellow_compacted_elements;

// The list element's instance data: entry, flags and GI offset (set by fellow_element_init() at the start of the
// vertex and fragment stages).
uint fellow_slot;
uint fellow_flags;
uint fellow_gi_offset;

#define FELLOW_INSTANCE(m_field) (sc_fellow_persistent_instances() ? fellow_slots.data[fellow_slot].m_field : instances.data[fellow_slot].m_field)

void fellow_element_init(uint p_element) {
	if (sc_fellow_persistent_instances()) {
		uvec4 element = (p_element & 0x80000000u) != 0u ? fellow_compacted_elements.data[p_element & 0x7FFFFFFFu] : fellow_elements.data[p_element];
		fellow_slot = element.x;
		fellow_flags = element.y;
		fellow_gi_offset = element.z;
	} else {
		fellow_slot = p_element;
		fellow_flags = instances.data[p_element].flags;
		fellow_gi_offset = instances.data[p_element].gi_offset;
	}
}

#ifdef USE_RADIANCE_OCTMAP_ARRAY

layout(set = 1, binding = 3) uniform texture2DArray radiance_octmap;

#else

layout(set = 1, binding = 3) uniform texture2D radiance_octmap;

#endif

layout(set = 1, binding = 4) uniform texture2DArray reflection_atlas;

layout(set = 1, binding = 5) uniform texture2D shadow_atlas;

layout(set = 1, binding = 6) uniform texture2D directional_shadow_atlas;

layout(set = 1, binding = 7) uniform texture2DArray lightmap_textures[MAX_LIGHTMAP_TEXTURES * 2];

layout(set = 1, binding = 8) uniform texture3D voxel_gi_textures[MAX_VOXEL_GI_INSTANCES];

layout(set = 1, binding = 9, std430) buffer restrict readonly ClusterBuffer {
	uint data[];
}
cluster_buffer;

layout(set = 1, binding = 10) uniform sampler decal_sampler;

layout(set = 1, binding = 11) uniform sampler light_projector_sampler;

layout(set = 1, binding = 12 + 0) uniform sampler SAMPLER_NEAREST_CLAMP;
layout(set = 1, binding = 12 + 1) uniform sampler SAMPLER_LINEAR_CLAMP;
layout(set = 1, binding = 12 + 2) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 12 + 3) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;
layout(set = 1, binding = 12 + 4) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 12 + 5) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 1, binding = 12 + 6) uniform sampler SAMPLER_NEAREST_REPEAT;
layout(set = 1, binding = 12 + 7) uniform sampler SAMPLER_LINEAR_REPEAT;
layout(set = 1, binding = 12 + 8) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 12 + 9) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT;
layout(set = 1, binding = 12 + 10) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_REPEAT;
layout(set = 1, binding = 12 + 11) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_REPEAT;

#ifdef MODE_RENDER_SDF

layout(r16ui, set = 1, binding = 24) uniform restrict writeonly uimage3D albedo_volume_grid;
layout(r32ui, set = 1, binding = 25) uniform restrict writeonly uimage3D emission_grid;
layout(r32ui, set = 1, binding = 26) uniform restrict writeonly uimage3D emission_aniso_grid;
layout(r32ui, set = 1, binding = 27) uniform restrict uimage3D geom_facing_grid;

//still need to be present for shaders that use it, so remap them to something
#define depth_buffer shadow_atlas
#define color_buffer shadow_atlas
#define normal_roughness_buffer shadow_atlas

#define multiviewSampler sampler2D
#else

#ifdef USE_MULTIVIEW
layout(set = 1, binding = 24) uniform texture2DArray depth_buffer;
layout(set = 1, binding = 25) uniform texture2DArray color_buffer;
layout(set = 1, binding = 26) uniform texture2DArray normal_roughness_buffer;
layout(set = 1, binding = 27) uniform texture2DArray ao_buffer;
layout(set = 1, binding = 28) uniform texture2DArray ambient_buffer;
layout(set = 1, binding = 29) uniform texture2DArray reflection_buffer;
#define multiviewSampler sampler2DArray
#else // USE_MULTIVIEW
layout(set = 1, binding = 24) uniform texture2D depth_buffer;
layout(set = 1, binding = 25) uniform texture2D color_buffer;
layout(set = 1, binding = 26) uniform texture2D normal_roughness_buffer;
layout(set = 1, binding = 27) uniform texture2D ao_buffer;
layout(set = 1, binding = 28) uniform texture2D ambient_buffer;
layout(set = 1, binding = 29) uniform texture2D reflection_buffer;
#define multiviewSampler sampler2D
#endif
layout(set = 1, binding = 30) uniform texture2DArray sdfgi_lightprobe_texture;
layout(set = 1, binding = 31) uniform texture3D sdfgi_occlusion_cascades;

struct VoxelGIData {
	mat4 xform; // 64 - 64

	vec3 bounds; // 12 - 76
	float dynamic_range; // 4 - 80

	float bias; // 4 - 84
	float normal_bias; // 4 - 88
	bool blend_ambient; // 4 - 92
	uint mipmaps; // 4 - 96

	vec3 pad; // 12 - 108
	float exposure_normalization; // 4 - 112
};

layout(set = 1, binding = 32, std140) uniform VoxelGIs {
	VoxelGIData data[MAX_VOXEL_GI_INSTANCES];
}
voxel_gi_instances;

layout(set = 1, binding = 33) uniform texture3D volumetric_fog_texture;

#ifdef USE_MULTIVIEW
layout(set = 1, binding = 34) uniform texture2DArray ssil_buffer;
layout(set = 1, binding = 35) uniform texture2DArray ssr_buffer;
layout(set = 1, binding = 36) uniform texture2DArray ssr_mip_level_buffer;
#else
layout(set = 1, binding = 34) uniform texture2D ssil_buffer;
layout(set = 1, binding = 35) uniform texture2D ssr_buffer;
layout(set = 1, binding = 36) uniform texture2D ssr_mip_level_buffer;
#endif // USE_MULTIVIEW

// FellowEngine: max pyramid of the directional shadow atlas (level 0 = atlas / 2).
layout(set = 1, binding = 37) uniform texture2D directional_shadow_max_mip;
// FellowEngine: min/max window pyramid of the positional shadow atlas.
layout(set = 1, binding = 38) uniform texture2D positional_shadow_max_mip;
#define FELLOW_SHADOW_MAX_MIP
// FellowEngine ray-traced sun shadows (decision 0082): x the shadow, y 1 where it was traced.
layout(set = 1, binding = 43) uniform texture2D fellow_rt_shadow_mask;
// FellowEngine DDGI (decision 0083): the probe volume around the camera.
layout(set = 1, binding = 44) uniform texture2D fellow_ddgi_irradiance_atlas;
layout(set = 1, binding = 45) uniform texture2D fellow_ddgi_distance_atlas;
layout(set = 1, binding = 46, std140) uniform FellowDDGIBlock {
	FellowDDGIVolume volume;
}
fellow_ddgi;
// FellowEngine ray-traced reflections (decision 0084): RGB the reflected radiance, A its weight.
layout(set = 1, binding = 47) uniform texture2D fellow_rt_reflection;
// FellowEngine DDGI's screen pass (decision 0083): RGB the irradiance, A the volume's coverage.
layout(set = 1, binding = 48) uniform texture2D fellow_ddgi_screen;
// FellowEngine weather (decision 0124): its parameters, the scene's heightfield seen from above (where the rain falls),
// the rain's ripples (slopes in rg, tiling).
struct FellowWeatherData {
	vec4 params; // wetness, puddles, rain, time
	vec4 occlusion; // origin x, origin z, 1 / width x, 1 / width z
	vec4 occlusion2; // origin y, half height, enabled, bias
	vec4 ripples; // 1 / tile, strength
	// The wind (decision 0125), this frame's and the previous one's (motion vectors).
	vec4 wind; // direction x, direction z, speed (m/s), gustiness
	vec4 wind2; // the gusts' offset along the wind (m), 1 / gust size, flutter, enabled
	vec4 wind_prev;
	vec4 wind2_prev;
	vec4 wind_time; // this frame's and the previous frame's time of the wind (s)
	vec4 snow; // the snow's cover (0-1), its sparkle (decision 0128), its depth (m), tracks on (decision 0130)
	vec4 tracks; // the tracks' window: centre x, z (m), 1 / side (m), half its side less a margin (m)
	vec4 cloud_shadow; // the clouds' shadow map (decision 0133): centre x, z (m), 1 / side (1/m), strength
	vec4 cloud_shadow2; // the sun's directional light index, its direction's x / y and z / y, the ground's height (m)
	vec4 cloud_shadow3; // beyond the map: the sun's share there (the clear sky's share); the sky light's cover
	vec4 frost; // decision 0136: how much of the wet surfaces' water is ice (0..1), the hoarfrost (0..1); z: decision 0138's drying patches (0..1); w: decision 0140's slush (0..1)
	vec4 slant; // decision 0139: the rain's drift per metre of fall (x, z), how slanted the shelter is (0..1)
};
layout(set = 1, binding = 49, std140) uniform FellowWeatherBlock {
	FellowWeatherData data;
}
fellow_weather;
layout(set = 1, binding = 50) uniform texture2D fellow_rain_occlusion;
layout(set = 1, binding = 51) uniform texture2D fellow_rain_ripples;
// The wind's gusts: a tiling noise (r), laid along the wind and travelling with it.
layout(set = 1, binding = 52) uniform texture2D fellow_wind_gusts;
// The snow's tracks (decision 0130): their depth in m (negative: a rim), world-anchored, wrapping.
layout(set = 1, binding = 53) uniform texture2D fellow_snow_tracks;
// The volumetric clouds' shadow (decision 0133): the sun's transmittance through the cloud layer over the ground.
layout(set = 1, binding = 54) uniform texture2D fellow_cloud_shadow;

#endif

vec4 normal_roughness_compatibility(vec4 p_normal_roughness) {
	float roughness = p_normal_roughness.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness /= (127.0 / 255.0);
	return vec4(normalize(p_normal_roughness.xyz * 2.0 - 1.0) * 0.5 + 0.5, roughness);
}

// https://google.github.io/filament/Filament.html#toc5.3.4.7
// Note: The roughness value is inverted
vec3 prefiltered_dfg(float lod, float NoV) {
	return textureLod(sampler2D(dfg, SAMPLER_LINEAR_CLAMP), vec2(NoV, 1.0 - lod), 0.0).rgb;
}

// Compute multiscatter compensation
// https://google.github.io/filament/Filament.html#listing_energycompensationimpl
vec3 get_energy_compensation(vec3 f0, float env) {
	return 1.0 + f0 * (1.0 / env - 1.0);
}

/* Set 2 Skeleton & Instancing (can change per item) */

layout(set = 2, binding = 0, std430) restrict readonly buffer Transforms {
	vec4 data[];
}
transforms;

/* Set 3 User Material */
