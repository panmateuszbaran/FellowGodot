#!/usr/bin/env python3
"""Generates drivers/vulkan/fellow_vulkan_profile_data.h (milestone 49, decision 0150): the simulated Vulkan devices of
FELLOW_VULKAN_PROFILE, from the JSON profiles of the Khronos Vulkan-Profiles repository
(https://github.com/KhronosGroup/Vulkan-Profiles, Apache-2.0): Android's profiles and device captures from
vulkan.gpuinfo.org that LunarG's desktop baselines were built from. Reads the JSON files only.

  make_vulkan_profiles.py <Vulkan-Profiles checkout> [output header]
"""

import json
import os
import sys

# name, file under profiles/, what it stands for.
PROFILES = [
    ("android_2022", "Android/VP_ANDROID_vulkan_profile_2022.json", "Android's Vulkan profile 2022 (the baseline of 2022's Android devices)"),
    ("android_2025", "Android/VP_ANDROID_vulkan_profile_2025.json", "Android's Vulkan profile 2025"),
    ("intel_uhd620", "LunarG/VP_LUNARG_desktop_baseline_2024/vp_gpuinfo_intel_r__uhd_graphics_620_0_404_2125_windows_10.json", "Intel UHD Graphics 620 (Windows 10 driver 0.404.2125)"),
    ("intel_arc_a770", "LunarG/VP_LUNARG_desktop_baseline_2023/vp_gpuinfo_intel_r__arc_tm__a770_graphics_0_405_1234_windows_11.json", "Intel Arc A770 (Windows 11 driver 0.405.1234)"),
    ("gtx1060", "LunarG/VP_LUNARG_desktop_baseline_2024/vp_gpuinfo_nvidia_geforce_gtx_1060_537_13_0_0_windows_10.json", "NVIDIA GeForce GTX 1060 (Windows 10 driver 537.13)"),
    ("rtx4070", "LunarG/VP_LUNARG_desktop_baseline_2024/vp_gpuinfo_nvidia_geforce_rtx_4070_551_61_0_0_windows_11.json", "NVIDIA GeForce RTX 4070 (Windows 11 driver 551.61)"),
    ("rx580", "LunarG/VP_LUNARG_desktop_baseline_2024/vp_gpuinfo_radeon_rx_580_series_2_0_258_windows_10.json", "AMD Radeon RX 580 (Windows 10 driver 2.0.258)"),
    ("rx7800xt", "LunarG/VP_LUNARG_desktop_baseline_2024/vp_gpuinfo_amd_radeon_rx_7800_xt_2_0_294_windows_11.json", "AMD Radeon RX 7800 XT (Windows 11 driver 2.0.294)"),
    ("apple_m1", "LunarG/VP_LUNARG_desktop_baseline_2026/vp_gpuinfo_apple_m1_0_2_2209_macos_26_5_1.json", "Apple M1 through MoltenVK 0.2.2209 (macOS 26.5.1)"),
]

# The device extensions Godot and FellowEngine ask for (RenderingDeviceDriverVulkan::_initialize_device_extensions()).
REQUESTED_EXTENSIONS = [
    "VK_KHR_swapchain", "VK_KHR_multiview", "VK_KHR_fragment_shading_rate", "VK_EXT_fragment_density_map",
    "VK_EXT_fragment_density_map2", "VK_QCOM_fragment_density_map_offset", "VK_KHR_create_renderpass2",
    "VK_KHR_shader_float16_int8", "VK_KHR_storage_buffer_storage_class", "VK_KHR_16bit_storage",
    "VK_KHR_image_format_list", "VK_KHR_maintenance2", "VK_EXT_pipeline_creation_cache_control",
    "VK_EXT_subgroup_size_control", "VK_EXT_astc_decode_mode", "VK_KHR_buffer_device_address",
    "VK_KHR_vulkan_memory_model", "VK_EXT_texture_compression_astc_hdr", "VK_KHR_depth_stencil_resolve",
    "VK_KHR_acceleration_structure", "VK_KHR_deferred_host_operations", "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_ray_query", "VK_KHR_ray_tracing_position_fetch", "VK_NV_ray_tracing_validation",
    "VK_KHR_synchronization2", "VK_KHR_cooperative_matrix", "VK_KHR_external_memory_fd", "VK_KHR_driver_properties",
]

# Extensions promoted to core: a device of that version has their functionality (and drivers keep advertising them).
PROMOTED = {
    (1, 1): ["VK_KHR_multiview", "VK_KHR_maintenance2", "VK_KHR_16bit_storage", "VK_KHR_storage_buffer_storage_class"],
    (1, 2): ["VK_KHR_create_renderpass2", "VK_KHR_shader_float16_int8", "VK_KHR_image_format_list",
             "VK_KHR_buffer_device_address", "VK_KHR_vulkan_memory_model", "VK_KHR_depth_stencil_resolve",
             "VK_KHR_driver_properties"],
    (1, 3): ["VK_EXT_pipeline_creation_cache_control", "VK_EXT_subgroup_size_control", "VK_KHR_synchronization2",
             "VK_EXT_texture_compression_astc_hdr"],
}

BASE_FEATURES = """fullDrawIndexUint32 imageCubeArray independentBlend geometryShader tessellationShader sampleRateShading
dualSrcBlend logicOp multiDrawIndirect drawIndirectFirstInstance depthClamp depthBiasClamp fillModeNonSolid depthBounds
wideLines largePoints alphaToOne multiViewport samplerAnisotropy textureCompressionETC2 textureCompressionASTC_LDR
textureCompressionBC vertexPipelineStoresAndAtomics fragmentStoresAndAtomics shaderTessellationAndGeometryPointSize
shaderImageGatherExtended shaderStorageImageExtendedFormats shaderStorageImageReadWithoutFormat
shaderStorageImageWriteWithoutFormat shaderUniformBufferArrayDynamicIndexing shaderSampledImageArrayDynamicIndexing
shaderStorageBufferArrayDynamicIndexing shaderStorageImageArrayDynamicIndexing shaderClipDistance shaderCullDistance
shaderFloat64 shaderInt64 shaderInt16 shaderResourceMinLod variableMultisampleRate""".split()
EXT_FEATURES = """shaderFloat16 shaderInt8 storageBuffer16BitAccess uniformAndStorageBuffer16BitAccess storagePushConstant16
storageInputOutput16 multiview bufferDeviceAddress vulkanMemoryModel vulkanMemoryModelDeviceScope
pipelineCreationCacheControl synchronization2 accelerationStructure rayQuery rayTracingPipeline rayTracingPositionFetch
cooperativeMatrix pipelineFragmentShadingRate primitiveFragmentShadingRate attachmentFragmentShadingRate
fragmentDensityMap shaderSubgroupExtendedTypes""".split()

MAXIMA = """maxImageDimension1D maxImageDimension2D maxImageDimension3D maxImageDimensionCube maxImageArrayLayers
maxTexelBufferElements maxUniformBufferRange maxStorageBufferRange maxPushConstantsSize maxMemoryAllocationCount
maxSamplerAllocationCount maxBoundDescriptorSets maxPerStageDescriptorSamplers maxPerStageDescriptorUniformBuffers
maxPerStageDescriptorStorageBuffers maxPerStageDescriptorSampledImages maxPerStageDescriptorStorageImages
maxPerStageDescriptorInputAttachments maxPerStageResources maxDescriptorSetSamplers maxDescriptorSetUniformBuffers
maxDescriptorSetUniformBuffersDynamic maxDescriptorSetStorageBuffers maxDescriptorSetStorageBuffersDynamic
maxDescriptorSetSampledImages maxDescriptorSetStorageImages maxDescriptorSetInputAttachments maxVertexInputAttributes
maxVertexInputBindings maxVertexInputAttributeOffset maxVertexInputBindingStride maxVertexOutputComponents
maxFragmentInputComponents maxFragmentOutputAttachments maxFragmentCombinedOutputResources maxComputeSharedMemorySize
maxComputeWorkGroupInvocations maxDrawIndexedIndexValue maxDrawIndirectCount maxFramebufferWidth maxFramebufferHeight
maxFramebufferLayers maxColorAttachments maxViewports maxClipDistances maxCullDistances""".split()
ALIGNMENTS = """minTexelBufferOffsetAlignment minUniformBufferOffsetAlignment minStorageBufferOffsetAlignment
optimalBufferCopyOffsetAlignment optimalBufferCopyRowPitchAlignment nonCoherentAtomSize""".split()

# The Vulkan specification's required limits ("Required Limits"), for what a profile leaves unsaid.
SPEC_MINIMUM = {
    "maxImageDimension1D": 4096, "maxImageDimension2D": 4096, "maxImageDimension3D": 256, "maxImageDimensionCube": 4096,
    "maxImageArrayLayers": 256, "maxTexelBufferElements": 65536, "maxUniformBufferRange": 16384,
    "maxStorageBufferRange": 1 << 27, "maxPushConstantsSize": 128, "maxMemoryAllocationCount": 4096,
    "maxSamplerAllocationCount": 4000, "maxBoundDescriptorSets": 4, "maxPerStageDescriptorSamplers": 16,
    "maxPerStageDescriptorUniformBuffers": 12, "maxPerStageDescriptorStorageBuffers": 4,
    "maxPerStageDescriptorSampledImages": 16, "maxPerStageDescriptorStorageImages": 4,
    "maxPerStageDescriptorInputAttachments": 4, "maxPerStageResources": 128, "maxDescriptorSetSamplers": 96,
    "maxDescriptorSetUniformBuffers": 72, "maxDescriptorSetUniformBuffersDynamic": 8,
    "maxDescriptorSetStorageBuffers": 24, "maxDescriptorSetStorageBuffersDynamic": 4,
    "maxDescriptorSetSampledImages": 96, "maxDescriptorSetStorageImages": 24, "maxDescriptorSetInputAttachments": 4,
    "maxVertexInputAttributes": 16, "maxVertexInputBindings": 16, "maxVertexInputAttributeOffset": 2047,
    "maxVertexInputBindingStride": 2048, "maxVertexOutputComponents": 64, "maxFragmentInputComponents": 64,
    "maxFragmentOutputAttachments": 4, "maxFragmentCombinedOutputResources": 4, "maxComputeSharedMemorySize": 16384,
    "maxComputeWorkGroupInvocations": 128, "maxDrawIndexedIndexValue": (1 << 24) - 1, "maxDrawIndirectCount": 1,
    "maxFramebufferWidth": 4096, "maxFramebufferHeight": 4096, "maxFramebufferLayers": 256, "maxColorAttachments": 4,
    "maxViewports": 1, "maxClipDistances": 0, "maxCullDistances": 0,
    "minTexelBufferOffsetAlignment": 256, "minUniformBufferOffsetAlignment": 256,
    "minStorageBufferOffsetAlignment": 256, "optimalBufferCopyOffsetAlignment": 1,
    "optimalBufferCopyRowPitchAlignment": 1, "nonCoherentAtomSize": 256,
    "maxComputeWorkGroupCount": [65535, 65535, 65535], "maxComputeWorkGroupSize": [128, 128, 64],
}

STAGES = {
    "VK_SHADER_STAGE_VERTEX_BIT": 0x1, "VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT": 0x2,
    "VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT": 0x4, "VK_SHADER_STAGE_GEOMETRY_BIT": 0x8,
    "VK_SHADER_STAGE_FRAGMENT_BIT": 0x10, "VK_SHADER_STAGE_COMPUTE_BIT": 0x20, "VK_SHADER_STAGE_ALL_GRAPHICS": 0x1F,
    "VK_SHADER_STAGE_ALL": 0x7FFFFFFF, "VK_SHADER_STAGE_TASK_BIT_EXT": 0x40, "VK_SHADER_STAGE_MESH_BIT_EXT": 0x80,
    "VK_SHADER_STAGE_RAYGEN_BIT_KHR": 0x100, "VK_SHADER_STAGE_ANY_HIT_BIT_KHR": 0x200,
    "VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR": 0x400, "VK_SHADER_STAGE_MISS_BIT_KHR": 0x800,
    "VK_SHADER_STAGE_INTERSECTION_BIT_KHR": 0x1000, "VK_SHADER_STAGE_CALLABLE_BIT_KHR": 0x2000,
}
SUBGROUP_OPS = {
    "VK_SUBGROUP_FEATURE_BASIC_BIT": 0x1, "VK_SUBGROUP_FEATURE_VOTE_BIT": 0x2, "VK_SUBGROUP_FEATURE_ARITHMETIC_BIT": 0x4,
    "VK_SUBGROUP_FEATURE_BALLOT_BIT": 0x8, "VK_SUBGROUP_FEATURE_SHUFFLE_BIT": 0x10,
    "VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT": 0x20, "VK_SUBGROUP_FEATURE_CLUSTERED_BIT": 0x40,
    "VK_SUBGROUP_FEATURE_QUAD_BIT": 0x80, "VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV": 0x100,
    "VK_SUBGROUP_FEATURE_ROTATE_BIT": 0x200, "VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT": 0x400,
    "VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR": 0x200, "VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT_KHR": 0x400,
}


def flags(values, table):
    out = 0
    for v in values or []:
        out |= table.get(v, 0)
    return out


def blocks(doc):
    """The capability blocks the (single) profile of a file uses, merged (the first of each set of alternatives)."""
    (profile,) = doc["profiles"].values()
    names = []
    for c in profile["capabilities"]:
        names.append(c[0] if isinstance(c, list) else c)
    return profile, [doc["capabilities"][n] for n in names]


def convert(path):
    doc = json.load(open(path))
    profile, caps = blocks(doc)
    is_capture = "gpuinfo" in profile.get("description", "")
    version = tuple(int(x) for x in profile["api-version"].split(".")[:2])
    extensions, features, limits, subgroup = set(), set(), {}, {}
    for block in caps:
        ext = block.get("extensions", {})
        extensions |= set(ext.keys() if isinstance(ext, dict) else ext)
        for fields in block.get("features", {}).values():
            features |= {f for f, on in fields.items() if on is True}
        for struct, fields in block.get("properties", {}).items():
            if struct in ("VkPhysicalDeviceProperties", "VkPhysicalDeviceProperties2"):
                limits.update(fields.get("limits", {}))
            if struct in ("VkPhysicalDeviceSubgroupProperties", "VkPhysicalDeviceVulkan11Properties"):
                for k, v in fields.items():
                    k = {"subgroupSupportedStages": "supportedStages", "subgroupSupportedOperations": "supportedOperations"}.get(k, k)
                    subgroup.setdefault(k, v)
    for (major, minor), names in PROMOTED.items():
        if version >= (major, minor):
            extensions |= set(names)
    # A feature of an extension the device lacks (and that its version does not include) is not there.
    if "VK_KHR_ray_query" not in extensions:
        features.discard("rayQuery")
    if "VK_KHR_acceleration_structure" not in extensions:
        features.discard("accelerationStructure")
    if "VK_KHR_ray_tracing_pipeline" not in extensions:
        features.discard("rayTracingPipeline")
    if "VK_KHR_ray_tracing_position_fetch" not in extensions:
        features.discard("rayTracingPositionFetch")
    if "VK_KHR_cooperative_matrix" not in extensions:
        features.discard("cooperativeMatrix")
    if "VK_KHR_fragment_shading_rate" not in extensions:
        features -= {"pipelineFragmentShadingRate", "primitiveFragmentShadingRate", "attachmentFragmentShadingRate"}
    if "VK_EXT_fragment_density_map" not in extensions:
        features.discard("fragmentDensityMap")
    out = {
        "version": version,
        "extensions": sorted(e for e in extensions if e in REQUESTED_EXTENSIONS),
        "features": [f for f in BASE_FEATURES + EXT_FEATURES if f in features],
        "limits": {},
        "count": None,
        "size": None,
    }
    for k in MAXIMA + ALIGNMENTS:
        v = limits.get(k, None if is_capture else SPEC_MINIMUM[k])
        if v is not None:
            out["limits"][k] = min(int(v), 0xFFFFFFFF)
    out["count"] = limits.get("maxComputeWorkGroupCount", None if is_capture else SPEC_MINIMUM["maxComputeWorkGroupCount"])
    out["size"] = limits.get("maxComputeWorkGroupSize", None if is_capture else SPEC_MINIMUM["maxComputeWorkGroupSize"])
    if subgroup:
        out["stages"] = flags(subgroup.get("supportedStages"), STAGES)
        out["ops"] = flags(subgroup.get("supportedOperations"), SUBGROUP_OPS)
    else:  # Vulkan 1.1's minimum: basic operations in compute shaders.
        out["stages"], out["ops"] = 0x20, 0x1
    return out


def main():
    root = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "../../../drivers/vulkan/fellow_vulkan_profile_data.h")
    lines = [
        "// Generated by benchmarks/tools/platforms/make_vulkan_profiles.py from the Khronos Vulkan-Profiles repository",
        "// (https://github.com/KhronosGroup/Vulkan-Profiles, Apache-2.0; device captures from https://vulkan.gpuinfo.org).",
        "// Do not edit. Milestone 49, decision 0150.",
        "",
        "#pragma once",
        "",
        '#include "drivers/vulkan/fellow_vulkan_profile.h"',
        '#include "drivers/vulkan/godot_vulkan.h"',
        "",
        "static void fellow_vulkan_profiles_generated(LocalVector<FellowVulkanProfile> &r_profiles) {",
    ]
    for name, rel, label in PROFILES:
        p = convert(os.path.join(root, "profiles", rel))
        lines.append("\t{")
        lines.append("\t\tstatic const char *const extensions[] = { " + ", ".join('"%s"' % e for e in p["extensions"]) + ", nullptr };")
        lines.append("\t\tFellowVulkanProfile p;")
        lines.append('\t\tp.name = "%s";' % name)
        lines.append('\t\tp.source = "%s (%s)";' % (label, os.path.basename(rel)))
        lines.append("\t\tp.api_version = VK_MAKE_API_VERSION(0, %d, %d, 0);" % p["version"])
        lines.append("\t\tp.extensions = extensions;")
        feats = p["features"]
        lines.append("\t\tp.features = 0;")
        for i in range(0, len(feats), 4):
            lines.append("\t\tp.features |= " + " | ".join("FELLOW_VKF_BIT(%s)" % f for f in feats[i : i + 4]) + ";")
        for k, v in p["limits"].items():
            lines.append("\t\tp.limits[FELLOW_VKL_%s] = %du;" % (k, v))
        for field, key in (("compute_workgroup_count", "count"), ("compute_workgroup_size", "size")):
            if p[key]:
                for i in range(3):
                    lines.append("\t\tp.%s[%d] = %du;" % (field, i, p[key][i]))
        lines.append("\t\tp.subgroup_stages = 0x%Xu;" % p["stages"])
        lines.append("\t\tp.subgroup_operations = 0x%Xu;" % p["ops"])
        lines.append("\t\tr_profiles.push_back(p);")
        lines.append("\t}")
    lines.append("}")
    lines.append("")
    open(dst, "w").write("\n".join(lines))
    print("wrote", dst)


if __name__ == "__main__":
    main()
