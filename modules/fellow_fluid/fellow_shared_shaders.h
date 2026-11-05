/**************************************************************************/
/*  fellow_shared_shaders.h                                               */
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

#include "core/io/resource.h"
#include "core/string/ustring.h"

class RDShaderFile;
class Shader;

// FellowEngine (milestone 53): the shaders FellowEngine's nodes make from code, made once per session and shared by
// every instance — a scene with several water bodies, or opened again in the editor, compiles each once. Before, each
// instance compiled its material's shader (the shading language, then SPIR-V) and its compute kernels (glslang) anew:
// 130-290 ms a FellowWater3D on the main thread with a RenderingDevice.
namespace FellowSharedShaders {

// A Shader with this code (main thread). Never change its code: it is shared; ask for the other code instead.
Ref<Shader> shader(const String &p_code);
// The compute kernels of a GLSL source (#[versions]), compiled to SPIR-V once (any thread); null after a compile error
// (printed once, under p_name).
Ref<RDShaderFile> file(const char *p_glsl, const char *p_name);
// Other resources the nodes build the same way for the same parameters (the water's grid and foam texture), under a key
// that names those parameters (main thread). Never change them: they are shared.
Ref<Resource> resource(const String &p_key);
void store(const String &p_key, const Ref<Resource> &p_resource);
// At the module's end: the shaders released while the RenderingServer lives.
void clear();

} // namespace FellowSharedShaders
