/**************************************************************************/
/*  shader_rd.cpp                                                         */
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

#include "shader_rd.h"

#include "core/config/engine.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "core/templates/pair.h"
#include "core/version.h"
#include "servers/rendering/fellow_parallel.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/shader_include_db.h"

#define ENABLE_SHADER_CACHE 1

void ShaderRD::_add_stage(const char *p_code, StageType p_stage_type) {
	Vector<String> lines = String(p_code).split("\n");

	String text;

	int line_count = lines.size();
	for (int i = 0; i < line_count; i++) {
		const String &l = lines[i];
		bool push_chunk = false;

		StageTemplate::Chunk chunk;

		if (l.begins_with("#VERSION_DEFINES")) {
			chunk.type = StageTemplate::Chunk::TYPE_VERSION_DEFINES;
			push_chunk = true;
		} else if (l.begins_with("#GLOBALS")) {
			switch (p_stage_type) {
				case STAGE_TYPE_VERTEX:
					chunk.type = StageTemplate::Chunk::TYPE_VERTEX_GLOBALS;
					break;
				case STAGE_TYPE_FRAGMENT:
					chunk.type = StageTemplate::Chunk::TYPE_FRAGMENT_GLOBALS;
					break;
				case STAGE_TYPE_COMPUTE:
					chunk.type = StageTemplate::Chunk::TYPE_COMPUTE_GLOBALS;
					break;
				case STAGE_TYPE_RAYGEN:
					chunk.type = StageTemplate::Chunk::TYPE_RAYGEN_GLOBALS;
					break;
				case STAGE_TYPE_ANY_HIT:
					chunk.type = StageTemplate::Chunk::TYPE_ANY_HIT_GLOBALS;
					break;
				case STAGE_TYPE_CLOSEST_HIT:
					chunk.type = StageTemplate::Chunk::TYPE_CLOSEST_HIT_GLOBALS;
					break;
				case STAGE_TYPE_MISS:
					chunk.type = StageTemplate::Chunk::TYPE_MISS_GLOBALS;
					break;
				case STAGE_TYPE_INTERSECTION:
					chunk.type = StageTemplate::Chunk::TYPE_INTERSECTION_GLOBALS;
					break;
				default: {
				}
			}

			push_chunk = true;
		} else if (l.begins_with("#MATERIAL_UNIFORMS")) {
			chunk.type = StageTemplate::Chunk::TYPE_MATERIAL_UNIFORMS;
			push_chunk = true;
		} else if (l.begins_with("#CODE")) {
			chunk.type = StageTemplate::Chunk::TYPE_CODE;
			push_chunk = true;
			chunk.code = l.replace_first("#CODE", String()).remove_char(':').strip_edges().to_upper();
		} else if (l.begins_with("#include ")) {
			String include_file = l.replace("#include ", "").strip_edges();
			if (include_file[0] == '"') {
				int end_pos = include_file.find_char('"', 1);
				if (end_pos >= 0) {
					include_file = include_file.substr(1, end_pos - 1);

					String include_code = ShaderIncludeDB::get_built_in_include_file(include_file);
					if (!include_code.is_empty()) {
						// Add these lines into our parse list so we parse them as well.
						Vector<String> include_lines = include_code.split("\n");

						for (int j = include_lines.size() - 1; j >= 0; j--) {
							lines.insert(i + 1, include_lines[j]);
						}

						line_count = lines.size();
					} else {
						// Add it in as is.
						text += l + "\n";
					}
				} else {
					// Add it in as is.
					text += l + "\n";
				}
			} else {
				// Add it in as is.
				text += l + "\n";
			}
		} else {
			text += l + "\n";
		}

		if (push_chunk) {
			if (!text.is_empty()) {
				StageTemplate::Chunk text_chunk;
				text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
				text_chunk.text = text.utf8();
				stage_templates[p_stage_type].chunks.push_back(text_chunk);
				text = String();
			}
			stage_templates[p_stage_type].chunks.push_back(chunk);
		}
	}

	if (!text.is_empty()) {
		StageTemplate::Chunk text_chunk;
		text_chunk.type = StageTemplate::Chunk::TYPE_TEXT;
		text_chunk.text = text.utf8();
		stage_templates[p_stage_type].chunks.push_back(text_chunk);
		text = String();
	}
}

void ShaderRD::setup(const char *p_vertex_code, const char *p_fragment_code, const char *p_compute_code, const char *p_name) {
	name = p_name;
	fellow_source[0] = p_vertex_code;
	fellow_source[1] = p_fragment_code;
	fellow_source[2] = p_compute_code;

	if (p_compute_code) {
		_add_stage(p_compute_code, STAGE_TYPE_COMPUTE);
		pipeline_type = RD::PIPELINE_TYPE_COMPUTE;
	} else {
		pipeline_type = RD::PIPELINE_TYPE_RASTERIZATION;
		if (p_vertex_code) {
			_add_stage(p_vertex_code, STAGE_TYPE_VERTEX);
		}
		if (p_fragment_code) {
			_add_stage(p_fragment_code, STAGE_TYPE_FRAGMENT);
		}
	}

	StringBuilder tohash;
	tohash.append("[GodotVersionNumber]");
	tohash.append(GODOT_VERSION_NUMBER);
	tohash.append("[GodotVersionHash]");
	tohash.append(GODOT_VERSION_HASH);
	tohash.append("[Vertex]");
	tohash.append(p_vertex_code ? p_vertex_code : "");
	tohash.append("[Fragment]");
	tohash.append(p_fragment_code ? p_fragment_code : "");
	tohash.append("[Compute]");
	tohash.append(p_compute_code ? p_compute_code : "");
	tohash.append("[DebugInfo]");
	tohash.append(Engine::get_singleton()->is_generate_spirv_debug_info_enabled() ? "1" : "0");

	base_sha256 = tohash.as_string().sha256_text();
}

String ShaderRD::fellow_source_sha256(const String &p_defines) const {
	StringBuilder tohash;
	tohash.append("[Vertex]");
	tohash.append(fellow_source[0] ? fellow_source[0] : "");
	tohash.append("[Fragment]");
	tohash.append(fellow_source[1] ? fellow_source[1] : "");
	tohash.append("[Compute]");
	tohash.append(fellow_source[2] ? fellow_source[2] : "");
	tohash.append("[Defines]");
	tohash.append(p_defines);
	return tohash.as_string().sha256_text();
}

void ShaderRD::setup_raytracing(const char *p_raygen_code, const char *p_any_hit_code, const char *p_closest_hit_code, const char *p_miss_code, const char *p_intersection_code, const char *p_name) {
	name = p_name;

	pipeline_type = RD::PIPELINE_TYPE_RAYTRACING;
	if (p_raygen_code) {
		_add_stage(p_raygen_code, STAGE_TYPE_RAYGEN);
	}
	if (p_any_hit_code) {
		_add_stage(p_any_hit_code, STAGE_TYPE_ANY_HIT);
	}
	if (p_closest_hit_code) {
		_add_stage(p_closest_hit_code, STAGE_TYPE_CLOSEST_HIT);
	}
	if (p_miss_code) {
		_add_stage(p_miss_code, STAGE_TYPE_MISS);
	}
	if (p_intersection_code) {
		_add_stage(p_intersection_code, STAGE_TYPE_INTERSECTION);
	}

	StringBuilder tohash;
	tohash.append("[GodotVersionNumber]");
	tohash.append(GODOT_VERSION_NUMBER);
	tohash.append("[GodotVersionHash]");
	tohash.append(GODOT_VERSION_HASH);
	tohash.append("[Raygen]");
	tohash.append(p_raygen_code ? p_raygen_code : "");
	tohash.append("[AnyHit]");
	tohash.append(p_any_hit_code ? p_any_hit_code : "");
	tohash.append("[ClosestHit]");
	tohash.append(p_closest_hit_code ? p_closest_hit_code : "");
	tohash.append("[Miss]");
	tohash.append(p_miss_code ? p_miss_code : "");
	tohash.append("[Intersection]");
	tohash.append(p_intersection_code ? p_intersection_code : "");
	tohash.append("[DebugInfo]");
	tohash.append(Engine::get_singleton()->is_generate_spirv_debug_info_enabled() ? "1" : "0");

	base_sha256 = tohash.as_string().sha256_text();
}

RID ShaderRD::version_create(bool p_embedded) {
	//initialize() was never called
	ERR_FAIL_COND_V(group_to_variant_map.is_empty(), RID());

	Version version;
	version.dirty = true;
	version.valid = false;
	version.initialize_needed = true;
	version.embedded = p_embedded;
	version.variants.clear();
	version.variant_data.clear();

	version.mutex = memnew(Mutex);
	RID rid = version_owner.make_rid(version);
	version_owner.get_or_null(rid)->fellow_rid = rid;
	{
		MutexLock lock(versions_mutex);
		version_mutexes.insert(rid, version.mutex);
	}

	if (p_embedded) {
		MutexLock lock(shader_versions_embedded_set_mutex);
		shader_versions_embedded_set.insert({ this, rid });
	}

	return rid;
}

void ShaderRD::_initialize_version(Version *p_version) {
	_clear_version(p_version);

	p_version->valid = false;
	p_version->dirty = false;

	p_version->variants.resize_initialized(variant_defines.size());
	p_version->variant_data.resize(variant_defines.size());
	p_version->fellow_lazy.resize_initialized(variant_defines.size());
	p_version->fellow_lazy_count = 0;
	p_version->group_compilation_tasks.resize_initialized(group_enabled.size());
	p_version->fellow_claims.resize(group_enabled.size());
	for (FellowGroupClaims *&c : p_version->fellow_claims) {
		c = nullptr;
	}
}

void ShaderRD::_clear_version(Version *p_version) {
	_compile_ensure_finished(p_version);

	// Clear versions if they exist.
	if (!p_version->variants.is_empty()) {
		for (int i = 0; i < variant_defines.size(); i++) {
			if (p_version->variants[i].is_valid()) {
				RD::get_singleton()->free_rid(p_version->variants[i]);
			}
		}

		p_version->variants.clear();
		p_version->variant_data.clear();
	}
	p_version->fellow_lazy.clear();
	p_version->fellow_lazy_count = 0;
}

void ShaderRD::_build_variant_code(StringBuilder &builder, uint32_t p_variant, const Version *p_version, const StageTemplate &p_template) {
	for (const StageTemplate::Chunk &chunk : p_template.chunks) {
		switch (chunk.type) {
			case StageTemplate::Chunk::TYPE_VERSION_DEFINES: {
				builder.append("\n"); //make sure defines begin at newline
				builder.append(general_defines.get_data());
				builder.append(variant_defines[p_variant].text.get_data());
				for (int j = 0; j < p_version->custom_defines.size(); j++) {
					builder.append(p_version->custom_defines[j].get_data());
				}
				builder.append("\n"); //make sure defines begin at newline
				if (p_version->uniforms.size()) {
					builder.append("#define MATERIAL_UNIFORMS_USED\n");
				}
				for (const KeyValue<StringName, CharString> &E : p_version->code_sections) {
					builder.append(String("#define ") + String(E.key) + "_CODE_USED\n");
				}
				builder.append(String("#define RENDER_DRIVER_") + OS::get_singleton()->get_current_rendering_driver_name().to_upper() + "\n");
				builder.append("#define samplerExternalOES sampler2D\n");
				builder.append("#define textureExternalOES texture2D\n");
			} break;
			case StageTemplate::Chunk::TYPE_MATERIAL_UNIFORMS: {
				builder.append(p_version->uniforms.get_data()); //uniforms (same for vertex and fragment)
			} break;
			case StageTemplate::Chunk::TYPE_VERTEX_GLOBALS: {
				builder.append(p_version->vertex_globals.get_data()); // vertex globals
			} break;
			case StageTemplate::Chunk::TYPE_FRAGMENT_GLOBALS: {
				builder.append(p_version->fragment_globals.get_data()); // fragment globals
			} break;
			case StageTemplate::Chunk::TYPE_COMPUTE_GLOBALS: {
				builder.append(p_version->compute_globals.get_data()); // compute globals
			} break;
			case StageTemplate::Chunk::TYPE_RAYGEN_GLOBALS: {
				builder.append(p_version->raygen_globals.get_data()); // raygen globals
			} break;
			case StageTemplate::Chunk::TYPE_ANY_HIT_GLOBALS: {
				builder.append(p_version->any_hit_globals.get_data()); // any_hit globals
			} break;
			case StageTemplate::Chunk::TYPE_CLOSEST_HIT_GLOBALS: {
				builder.append(p_version->closest_hit_globals.get_data()); // closest_hit globals
			} break;
			case StageTemplate::Chunk::TYPE_MISS_GLOBALS: {
				builder.append(p_version->miss_globals.get_data()); // miss globals
			} break;
			case StageTemplate::Chunk::TYPE_INTERSECTION_GLOBALS: {
				builder.append(p_version->intersection_globals.get_data()); // intersection globals
			} break;
			case StageTemplate::Chunk::TYPE_CODE: {
				if (p_version->code_sections.has(chunk.code)) {
					builder.append(p_version->code_sections[chunk.code].get_data());
				}
			} break;
			case StageTemplate::Chunk::TYPE_TEXT: {
				builder.append(chunk.text.get_data());
			} break;
		}
	}
}

Vector<String> ShaderRD::_build_variant_stage_sources(uint32_t p_variant, CompileData p_data) {
	if (!variants_enabled[p_variant]) {
		return Vector<String>(); // Variant is disabled, return.
	}

	Vector<String> stage_sources;
	stage_sources.resize(RD::SHADER_STAGE_MAX);

	if (pipeline_type == RD::PIPELINE_TYPE_COMPUTE) {
		// Compute stage.
		StringBuilder builder;
		_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_COMPUTE]);
		stage_sources.write[RD::SHADER_STAGE_COMPUTE] = builder.as_string();
	} else if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
		{
			// Raygen stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_RAYGEN]);
			stage_sources.write[RD::SHADER_STAGE_RAYGEN] = builder.as_string();
		}

		{
			// Any hit stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_ANY_HIT]);
			stage_sources.write[RD::SHADER_STAGE_ANY_HIT] = builder.as_string();
		}

		{
			// Closest hit stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_CLOSEST_HIT]);
			stage_sources.write[RD::SHADER_STAGE_CLOSEST_HIT] = builder.as_string();
		}

		{
			// Miss stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_MISS]);
			stage_sources.write[RD::SHADER_STAGE_MISS] = builder.as_string();
		}

		{
			// Intersection stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_INTERSECTION]);
			stage_sources.write[RD::SHADER_STAGE_INTERSECTION] = builder.as_string();
		}
	} else {
		{
			// Vertex stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_VERTEX]);
			stage_sources.write[RD::SHADER_STAGE_VERTEX] = builder.as_string();
		}

		{
			// Fragment stage.
			StringBuilder builder;
			_build_variant_code(builder, p_variant, p_data.version, stage_templates[STAGE_TYPE_FRAGMENT]);
			stage_sources.write[RD::SHADER_STAGE_FRAGMENT] = builder.as_string();
		}
	}

	return stage_sources;
}

void ShaderRD::_compile_variant(uint32_t p_variant, CompileData p_data) {
	uint32_t variant = group_to_variant_map[p_data.group][p_variant];
	if (!variants_enabled[variant]) {
		return; // Variant is disabled, return.
	}
	Fellow::CostScope fellow_cost(Fellow::stats().cost_shader_variant_usec, Fellow::stats().cost_shader_variants);

	Vector<String> variant_stage_sources = _build_variant_stage_sources(variant, p_data);
	Vector<RD::ShaderStageSPIRVData> variant_stages = compile_stages(variant_stage_sources, dynamic_buffers);
	ERR_FAIL_COND(variant_stages.is_empty());

	Vector<uint8_t> shader_data = RD::get_singleton()->shader_compile_binary_from_spirv(variant_stages, name + ":" + itos(variant));
	ERR_FAIL_COND(shader_data.is_empty());

	{
		p_data.version->variants.write[variant] = RD::get_singleton()->shader_create_from_bytecode_with_samplers(shader_data, p_data.version->variants[variant], immutable_samplers);
		p_data.version->variant_data.write[variant] = shader_data;
	}
}

Vector<String> ShaderRD::version_build_variant_stage_sources(RID p_version, int p_variant) {
	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL_V(version, Vector<String>());

	if (version->dirty) {
		_initialize_version(version);
	}

	CompileData compile_data;
	compile_data.version = version;
	compile_data.group = variant_to_group[p_variant];
	return _build_variant_stage_sources(p_variant, compile_data);
}

RenderingServerTypes::ShaderNativeSourceCode ShaderRD::version_get_native_source_code(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	RenderingServerTypes::ShaderNativeSourceCode source_code;
	ERR_FAIL_NULL_V(version, source_code);

	MutexLock lock(*version->mutex);

	source_code.versions.resize(variant_defines.size());

	for (int i = 0; i < source_code.versions.size(); i++) {
		if (pipeline_type == RD::PIPELINE_TYPE_RASTERIZATION) {
			// Vertex stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_VERTEX]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "vertex";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}

		if (pipeline_type == RD::PIPELINE_TYPE_RASTERIZATION) {
			// Fragment stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_FRAGMENT]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "fragment";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}

		if (pipeline_type == RD::PIPELINE_TYPE_COMPUTE) {
			// Compute stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_COMPUTE]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "compute";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}

		if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
			// Raygen stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_RAYGEN]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "raygen";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
		if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
			// Any hit stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_ANY_HIT]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "any_hit";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
		if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
			// Closest hit stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_CLOSEST_HIT]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "closest_hit";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
		if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
			// Miss stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_MISS]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "miss";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
		if (pipeline_type == RD::PIPELINE_TYPE_RAYTRACING) {
			// Intersection stage.

			StringBuilder builder;
			_build_variant_code(builder, i, version, stage_templates[STAGE_TYPE_INTERSECTION]);

			RenderingServerTypes::ShaderNativeSourceCode::Version::Stage stage;
			stage.name = "intersection";
			stage.code = builder.as_string();

			source_code.versions.write[i].stages.push_back(stage);
		}
	}

	return source_code;
}

String ShaderRD::version_get_cache_file_relative_path(RID p_version, int p_group, const String &p_api_name) {
	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL_V(version, String());

	return _get_cache_file_relative_path(version, p_group, p_api_name);
}

String ShaderRD::_version_get_sha1(Version *p_version) const {
	StringBuilder hash_build;

	hash_build.append("[uniforms]");
	hash_build.append(p_version->uniforms.get_data());
	hash_build.append("[vertex_globals]");
	hash_build.append(p_version->vertex_globals.get_data());
	hash_build.append("[fragment_globals]");
	hash_build.append(p_version->fragment_globals.get_data());
	hash_build.append("[compute_globals]");
	hash_build.append(p_version->compute_globals.get_data());
	hash_build.append("[raygen_globals]");
	hash_build.append(p_version->raygen_globals.get_data());
	hash_build.append("[any_hit_globals]");
	hash_build.append(p_version->any_hit_globals.get_data());
	hash_build.append("[closest_hit_globals]");
	hash_build.append(p_version->closest_hit_globals.get_data());
	hash_build.append("[miss_globals]");
	hash_build.append(p_version->miss_globals.get_data());
	hash_build.append("[intersection_globals]");
	hash_build.append(p_version->intersection_globals.get_data());

	Vector<StringName> code_sections;
	for (const KeyValue<StringName, CharString> &E : p_version->code_sections) {
		code_sections.push_back(E.key);
	}
	code_sections.sort_custom<StringName::AlphCompare>();

	for (int i = 0; i < code_sections.size(); i++) {
		hash_build.append(String("[code:") + String(code_sections[i]) + "]");
		hash_build.append(p_version->code_sections[code_sections[i]].get_data());
	}
	for (int i = 0; i < p_version->custom_defines.size(); i++) {
		hash_build.append("[custom_defines:" + itos(i) + "]");
		hash_build.append(p_version->custom_defines[i].get_data());
	}

	return hash_build.as_string().sha1_text();
}

static const char *shader_file_header = "GDSC";
static const uint32_t cache_file_version = 4;

String ShaderRD::_get_cache_file_relative_path(Version *p_version, int p_group, const String &p_api_name) {
	String sha1 = _version_get_sha1(p_version);
	return name.path_join(group_sha256[p_group]).path_join(sha1) + "." + p_api_name + ".cache";
}

String ShaderRD::_get_cache_file_path(Version *p_version, int p_group, const String &p_api_name, bool p_user_dir) {
	const String &shader_cache_dir = p_user_dir ? shader_cache_user_dir : shader_cache_res_dir;
	String relative_path = _get_cache_file_relative_path(p_version, p_group, p_api_name);
	return shader_cache_dir.path_join(relative_path);
}

static Mutex fellow_cache_create_mutex;
static FellowParallelFor *fellow_cache_create_parallel = nullptr;

void ShaderRD::_fellow_create_cached_variant(void *p_data, uint32_t p_index) {
	FellowCacheCreate *data = static_cast<FellowCacheCreate *>(p_data);
	const int variant_id = data->variant_ids[p_index];
	// The placeholder is the variant's RID: on success the same RID comes back.
	data->created[p_index] = RD::get_singleton()->shader_create_from_bytecode_with_samplers(data->version->variant_data[variant_id], data->placeholders[variant_id], data->shader->immutable_samplers);
}

void ShaderRD::_fellow_create_lazy(Version *p_version, int p_variant, bool p_background) {
	// Called by version_get_shader() (or the background filling) with the version's mutex held.
	p_version->fellow_lazy[p_variant] = 0;
	p_version->fellow_lazy_count--;
	if (p_background) {
		Fellow::stats().cost_shader_background_creates.increment();
	} else {
		Fellow::stats().cost_shader_lazy_creates.increment();
	}
	// FELLOW_LOG_COMPILES=1 (milestone 68): which variant was made when first used (a hitch's cause in a soak).
	static const bool fellow_log = OS::get_singleton()->has_environment("FELLOW_LOG_COMPILES");
	if (fellow_log && !p_background) {
		print_line(vformat("[fellow] lazy shader variant: %s, variant %d, frame %d", name, p_variant, int64_t(Engine::get_singleton()->get_frames_drawn())));
	}
	// A placeholder (the variant's RID while its group was disabled) is filled and keeps its RID.
	RID shader = RD::get_singleton()->shader_create_from_bytecode_with_samplers(p_version->variant_data[p_variant], p_version->variants[p_variant], immutable_samplers);
	ERR_FAIL_COND_MSG(shader.is_null(), vformat("Failed to create variant %d of shader %s from the shader cache.", p_variant, name));
	p_version->variants.write[p_variant] = shader;
}

// FellowEngine (milestone 68, decision 0176): the lazy variants filled in the background, one task at a time.
static Mutex fellow_fill_mutex;
static LocalVector<Pair<ShaderRD *, RID>> fellow_fill_queue;
static bool fellow_fill_running = false;
static bool fellow_fill_stop = false;
static ShaderRD *fellow_fill_current = nullptr;
static WorkerThreadPool::TaskID fellow_fill_task_id = WorkerThreadPool::INVALID_TASK_ID;

void ShaderRD::_fellow_fill_enqueue(Version *p_version) {
	if (!Fellow::frame_lazy_shader_variants_fill() || p_version->fellow_rid.is_null() || p_version->fellow_lazy_count == 0) {
		return;
	}
	MutexLock lock(fellow_fill_mutex);
	if (fellow_fill_stop) {
		return;
	}
	fellow_fill_queue.push_back(Pair<ShaderRD *, RID>(this, p_version->fellow_rid));
	if (!fellow_fill_running) {
		fellow_fill_running = true;
		if (fellow_fill_task_id != WorkerThreadPool::INVALID_TASK_ID) {
			// The previous task has left its loop (it cleared fellow_fill_running under this lock): only its return is left.
			WorkerThreadPool::get_singleton()->wait_for_task_completion(fellow_fill_task_id);
		}
		fellow_fill_task_id = WorkerThreadPool::get_singleton()->add_native_task(&ShaderRD::_fellow_fill_task, nullptr, false, "FellowEngine shader variants");
	}
}

// One lazy variant of the version created; true if it has more (or was busy: later). The shader is created without the
// version's mutex held: the render thread takes it whenever it asks for one of the version's shaders, and would wait as
// long as the creation takes (milestone 68: frames of 17-40 ms after a load). A variant asked for meanwhile is made by
// the asking thread, as without the filling, and the copy made here freed.
bool ShaderRD::_fellow_fill_version(RID p_version) {
	Mutex *m = nullptr;
	{
		MutexLock lock(versions_mutex);
		Mutex **mp = version_mutexes.getptr(p_version);
		if (mp == nullptr) {
			return false; // freed
		}
		m = *mp;
		// Never waits for a version in use (its user may wait for something this thread holds): later.
		if (!m->try_lock()) {
			return true;
		}
	}
	// Locked, it cannot be freed (version_free() locks it first).
	Version *version = version_owner.get_or_null(p_version);
	bool more = false;
	int variant = -1;
	Vector<uint8_t> bytes;
	if (version != nullptr && version->valid && !version->dirty && version->fellow_lazy_count > 0) {
		for (uint32_t v = 0; v < version->fellow_lazy.size(); v++) {
			if (version->fellow_lazy[v] != 1) {
				continue;
			}
			if (version->variants[v].is_valid()) {
				// A placeholder (its group enabled after the version was made) keeps its RID: filled here.
				_fellow_create_lazy(version, int(v), true);
			} else {
				version->fellow_lazy[v] = 2;
				variant = int(v);
				bytes = version->variant_data[v]; // shared, not copied: kept alive if the version drops it
			}
			break;
		}
		more = version->fellow_lazy_count > 0;
	}
	m->unlock();
	if (variant < 0) {
		return more;
	}

	RID shader = RD::get_singleton()->shader_create_from_bytecode_with_samplers(bytes, RID(), immutable_samplers);

	// The version again; freed meanwhile, or busy for a moment (try_lock(): never blocking on it while versions_mutex is
	// held).
	while (true) {
		{
			MutexLock lock(versions_mutex);
			Mutex **mp = version_mutexes.getptr(p_version);
			if (mp == nullptr) {
				m = nullptr;
				break;
			}
			m = *mp;
			if (m->try_lock()) {
				break;
			}
		}
		OS::get_singleton()->delay_usec(50);
	}
	bool used = false;
	more = false;
	if (m != nullptr) {
		version = version_owner.get_or_null(p_version);
		// Still this claim (not made by an asking thread, not cleared by new code for the version).
		if (version != nullptr && uint32_t(variant) < version->fellow_lazy.size() && version->fellow_lazy[variant] == 2) {
			if (shader.is_valid()) {
				version->fellow_lazy[variant] = 0;
				version->fellow_lazy_count--;
				version->variants.write[variant] = shader;
				used = true;
				Fellow::stats().cost_shader_background_creates.increment();
			} else {
				version->fellow_lazy[variant] = 1; // left to the first use, which reports the failure
			}
		}
		more = version != nullptr && version->fellow_lazy_count > 0;
		m->unlock();
	}
	if (!used && shader.is_valid()) {
		// RenderingDevice frees on the render thread only.
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&ShaderRD::_fellow_free_shader).bind(shader));
	}
	return more;
}

void ShaderRD::_fellow_free_shader(RID p_shader) {
	RD::get_singleton()->free_rid(p_shader);
}

void ShaderRD::_fellow_fill_task(void *p_userdata) {
	while (true) {
		ShaderRD *shader = nullptr;
		RID rid;
		{
			MutexLock lock(fellow_fill_mutex);
			if (fellow_fill_stop || fellow_fill_queue.is_empty()) {
				fellow_fill_running = false;
				return;
			}
			shader = fellow_fill_queue[0].first;
			rid = fellow_fill_queue[0].second;
			fellow_fill_queue.remove_at(0);
			fellow_fill_current = shader;
		}
		const bool more = shader->_fellow_fill_version(rid);
		{
			MutexLock lock(fellow_fill_mutex);
			fellow_fill_current = nullptr;
			if (more && !fellow_fill_stop) {
				fellow_fill_queue.push_back(Pair<ShaderRD *, RID>(shader, rid)); // the others' turn first
			}
		}
	}
}

void ShaderRD::_fellow_fill_forget(ShaderRD *p_shader) {
	fellow_fill_mutex.lock();
	for (uint32_t i = 0; i < fellow_fill_queue.size();) {
		if (fellow_fill_queue[i].first == p_shader) {
			fellow_fill_queue.remove_at(i);
		} else {
			i++;
		}
	}
	while (fellow_fill_current == p_shader) {
		fellow_fill_mutex.unlock();
		OS::get_singleton()->delay_usec(100);
		fellow_fill_mutex.lock();
	}
	fellow_fill_mutex.unlock();
}

void ShaderRD::fellow_finalize() {
	{
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		{
			MutexLock lock(fellow_fill_mutex);
			fellow_fill_stop = true;
			fellow_fill_queue.clear();
			task = fellow_fill_task_id;
			fellow_fill_task_id = WorkerThreadPool::INVALID_TASK_ID;
		}
		if (task != WorkerThreadPool::INVALID_TASK_ID) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
		}
	}
	MutexLock lock(fellow_cache_create_mutex);
	if (fellow_cache_create_parallel != nullptr) {
		memdelete(fellow_cache_create_parallel); // waits for its helpers
		fellow_cache_create_parallel = nullptr;
	}
}

bool ShaderRD::_load_from_cache(Version *p_version, int p_group) {
	String api_safe_name = String(RD::get_singleton()->get_device_api_name()).validate_filename().to_lower();
	Ref<FileAccess> f;
	if (shader_cache_user_dir_valid) {
		f = FileAccess::open(_get_cache_file_path(p_version, p_group, api_safe_name, true), FileAccess::READ);
	}

	if (f.is_null() && shader_cache_res_dir_valid) {
		f = FileAccess::open(_get_cache_file_path(p_version, p_group, api_safe_name, false), FileAccess::READ);
	}

	if (f.is_null()) {
		const String &sha1 = _version_get_sha1(p_version);
		print_verbose(vformat("Shader cache miss for %s", name.path_join(group_sha256[p_group]).path_join(sha1)));
		return false;
	}

	char header[5] = { 0, 0, 0, 0, 0 };
	f->get_buffer((uint8_t *)header, 4);
	ERR_FAIL_COND_V(header != String(shader_file_header), false);

	uint32_t file_version = f->get_32();
	if (file_version != cache_file_version) {
		return false; // wrong version
	}

	uint32_t variant_count = f->get_32();

	ERR_FAIL_COND_V(variant_count != (uint32_t)group_to_variant_map[p_group].size(), false); //should not happen but check

	for (uint32_t i = 0; i < variant_count; i++) {
		int variant_id = group_to_variant_map[p_group][i];
		uint32_t variant_size = f->get_32();
		if (!variants_enabled[variant_id]) {
			f->seek(f->get_position() + variant_size);
			continue;
		}
		if (variant_size == 0) {
			// A new variant has been requested, failing the entire load will generate it
			print_verbose(vformat("Shader cache miss for %s due to missing variant %d", name.path_join(group_sha256[p_group]).path_join(_version_get_sha1(p_version)), variant_id));
			return false;
		}
		Vector<uint8_t> variant_bytes;
		variant_bytes.resize(variant_size);

		uint32_t br = f->get_buffer(variant_bytes.ptrw(), variant_size);

		ERR_FAIL_COND_V(br != variant_size, false);

		p_version->variant_data.write[variant_id] = variant_bytes;
	}

	if (Fellow::frame_lazy_shader_variants() && p_version->fellow_lazy.size() == (uint32_t)variant_defines.size()) {
		// FellowEngine (decision 0061): each variant's shader is created when version_get_shader() first asks for it (from
		// the bytes just read, kept in variant_data as stock keeps them).
		for (uint32_t i = 0; i < variant_count; i++) {
			int variant_id = group_to_variant_map[p_group][i];
			if (!variants_enabled[variant_id]) {
				p_version->variants.write[variant_id] = RID();
				continue;
			}
			if (!p_version->fellow_lazy[variant_id]) {
				p_version->fellow_lazy[variant_id] = 1;
				p_version->fellow_lazy_count++;
			}
		}
		p_version->valid = true;
		Fellow::stats().cost_shader_cache_loads.increment();
		_fellow_fill_enqueue(p_version); // milestone 68: made in the background before they are asked for
		return true;
	}

	if (Fellow::frame_parallel_shader_create()) {
		// FellowEngine (decision 0061): every variant at once on the worker threads, each into its own slot.
		LocalVector<int> todo;
		for (uint32_t i = 0; i < variant_count; i++) {
			int variant_id = group_to_variant_map[p_group][i];
			if (!variants_enabled[variant_id]) {
				p_version->variants.write[variant_id] = RID();
				continue;
			}
			todo.push_back(variant_id);
		}
		LocalVector<RID> created;
		created.resize(todo.size());
		FellowCacheCreate data;
		data.shader = this;
		data.version = p_version;
		data.variant_ids = todo.ptr();
		data.placeholders = p_version->variants.ptr();
		data.created = created.ptr();
		// The calling thread creates variants itself and worker threads that start in time take the others: it never
		// waits for a worker to become free (they may be busy compiling pipelines), only for variants being created. One
		// load at a time uses the helpers; a concurrent one (another thread's) creates its variants alone.
		if (todo.size() > 1 && fellow_cache_create_mutex.try_lock()) {
			if (fellow_cache_create_parallel == nullptr) {
				fellow_cache_create_parallel = memnew(FellowParallelFor);
			}
			fellow_cache_create_parallel->run(todo.size(), &ShaderRD::_fellow_create_cached_variant, &data, todo.size() - 1);
			fellow_cache_create_mutex.unlock();
		} else {
			for (uint32_t i = 0; i < todo.size(); i++) {
				_fellow_create_cached_variant(&data, i);
			}
		}
		bool failed = false;
		for (const RID &rid : created) {
			failed = failed || rid.is_null();
		}
		if (failed) {
			// As stock's serial path: the variants created are freed and the version is compiled from its source.
			for (const RID &rid : created) {
				if (rid.is_valid()) {
					RD::get_singleton()->free_rid(rid);
				}
			}
			ERR_FAIL_V_MSG(false, vformat("Failed to create shader %s from the shader cache.", name));
		}
		for (uint32_t i = 0; i < todo.size(); i++) {
			p_version->variants.write[todo[i]] = created[i];
		}
		p_version->valid = true;
		Fellow::stats().cost_shader_cache_loads.increment();
		return true;
	}

	for (uint32_t i = 0; i < variant_count; i++) {
		int variant_id = group_to_variant_map[p_group][i];
		if (!variants_enabled[variant_id]) {
			p_version->variants.write[variant_id] = RID();
			continue;
		}
		print_verbose(vformat("Loading cache for shader %s, variant %d", name, i));
		{
			RID shader = RD::get_singleton()->shader_create_from_bytecode_with_samplers(p_version->variant_data[variant_id], p_version->variants[variant_id], immutable_samplers);
			if (shader.is_null()) {
				for (uint32_t j = 0; j < i; j++) {
					int variant_free_id = group_to_variant_map[p_group][j];
					RD::get_singleton()->free_rid(p_version->variants[variant_free_id]);
				}
				ERR_FAIL_COND_V(shader.is_null(), false);
			}

			p_version->variants.write[variant_id] = shader;
		}
	}

	p_version->valid = true;
	Fellow::stats().cost_shader_cache_loads.increment();
	return true;
}

void ShaderRD::_save_to_cache(Version *p_version, int p_group) {
	ERR_FAIL_COND(!shader_cache_user_dir_valid);
	String api_safe_name = String(RD::get_singleton()->get_device_api_name()).validate_filename().to_lower();
	const String &path = _get_cache_file_path(p_version, p_group, api_safe_name, true);
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND(f.is_null());

	PackedByteArray shader_cache_bytes = ShaderRD::save_shader_cache_bytes(group_to_variant_map[p_group], p_version->variant_data);
	f->store_buffer(shader_cache_bytes);
}

void ShaderRD::_allocate_placeholders(Version *p_version, int p_group) {
	ERR_FAIL_COND(p_version->variants.is_empty());

	for (uint32_t i = 0; i < group_to_variant_map[p_group].size(); i++) {
		int variant_id = group_to_variant_map[p_group][i];
		RID shader = RD::get_singleton()->shader_create_placeholder();
		{
			p_version->variants.write[variant_id] = shader;
		}
	}
}

// Try to compile all variants for a given group.
// Will skip variants that are disabled.
void ShaderRD::_compile_version_start(Version *p_version, int p_group) {
	if (!group_enabled[p_group]) {
		return;
	}
	Fellow::CostScope fellow_cost(Fellow::stats().cost_shader_start_usec, Fellow::stats().cost_shader_starts);

	p_version->dirty = false;

#if ENABLE_SHADER_CACHE
	if (shader_cache_user_dir_valid || shader_cache_res_dir_valid) {
		if (_load_from_cache(p_version, p_group)) {
			return;
		}
	}
#endif

	CompileData compile_data;
	compile_data.version = p_version;
	compile_data.group = p_group;

	_fellow_reap(false);
	FellowGroupClaims *claims = memnew(FellowGroupClaims);
	claims->data = compile_data;
	claims->count = group_to_variant_map[p_group].size();
	claims->claimed = memnew_arr(std::atomic<uint8_t>, MAX(claims->count, 1u));
	for (uint32_t i = 0; i < claims->count; i++) {
		claims->claimed[i].store(0, std::memory_order_relaxed);
	}
	p_version->fellow_claims[p_group] = claims;
	WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_template_group_task(this, &ShaderRD::_fellow_compile_claimed, claims, claims->count, -1, true, SNAME("ShaderCompilation"));
	p_version->group_compilation_tasks.write[p_group] = group_task;
}

void ShaderRD::_fellow_compile_claimed(uint32_t p_index, FellowGroupClaims *p_claims) {
	if (p_claims->claimed[p_index].exchange(1, std::memory_order_acq_rel) != 0) {
		return; // compiled by the thread that waited for the group
	}
	_compile_variant(p_index, p_claims->data);
	p_claims->done.increment();
}

void ShaderRD::_fellow_reap(bool p_wait) {
	// The groups whose waiter compiled their last variants: their tasks not started then return at once, whenever a
	// worker runs them; the group is waited for (which frees it) and its claims freed once they all have.
	MutexLock lock(fellow_reap_mutex);
	for (uint32_t i = 0; i < fellow_reap.size();) {
		const FellowReap r = fellow_reap[i];
		if (p_wait || WorkerThreadPool::get_singleton()->is_group_task_completed(r.group)) {
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(r.group);
			memdelete_arr(r.claims->claimed);
			memdelete(r.claims);
			fellow_reap.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

void ShaderRD::_compile_version_end(Version *p_version, int p_group) {
	if (p_version->group_compilation_tasks.size() <= p_group || p_version->group_compilation_tasks[p_group] == 0) {
		return;
	}
	WorkerThreadPool::GroupID group_task = p_version->group_compilation_tasks[p_group];
	FellowGroupClaims *claims = uint32_t(p_group) < p_version->fellow_claims.size() ? p_version->fellow_claims[p_group] : nullptr;
	if (claims) {
		// FellowEngine (decision 0165): the variants no task has started are compiled here; then the ones started finish
		// on their threads, which wait for nothing.
		Fellow::CostScope fellow_cost(Fellow::stats().cost_shader_wait_usec, Fellow::stats().cost_shader_waits);
		for (uint32_t i = 0; i < claims->count; i++) {
			if (claims->claimed[i].exchange(1, std::memory_order_acq_rel) == 0) {
				_compile_variant(i, claims->data);
				claims->done.increment();
			}
		}
		while (claims->done.get() < claims->count) {
			OS::get_singleton()->delay_usec(50);
		}
		p_version->fellow_claims[p_group] = nullptr;
		{
			MutexLock lock(fellow_reap_mutex);
			fellow_reap.push_back({ group_task, claims });
		}
		_fellow_reap(false);
	} else {
		Fellow::CostScope fellow_cost(Fellow::stats().cost_shader_wait_usec, Fellow::stats().cost_shader_waits);
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
	}
	p_version->group_compilation_tasks.write[p_group] = 0;

	bool all_valid = true;

	for (uint32_t i = 0; i < group_to_variant_map[p_group].size(); i++) {
		int variant_id = group_to_variant_map[p_group][i];
		if (!variants_enabled[variant_id]) {
			continue; // Disabled.
		}
		if (p_version->variants[variant_id].is_null()) {
			all_valid = false;
			break;
		}
	}

	if (!all_valid) {
		// Clear versions if they exist.
		for (int i = 0; i < variant_defines.size(); i++) {
			if (!variants_enabled[i] || !group_enabled[variant_defines[i].group]) {
				continue; // Disabled.
			}
			if (!p_version->variants[i].is_null()) {
				RD::get_singleton()->free_rid(p_version->variants[i]);
			}
		}

		p_version->variants.clear();
		p_version->variant_data.clear();
		return;
	}
#if ENABLE_SHADER_CACHE
	else if (shader_cache_user_dir_valid) {
		_save_to_cache(p_version, p_group);
	}
#endif

	p_version->valid = true;
}

void ShaderRD::_compile_ensure_finished(Version *p_version) {
	// Wait for compilation of existing groups if necessary.
	for (int i = 0; i < group_enabled.size(); i++) {
		_compile_version_end(p_version, i);
	}
}

void ShaderRD::_version_set(Version *p_version, const HashMap<String, String> &p_code, const Vector<String> &p_custom_defines) {
	p_version->code_sections.clear();
	for (const KeyValue<String, String> &E : p_code) {
		p_version->code_sections[StringName(E.key.to_upper())] = E.value.utf8();
	}

	p_version->custom_defines.clear();
	for (const String &custom_define : p_custom_defines) {
		p_version->custom_defines.push_back(custom_define.utf8());
	}

	p_version->dirty = true;
	if (p_version->initialize_needed) {
		_initialize_version(p_version);
		for (int i = 0; i < group_enabled.size(); i++) {
			if (!group_enabled[i]) {
				_allocate_placeholders(p_version, i);
				continue;
			}
			_compile_version_start(p_version, i);
		}
		p_version->initialize_needed = false;
	}
}

void ShaderRD::version_set_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_vertex_globals, const String &p_fragment_globals, const Vector<String> &p_custom_defines) {
	ERR_FAIL_COND(pipeline_type != RD::PIPELINE_TYPE_RASTERIZATION);

	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL(version);

	MutexLock lock(*version->mutex);

	_compile_ensure_finished(version);

	version->vertex_globals = p_vertex_globals.utf8();
	version->fragment_globals = p_fragment_globals.utf8();
	version->uniforms = p_uniforms.utf8();

	_version_set(version, p_code, p_custom_defines);
}

void ShaderRD::version_set_compute_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_compute_globals, const Vector<String> &p_custom_defines) {
	ERR_FAIL_COND(pipeline_type != RD::PIPELINE_TYPE_COMPUTE);

	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL(version);

	MutexLock lock(*version->mutex);

	_compile_ensure_finished(version);

	version->compute_globals = p_compute_globals.utf8();
	version->uniforms = p_uniforms.utf8();

	_version_set(version, p_code, p_custom_defines);
}

void ShaderRD::version_set_raytracing_code(RID p_version, const HashMap<String, String> &p_code, const String &p_uniforms, const String &p_raygen_globals, const String &p_any_hit_globals, const String &p_closest_hit_globals, const String &p_miss_globals, const String &p_intersection_globals, const Vector<String> &p_custom_defines) {
	ERR_FAIL_COND(pipeline_type != RD::PIPELINE_TYPE_RAYTRACING);

	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL(version);

	version->raygen_globals = p_raygen_globals.utf8();
	version->any_hit_globals = p_any_hit_globals.utf8();
	version->closest_hit_globals = p_closest_hit_globals.utf8();
	version->miss_globals = p_miss_globals.utf8();
	version->intersection_globals = p_intersection_globals.utf8();
	version->uniforms = p_uniforms.utf8();

	_version_set(version, p_code, p_custom_defines);
}

void ShaderRD::fellow_version_compile_start(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL(version);

	MutexLock lock(*version->mutex);

	if (!version->dirty) {
		return;
	}
	_initialize_version(version);
	for (int i = 0; i < group_enabled.size(); i++) {
		if (!group_enabled[i]) {
			_allocate_placeholders(version, i);
			continue;
		}
		_compile_version_start(version, i);
	}
}

bool ShaderRD::version_is_valid(RID p_version) {
	Version *version = version_owner.get_or_null(p_version);
	ERR_FAIL_NULL_V(version, false);

	MutexLock lock(*version->mutex);

	if (version->dirty) {
		_initialize_version(version);
		for (int i = 0; i < group_enabled.size(); i++) {
			if (!group_enabled[i]) {
				_allocate_placeholders(version, i);
				continue;
			}
			_compile_version_start(version, i);
		}
	}

	_compile_ensure_finished(version);

	return version->valid;
}

bool ShaderRD::version_free(RID p_version) {
	if (version_owner.owns(p_version)) {
		{
			MutexLock lock(versions_mutex);
			version_mutexes.erase(p_version);
		}

		Version *version = version_owner.get_or_null(p_version);
		if (version->embedded) {
			MutexLock lock(shader_versions_embedded_set_mutex);
			shader_versions_embedded_set.erase({ this, p_version });
		}

		version->mutex->lock();
		_clear_version(version);
		version_owner.free(p_version);
		version->mutex->unlock();
		memdelete(version->mutex);
	} else {
		return false;
	}

	return true;
}

void ShaderRD::set_variant_enabled(int p_variant, bool p_enabled) {
	ERR_FAIL_COND(version_owner.get_rid_count() > 0); //versions exist
	ERR_FAIL_INDEX(p_variant, variants_enabled.size());
	variants_enabled.write[p_variant] = p_enabled;
}

bool ShaderRD::is_variant_enabled(int p_variant) const {
	ERR_FAIL_INDEX_V(p_variant, variants_enabled.size(), false);
	return variants_enabled[p_variant];
}

int64_t ShaderRD::get_variant_count() const {
	return variants_enabled.size();
}

int ShaderRD::get_variant_to_group(int p_variant) const {
	return variant_to_group[p_variant];
}

void ShaderRD::enable_group(int p_group) {
	ERR_FAIL_INDEX(p_group, group_enabled.size());

	if (group_enabled[p_group]) {
		// Group already enabled, do nothing.
		return;
	}

	group_enabled.write[p_group] = true;

	// Compile all versions again to include the new group.
	for (const RID &version_rid : version_owner.get_owned_list()) {
		Version *version = version_owner.get_or_null(version_rid);
		version->mutex->lock();
		_compile_version_start(version, p_group);
		version->mutex->unlock();
	}
}

bool ShaderRD::is_group_enabled(int p_group) const {
	return group_enabled[p_group];
}

int64_t ShaderRD::get_group_count() const {
	return group_enabled.size();
}

const LocalVector<int> &ShaderRD::get_group_to_variants(int p_group) const {
	return group_to_variant_map[p_group];
}

const String &ShaderRD::get_name() const {
	return name;
}

const Vector<uint64_t> &ShaderRD::get_dynamic_buffers() const {
	return dynamic_buffers;
}

bool ShaderRD::shader_cache_cleanup_on_start = false;

ShaderRD::ShaderRD() {
	// Do not feel forced to use this, in most cases it makes little to no difference.
	bool use_32_threads = false;
	if (RD::get_singleton()->get_device_vendor_name() == "NVIDIA") {
		use_32_threads = true;
	}
	String base_compute_define_text;
	if (use_32_threads) {
		base_compute_define_text = "\n#define NATIVE_LOCAL_GROUP_SIZE 32\n#define NATIVE_LOCAL_SIZE_2D_X 8\n#define NATIVE_LOCAL_SIZE_2D_Y 4\n";
	} else {
		base_compute_define_text = "\n#define NATIVE_LOCAL_GROUP_SIZE 64\n#define NATIVE_LOCAL_SIZE_2D_X 8\n#define NATIVE_LOCAL_SIZE_2D_Y 8\n";
	}

	base_compute_defines = base_compute_define_text.ascii();
}

void ShaderRD::initialize(const Vector<String> &p_variant_defines, const String &p_general_defines, const Vector<RD::PipelineImmutableSampler> &p_immutable_samplers, const Vector<uint64_t> &p_dynamic_buffers) {
	ERR_FAIL_COND(variant_defines.size());
	ERR_FAIL_COND(p_variant_defines.is_empty());

	general_defines = p_general_defines.utf8();
	immutable_samplers = p_immutable_samplers;
	dynamic_buffers = p_dynamic_buffers;

	// When initialized this way, there is just one group and its always enabled.
	group_to_variant_map.insert(0, LocalVector<int>{});
	group_enabled.push_back(true);

	for (int i = 0; i < p_variant_defines.size(); i++) {
		variant_defines.push_back(VariantDefine(0, p_variant_defines[i], true));
		variants_enabled.push_back(true);
		variant_to_group.push_back(0);
		group_to_variant_map[0].push_back(i);
	}

	if (!shader_cache_user_dir.is_empty() || !shader_cache_res_dir.is_empty()) {
		group_sha256.resize(1);
		_initialize_cache();
	}
}

void ShaderRD::_initialize_cache() {
	shader_cache_user_dir_valid = !shader_cache_user_dir.is_empty();
	shader_cache_res_dir_valid = !shader_cache_res_dir.is_empty();
	if (!shader_cache_user_dir_valid) {
		return;
	}

	for (const KeyValue<int, LocalVector<int>> &E : group_to_variant_map) {
		StringBuilder hash_build;

		hash_build.append("[base_hash]");
		hash_build.append(base_sha256);
		hash_build.append("[general_defines]");
		hash_build.append(general_defines.get_data());
		hash_build.append("[group_id]");
		hash_build.append(itos(E.key));
		for (uint32_t i = 0; i < E.value.size(); i++) {
			hash_build.append("[variant_defines:" + itos(E.value[i]) + "]");
			hash_build.append(variant_defines[E.value[i]].text.get_data());
		}

		for (const uint64_t dyn_buffer : dynamic_buffers) {
			hash_build.append("[dynamic_buffer]");
			hash_build.append(uitos(dyn_buffer));
		}

		group_sha256[E.key] = hash_build.as_string().sha256_text();

		if (!shader_cache_user_dir.is_empty()) {
			// Validate if it's possible to write to all the directories required by in the user directory.
			Ref<DirAccess> d = DirAccess::open(shader_cache_user_dir);
			if (d.is_null()) {
				shader_cache_user_dir_valid = false;
				ERR_FAIL_MSG(vformat("Unable to open shader cache directory at %s.", shader_cache_user_dir));
			}

			if (d->change_dir(name) != OK) {
				Error err = d->make_dir(name);
				if (err != OK) {
					shader_cache_user_dir_valid = false;
					ERR_FAIL_MSG(vformat("Unable to create shader cache directory %s at %s.", name, shader_cache_user_dir));
				}

				d->change_dir(name);
			}

			if (d->change_dir(group_sha256[E.key]) != OK) {
				Error err = d->make_dir(group_sha256[E.key]);
				if (err != OK) {
					shader_cache_user_dir_valid = false;
					ERR_FAIL_MSG(vformat("Unable to create shader cache directory %s/%s at %s.", name, group_sha256[E.key], shader_cache_user_dir));
				}
			}
		}

		print_verbose("Shader '" + name + "' (group " + itos(E.key) + ") SHA256: " + group_sha256[E.key]);
	}
}

// Same as above, but allows specifying shader compilation groups.
void ShaderRD::initialize(const Vector<VariantDefine> &p_variant_defines, const String &p_general_defines, const Vector<RD::PipelineImmutableSampler> &p_immutable_samplers, const Vector<uint64_t> &p_dynamic_buffers) {
	ERR_FAIL_COND(variant_defines.size());
	ERR_FAIL_COND(p_variant_defines.is_empty());

	general_defines = p_general_defines.utf8();
	immutable_samplers = p_immutable_samplers;
	dynamic_buffers = p_dynamic_buffers;

	int max_group_id = 0;

	for (int i = 0; i < p_variant_defines.size(); i++) {
		// Fill variant array.
		variant_defines.push_back(p_variant_defines[i]);
		variants_enabled.push_back(true);
		variant_to_group.push_back(p_variant_defines[i].group);

		// Map variant array index to group id, so we can iterate over groups later.
		if (!group_to_variant_map.has(p_variant_defines[i].group)) {
			group_to_variant_map.insert(p_variant_defines[i].group, LocalVector<int>{});
		}
		group_to_variant_map[p_variant_defines[i].group].push_back(i);

		// Track max size.
		if (p_variant_defines[i].group > max_group_id) {
			max_group_id = p_variant_defines[i].group;
		}
	}

	// Set all to groups to false, then enable those that should be default.
	group_enabled.resize_initialized(max_group_id + 1);
	bool *enabled_ptr = group_enabled.ptrw();
	for (int i = 0; i < p_variant_defines.size(); i++) {
		if (p_variant_defines[i].default_enabled) {
			enabled_ptr[p_variant_defines[i].group] = true;
		}
	}

	if (!shader_cache_user_dir.is_empty()) {
		group_sha256.resize(max_group_id + 1);
		_initialize_cache();
	}
}

void ShaderRD::shaders_embedded_set_lock() {
	shader_versions_embedded_set_mutex.lock();
}

const ShaderRD::ShaderVersionPairSet &ShaderRD::shaders_embedded_set_get() {
	return shader_versions_embedded_set;
}

void ShaderRD::shaders_embedded_set_unlock() {
	shader_versions_embedded_set_mutex.unlock();
}

void ShaderRD::set_shader_cache_user_dir(const String &p_dir) {
	shader_cache_user_dir = p_dir;
}

const String &ShaderRD::get_shader_cache_user_dir() {
	return shader_cache_user_dir;
}

void ShaderRD::set_shader_cache_res_dir(const String &p_dir) {
	shader_cache_res_dir = p_dir;
}

const String &ShaderRD::get_shader_cache_res_dir() {
	return shader_cache_res_dir;
}

void ShaderRD::set_shader_cache_save_compressed(bool p_enable) {
	shader_cache_save_compressed = p_enable;
}

void ShaderRD::set_shader_cache_save_compressed_zstd(bool p_enable) {
	shader_cache_save_compressed_zstd = p_enable;
}

void ShaderRD::set_shader_cache_save_debug(bool p_enable) {
	shader_cache_save_debug = p_enable;
}

Vector<RD::ShaderStageSPIRVData> ShaderRD::compile_stages(const Vector<String> &p_stage_sources, const Vector<uint64_t> &p_dynamic_buffers) {
	RD::ShaderStageSPIRVData stage;
	Vector<RD::ShaderStageSPIRVData> stages;
	String error;
	RD::ShaderStage compilation_failed_stage = RD::SHADER_STAGE_MAX;
	bool compilation_failed = false;
	for (int64_t i = 0; i < p_stage_sources.size() && !compilation_failed; i++) {
		if (p_stage_sources[i].is_empty()) {
			continue;
		}

		stage.spirv = RD::get_singleton()->shader_compile_spirv_from_source(RD::ShaderStage(i), p_stage_sources[i], RD::SHADER_LANGUAGE_GLSL, &error);
		stage.dynamic_buffers = p_dynamic_buffers;
		stage.shader_stage = RD::ShaderStage(i);
		if (!stage.spirv.is_empty()) {
			stages.push_back(stage);

		} else {
			compilation_failed_stage = RD::ShaderStage(i);
			compilation_failed = true;
		}
	}

	if (compilation_failed) {
		ERR_PRINT("Error compiling " + String(compilation_failed_stage == RD::SHADER_STAGE_COMPUTE ? "Compute " : (compilation_failed_stage == RD::SHADER_STAGE_VERTEX ? "Vertex" : "Fragment")) + " shader.");
		ERR_PRINT(error);

#ifdef DEBUG_ENABLED
		ERR_PRINT("code:\n" + p_stage_sources[compilation_failed_stage].get_with_code_lines());
#endif

		return Vector<RD::ShaderStageSPIRVData>();
	} else {
		return stages;
	}
}

PackedByteArray ShaderRD::save_shader_cache_bytes(const LocalVector<int> &p_variants, const Vector<Vector<uint8_t>> &p_variant_data) {
	uint32_t variant_count = p_variants.size();
	PackedByteArray bytes;
	int64_t total_size = 0;
	total_size += 4 + sizeof(uint32_t) * 2;
	for (uint32_t i = 0; i < variant_count; i++) {
		total_size += sizeof(uint32_t) + p_variant_data[p_variants[i]].size();
	}

	bytes.resize(total_size);

	uint8_t *bytes_ptr = bytes.ptrw();
	memcpy(bytes_ptr, shader_file_header, 4);
	bytes_ptr += 4;

	*(uint32_t *)(bytes_ptr) = cache_file_version;
	bytes_ptr += sizeof(uint32_t);

	*(uint32_t *)(bytes_ptr) = variant_count;
	bytes_ptr += sizeof(uint32_t);

	for (uint32_t i = 0; i < variant_count; i++) {
		int variant_id = p_variants[i];
		*(uint32_t *)(bytes_ptr) = uint32_t(p_variant_data[variant_id].size());
		bytes_ptr += sizeof(uint32_t);

		memcpy(bytes_ptr, p_variant_data[variant_id].ptr(), p_variant_data[variant_id].size());
		bytes_ptr += p_variant_data[variant_id].size();
	}

	DEV_ASSERT((bytes.ptrw() + bytes.size()) == bytes_ptr);
	return bytes;
}

String ShaderRD::shader_cache_user_dir;
String ShaderRD::shader_cache_res_dir;
bool ShaderRD::shader_cache_save_compressed = true;
bool ShaderRD::shader_cache_save_compressed_zstd = true;
bool ShaderRD::shader_cache_save_debug = true;

ShaderRD::~ShaderRD() {
	_fellow_fill_forget(this);
	LocalVector<RID> remaining = version_owner.get_owned_list();
	if (remaining.size()) {
		ERR_PRINT(itos(remaining.size()) + " shaders of type " + name + " were never freed");
		for (const RID &version_rid : remaining) {
			version_free(version_rid);
		}
	}
	_fellow_reap(true);
}
