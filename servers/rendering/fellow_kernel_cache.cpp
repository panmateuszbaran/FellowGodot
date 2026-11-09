/**************************************************************************/
/*  fellow_kernel_cache.cpp                                               */
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

#include "fellow_kernel_cache.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "servers/rendering/fellow_settings.h"

Ref<RDShaderFile> FellowKernelCache::parse(const String &p_glsl, const String &p_defines, const String &p_name) {
	const bool use_cache = GLOBAL_GET("rendering/fellow/frame/kernel_spirv_cache");
	String cache_path;
	if (use_cache) {
		const String build = Engine::get_singleton()->get_version_info()["hash"];
		cache_path = vformat("user://fellow_kernel_cache/%s_%s.res", p_name.validate_filename(), (p_glsl + "\n" + p_defines + "\n" + build).md5_text());
		if (FileAccess::exists(cache_path)) {
			Fellow::CostScope cost(Fellow::stats().cost_glsl_cached_usec, Fellow::stats().cost_glsl_cached);
			Ref<RDShaderFile> cached = ResourceLoader::load(cache_path, "RDShaderFile", ResourceFormatLoader::CACHE_MODE_IGNORE);
			if (cached.is_valid() && cached->get_base_error().is_empty() && !cached->get_version_list().is_empty()) {
				return cached;
			}
		}
	}
	Ref<RDShaderFile> f;
	f.instantiate();
	Error err;
	{
		Fellow::CostScope cost(Fellow::stats().cost_glsl_compile_usec, Fellow::stats().cost_glsl_compiles);
		err = f->parse_versions_from_text(p_glsl, p_defines);
	}
	if (err != OK) {
		f->print_errors(p_name);
		return Ref<RDShaderFile>();
	}
	// Kept only when every version and stage compiled (a failing kernel's errors are printed where it is used).
	bool compiled = f->get_base_error().is_empty();
	const TypedArray<StringName> versions = f->get_version_list();
	for (int i = 0; i < versions.size() && compiled; i++) {
		for (int stage = 0; stage < RD::SHADER_STAGE_MAX; stage++) {
			compiled = compiled && f->get_spirv(versions[i])->get_stage_compile_error(RD::ShaderStage(stage)).is_empty();
		}
	}
	if (use_cache && compiled) {
		DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir());
		ResourceSaver::save(f, cache_path);
	}
	return f;
}
