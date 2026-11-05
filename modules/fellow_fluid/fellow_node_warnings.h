/**************************************************************************/
/*  fellow_node_warnings.h                                                */
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

#include "core/config/engine.h"
#include "core/os/os.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/rendering/fellow_settings.h"

// FellowEngine (milestone 52): the configuration warnings FellowEngine's nodes share — what the Scene dock's warning
// icon says when a node will not do what it is there for.
namespace FellowWarnings {

// A path property set, leading to no node of the class.
inline void path(PackedStringArray &r_warnings, const Node *p_node, const NodePath &p_path, const char *p_property, const char *p_class) {
	if (p_path.is_empty() || !p_node->is_inside_tree()) {
		return;
	}
	const Node *n = p_node->get_node_or_null(p_path);
	if (!n) {
		r_warnings.push_back(vformat(RTR("%s (%s) leads to no node."), p_property, String(p_path)));
	} else if (!n->is_class(p_class)) {
		r_warnings.push_back(vformat(RTR("%s leads to a %s, not a %s."), p_property, n->get_class(), p_class));
	}
}

// The fluid nodes do nothing with FellowEngine's fluids off.
inline bool fluids_off(PackedStringArray &r_warnings) {
	if (Fellow::fluid_enabled()) {
		return false;
	}
	r_warnings.push_back(RTR("FellowEngine's fluids are off in the Project Settings (Rendering > Fellow > Enabled, or Rendering > Fellow > Fluid > Enabled): this node does nothing."));
	return true;
}

// Where to look for the nodes a node works with: the edited scene in the editor, the whole tree in a game.
inline const Node *scene_root(const Node *p_node) {
	if (!p_node->is_inside_tree()) {
		return nullptr;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return p_node->get_tree()->get_edited_scene_root();
	}
	return p_node->get_tree()->get_root();
}

// The renderer the project draws with. Neither is true without one (headless, the dummy renderer: nothing is drawn, and
// nothing said about it).
inline bool compatibility() {
	return OS::get_singleton()->get_current_rendering_method() == "gl_compatibility";
}

inline bool mobile() {
	return OS::get_singleton()->get_current_rendering_method() == "mobile";
}

} // namespace FellowWarnings
