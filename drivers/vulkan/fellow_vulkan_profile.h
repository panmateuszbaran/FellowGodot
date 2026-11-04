/**************************************************************************/
/*  fellow_vulkan_profile.h                                               */
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

#pragma once

// FellowEngine (milestone 49, decision 0150): simulated Vulkan devices. FELLOW_VULKAN_PROFILE=<name> makes the Vulkan
// driver present the device it runs on as a weaker one — an Android baseline, an Intel iGPU, a GTX 1060, an Apple M1
// through MoltenVK — by hiding the extensions, features and subgroup operations that device lacks and clamping its
// limits, so that a machine with one GPU (or Mesa's lavapipe) can show what Godot's renderers and FellowEngine's
// features do on hardware it does not have. The data of the device profiles is generated from the Khronos
// Vulkan-Profiles repository (fellow_vulkan_profile_data.h). Shaders are checked against the simulated device
// (fellow_spirv_summarize()): capabilities it lacks, workgroups and shared memory beyond its limits, descriptors
// beyond its per-stage limits. Subgroup sizes, performance and driver bugs are not simulated.

#include "core/templates/local_vector.h"

#include <cstddef>
#include <cstdint>

// VkPhysicalDeviceFeatures that Godot enables (RenderingDeviceDriverVulkan::_check_device_features()).
#define FELLOW_VK_BASE_FEATURES(X) \
	X(fullDrawIndexUint32) \
	X(imageCubeArray) \
	X(independentBlend) \
	X(geometryShader) \
	X(tessellationShader) \
	X(sampleRateShading) \
	X(dualSrcBlend) \
	X(logicOp) \
	X(multiDrawIndirect) \
	X(drawIndirectFirstInstance) \
	X(depthClamp) \
	X(depthBiasClamp) \
	X(fillModeNonSolid) \
	X(depthBounds) \
	X(wideLines) \
	X(largePoints) \
	X(alphaToOne) \
	X(multiViewport) \
	X(samplerAnisotropy) \
	X(textureCompressionETC2) \
	X(textureCompressionASTC_LDR) \
	X(textureCompressionBC) \
	X(vertexPipelineStoresAndAtomics) \
	X(fragmentStoresAndAtomics) \
	X(shaderTessellationAndGeometryPointSize) \
	X(shaderImageGatherExtended) \
	X(shaderStorageImageExtendedFormats) \
	X(shaderStorageImageReadWithoutFormat) \
	X(shaderStorageImageWriteWithoutFormat) \
	X(shaderUniformBufferArrayDynamicIndexing) \
	X(shaderSampledImageArrayDynamicIndexing) \
	X(shaderStorageBufferArrayDynamicIndexing) \
	X(shaderStorageImageArrayDynamicIndexing) \
	X(shaderClipDistance) \
	X(shaderCullDistance) \
	X(shaderFloat64) \
	X(shaderInt64) \
	X(shaderInt16) \
	X(shaderResourceMinLod) \
	X(variableMultisampleRate)

// Features of later Vulkan versions and extensions that Godot or FellowEngine enable.
#define FELLOW_VK_EXT_FEATURES(X) \
	X(shaderFloat16) \
	X(shaderInt8) \
	X(storageBuffer16BitAccess) \
	X(uniformAndStorageBuffer16BitAccess) \
	X(storagePushConstant16) \
	X(storageInputOutput16) \
	X(multiview) \
	X(bufferDeviceAddress) \
	X(vulkanMemoryModel) \
	X(vulkanMemoryModelDeviceScope) \
	X(pipelineCreationCacheControl) \
	X(synchronization2) \
	X(accelerationStructure) \
	X(rayQuery) \
	X(rayTracingPipeline) \
	X(rayTracingPositionFetch) \
	X(cooperativeMatrix) \
	X(pipelineFragmentShadingRate) \
	X(primitiveFragmentShadingRate) \
	X(attachmentFragmentShadingRate) \
	X(fragmentDensityMap) \
	X(shaderSubgroupExtendedTypes)

enum FellowVulkanFeature {
#define FELLOW_VK_FEATURE_ENUM(m_name) FELLOW_VKF_##m_name,
	FELLOW_VK_BASE_FEATURES(FELLOW_VK_FEATURE_ENUM)
			FELLOW_VK_EXT_FEATURES(FELLOW_VK_FEATURE_ENUM)
#undef FELLOW_VK_FEATURE_ENUM
					FELLOW_VKF_MAX,
};
static_assert(FELLOW_VKF_MAX <= 64, "The features are a 64-bit mask.");
#define FELLOW_VKF_BIT(m_name) (uint64_t(1) << FELLOW_VKF_##m_name)

// VkPhysicalDeviceLimits clamped by a profile: upper bounds (min with the device's) and alignments (max with it).
#define FELLOW_VK_LIMITS(MAXIMUM, ALIGNMENT) \
	MAXIMUM(maxImageDimension1D) \
	MAXIMUM(maxImageDimension2D) \
	MAXIMUM(maxImageDimension3D) \
	MAXIMUM(maxImageDimensionCube) \
	MAXIMUM(maxImageArrayLayers) \
	MAXIMUM(maxTexelBufferElements) \
	MAXIMUM(maxUniformBufferRange) \
	MAXIMUM(maxStorageBufferRange) \
	MAXIMUM(maxPushConstantsSize) \
	MAXIMUM(maxMemoryAllocationCount) \
	MAXIMUM(maxSamplerAllocationCount) \
	MAXIMUM(maxBoundDescriptorSets) \
	MAXIMUM(maxPerStageDescriptorSamplers) \
	MAXIMUM(maxPerStageDescriptorUniformBuffers) \
	MAXIMUM(maxPerStageDescriptorStorageBuffers) \
	MAXIMUM(maxPerStageDescriptorSampledImages) \
	MAXIMUM(maxPerStageDescriptorStorageImages) \
	MAXIMUM(maxPerStageDescriptorInputAttachments) \
	MAXIMUM(maxPerStageResources) \
	MAXIMUM(maxDescriptorSetSamplers) \
	MAXIMUM(maxDescriptorSetUniformBuffers) \
	MAXIMUM(maxDescriptorSetUniformBuffersDynamic) \
	MAXIMUM(maxDescriptorSetStorageBuffers) \
	MAXIMUM(maxDescriptorSetStorageBuffersDynamic) \
	MAXIMUM(maxDescriptorSetSampledImages) \
	MAXIMUM(maxDescriptorSetStorageImages) \
	MAXIMUM(maxDescriptorSetInputAttachments) \
	MAXIMUM(maxVertexInputAttributes) \
	MAXIMUM(maxVertexInputBindings) \
	MAXIMUM(maxVertexInputAttributeOffset) \
	MAXIMUM(maxVertexInputBindingStride) \
	MAXIMUM(maxVertexOutputComponents) \
	MAXIMUM(maxFragmentInputComponents) \
	MAXIMUM(maxFragmentOutputAttachments) \
	MAXIMUM(maxFragmentCombinedOutputResources) \
	MAXIMUM(maxComputeSharedMemorySize) \
	MAXIMUM(maxComputeWorkGroupInvocations) \
	MAXIMUM(maxDrawIndexedIndexValue) \
	MAXIMUM(maxDrawIndirectCount) \
	MAXIMUM(maxFramebufferWidth) \
	MAXIMUM(maxFramebufferHeight) \
	MAXIMUM(maxFramebufferLayers) \
	MAXIMUM(maxColorAttachments) \
	MAXIMUM(maxViewports) \
	MAXIMUM(maxClipDistances) \
	MAXIMUM(maxCullDistances) \
	ALIGNMENT(minTexelBufferOffsetAlignment) \
	ALIGNMENT(minUniformBufferOffsetAlignment) \
	ALIGNMENT(minStorageBufferOffsetAlignment) \
	ALIGNMENT(optimalBufferCopyOffsetAlignment) \
	ALIGNMENT(optimalBufferCopyRowPitchAlignment) \
	ALIGNMENT(nonCoherentAtomSize)

enum FellowVulkanLimit {
#define FELLOW_VK_LIMIT_ENUM(m_name) FELLOW_VKL_##m_name,
	FELLOW_VK_LIMITS(FELLOW_VK_LIMIT_ENUM, FELLOW_VK_LIMIT_ENUM)
#undef FELLOW_VK_LIMIT_ENUM
			FELLOW_VKL_MAX,
};

struct FellowVulkanProfile {
	const char *name = nullptr;
	const char *source = nullptr; // The profile or device capture the data came from.
	uint32_t api_version = 0; // 0: the device's.
	const char *const *extensions = nullptr; // nullptr-terminated; nullptr: every extension of the device.
	const char *const *denied_extensions = nullptr; // nullptr-terminated; hidden even when the device has them.
	uint64_t features = ~uint64_t(0); // FELLOW_VKF_BIT()s the simulated device has.
	uint32_t limits[FELLOW_VKL_MAX] = {}; // 0: the device's.
	uint32_t compute_workgroup_count[3] = {}; // 0: the device's.
	uint32_t compute_workgroup_size[3] = {};
	uint32_t subgroup_stages = ~0u; // VkShaderStageFlags, ANDed with the device's.
	uint32_t subgroup_operations = ~0u; // VkSubgroupFeatureFlags, ANDed with the device's.
};

// The profile FELLOW_VULKAN_PROFILE names (read once), or nullptr. An unknown name prints the known ones.
const FellowVulkanProfile *fellow_vulkan_profile_get();
// Whether the profile lets an extension the device has through.
bool fellow_vulkan_profile_allows_extension(const FellowVulkanProfile *p_profile, const char *p_extension);
// The names of the profiles, comma-separated (for messages).
const char *fellow_vulkan_profile_names();

// What a SPIR-V module asks of the device: its capabilities, a compute entry point's workgroup size and the bytes of
// its Workgroup variables (specialization constants at their defaults; sizes as the validation layers count them).
struct FellowSpirvSummary {
	LocalVector<uint32_t> capabilities;
	uint32_t local_size[3] = { 0, 0, 0 };
	uint64_t workgroup_bytes = 0;
	bool valid = false;
};
bool fellow_spirv_summarize(const uint32_t *p_words, size_t p_word_count, FellowSpirvSummary &r_summary);
// SPIR-V capability names for messages ("Capability <n>" for the unnamed).
const char *fellow_spirv_capability_name(uint32_t p_capability);
