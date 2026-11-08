/**************************************************************************/
/*  fellow_bake_shaders.h                                                 */
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

#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

// FellowEngine (milestone 61): the spatial shaders FellowEngine's nodes make from code (a water's surface, rain's drops,
// a terrain's layers) are not resources of an export, so Godot's shader baker does not see them. Modules register a
// function giving their codes; the renderer creates them when the baker starts an export
// (RendererSceneRenderRD::fellow_create_versions_for_baking()), so that their pipelines' shaders are baked.
namespace FellowBakeShaders {

typedef void (*Provider)(Vector<String> &r_codes);

inline LocalVector<Provider> &providers() {
	static LocalVector<Provider> list;
	return list;
}

inline void register_provider(Provider p_provider) {
	if (!providers().has(p_provider)) {
		providers().push_back(p_provider);
	}
}

inline void get_codes(Vector<String> &r_codes) {
	for (Provider p : providers()) {
		p(r_codes);
	}
}

} // namespace FellowBakeShaders
