/**************************************************************************/
/*  fellow_shared_shaders.cpp                                             */
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

#include "fellow_shared_shaders.h"

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "scene/resources/shader.h"
#include "servers/rendering/fellow_kernel_cache.h"
#include "servers/rendering/rendering_device_binds.h"

static Mutex fellow_shared_mutex;
static HashMap<String, Ref<Shader>> *fellow_shared_shaders = nullptr;
static HashMap<const char *, Ref<RDShaderFile>> *fellow_shared_files = nullptr;
static HashMap<const char *, bool> *fellow_shared_failed = nullptr;
static HashMap<String, Ref<Resource>> *fellow_shared_resources = nullptr;

Ref<Resource> FellowSharedShaders::resource(const String &p_key) {
	MutexLock lock(fellow_shared_mutex);
	if (fellow_shared_resources) {
		if (Ref<Resource> *found = fellow_shared_resources->getptr(p_key)) {
			return *found;
		}
	}
	return Ref<Resource>();
}

void FellowSharedShaders::store(const String &p_key, const Ref<Resource> &p_resource) {
	MutexLock lock(fellow_shared_mutex);
	if (!fellow_shared_resources) {
		fellow_shared_resources = memnew((HashMap<String, Ref<Resource>>));
	}
	fellow_shared_resources->insert(p_key, p_resource);
}

Ref<Shader> FellowSharedShaders::shader(const String &p_code) {
	MutexLock lock(fellow_shared_mutex);
	if (!fellow_shared_shaders) {
		fellow_shared_shaders = memnew((HashMap<String, Ref<Shader>>));
	}
	Ref<Shader> *found = fellow_shared_shaders->getptr(p_code);
	if (found) {
		return *found;
	}
	Ref<Shader> s;
	s.instantiate();
	s->set_code(p_code);
	fellow_shared_shaders->insert(p_code, s);
	return s;
}

Ref<RDShaderFile> FellowSharedShaders::file(const char *p_glsl, const char *p_name) {
	MutexLock lock(fellow_shared_mutex);
	if (!fellow_shared_files) {
		fellow_shared_files = memnew((HashMap<const char *, Ref<RDShaderFile>>));
		fellow_shared_failed = memnew((HashMap<const char *, bool>));
	}
	if (Ref<RDShaderFile> *found = fellow_shared_files->getptr(p_glsl)) {
		return *found;
	}
	if (fellow_shared_failed->has(p_glsl)) {
		return Ref<RDShaderFile>();
	}
	// Milestone 61 (decision 0169): compiled once per machine and build (glslang: tens to hundreds of milliseconds a
	// file, on the thread setting the node up).
	Ref<RDShaderFile> f = FellowKernelCache::parse(String(p_glsl), String(), String(p_name));
	if (f.is_null()) {
		fellow_shared_failed->insert(p_glsl, true);
		return Ref<RDShaderFile>();
	}
	fellow_shared_files->insert(p_glsl, f);
	return f;
}

void FellowSharedShaders::clear() {
	MutexLock lock(fellow_shared_mutex);
	if (fellow_shared_shaders) {
		memdelete(fellow_shared_shaders);
		fellow_shared_shaders = nullptr;
	}
	if (fellow_shared_resources) {
		memdelete(fellow_shared_resources);
		fellow_shared_resources = nullptr;
	}
	if (fellow_shared_files) {
		memdelete(fellow_shared_files);
		memdelete(fellow_shared_failed);
		fellow_shared_files = nullptr;
		fellow_shared_failed = nullptr;
	}
}
