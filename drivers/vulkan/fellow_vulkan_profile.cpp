/**************************************************************************/
/*  fellow_vulkan_profile.cpp                                             */
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

#include "fellow_vulkan_profile.h"

#include "fellow_vulkan_profile_data.h"

#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"
#include "drivers/vulkan/godot_vulkan.h"

#include <cstring>

// Single-axis profiles: the device as it is, with one family of features hidden.
static void _fellow_vulkan_profiles_synthetic(LocalVector<FellowVulkanProfile> &r_profiles) {
	{
		static const char *const denied[] = { "VK_KHR_acceleration_structure", "VK_KHR_ray_query", "VK_KHR_ray_tracing_pipeline", "VK_KHR_ray_tracing_position_fetch", "VK_KHR_deferred_host_operations", nullptr };
		FellowVulkanProfile p;
		p.name = "no_ray_tracing";
		p.source = "the device without ray tracing";
		p.denied_extensions = denied;
		p.features &= ~(FELLOW_VKF_BIT(accelerationStructure) | FELLOW_VKF_BIT(rayQuery) | FELLOW_VKF_BIT(rayTracingPipeline) | FELLOW_VKF_BIT(rayTracingPositionFetch));
		r_profiles.push_back(p);
	}
	{
		FellowVulkanProfile p;
		p.name = "no_float16";
		p.source = "the device without 16-bit floats and storage";
		p.features &= ~(FELLOW_VKF_BIT(shaderFloat16) | FELLOW_VKF_BIT(storageBuffer16BitAccess) | FELLOW_VKF_BIT(uniformAndStorageBuffer16BitAccess) | FELLOW_VKF_BIT(storagePushConstant16) | FELLOW_VKF_BIT(storageInputOutput16) | FELLOW_VKF_BIT(shaderSubgroupExtendedTypes) | FELLOW_VKF_BIT(cooperativeMatrix));
		r_profiles.push_back(p);
	}
	{
		FellowVulkanProfile p;
		p.name = "subgroup_basic";
		p.source = "the device with Vulkan 1.1's minimum subgroup support (basic operations, compute only)";
		p.subgroup_stages = 0x20; // VK_SHADER_STAGE_COMPUTE_BIT
		p.subgroup_operations = 0x1; // VK_SUBGROUP_FEATURE_BASIC_BIT
		r_profiles.push_back(p);
	}
}

static LocalVector<FellowVulkanProfile> &_fellow_vulkan_profiles() {
	static LocalVector<FellowVulkanProfile> profiles;
	if (profiles.is_empty()) {
		fellow_vulkan_profiles_generated(profiles);
		_fellow_vulkan_profiles_synthetic(profiles);
	}
	return profiles;
}

const char *fellow_vulkan_profile_names() {
	static String names;
	if (names.is_empty()) {
		for (const FellowVulkanProfile &p : _fellow_vulkan_profiles()) {
			names += (names.is_empty() ? "" : ", ") + String(p.name);
		}
	}
	static CharString utf8 = names.utf8();
	return utf8.get_data();
}

const FellowVulkanProfile *fellow_vulkan_profile_get() {
	static bool resolved = false;
	static const FellowVulkanProfile *profile = nullptr;
	if (resolved) {
		return profile;
	}
	resolved = true;
	const String name = OS::get_singleton()->get_environment("FELLOW_VULKAN_PROFILE");
	if (name.is_empty()) {
		return nullptr;
	}
	for (const FellowVulkanProfile &p : _fellow_vulkan_profiles()) {
		if (name == p.name) {
			profile = &p;
			print_line(vformat("FellowEngine: simulating the Vulkan device \"%s\": %s.", p.name, p.source));
			return profile;
		}
	}
	ERR_PRINT(vformat("FELLOW_VULKAN_PROFILE: unknown profile \"%s\" (known: %s); the device is used as it is.", name, fellow_vulkan_profile_names()));
	return nullptr;
}

bool fellow_vulkan_profile_allows_extension(const FellowVulkanProfile *p_profile, const char *p_extension) {
	if (!p_profile) {
		return true;
	}
	// Diagnostics, never part of what a device can do for a game.
	static const char *const always[] = { "VK_KHR_shader_non_semantic_info", "VK_EXT_debug_marker", "VK_EXT_device_fault", "VK_EXT_device_memory_report", "VK_KHR_pipeline_executable_properties" };
	for (const char *a : always) {
		if (strcmp(a, p_extension) == 0) {
			return true;
		}
	}
	if (p_profile->denied_extensions) {
		for (const char *const *e = p_profile->denied_extensions; *e; e++) {
			if (strcmp(*e, p_extension) == 0) {
				return false;
			}
		}
	}
	if (!p_profile->extensions) {
		return true;
	}
	for (const char *const *e = p_profile->extensions; *e; e++) {
		if (strcmp(*e, p_extension) == 0) {
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------ SPIR-V

// SPIR-V opcodes and enums used below (the SPIR-V specification, unified1).
enum : uint32_t {
	SPV_MAGIC = 0x07230203,
	SPV_OP_EXECUTION_MODE = 16,
	SPV_OP_CAPABILITY = 17,
	SPV_OP_TYPE_BOOL = 20,
	SPV_OP_TYPE_INT = 21,
	SPV_OP_TYPE_FLOAT = 22,
	SPV_OP_TYPE_VECTOR = 23,
	SPV_OP_TYPE_MATRIX = 24,
	SPV_OP_TYPE_ARRAY = 28,
	SPV_OP_TYPE_STRUCT = 30,
	SPV_OP_TYPE_POINTER = 32,
	SPV_OP_CONSTANT = 43,
	SPV_OP_CONSTANT_COMPOSITE = 44,
	SPV_OP_SPEC_CONSTANT = 50,
	SPV_OP_SPEC_CONSTANT_COMPOSITE = 51,
	SPV_OP_VARIABLE = 59,
	SPV_OP_DECORATE = 71,
	SPV_OP_EXECUTION_MODE_ID = 331,
	SPV_EXECUTION_MODE_LOCAL_SIZE = 17,
	SPV_EXECUTION_MODE_LOCAL_SIZE_ID = 38,
	SPV_DECORATION_BUILT_IN = 11,
	SPV_BUILT_IN_WORKGROUP_SIZE = 25,
	SPV_STORAGE_CLASS_WORKGROUP = 4,
};

bool fellow_spirv_summarize(const uint32_t *p_words, size_t p_word_count, FellowSpirvSummary &r_summary) {
	r_summary = FellowSpirvSummary();
	if (p_word_count < 5 || p_words[0] != SPV_MAGIC) {
		return false;
	}
	HashMap<uint32_t, uint64_t> type_size; // bytes
	HashMap<uint32_t, uint32_t> constant; // scalar constants' values (specialization constants at their defaults)
	HashMap<uint32_t, LocalVector<uint32_t>> composite; // constant composites' constituents
	HashMap<uint32_t, uint32_t> pointee; // Workgroup pointers' pointee types
	uint32_t workgroup_size_id = 0;
	uint32_t local_size_ids[3] = { 0, 0, 0 };
	size_t i = 5;
	while (i < p_word_count) {
		const uint32_t count = p_words[i] >> 16;
		const uint32_t op = p_words[i] & 0xFFFF;
		if (count == 0 || i + count > p_word_count) {
			return false;
		}
		const uint32_t *w = p_words + i;
		switch (op) {
			case SPV_OP_CAPABILITY:
				r_summary.capabilities.push_back(w[1]);
				break;
			case SPV_OP_EXECUTION_MODE:
				if (count >= 6 && w[2] == SPV_EXECUTION_MODE_LOCAL_SIZE) {
					r_summary.local_size[0] = w[3];
					r_summary.local_size[1] = w[4];
					r_summary.local_size[2] = w[5];
				}
				break;
			case SPV_OP_EXECUTION_MODE_ID:
				if (count >= 6 && w[2] == SPV_EXECUTION_MODE_LOCAL_SIZE_ID) {
					local_size_ids[0] = w[3];
					local_size_ids[1] = w[4];
					local_size_ids[2] = w[5];
				}
				break;
			case SPV_OP_DECORATE:
				if (count >= 4 && w[2] == SPV_DECORATION_BUILT_IN && w[3] == SPV_BUILT_IN_WORKGROUP_SIZE) {
					workgroup_size_id = w[1];
				}
				break;
			case SPV_OP_TYPE_BOOL:
				type_size[w[1]] = 4;
				break;
			case SPV_OP_TYPE_INT:
			case SPV_OP_TYPE_FLOAT:
				type_size[w[1]] = w[2] / 8;
				break;
			case SPV_OP_TYPE_VECTOR:
			case SPV_OP_TYPE_MATRIX:
				type_size[w[1]] = (type_size.has(w[2]) ? type_size[w[2]] : 0) * w[3];
				break;
			case SPV_OP_TYPE_ARRAY:
				type_size[w[1]] = (type_size.has(w[2]) ? type_size[w[2]] : 0) * (constant.has(w[3]) ? constant[w[3]] : 0);
				break;
			case SPV_OP_TYPE_STRUCT: {
				uint64_t s = 0;
				for (uint32_t m = 2; m < count; m++) {
					s += type_size.has(w[m]) ? type_size[w[m]] : 0;
				}
				type_size[w[1]] = s;
			} break;
			case SPV_OP_TYPE_POINTER:
				if (w[2] == SPV_STORAGE_CLASS_WORKGROUP) {
					pointee[w[1]] = w[3];
				}
				break;
			case SPV_OP_CONSTANT:
			case SPV_OP_SPEC_CONSTANT:
				if (count >= 4) {
					constant[w[2]] = w[3];
				}
				break;
			case SPV_OP_CONSTANT_COMPOSITE:
			case SPV_OP_SPEC_CONSTANT_COMPOSITE: {
				LocalVector<uint32_t> parts;
				for (uint32_t m = 3; m < count; m++) {
					parts.push_back(w[m]);
				}
				composite[w[2]] = parts;
			} break;
			case SPV_OP_VARIABLE:
				if (count >= 4 && w[3] == SPV_STORAGE_CLASS_WORKGROUP && pointee.has(w[1])) {
					const uint32_t t = pointee[w[1]];
					r_summary.workgroup_bytes += type_size.has(t) ? type_size[t] : 0;
				}
				break;
			default:
				break;
		}
		i += count;
	}
	if (local_size_ids[0]) {
		for (int k = 0; k < 3; k++) {
			r_summary.local_size[k] = constant.has(local_size_ids[k]) ? constant[local_size_ids[k]] : 1;
		}
	}
	if (workgroup_size_id && composite.has(workgroup_size_id) && composite[workgroup_size_id].size() == 3) {
		for (int k = 0; k < 3; k++) {
			const uint32_t id = composite[workgroup_size_id][k];
			r_summary.local_size[k] = constant.has(id) ? constant[id] : 1;
		}
	}
	r_summary.valid = true;
	return true;
}

const char *fellow_spirv_capability_name(uint32_t p_capability) {
	switch (p_capability) {
		case 2:
			return "Geometry";
		case 3:
			return "Tessellation";
		case 9:
			return "Float16";
		case 10:
			return "Float64";
		case 11:
			return "Int64";
		case 12:
			return "Int64Atomics";
		case 22:
			return "Int16";
		case 25:
			return "ImageGatherExtended";
		case 27:
			return "StorageImageMultisample";
		case 32:
			return "ClipDistance";
		case 33:
			return "CullDistance";
		case 34:
			return "ImageCubeArray";
		case 35:
			return "SampleRateShading";
		case 39:
			return "Int8";
		case 42:
			return "MinLod";
		case 45:
			return "SampledCubeArray";
		case 49:
			return "StorageImageExtendedFormats";
		case 55:
			return "StorageImageReadWithoutFormat";
		case 56:
			return "StorageImageWriteWithoutFormat";
		case 57:
			return "MultiViewport";
		case 61:
			return "GroupNonUniform";
		case 62:
			return "GroupNonUniformVote";
		case 63:
			return "GroupNonUniformArithmetic";
		case 64:
			return "GroupNonUniformBallot";
		case 65:
			return "GroupNonUniformShuffle";
		case 66:
			return "GroupNonUniformShuffleRelative";
		case 67:
			return "GroupNonUniformClustered";
		case 68:
			return "GroupNonUniformQuad";
		case 4422:
			return "FragmentShadingRateKHR";
		case 4433:
			return "StorageBuffer16BitAccess";
		case 4434:
			return "UniformAndStorageBuffer16BitAccess";
		case 4435:
			return "StoragePushConstant16";
		case 4436:
			return "StorageInputOutput16";
		case 4439:
			return "MultiView";
		case 4448:
			return "StorageBuffer8BitAccess";
		case 4449:
			return "UniformAndStorageBuffer8BitAccess";
		case 4472:
			return "RayQueryKHR";
		case 4479:
			return "RayTracingKHR";
		case 5291:
			return "FragmentDensityEXT";
		case 5336:
			return "RayTracingPositionFetchKHR";
		case 5345:
			return "VulkanMemoryModel";
		case 5346:
			return "VulkanMemoryModelDeviceScope";
		case 5347:
			return "PhysicalStorageBufferAddresses";
		case 5391:
			return "RayQueryPositionFetchKHR";
		case 6022:
			return "CooperativeMatrixKHR";
		default:
			return nullptr;
	}
}
