/**************************************************************************/
/*  fsr3.cpp                                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "fsr3.h"

#include "core/config/project_settings.h"
#include "core/os/os.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_accumulate_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_autogen_reactive_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_debug_view_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_luma_instability_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_luma_pyramid_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_prepare_inputs_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_prepare_reactivity_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_rcas_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_shading_change_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fsr3/fsr3_shading_change_pyramid_pass.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

#include <cwchar>
#include <initializer_list>

// This flag doesn't control anything GCC specific: it decides whether the SDK's symbols are exported.
#ifndef FFX_GCC
#define FFX_GCC
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "FidelityFX/host/ffx_fsr3upscaler.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace RendererRD;

// The constant buffers one dispatch stages (the upscaler's, SPD's, RCAS's), and how many sets of uniform buffers turn
// over (frames in flight, views a frame).
#define FSR3_STAGING_SLOTS 4
#define FSR3_STAGING_WORDS 64
#define FSR3_UBO_SETS 8
#define FSR3_MAX_JOB_BINDINGS 16

static constexpr uint32_t FSR3_SAMPLER_POINT_BINDING = 1000;
static constexpr uint32_t FSR3_SAMPLER_LINEAR_BINDING = 1001;

// Fs3UpscalerShaderPermutationOptions (thirdparty/amd-fsr3/src/ffx_fsr3upscaler_private.h).
static constexpr uint32_t FSR3_PERMUTATION_ENABLE_SHARPENING = 1 << 5;
static constexpr uint32_t FSR3_PERMUTATION_ALLOW_FP16 = 1 << 7;

namespace {

struct BindingName {
	uint32_t slot;
	const wchar_t *name;
};

struct PipelineRef;

} //namespace

// The shaders of a pass and the bindings their GLSL declares (Godot has no reflection of the names, so they are listed
// by hand from the FSR3UPSCALER_BIND_* defines of thirdparty/amd-fsr3/FidelityFX/gpu/ffx_fsr3upscaler_*_pass.glsl and
// the names in fsr3upscaler/ffx_fsr3upscaler_callbacks_glsl.h; the SDK maps the names to its resources).
struct FSR3Effect::Device {
	struct Pass {
		ShaderRD *shader = nullptr;
		RID shader_version;
		uint32_t variant_count = 1;
		bool fp16 = false; // Variants: [fp16 * 2 +] [sharpen].
		bool sharpen_variants = false;
		RID shader_rids[4];
		RID pipelines[4];
		LocalVector<BindingName> srvs;
		LocalVector<BindingName> uavs;
		LocalVector<BindingName> cbs;
	};

	struct Pipeline {
		Pass *pass = nullptr;
		uint32_t variant = 0;
	};

	Pass passes[FFX_FSR3UPSCALER_PASS_COUNT];
	Pipeline pipelines[FFX_FSR3UPSCALER_PASS_COUNT][4];
	FfxDeviceCapabilities capabilities = {};
	RID point_clamp_sampler;
	RID linear_clamp_sampler;
	bool use_lut = false;

	struct {
		Fsr3PrepareInputsPassShaderRD prepare_inputs;
		Fsr3LumaPyramidPassShaderRD luma_pyramid;
		Fsr3ShadingChangePyramidPassShaderRD shading_change_pyramid;
		Fsr3ShadingChangePassShaderRD shading_change;
		Fsr3PrepareReactivityPassShaderRD prepare_reactivity;
		Fsr3LumaInstabilityPassShaderRD luma_instability;
		Fsr3AccumulatePassShaderRD accumulate;
		Fsr3RcasPassShaderRD rcas;
		Fsr3DebugViewPassShaderRD debug_view;
		Fsr3AutogenReactivePassShaderRD autogen_reactive;
	} shaders;

	RID get_pipeline(Pass &p_pass, uint32_t p_variant, RID &r_shader) {
		ERR_FAIL_UNSIGNED_INDEX_V(p_variant, p_pass.variant_count, RID());
		if (p_pass.pipelines[p_variant].is_null()) {
			p_pass.shader_rids[p_variant] = p_pass.shader->version_get_shader(p_pass.shader_version, p_variant);
			ERR_FAIL_COND_V(p_pass.shader_rids[p_variant].is_null(), RID());
			p_pass.pipelines[p_variant] = RD::get_singleton()->compute_pipeline_create(p_pass.shader_rids[p_variant]);
		}
		r_shader = p_pass.shader_rids[p_variant];
		return p_pass.pipelines[p_variant];
	}
};

namespace {
// The SDK's resource description in a type of this file's own. FSR 2.2's SDK (thirdparty/amd-fsr2, the stock FSR 2
// backend) has another FfxResourceDescription (28 bytes, FSR 3.1's 32), and a LocalVector of either is the same
// LocalVector<FfxResourceDescription> to the linker (an ODR violation): one backend's reserve() serves both, and FSR 2.2's
// allocated FSR 3.1's table too small — a heap overflow at the context's creation, a crash in MSVC's build.
struct FSR3ResourceDescription {
	FfxResourceDescription d = {};
};
} // namespace

class RendererRD::FSR3Context {
public:
	struct Resources {
		LocalVector<RID> rids;
		LocalVector<LocalVector<RID>> mip_slice_rids;
		LocalVector<FSR3ResourceDescription> descriptions;
		LocalVector<bool> owned;
		LocalVector<uint32_t> dynamic_list;
		LocalVector<uint32_t> free_list;

		uint32_t add(RID p_rid, bool p_dynamic, bool p_owned, const FfxResourceDescription &p_description) {
			uint32_t index;
			if (free_list.is_empty()) {
				index = rids.size();
				rids.resize(index + 1);
				mip_slice_rids.resize(index + 1);
				descriptions.resize(index + 1);
				owned.resize(index + 1);
			} else {
				index = free_list[free_list.size() - 1];
				free_list.resize(free_list.size() - 1);
			}
			rids[index] = p_rid;
			mip_slice_rids[index].clear();
			descriptions[index].d = p_description;
			owned[index] = p_owned;
			if (p_dynamic) {
				dynamic_list.push_back(index);
			}
			return index;
		}

		void remove(uint32_t p_index) {
			ERR_FAIL_UNSIGNED_INDEX(p_index, rids.size());
			free_list.push_back(p_index);
			rids[p_index] = RID();
			mip_slice_rids[p_index].clear();
			descriptions[p_index].d = {};
			owned[p_index] = false;
			dynamic_list.erase(p_index);
		}

		uint32_t size() const {
			return rids.size();
		}

		RID get_mip(uint32_t p_index, uint32_t p_mip) {
			const FfxResourceDescription &desc = descriptions[p_index].d;
			if (desc.mipCount <= 1) {
				return rids[p_index];
			}
			// A storage image of a texture with a mip chain is bound one level at a time.
			LocalVector<RID> &slices = mip_slice_rids[p_index];
			if (slices.is_empty()) {
				slices.resize(desc.mipCount);
			}
			ERR_FAIL_UNSIGNED_INDEX_V(p_mip, slices.size(), RID());
			if (slices[p_mip].is_null()) {
				slices[p_mip] = RD::get_singleton()->texture_create_shared_from_slice(RD::TextureView(), rids[p_index], 0, p_mip);
			}
			return slices[p_mip];
		}
	};

	struct Binding {
		uint32_t slot = 0;
		int32_t resource = -1;
		uint32_t mip = 0;
	};

	struct Job {
		FfxGpuJobType type = FFX_GPU_JOB_COMPUTE;
		// Clear.
		int32_t target = -1;
		Color color;
		// Copy.
		int32_t source = -1;
		// Compute.
		FSR3Effect::Device::Pipeline *pipeline = nullptr;
		uint32_t dimensions[3] = {};
		Binding srvs[FSR3_MAX_JOB_BINDINGS];
		Binding uavs[FSR3_MAX_JOB_BINDINGS];
		Binding cbs[FFX_MAX_NUM_CONST_BUFFERS]; // resource: the staging slot.
		uint32_t srv_count = 0;
		uint32_t uav_count = 0;
		uint32_t cb_count = 0;
	};

	struct Scratch {
		Resources resources;
		LocalVector<Job> jobs;
		uint32_t staging[FSR3_STAGING_SLOTS][FSR3_STAGING_WORDS] = {};
		uint32_t staging_words[FSR3_STAGING_SLOTS] = {};
		uint32_t staging_count = 0;
		RID ubos[FSR3_UBO_SETS][FSR3_STAGING_SLOTS];
		bool uploaded[FSR3_STAGING_SLOTS] = {};
		uint32_t ubo_set = 0;
		FSR3Effect::Device *device = nullptr;
		bool error = false;
	};

	Scratch scratch;
	FfxFsr3UpscalerContext *fsr_context = nullptr;
	FfxFsr3UpscalerContextDescription fsr_desc = {};
	bool created = false;
	Size2i internal_size;
	Size2i target_size;
	RID dilated_depth;
	RID dilated_motion_vectors;
	RID reconstructed_previous_nearest_depth;
	FSR3Effect::Tuning tuning;

	~FSR3Context();
};

// Formats.

static RD::DataFormat ffx_surface_format_to_rd_format(FfxSurfaceFormat p_format) {
	switch (p_format) {
		case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
		case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
			return RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
		case FFX_SURFACE_FORMAT_R32G32B32A32_UINT:
			return RD::DATA_FORMAT_R32G32B32A32_UINT;
		case FFX_SURFACE_FORMAT_R16G16B16A16_TYPELESS:
		case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
			return RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		case FFX_SURFACE_FORMAT_R32G32_TYPELESS:
		case FFX_SURFACE_FORMAT_R32G32_FLOAT:
			return RD::DATA_FORMAT_R32G32_SFLOAT;
		case FFX_SURFACE_FORMAT_R32_UINT:
			return RD::DATA_FORMAT_R32_UINT;
		case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
		case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
			return RD::DATA_FORMAT_R8G8B8A8_UNORM;
		case FFX_SURFACE_FORMAT_R8G8B8A8_SNORM:
			return RD::DATA_FORMAT_R8G8B8A8_SNORM;
		case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
			return RD::DATA_FORMAT_B10G11R11_UFLOAT_PACK32;
		case FFX_SURFACE_FORMAT_R16G16_TYPELESS:
		case FFX_SURFACE_FORMAT_R16G16_FLOAT:
			return RD::DATA_FORMAT_R16G16_SFLOAT;
		case FFX_SURFACE_FORMAT_R16G16_UINT:
			return RD::DATA_FORMAT_R16G16_UINT;
		case FFX_SURFACE_FORMAT_R16_TYPELESS:
		case FFX_SURFACE_FORMAT_R16_FLOAT:
			return RD::DATA_FORMAT_R16_SFLOAT;
		case FFX_SURFACE_FORMAT_R16_UINT:
			return RD::DATA_FORMAT_R16_UINT;
		case FFX_SURFACE_FORMAT_R16_UNORM:
			return RD::DATA_FORMAT_R16_UNORM;
		case FFX_SURFACE_FORMAT_R16_SNORM:
			return RD::DATA_FORMAT_R16_SNORM;
		case FFX_SURFACE_FORMAT_R8_TYPELESS:
		case FFX_SURFACE_FORMAT_R8_UNORM:
			return RD::DATA_FORMAT_R8_UNORM;
		case FFX_SURFACE_FORMAT_R8_UINT:
			return RD::DATA_FORMAT_R8_UINT;
		case FFX_SURFACE_FORMAT_R8G8_TYPELESS:
		case FFX_SURFACE_FORMAT_R8G8_UNORM:
			return RD::DATA_FORMAT_R8G8_UNORM;
		case FFX_SURFACE_FORMAT_R32_TYPELESS:
		case FFX_SURFACE_FORMAT_R32_FLOAT:
			return RD::DATA_FORMAT_R32_SFLOAT;
		default:
			return RD::DATA_FORMAT_MAX;
	}
}

static FfxSurfaceFormat rd_format_to_ffx_surface_format(RD::DataFormat p_format) {
	switch (p_format) {
		case RD::DATA_FORMAT_R32G32B32A32_SFLOAT:
			return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
		case RD::DATA_FORMAT_R16G16B16A16_SFLOAT:
			return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
		case RD::DATA_FORMAT_R32G32_SFLOAT:
			return FFX_SURFACE_FORMAT_R32G32_FLOAT;
		case RD::DATA_FORMAT_R32_UINT:
			return FFX_SURFACE_FORMAT_R32_UINT;
		case RD::DATA_FORMAT_R8G8B8A8_UNORM:
			return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
		case RD::DATA_FORMAT_B10G11R11_UFLOAT_PACK32:
			return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
		case RD::DATA_FORMAT_R16G16_SFLOAT:
			return FFX_SURFACE_FORMAT_R16G16_FLOAT;
		case RD::DATA_FORMAT_R16G16_UINT:
			return FFX_SURFACE_FORMAT_R16G16_UINT;
		case RD::DATA_FORMAT_R16_SFLOAT:
			return FFX_SURFACE_FORMAT_R16_FLOAT;
		case RD::DATA_FORMAT_R16_UINT:
			return FFX_SURFACE_FORMAT_R16_UINT;
		case RD::DATA_FORMAT_R16_UNORM:
			return FFX_SURFACE_FORMAT_R16_UNORM;
		case RD::DATA_FORMAT_R16_SNORM:
			return FFX_SURFACE_FORMAT_R16_SNORM;
		case RD::DATA_FORMAT_R8_UNORM:
			return FFX_SURFACE_FORMAT_R8_UNORM;
		case RD::DATA_FORMAT_R8_UINT:
			return FFX_SURFACE_FORMAT_R8_UINT;
		case RD::DATA_FORMAT_R8G8_UNORM:
			return FFX_SURFACE_FORMAT_R8G8_UNORM;
		case RD::DATA_FORMAT_R32_SFLOAT:
			return FFX_SURFACE_FORMAT_R32_FLOAT;
		default:
			return FFX_SURFACE_FORMAT_UNKNOWN;
	}
}

static uint32_t ffx_usage_to_rd_usage_bits(uint32_t p_usage) {
	uint32_t bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	if (p_usage & FFX_RESOURCE_USAGE_UAV) {
		bits |= RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	}
	return bits;
}

static FSR3Context::Scratch &get_scratch(FfxInterface *p_backend_interface) {
	return *reinterpret_cast<FSR3Context::Scratch *>(p_backend_interface->scratchBuffer);
}

// FELLOW_FSR3_DEBUG=1 (the environment): the backend's resources and jobs printed as the SDK asks for them.
static bool fsr3_debug() {
	static const bool debug = OS::get_singleton()->get_environment("FELLOW_FSR3_DEBUG") == "1";
	return debug;
}

static uint32_t fsr3_texture_mips(RID p_rid) {
	return p_rid.is_valid() ? RD::get_singleton()->texture_get_format(p_rid).mipmaps : 0;
}

// The backend (FfxInterface's callbacks).

static FfxVersionNumber get_sdk_version_rd(FfxInterface *p_backend_interface) {
	return FFX_SDK_MAKE_VERSION(FFX_SDK_VERSION_MAJOR, FFX_SDK_VERSION_MINOR, FFX_SDK_VERSION_PATCH);
}

static FfxErrorCode get_effect_gpu_memory_usage_rd(FfxInterface *p_backend_interface, FfxUInt32 p_effect_context_id, FfxEffectMemoryUsage *r_usage) {
	*r_usage = {};
	return FFX_OK;
}

static FfxErrorCode create_backend_context_rd(FfxInterface *p_backend_interface, FfxEffect p_effect, FfxEffectBindlessConfig *p_bindless_config, FfxUInt32 *r_effect_context_id) {
	FSR3Context::Scratch &scratch = get_scratch(p_backend_interface);
	for (uint32_t set = 0; set < FSR3_UBO_SETS; set++) {
		for (uint32_t slot = 0; slot < FSR3_STAGING_SLOTS; slot++) {
			scratch.ubos[set][slot] = RD::get_singleton()->uniform_buffer_create(FSR3_STAGING_WORDS * sizeof(uint32_t));
			ERR_FAIL_COND_V(scratch.ubos[set][slot].is_null(), FFX_ERROR_BACKEND_API_ERROR);
		}
	}
	*r_effect_context_id = 0;
	return FFX_OK;
}

static FfxErrorCode get_device_capabilities_rd(FfxInterface *p_backend_interface, FfxDeviceCapabilities *r_capabilities) {
	*r_capabilities = get_scratch(p_backend_interface).device->capabilities;
	return FFX_OK;
}

static FfxErrorCode destroy_backend_context_rd(FfxInterface *p_backend_interface, FfxUInt32 p_effect_context_id) {
	FSR3Context::Scratch &scratch = get_scratch(p_backend_interface);
	for (uint32_t set = 0; set < FSR3_UBO_SETS; set++) {
		for (uint32_t slot = 0; slot < FSR3_STAGING_SLOTS; slot++) {
			if (scratch.ubos[set][slot].is_valid()) {
				RD::get_singleton()->free_rid(scratch.ubos[set][slot]);
				scratch.ubos[set][slot] = RID();
			}
		}
	}
	return FFX_OK;
}

static FfxErrorCode create_resource_rd(FfxInterface *p_backend_interface, const FfxCreateResourceDescription *p_description, FfxUInt32 p_effect_context_id, FfxResourceInternal *r_resource) {
	// The upscaler only asks for textures in the default heap.
	ERR_FAIL_COND_V(p_description->heapType != FFX_HEAP_TYPE_DEFAULT, FFX_ERROR_INVALID_ARGUMENT);
	FfxResourceDescription desc = p_description->resourceDescription;
	ERR_FAIL_COND_V(desc.type != FFX_RESOURCE_TYPE_TEXTURE2D, FFX_ERROR_INVALID_ARGUMENT);

	if (desc.mipCount == 0) {
		desc.mipCount = uint32_t(1 + std::floor(std::log2(MAX(MAX(desc.width, desc.height), 1u))));
	}

	RD::TextureFormat format;
	format.texture_type = RD::TEXTURE_TYPE_2D;
	format.format = ffx_surface_format_to_rd_format(desc.format);
	ERR_FAIL_COND_V(format.format == RD::DATA_FORMAT_MAX, FFX_ERROR_INVALID_ARGUMENT);
	format.usage_bits = ffx_usage_to_rd_usage_bits(desc.usage);
	format.width = MAX(desc.width, 1u);
	format.height = MAX(desc.height, 1u);
	format.depth = 1;
	format.mipmaps = desc.mipCount;
	format.is_discardable = (desc.flags & FFX_RESOURCE_FLAGS_ALIASABLE) != 0;

	Vector<Vector<uint8_t>> initial_data;
	const FfxResourceInitData &init = p_description->initData;
	if (init.type != FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED && init.size > 0) {
		ERR_FAIL_COND_V(desc.mipCount != 1, FFX_ERROR_INVALID_ARGUMENT);
		Vector<uint8_t> bytes;
		bytes.resize(init.size);
		if (init.type == FFX_RESOURCE_INIT_DATA_TYPE_BUFFER) {
			memcpy(bytes.ptrw(), init.buffer, init.size);
		} else {
			memset(bytes.ptrw(), init.value, init.size);
		}
		initial_data.push_back(bytes);
	}

	RID texture = RD::get_singleton()->texture_create(format, RD::TextureView(), initial_data);
	ERR_FAIL_COND_V(texture.is_null(), FFX_ERROR_BACKEND_API_ERROR);
	if (p_description->name) {
		RD::get_singleton()->set_resource_name(texture, String(p_description->name));
	}

	FSR3Context::Resources &resources = get_scratch(p_backend_interface).resources;
	r_resource->internalIndex = resources.add(texture, false, true, desc);
	if (fsr3_debug()) {
		print_line(vformat("FSR3_DEBUG create #%d \"%s\" %dx%d mips %d (texture %d) format %d usage 0x%x flags 0x%x init %d", r_resource->internalIndex, p_description->name ? String(p_description->name) : String(), desc.width, desc.height, desc.mipCount, fsr3_texture_mips(texture), int(desc.format), uint32_t(desc.usage), uint32_t(desc.flags), int(init.type)));
	}
	if (init.type != FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED && uint32_t(r_resource->internalIndex) + 1 == resources.size()) {
		// The SDK releases a resource with initial data and "its copy" at the next index (ffxSafeReleaseCopyResource,
		// the Vulkan backend's staging buffer): keep that index empty.
		resources.add(RID(), false, false, {});
	}
	return FFX_OK;
}

static FfxErrorCode register_resource_rd(FfxInterface *p_backend_interface, const FfxResource *p_resource, FfxUInt32 p_effect_context_id, FfxResourceInternal *r_resource) {
	if (p_resource->resource == nullptr) {
		r_resource->internalIndex = -1;
		return FFX_OK;
	}
	const RID &rid = *reinterpret_cast<const RID *>(p_resource->resource);
	ERR_FAIL_COND_V(rid.is_null(), FFX_ERROR_INVALID_ARGUMENT);
	r_resource->internalIndex = get_scratch(p_backend_interface).resources.add(rid, true, false, p_resource->description);
	if (fsr3_debug()) {
		print_line(vformat("FSR3_DEBUG register #%d \"%s\" %dx%d mips %d (texture %d)", r_resource->internalIndex, String(p_resource->name), p_resource->description.width, p_resource->description.height, p_resource->description.mipCount, fsr3_texture_mips(rid)));
	}
	return FFX_OK;
}

static FfxErrorCode unregister_resources_rd(FfxInterface *p_backend_interface, FfxCommandList p_command_list, FfxUInt32 p_effect_context_id) {
	FSR3Context::Resources &resources = get_scratch(p_backend_interface).resources;
	LocalVector<uint32_t> dynamic_list(resources.dynamic_list);
	for (uint32_t index : dynamic_list) {
		// Mip views of a dynamic texture would be views of the game's texture: free them with the registration.
		for (const RID &slice : resources.mip_slice_rids[index]) {
			if (slice.is_valid()) {
				RD::get_singleton()->free_rid(slice);
			}
		}
		resources.remove(index);
	}
	return FFX_OK;
}

static FfxResourceDescription get_resource_description_rd(FfxInterface *p_backend_interface, FfxResourceInternal p_resource) {
	FSR3Context::Resources &resources = get_scratch(p_backend_interface).resources;
	if (p_resource.internalIndex >= 0 && uint32_t(p_resource.internalIndex) < resources.size()) {
		return resources.descriptions[p_resource.internalIndex].d;
	}
	return {};
}

static FfxErrorCode destroy_resource_rd(FfxInterface *p_backend_interface, FfxResourceInternal p_resource, FfxUInt32 p_effect_context_id) {
	FSR3Context::Resources &resources = get_scratch(p_backend_interface).resources;
	if (p_resource.internalIndex < 0 || uint32_t(p_resource.internalIndex) >= resources.size()) {
		return FFX_OK;
	}
	const uint32_t index = p_resource.internalIndex;
	if (fsr3_debug()) {
		print_line(vformat("FSR3_DEBUG destroy #%d (owned %d, valid %d)", index, int(resources.owned[index]), int(resources.rids[index].is_valid())));
	}
	if (resources.owned[index] && resources.rids[index].is_valid()) {
		// Freeing the texture frees its mip views with it.
		RD::get_singleton()->free_rid(resources.rids[index]);
		resources.remove(index);
	}
	return FFX_OK;
}

static FfxErrorCode stage_constant_buffer_data_rd(FfxInterface *p_backend_interface, void *p_data, FfxUInt32 p_size, FfxConstantBuffer *r_constant_buffer) {
	FSR3Context::Scratch &scratch = get_scratch(p_backend_interface);
	ERR_FAIL_COND_V(scratch.staging_count >= FSR3_STAGING_SLOTS, FFX_ERROR_OUT_OF_MEMORY);
	ERR_FAIL_COND_V(p_size > FSR3_STAGING_WORDS * sizeof(uint32_t), FFX_ERROR_OUT_OF_RANGE);
	const uint32_t slot = scratch.staging_count++;
	memcpy(scratch.staging[slot], p_data, p_size);
	scratch.staging_words[slot] = (p_size + 3) / 4;
	r_constant_buffer->data = scratch.staging[slot];
	r_constant_buffer->num32BitEntries = scratch.staging_words[slot];
	return FFX_OK;
}

static FfxErrorCode create_pipeline_rd(FfxInterface *p_backend_interface, FfxEffect p_effect, FfxPass p_pass, uint32_t p_permutation_options, const FfxPipelineDescription *p_description, FfxUInt32 p_effect_context_id, FfxPipelineState *r_pipeline) {
	ERR_FAIL_COND_V(p_effect != FFX_EFFECT_FSR3UPSCALER, FFX_ERROR_INVALID_ARGUMENT);
	ERR_FAIL_UNSIGNED_INDEX_V(p_pass, FFX_FSR3UPSCALER_PASS_COUNT, FFX_ERROR_INVALID_ARGUMENT);
	FSR3Effect::Device &device = *get_scratch(p_backend_interface).device;
	FSR3Effect::Device::Pass &pass = device.passes[p_pass];

	*r_pipeline = {};
	r_pipeline->passId = p_pass;
	if (pass.shader == nullptr) {
		// Passes the backend does not build (the deprecated TCR autogeneration).
		return FFX_OK;
	}

	// The shaders are built with the options the context is created with (HDR colour, render-resolution motion vectors
	// without jitter, inverted depth); the permutation chooses half precision and sharpening.
	uint32_t variant = 0;
	if (pass.sharpen_variants && (p_permutation_options & FSR3_PERMUTATION_ENABLE_SHARPENING)) {
		variant += 1;
	}
	if (pass.fp16 && (p_permutation_options & FSR3_PERMUTATION_ALLOW_FP16)) {
		variant += pass.sharpen_variants ? 2 : 1;
	}
	ERR_FAIL_UNSIGNED_INDEX_V(variant, pass.variant_count, FFX_ERROR_INVALID_ARGUMENT);
	FSR3Effect::Device::Pipeline &pipeline = device.pipelines[p_pass][variant];
	pipeline.pass = &pass;
	pipeline.variant = variant;
	r_pipeline->pipeline = reinterpret_cast<FfxPipeline>(&pipeline);
	r_pipeline->rootSignature = reinterpret_cast<FfxRootSignature>(&pipeline);

	auto fill = [](const LocalVector<BindingName> &p_names, FfxResourceBinding *r_bindings) {
		for (uint32_t i = 0; i < p_names.size(); i++) {
			r_bindings[i] = {};
			r_bindings[i].slotIndex = p_names[i].slot;
			wcscpy(r_bindings[i].name, p_names[i].name);
		}
	};
	r_pipeline->srvTextureCount = pass.srvs.size();
	fill(pass.srvs, r_pipeline->srvTextureBindings);
	r_pipeline->uavTextureCount = pass.uavs.size();
	fill(pass.uavs, r_pipeline->uavTextureBindings);
	r_pipeline->constCount = pass.cbs.size();
	fill(pass.cbs, r_pipeline->constantBufferBindings);
	if (p_description) {
		wcsncpy(r_pipeline->name, p_description->name, FFX_RESOURCE_NAME_SIZE - 1);
	}
	return FFX_OK;
}

static FfxErrorCode destroy_pipeline_rd(FfxInterface *p_backend_interface, FfxPipelineState *p_pipeline, FfxUInt32 p_effect_context_id) {
	// The pipelines belong to the effect (the device), shared by every context.
	return FFX_OK;
}

static FfxErrorCode schedule_gpu_job_rd(FfxInterface *p_backend_interface, const FfxGpuJobDescription *p_job) {
	ERR_FAIL_NULL_V(p_job, FFX_ERROR_INVALID_ARGUMENT);
	FSR3Context::Scratch &scratch = get_scratch(p_backend_interface);

	switch (p_job->jobType) {
		case FFX_GPU_JOB_CLEAR_FLOAT: {
			FSR3Context::Job job;
			job.type = FFX_GPU_JOB_CLEAR_FLOAT;
			job.target = p_job->clearJobDescriptor.target.internalIndex;
			if (fsr3_debug()) {
				print_line(vformat("FSR3_DEBUG schedule clear #%d \"%s\"", job.target, String(p_job->jobLabel)));
			}
			const float *c = p_job->clearJobDescriptor.color;
			job.color = Color(c[0], c[1], c[2], c[3]);
			scratch.jobs.push_back(job);
		} break;
		case FFX_GPU_JOB_COPY: {
			FSR3Context::Job job;
			job.type = FFX_GPU_JOB_COPY;
			job.source = p_job->copyJobDescriptor.src.internalIndex;
			job.target = p_job->copyJobDescriptor.dst.internalIndex;
			if (fsr3_debug()) {
				print_line(vformat("FSR3_DEBUG schedule copy #%d -> #%d \"%s\"", job.source, job.target, String(p_job->jobLabel)));
			}
			scratch.jobs.push_back(job);
		} break;
		case FFX_GPU_JOB_COMPUTE: {
			const FfxComputeJobDescription &compute = p_job->computeJobDescriptor;
			const FfxPipelineState &state = compute.pipeline;
			ERR_FAIL_NULL_V(state.pipeline, FFX_ERROR_INVALID_ARGUMENT);
			ERR_FAIL_COND_V(state.srvTextureCount > FSR3_MAX_JOB_BINDINGS || state.uavTextureCount > FSR3_MAX_JOB_BINDINGS || state.constCount > FFX_MAX_NUM_CONST_BUFFERS, FFX_ERROR_OUT_OF_RANGE);

			FSR3Context::Job job;
			job.type = FFX_GPU_JOB_COMPUTE;
			job.pipeline = reinterpret_cast<FSR3Effect::Device::Pipeline *>(state.pipeline);
			for (uint32_t i = 0; i < 3; i++) {
				job.dimensions[i] = compute.dimensions[i];
			}
			job.srv_count = state.srvTextureCount;
			for (uint32_t i = 0; i < job.srv_count; i++) {
				job.srvs[i].slot = state.srvTextureBindings[i].slotIndex;
				job.srvs[i].resource = compute.srvTextures[i].resource.internalIndex;
			}
			job.uav_count = state.uavTextureCount;
			for (uint32_t i = 0; i < job.uav_count; i++) {
				job.uavs[i].slot = state.uavTextureBindings[i].slotIndex;
				job.uavs[i].resource = compute.uavTextures[i].resource.internalIndex;
				job.uavs[i].mip = compute.uavTextures[i].mip;
			}
			job.cb_count = state.constCount;
			for (uint32_t i = 0; i < job.cb_count; i++) {
				job.cbs[i].slot = state.constantBufferBindings[i].slotIndex;
				const uint32_t *data = compute.cbs[i].data;
				int32_t slot = -1;
				for (uint32_t s = 0; s < scratch.staging_count; s++) {
					if (data == scratch.staging[s]) {
						slot = s;
						break;
					}
				}
				ERR_FAIL_COND_V_MSG(slot < 0, FFX_ERROR_INVALID_ARGUMENT, "FSR 3.1: a constant buffer that was not staged.");
				job.cbs[i].resource = slot;
			}
			scratch.jobs.push_back(job);
		} break;
		default: {
			// Discards and barriers: RenderingDevice tracks the textures' use itself.
		} break;
	}
	return FFX_OK;
}

static FfxErrorCode execute_compute_job_rd(FSR3Context::Scratch &p_scratch, FSR3Context::Job &p_job) {
	RenderingDevice *rd = RD::get_singleton();
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL_V(uniform_set_cache, FFX_ERROR_BACKEND_API_ERROR);
	ERR_FAIL_NULL_V(p_job.pipeline, FFX_ERROR_INVALID_ARGUMENT);

	FSR3Effect::Device &device = *p_scratch.device;
	RID shader;
	RID pipeline = device.get_pipeline(*p_job.pipeline->pass, p_job.pipeline->variant, shader);
	ERR_FAIL_COND_V(pipeline.is_null(), FFX_ERROR_BACKEND_API_ERROR);

	FSR3Context::Resources &resources = p_scratch.resources;
	thread_local LocalVector<RD::Uniform> uniforms;
	uniforms.clear();

	for (uint32_t i = 0; i < p_job.srv_count; i++) {
		const int32_t index = p_job.srvs[i].resource;
		ERR_FAIL_COND_V(index < 0 || uint32_t(index) >= resources.size() || resources.rids[index].is_null(), FFX_ERROR_INVALID_ARGUMENT);
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, p_job.srvs[i].slot, resources.rids[index]));
	}
	for (uint32_t i = 0; i < p_job.uav_count; i++) {
		const int32_t index = p_job.uavs[i].resource;
		ERR_FAIL_COND_V(index < 0 || uint32_t(index) >= resources.size() || resources.rids[index].is_null(), FFX_ERROR_INVALID_ARGUMENT);
		RID image = resources.get_mip(index, p_job.uavs[i].mip);
		ERR_FAIL_COND_V(image.is_null(), FFX_ERROR_BACKEND_API_ERROR);
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, p_job.uavs[i].slot, image));
	}
	for (uint32_t i = 0; i < p_job.cb_count; i++) {
		const uint32_t slot = p_job.cbs[i].resource;
		RID ubo = p_scratch.ubos[p_scratch.ubo_set][slot];
		if (!p_scratch.uploaded[slot]) {
			rd->buffer_update(ubo, 0, p_scratch.staging_words[slot] * sizeof(uint32_t), p_scratch.staging[slot]);
			p_scratch.uploaded[slot] = true;
		}
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, p_job.cbs[i].slot, ubo));
	}
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, FSR3_SAMPLER_POINT_BINDING, device.point_clamp_sampler));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, FSR3_SAMPLER_LINEAR_BINDING, device.linear_clamp_sampler));

	RID uniform_set = uniform_set_cache->get_cache_vec(shader, 0, uniforms);
	ERR_FAIL_COND_V(uniform_set.is_null(), FFX_ERROR_BACKEND_API_ERROR);

	RD::ComputeListID compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, pipeline);
	rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	rd->compute_list_dispatch(compute_list, p_job.dimensions[0], p_job.dimensions[1], p_job.dimensions[2]);
	rd->compute_list_end();
	return FFX_OK;
}

static FfxErrorCode execute_gpu_jobs_rd(FfxInterface *p_backend_interface, FfxCommandList p_command_list, FfxUInt32 p_effect_context_id) {
	FSR3Context::Scratch &scratch = get_scratch(p_backend_interface);
	FSR3Context::Resources &resources = scratch.resources;
	RenderingDevice *rd = RD::get_singleton();
	FfxErrorCode error = FFX_OK;

	for (FSR3Context::Job &job : scratch.jobs) {
		switch (job.type) {
			case FFX_GPU_JOB_CLEAR_FLOAT: {
				if (job.target < 0 || uint32_t(job.target) >= resources.size() || resources.rids[job.target].is_null()) {
					error = FFX_ERROR_INVALID_ARGUMENT;
					break;
				}
				const FfxResourceDescription &desc = resources.descriptions[job.target].d;
				const uint32_t texture_mips = fsr3_texture_mips(resources.rids[job.target]);
				if (fsr3_debug() || MAX(desc.mipCount, 1u) > texture_mips) {
					print_line(vformat("FSR3_DEBUG clear #%d mips %d (texture %d)", job.target, desc.mipCount, texture_mips));
				}
				rd->texture_clear(resources.rids[job.target], job.color, 0, MIN(MAX(desc.mipCount, 1u), MAX(texture_mips, 1u)), 0, 1);
			} break;
			case FFX_GPU_JOB_COPY: {
				if (job.target < 0 || job.source < 0 || uint32_t(job.target) >= resources.size() || uint32_t(job.source) >= resources.size()) {
					error = FFX_ERROR_INVALID_ARGUMENT;
					break;
				}
				const FfxResourceDescription &desc = resources.descriptions[job.source].d;
				for (uint32_t mip = 0; mip < MAX(desc.mipCount, 1u); mip++) {
					rd->texture_copy(resources.rids[job.source], resources.rids[job.target], Vector3(), Vector3(), Vector3(MAX(desc.width >> mip, 1u), MAX(desc.height >> mip, 1u), 1), mip, mip, 0, 0);
				}
			} break;
			case FFX_GPU_JOB_COMPUTE: {
				error = execute_compute_job_rd(scratch, job);
			} break;
			default:
				break;
		}
		if (error != FFX_OK) {
			break;
		}
	}
	scratch.jobs.clear();
	scratch.error = scratch.error || error != FFX_OK;
	return error;
}

static FfxResource get_resource_rd(RID *p_rid, const wchar_t *p_name) {
	FfxResource resource = {};
	if (p_rid == nullptr || p_rid->is_null()) {
		return resource;
	}
	wcsncpy(resource.name, p_name, FFX_RESOURCE_NAME_SIZE - 1);
	RD::TextureFormat format = RD::get_singleton()->texture_get_format(*p_rid);
	resource.description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
	resource.description.format = rd_format_to_ffx_surface_format(format.format);
	resource.description.width = format.width;
	resource.description.height = format.height;
	resource.description.depth = format.depth;
	resource.description.mipCount = format.mipmaps;
	resource.description.flags = FFX_RESOURCE_FLAGS_NONE;
	resource.description.usage = (format.usage_bits & RD::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? FFX_RESOURCE_USAGE_DEPTHTARGET : FFX_RESOURCE_USAGE_READ_ONLY;
	resource.resource = reinterpret_cast<void *>(p_rid);
	resource.state = FFX_RESOURCE_STATE_COMPUTE_READ;
	return resource;
}

static RID create_shared_texture(const FfxCreateResourceDescription &p_description) {
	const FfxResourceDescription &desc = p_description.resourceDescription;
	RD::TextureFormat format;
	format.texture_type = RD::TEXTURE_TYPE_2D;
	format.format = ffx_surface_format_to_rd_format(desc.format);
	format.usage_bits = ffx_usage_to_rd_usage_bits(desc.usage | FFX_RESOURCE_USAGE_UAV);
	format.width = desc.width;
	format.height = desc.height;
	format.mipmaps = 1;
	RID texture = RD::get_singleton()->texture_create(format, RD::TextureView());
	if (texture.is_valid() && p_description.name) {
		RD::get_singleton()->set_resource_name(texture, String(p_description.name));
	}
	return texture;
}

FSR3Context::~FSR3Context() {
	if (created) {
		ffxFsr3UpscalerContextDestroy(fsr_context);
	}
	if (fsr_context) {
		memfree(fsr_context);
	}
	for (RID *rid : { &dilated_depth, &dilated_motion_vectors, &reconstructed_previous_nearest_depth }) {
		if (rid->is_valid()) {
			RD::get_singleton()->free_rid(*rid);
		}
	}
}

// The effect.

FSR3Effect::FSR3Effect() {
	device = memnew(Device);
	RenderingDevice *rd = RD::get_singleton();

	FfxDeviceCapabilities &capabilities = device->capabilities;
	capabilities.maximumSupportedShaderModel = FFX_SHADER_MODEL_5_1;
	capabilities.waveLaneCountMin = 32;
	capabilities.waveLaneCountMax = 32;
	capabilities.fp16Supported = rd->has_feature(RD::Features::SUPPORTS_HALF_FLOAT) && GLOBAL_GET("rendering/fellow/upscaling/fsr3_half_precision");
	capabilities.raytracingSupported = false;

	// The SDK reprojects with a look-up table of the Lanczos kernel where a GPU runs waves of 32 and 64 (RDNA), with the
	// kernel computed elsewhere.
	const uint32_t subgroup_min = rd->limit_get(RD::LIMIT_SUBGROUP_MIN_SIZE);
	const uint32_t subgroup_max = rd->limit_get(RD::LIMIT_SUBGROUP_MAX_SIZE);
	device->use_lut = subgroup_min == 32 && subgroup_max == 64;

	// The options the SDK's Vulkan shaders are compiled with (CMakeCompileFSR3UpscalerShaders.txt), and the context's:
	// HDR colour, motion vectors at the render resolution without jitter, inverted depth; Godot's reactive mask clamp
	// and the motion vectors it derives from the depth where the velocity buffer has none.
	String general_defines =
			"\n#define FFX_GPU 1\n"
			"\n#define FFX_GLSL 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF 0\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF 0\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF 0\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE 2\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS 0\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_GODOT_REACTIVE_MASK_CLAMP 1\n"
			"\n#define FFX_FSR3UPSCALER_OPTION_GODOT_DERIVE_INVALID_MOTION_VECTORS 1\n";
	general_defines += device->use_lut ? "\n#define FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE 1\n" : "\n#define FFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE 0\n";

	Vector<String> modes_single;
	modes_single.push_back("");

	Vector<String> modes_fp16;
	modes_fp16.push_back("");
	modes_fp16.push_back("\n#define FFX_HALF 1\n");

	Vector<String> modes_sharpen_fp16;
	modes_sharpen_fp16.push_back("");
	modes_sharpen_fp16.push_back("\n#define FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING 1\n");
	modes_sharpen_fp16.push_back("\n#define FFX_HALF 1\n");
	modes_sharpen_fp16.push_back("\n#define FFX_HALF 1\n#define FFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING 1\n");

	const bool fp16 = capabilities.fp16Supported;

	auto setup = [&](FfxFsr3UpscalerPass p_pass, ShaderRD *p_shader, bool p_fp16_variants, bool p_sharpen_variants, std::initializer_list<BindingName> p_srvs, std::initializer_list<BindingName> p_uavs, std::initializer_list<BindingName> p_cbs) {
		Device::Pass &pass = device->passes[p_pass];
		pass.shader = p_shader;
		pass.sharpen_variants = p_sharpen_variants;
		pass.fp16 = p_fp16_variants && fp16;
		const Vector<String> &modes = p_sharpen_variants ? modes_sharpen_fp16 : (p_fp16_variants ? modes_fp16 : modes_single);
		pass.variant_count = modes.size();
		p_shader->initialize(modes, general_defines);
		if (!pass.fp16) {
			// Only the variants that can be chosen are compiled.
			for (int i = 0; i < modes.size(); i++) {
				if (modes[i].contains("FFX_HALF")) {
					p_shader->set_variant_enabled(i, false);
				}
			}
		}
		pass.shader_version = p_shader->version_create();
		for (const BindingName &b : p_srvs) {
			pass.srvs.push_back(b);
		}
		for (const BindingName &b : p_uavs) {
			pass.uavs.push_back(b);
		}
		for (const BindingName &b : p_cbs) {
			pass.cbs.push_back(b);
		}
	};

	setup(FFX_FSR3UPSCALER_PASS_PREPARE_INPUTS, &device->shaders.prepare_inputs, true, false,
			{ { 0, L"r_input_motion_vectors" }, { 1, L"r_input_depth" }, { 2, L"r_input_color_jittered" } },
			{ { 3, L"rw_dilated_motion_vectors" }, { 4, L"rw_dilated_depth" }, { 5, L"rw_reconstructed_previous_nearest_depth" }, { 6, L"rw_farthest_depth" }, { 7, L"rw_current_luma" } },
			{ { 8, L"cbFSR3Upscaler" } });

	// The pyramids (SPD) in full precision: SPD's half-precision path uses subgroup operations on 16-bit floats
	// (shaderSubgroupExtendedTypes, which RenderingDevice does not enable).
	setup(FFX_FSR3UPSCALER_PASS_LUMA_PYRAMID, &device->shaders.luma_pyramid, false, false,
			{ { 0, L"r_current_luma" }, { 1, L"r_farthest_depth" } },
			{ { 2, L"rw_spd_global_atomic" }, { 3, L"rw_frame_info" }, { 4, L"rw_spd_mip0" }, { 5, L"rw_spd_mip1" }, { 6, L"rw_spd_mip2" }, { 7, L"rw_spd_mip3" }, { 8, L"rw_spd_mip4" }, { 9, L"rw_spd_mip5" }, { 10, L"rw_farthest_depth_mip1" } },
			{ { 11, L"cbFSR3Upscaler" }, { 12, L"cbSPD" } });

	setup(FFX_FSR3UPSCALER_PASS_SHADING_CHANGE_PYRAMID, &device->shaders.shading_change_pyramid, false, false,
			{ { 0, L"r_current_luma" }, { 1, L"r_previous_luma" }, { 2, L"r_dilated_motion_vectors" }, { 3, L"r_input_exposure" } },
			{ { 4, L"rw_spd_global_atomic" }, { 5, L"rw_spd_mip0" }, { 6, L"rw_spd_mip1" }, { 7, L"rw_spd_mip2" }, { 8, L"rw_spd_mip3" }, { 9, L"rw_spd_mip4" }, { 10, L"rw_spd_mip5" } },
			{ { 11, L"cbFSR3Upscaler" }, { 12, L"cbSPD" } });

	setup(FFX_FSR3UPSCALER_PASS_SHADING_CHANGE, &device->shaders.shading_change, true, false,
			{ { 0, L"r_spd_mips" } },
			{ { 1, L"rw_shading_change" } },
			{ { 2, L"cbFSR3Upscaler" } });

	setup(FFX_FSR3UPSCALER_PASS_PREPARE_REACTIVITY, &device->shaders.prepare_reactivity, true, false,
			{ { 0, L"r_reconstructed_previous_nearest_depth" }, { 1, L"r_dilated_motion_vectors" }, { 2, L"r_dilated_depth" }, { 3, L"r_reactive_mask" }, { 4, L"r_transparency_and_composition_mask" }, { 5, L"r_accumulation" }, { 6, L"r_shading_change" }, { 7, L"r_current_luma" }, { 8, L"r_input_exposure" } },
			{ { 9, L"rw_dilated_reactive_masks" }, { 10, L"rw_new_locks" }, { 11, L"rw_accumulation" } },
			{ { 12, L"cbFSR3Upscaler" } });

	setup(FFX_FSR3UPSCALER_PASS_LUMA_INSTABILITY, &device->shaders.luma_instability, true, false,
			{ { 0, L"r_input_exposure" }, { 1, L"r_dilated_reactive_masks" }, { 2, L"r_dilated_motion_vectors" }, { 3, L"r_frame_info" }, { 4, L"r_luma_history" }, { 5, L"r_farthest_depth_mip1" }, { 6, L"r_current_luma" } },
			{ { 7, L"rw_luma_history" }, { 8, L"rw_luma_instability" } },
			{ { 9, L"cbFSR3Upscaler" } });

	// Accumulation with and without sharpening share the bindings; FSR 2.2's port leaves the accumulation in full
	// precision on NVIDIA (occupancy), and so does this one.
	const bool accumulate_fp16 = rd->get_device_vendor_name() != "NVIDIA";
	setup(FFX_FSR3UPSCALER_PASS_ACCUMULATE, &device->shaders.accumulate, accumulate_fp16, true,
			{ { 0, L"r_input_exposure" }, { 1, L"r_dilated_reactive_masks" }, { 2, L"r_dilated_motion_vectors" }, { 3, L"r_internal_upscaled_color" }, { 4, L"r_lanczos_lut" }, { 5, L"r_farthest_depth_mip1" }, { 6, L"r_current_luma" }, { 7, L"r_luma_instability" }, { 8, L"r_input_color_jittered" } },
			{ { 9, L"rw_internal_upscaled_color" }, { 10, L"rw_upscaled_output" }, { 11, L"rw_new_locks" } },
			{ { 12, L"cbFSR3Upscaler" } });
	{
		Device::Pass &accumulate = device->passes[FFX_FSR3UPSCALER_PASS_ACCUMULATE];
		Device::Pass &sharpen = device->passes[FFX_FSR3UPSCALER_PASS_ACCUMULATE_SHARPEN];
		sharpen.shader = accumulate.shader;
		sharpen.shader_version = accumulate.shader_version;
		sharpen.variant_count = accumulate.variant_count;
		sharpen.fp16 = accumulate.fp16;
		sharpen.sharpen_variants = true;
		sharpen.srvs = accumulate.srvs;
		sharpen.uavs = accumulate.uavs;
		sharpen.cbs = accumulate.cbs;
	}

	setup(FFX_FSR3UPSCALER_PASS_RCAS, &device->shaders.rcas, false, false,
			{ { 0, L"r_input_exposure" }, { 1, L"r_rcas_input" } },
			{ { 2, L"rw_upscaled_output" } },
			{ { 3, L"cbFSR3Upscaler" }, { 4, L"cbRCAS" } });

	setup(FFX_FSR3UPSCALER_PASS_DEBUG_VIEW, &device->shaders.debug_view, false, false,
			{ { 0, L"r_dilated_reactive_masks" }, { 1, L"r_dilated_motion_vectors" }, { 2, L"r_dilated_depth" }, { 3, L"r_internal_upscaled_color" }, { 4, L"r_input_exposure" } },
			{ { 5, L"rw_upscaled_output" } },
			{ { 6, L"cbFSR3Upscaler" } });

	// Godot renders no opaque-only colour for it: built for the API, never dispatched.
	setup(FFX_FSR3UPSCALER_PASS_GENERATE_REACTIVE, &device->shaders.autogen_reactive, true, false,
			{ { 0, L"r_input_opaque_only" }, { 1, L"r_input_color_jittered" } },
			{ { 2, L"rw_output_autoreactive" } },
			{ { 5, L"cbGenerateReactive" } });

	RD::SamplerState state;
	state.mag_filter = RD::SAMPLER_FILTER_NEAREST;
	state.min_filter = RD::SAMPLER_FILTER_NEAREST;
	state.mip_filter = RD::SAMPLER_FILTER_NEAREST;
	state.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	state.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	state.repeat_w = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	state.min_lod = -1000.0f;
	state.max_lod = 1000.0f;
	state.anisotropy_max = 1.0;
	device->point_clamp_sampler = rd->sampler_create(state);

	state.mag_filter = RD::SAMPLER_FILTER_LINEAR;
	state.min_filter = RD::SAMPLER_FILTER_LINEAR;
	state.mip_filter = RD::SAMPLER_FILTER_LINEAR;
	device->linear_clamp_sampler = rd->sampler_create(state);
}

FSR3Effect::~FSR3Effect() {
	RenderingDevice *rd = RD::get_singleton();
	rd->free_rid(device->point_clamp_sampler);
	rd->free_rid(device->linear_clamp_sampler);
	for (uint32_t i = 0; i < FFX_FSR3UPSCALER_PASS_COUNT; i++) {
		Device::Pass &pass = device->passes[i];
		if (pass.shader == nullptr || i == FFX_FSR3UPSCALER_PASS_ACCUMULATE_SHARPEN) {
			continue; // The sharpening pass shares the accumulation's shader.
		}
		// Freeing the shader version frees its pipelines.
		pass.shader->version_free(pass.shader_version);
	}
	memdelete(device);
}

FSR3Context *FSR3Effect::create_context(Size2i p_internal_size, Size2i p_target_size) {
	FSR3Context *context = memnew(FSR3Context);
	context->scratch.device = device;
	context->internal_size = p_internal_size;
	context->target_size = p_target_size;

	FfxFsr3UpscalerContextDescription &desc = context->fsr_desc;
	desc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED;
	desc.maxRenderSize.width = p_internal_size.x;
	desc.maxRenderSize.height = p_internal_size.y;
	desc.maxUpscaleSize.width = p_target_size.x;
	desc.maxUpscaleSize.height = p_target_size.y;

	FfxInterface &functions = desc.backendInterface;
	functions.fpGetSDKVersion = get_sdk_version_rd;
	functions.fpGetEffectGpuMemoryUsage = get_effect_gpu_memory_usage_rd;
	functions.fpCreateBackendContext = create_backend_context_rd;
	functions.fpGetDeviceCapabilities = get_device_capabilities_rd;
	functions.fpDestroyBackendContext = destroy_backend_context_rd;
	functions.fpCreateResource = create_resource_rd;
	functions.fpRegisterResource = register_resource_rd;
	functions.fpUnregisterResources = unregister_resources_rd;
	functions.fpGetResourceDescription = get_resource_description_rd;
	functions.fpDestroyResource = destroy_resource_rd;
	functions.fpStageConstantBufferDataFunc = stage_constant_buffer_data_rd;
	functions.fpCreatePipeline = create_pipeline_rd;
	functions.fpDestroyPipeline = destroy_pipeline_rd;
	functions.fpScheduleGpuJob = schedule_gpu_job_rd;
	functions.fpExecuteGpuJobs = execute_gpu_jobs_rd;
	functions.scratchBuffer = &context->scratch;
	functions.scratchBufferSize = sizeof(context->scratch);
	functions.device = device;

	if (fsr3_debug()) {
		print_line(vformat("FSR3_DEBUG context %s -> %s (context %d bytes, scratch %d bytes)", p_internal_size, p_target_size, int64_t(sizeof(FfxFsr3UpscalerContext)), int64_t(sizeof(context->scratch))));
	}
	context->fsr_context = reinterpret_cast<FfxFsr3UpscalerContext *>(memalloc(sizeof(FfxFsr3UpscalerContext)));
	FfxErrorCode result = ffxFsr3UpscalerContextCreate(context->fsr_context, &desc);
	if (result != FFX_OK) {
		ERR_PRINT(vformat("FSR 3.1: the context could not be created (error 0x%x).", uint32_t(result)));
		memdelete(context);
		return nullptr;
	}
	context->created = true;

	// The resources the upscaler shares with frame generation are the application's.
	FfxFsr3UpscalerSharedResourceDescriptions shared = {};
	ffxFsr3UpscalerGetSharedResourceDescriptions(context->fsr_context, &shared);
	context->dilated_depth = create_shared_texture(shared.dilatedDepth);
	context->dilated_motion_vectors = create_shared_texture(shared.dilatedMotionVectors);
	context->reconstructed_previous_nearest_depth = create_shared_texture(shared.reconstructedPrevNearestDepth);
	if (context->dilated_depth.is_null() || context->dilated_motion_vectors.is_null() || context->reconstructed_previous_nearest_depth.is_null()) {
		ERR_PRINT("FSR 3.1: the shared resources could not be created.");
		memdelete(context);
		return nullptr;
	}
	return context;
}

void FSR3Effect::free_context(FSR3Context *p_context) {
	if (p_context) {
		memdelete(p_context);
	}
}

void FSR3Effect::upscale(const Parameters &p_params, const Tuning &p_tuning, bool p_debug_view) {
	FSR3Context *context = p_params.context;
	ERR_FAIL_NULL(context);

	if (context->tuning != p_tuning) {
		Tuning t = p_tuning;
		ffxFsr3UpscalerSetConstant(context->fsr_context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, &t.velocity_factor);
		ffxFsr3UpscalerSetConstant(context->fsr_context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE, &t.reactiveness_scale);
		ffxFsr3UpscalerSetConstant(context->fsr_context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE, &t.shading_change_scale);
		ffxFsr3UpscalerSetConstant(context->fsr_context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME, &t.accumulation_added_per_frame);
		ffxFsr3UpscalerSetConstant(context->fsr_context, FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION, &t.min_disocclusion_accumulation);
		context->tuning = p_tuning;
	}

	// A new set of uniform buffers for this dispatch's constants.
	FSR3Context::Scratch &scratch = context->scratch;
	scratch.staging_count = 0;
	scratch.ubo_set = (scratch.ubo_set + 1) % FSR3_UBO_SETS;
	for (bool &uploaded : scratch.uploaded) {
		uploaded = false;
	}

	RID color = p_params.color;
	RID depth = p_params.depth;
	RID velocity = p_params.velocity;
	RID reactive = p_params.reactive;
	RID exposure = p_params.exposure;
	RID output = p_params.output;

	FfxFsr3UpscalerDispatchDescription dispatch = {};
	dispatch.commandList = reinterpret_cast<FfxCommandList>(context);
	dispatch.color = get_resource_rd(&color, L"color");
	dispatch.depth = get_resource_rd(&depth, L"depth");
	dispatch.motionVectors = get_resource_rd(&velocity, L"velocity");
	dispatch.exposure = get_resource_rd(&exposure, L"exposure");
	dispatch.reactive = get_resource_rd(&reactive, L"reactive");
	dispatch.transparencyAndComposition = {};
	dispatch.dilatedDepth = get_resource_rd(&context->dilated_depth, L"dilated_depth");
	dispatch.dilatedMotionVectors = get_resource_rd(&context->dilated_motion_vectors, L"dilated_motion_vectors");
	dispatch.reconstructedPrevNearestDepth = get_resource_rd(&context->reconstructed_previous_nearest_depth, L"reconstructed_previous_nearest_depth");
	dispatch.output = get_resource_rd(&output, L"output");
	dispatch.jitterOffset.x = p_params.jitter.x;
	dispatch.jitterOffset.y = p_params.jitter.y;
	dispatch.motionVectorScale.x = float(p_params.internal_size.width);
	dispatch.motionVectorScale.y = float(p_params.internal_size.height);
	dispatch.renderSize.width = p_params.internal_size.width;
	dispatch.renderSize.height = p_params.internal_size.height;
	dispatch.upscaleSize.width = p_params.target_size.width;
	dispatch.upscaleSize.height = p_params.target_size.height;
	dispatch.enableSharpening = p_params.sharpness > 1e-6f;
	dispatch.sharpness = p_params.sharpness;
	dispatch.frameTimeDelta = p_params.delta_time * 1000.0f; // Milliseconds.
	dispatch.preExposure = 1.0f;
	dispatch.reset = p_params.reset_accumulation;
	dispatch.cameraNear = p_params.z_near;
	dispatch.cameraFar = p_params.z_far;
	dispatch.cameraFovAngleVertical = p_params.fovy;
	dispatch.viewSpaceToMetersFactor = 1.0f;
	dispatch.flags = p_debug_view ? FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW : 0;
	MaterialStorage::store_camera(p_params.reprojection, dispatch.reprojectionMatrix);

	if (fsr3_debug()) {
		print_line(vformat("FSR3_DEBUG dispatch %dx%d -> %dx%d reset %d", dispatch.renderSize.width, dispatch.renderSize.height, dispatch.upscaleSize.width, dispatch.upscaleSize.height, int(dispatch.reset)));
	}
	FfxErrorCode result = ffxFsr3UpscalerContextDispatch(context->fsr_context, &dispatch);
	ERR_FAIL_COND_MSG(result != FFX_OK, vformat("FSR 3.1: dispatch failed (error 0x%x).", uint32_t(result)));
}
