/**************************************************************************/
/*  fellow_editor_plugin.cpp                                              */
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

#include "fellow_editor_plugin.h"

#include "core/io/image.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/math/geometry_3d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/scene/3d/gizmos/gizmo_3d_helper.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/animation.h"
#include "scene/scene_string_names.h"
#include "servers/rendering/fellow_settings.h"

// ------------------------------------------------------------------ gizmos

static const char *FELLOW_GIZMO_CLASSES[] = {
	"FellowGasVolume3D",
	"FellowGasEmitter3D",
	"FellowWater3D",
	"FellowShallowWater3D",
	"FellowStreamingCell3D",
	"FellowWind3D",
	"FellowLightning3D",
	"FellowSnowTracker3D",
	"FellowRain3D",
	"FellowWorldStreamer3D",
	"FellowHLOD3D",
	"FellowBurnable3D",
	"FellowMotionCharacter3D",
};

// The properties each class's lines are drawn from.
static Vector<StringName> _fellow_gizmo_properties(const StringName &p_class) {
	if (p_class == SNAME("FellowGasVolume3D")) {
		return { SNAME("size") };
	} else if (p_class == SNAME("FellowGasEmitter3D")) {
		return { SNAME("shape"), SNAME("radius"), SNAME("box_size"), SNAME("height") };
	} else if (p_class == SNAME("FellowWater3D")) {
		return { SNAME("extent") };
	} else if (p_class == SNAME("FellowShallowWater3D")) {
		return { SNAME("domain_size") };
	} else if (p_class == SNAME("FellowStreamingCell3D")) {
		return { SNAME("bounds"), SNAME("load_distance") };
	} else if (p_class == SNAME("FellowWind3D")) {
		return { SNAME("speed") };
	} else if (p_class == SNAME("FellowLightning3D")) {
		return { SNAME("distance_range"), SNAME("cloud_height") };
	} else if (p_class == SNAME("FellowSnowTracker3D")) {
		return { SNAME("radius"), SNAME("length") };
	}
	return {};
}

static StringName _fellow_gizmo_class(const Node3D *p_node) {
	for (const char *c : FELLOW_GIZMO_CLASSES) {
		if (p_node->is_class(c)) {
			return StringName(c);
		}
	}
	return StringName();
}

void FellowGizmoPlugin::_box_lines(const AABB &p_box, Vector<Vector3> &r_lines) {
	for (int i = 0; i < 12; i++) {
		Vector3 a, b;
		p_box.get_edge(i, a, b);
		r_lines.push_back(a);
		r_lines.push_back(b);
	}
}

void FellowGizmoPlugin::_circle_lines(const Vector3 &p_centre, real_t p_radius, int p_segments, Vector<Vector3> &r_lines) {
	for (int i = 0; i < p_segments; i++) {
		const real_t a0 = Math::TAU * i / p_segments;
		const real_t a1 = Math::TAU * (i + 1) / p_segments;
		r_lines.push_back(p_centre + Vector3(Math::cos(a0), 0, Math::sin(a0)) * p_radius);
		r_lines.push_back(p_centre + Vector3(Math::cos(a1), 0, Math::sin(a1)) * p_radius);
	}
}

Vector<Vector3> FellowGizmoPlugin::lines_for(const StringName &p_class, const Dictionary &p_properties) {
	Vector<Vector3> lines;
	if (p_class == SNAME("FellowGasVolume3D")) {
		// The box's floor is centred on the node (milestone 54: drawn centred before, half its height too low).
		const Vector3 size = p_properties.get("size", Vector3(1, 1, 1));
		_box_lines(AABB(Vector3(-size.x * 0.5, 0, -size.z * 0.5), size), lines);
	} else if (p_class == SNAME("FellowGasEmitter3D")) {
		const int shape = p_properties.get("shape", 0);
		const real_t radius = p_properties.get("radius", 0.5);
		if (shape == 1) { // box
			const Vector3 size = p_properties.get("box_size", Vector3(1, 1, 1));
			_box_lines(AABB(-size * 0.5, size), lines);
		} else if (shape == 2) { // cylinder (vertical)
			const real_t h = p_properties.get("height", 1.0);
			_circle_lines(Vector3(0, -h * 0.5, 0), radius, 24, lines);
			_circle_lines(Vector3(0, h * 0.5, 0), radius, 24, lines);
			for (int i = 0; i < 4; i++) {
				const real_t a = Math::TAU * i / 4;
				const Vector3 d(Math::cos(a) * radius, 0, Math::sin(a) * radius);
				lines.push_back(d + Vector3(0, -h * 0.5, 0));
				lines.push_back(d + Vector3(0, h * 0.5, 0));
			}
		} else { // sphere: three great circles
			_circle_lines(Vector3(), radius, 32, lines);
			for (int i = 0; i < 32; i++) {
				const real_t a0 = Math::TAU * i / 32;
				const real_t a1 = Math::TAU * (i + 1) / 32;
				lines.push_back(Vector3(Math::cos(a0), Math::sin(a0), 0) * radius);
				lines.push_back(Vector3(Math::cos(a1), Math::sin(a1), 0) * radius);
				lines.push_back(Vector3(0, Math::sin(a0), Math::cos(a0)) * radius);
				lines.push_back(Vector3(0, Math::sin(a1), Math::cos(a1)) * radius);
			}
		}
	} else if (p_class == SNAME("FellowWater3D") || p_class == SNAME("FellowShallowWater3D")) {
		const Vector2 e = p_properties.get(p_class == SNAME("FellowWater3D") ? "extent" : "domain_size", Vector2());
		if (e.x > 0 && e.y > 0) {
			const Vector3 h(e.x * 0.5, 0, e.y * 0.5);
			const Vector3 c[4] = { Vector3(-h.x, 0, -h.z), Vector3(h.x, 0, -h.z), Vector3(h.x, 0, h.z), Vector3(-h.x, 0, h.z) };
			for (int i = 0; i < 4; i++) {
				lines.push_back(c[i]);
				lines.push_back(c[(i + 1) % 4]);
			}
		}
	} else if (p_class == SNAME("FellowStreamingCell3D")) {
		// The cell's bounds, and with its own load distance the box the camera loads it from.
		const AABB bounds = p_properties.get("bounds", AABB());
		_box_lines(bounds, lines);
		const real_t d = p_properties.get("load_distance", 0.0);
		if (d > 0.0) {
			_box_lines(bounds.grow(d), lines);
		}
	} else if (p_class == SNAME("FellowWind3D")) {
		// An arrow along -Z (the wind blows the way the node faces), as long as the wind's speed (1-20 m).
		const real_t len = CLAMP(real_t(p_properties.get("speed", 5.0)), 1.0, 20.0);
		const Vector3 tip(0, 0, -len);
		lines.push_back(Vector3());
		lines.push_back(tip);
		const real_t head = MIN(len * 0.25, 2.0);
		lines.push_back(tip);
		lines.push_back(tip + Vector3(head * 0.5, 0, head));
		lines.push_back(tip);
		lines.push_back(tip + Vector3(-head * 0.5, 0, head));
	} else if (p_class == SNAME("FellowLightning3D")) {
		// Strikes fall between the two rings around the camera at play time (drawn around the node), from the clouds.
		const Vector2 range = p_properties.get("distance_range", Vector2(400, 3000));
		const real_t h = p_properties.get("cloud_height", 1200.0);
		_circle_lines(Vector3(), range.x, 64, lines);
		_circle_lines(Vector3(), range.y, 96, lines);
		lines.push_back(Vector3(range.x, 0, 0));
		lines.push_back(Vector3(range.x, h, 0));
	} else if (p_class == SNAME("FellowSnowTracker3D")) {
		// A capsule along the node's -Z, centred: the track it presses into the snow.
		const real_t r = p_properties.get("radius", 0.1);
		const real_t h = real_t(p_properties.get("length", 0.0)) * 0.5;
		_circle_lines(Vector3(0, 0, -h), r, 24, lines);
		if (h > 0.0) {
			_circle_lines(Vector3(0, 0, h), r, 24, lines);
			lines.push_back(Vector3(r, 0, -h));
			lines.push_back(Vector3(r, 0, h));
			lines.push_back(Vector3(-r, 0, -h));
			lines.push_back(Vector3(-r, 0, h));
		}
	}
	return lines;
}

FellowGizmoPlugin::FellowGizmoPlugin() {
	helper.instantiate();
	create_material("fellow_lines", Color(0.35, 0.85, 1.0));
	create_material("fellow_cell", Color(1.0, 0.7, 0.25));
	create_handle_material("handles");
	for (const char *c : FELLOW_GIZMO_CLASSES) {
		const Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid() && theme->has_icon(c, EditorStringName(EditorIcons))) {
			create_icon_material(String("icon_") + c, theme->get_icon(c, EditorStringName(EditorIcons)));
		}
	}
}

bool FellowGizmoPlugin::has_gizmo(Node3D *p_node) {
	return _fellow_gizmo_class(p_node) != StringName();
}

String FellowGizmoPlugin::get_gizmo_name() const {
	return "FellowEngine";
}

int FellowGizmoPlugin::get_priority() const {
	return -1;
}

void FellowGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	Dictionary props;
	for (const StringName &p : _fellow_gizmo_properties(cls)) {
		props[p] = node->get(p);
	}
	const Vector<Vector3> lines = lines_for(cls, props);
	if (!lines.is_empty()) {
		p_gizmo->add_lines(lines, get_material(cls == SNAME("FellowStreamingCell3D") ? "fellow_cell" : "fellow_lines", p_gizmo));
		p_gizmo->add_collision_segments(lines);
	}
	const Vector<Vector3> handles = _handle_positions(node, cls);
	if (!handles.is_empty()) {
		p_gizmo->add_handles(handles, get_material("handles"));
	}
	const String icon = String("icon_") + String(cls);
	if (materials.has(icon)) {
		p_gizmo->add_unscaled_billboard(get_material(icon, p_gizmo), 0.05);
	}
}

// Handles (milestone 50: the gas volume's box; milestone 52: the rest). Boxes and cylinders through Gizmo3DHelper
// (they move the node as one side is dragged, as Godot's shapes do); the others move along one of the node's axes and
// set one number of a property, the node left in place.
FellowGizmoPlugin::HandleKind FellowGizmoPlugin::_handle_kind(const Node3D *p_node, const StringName &p_class, StringName &r_size_property) {
	if (p_class == SNAME("FellowGasVolume3D")) {
		return HANDLES_AXES; // its floor stays on the node: width and depth about it, the height up from it
	}
	if (p_class == SNAME("FellowGasEmitter3D")) {
		const int shape = p_node->get(SNAME("shape"));
		if (shape == 1) {
			r_size_property = SNAME("box_size");
			return HANDLES_BOX;
		}
		return shape == 2 ? HANDLES_CYLINDER : HANDLES_AXES;
	}
	if (p_class == SNAME("FellowStreamingCell3D")) {
		return HANDLES_BOUNDS;
	}
	if (p_class == SNAME("FellowWater3D") || p_class == SNAME("FellowShallowWater3D") || p_class == SNAME("FellowSnowTracker3D") || p_class == SNAME("FellowLightning3D")) {
		return HANDLES_AXES;
	}
	return HANDLES_NONE;
}

Vector<FellowGizmoPlugin::AxisHandle> FellowGizmoPlugin::axis_handles_for(const StringName &p_class, const Dictionary &p_properties) {
	Vector<AxisHandle> handles;
	auto add = [&](const StringName &p_property, int p_component, int p_axis, real_t p_sign, real_t p_scale, real_t p_offset, const Vector3 &p_base, const String &p_name) {
		AxisHandle h;
		h.property = p_property;
		h.component = p_component;
		h.axis = p_axis;
		h.sign = p_sign;
		h.scale = p_scale;
		h.offset = p_offset;
		h.base = p_base;
		h.name = p_name;
		const Variant v = p_properties.get(p_property, 0.0);
		real_t value = 0.0;
		if (p_component < 0) {
			value = v;
		} else if (v.get_type() == Variant::VECTOR3) {
			value = Vector3(v)[p_component];
		} else {
			value = Vector2(v)[p_component];
		}
		h.position = p_base;
		h.position[p_axis] += p_sign * (value / p_scale + p_offset);
		handles.push_back(h);
	};
	if (p_class == SNAME("FellowGasVolume3D")) {
		const Vector3 size = p_properties.get("size", Vector3(2, 4, 2));
		add(SNAME("size"), 0, 0, 1, 2, 0, Vector3(0, size.y * 0.5, 0), TTR("Width (X)"));
		add(SNAME("size"), 2, 2, 1, 2, 0, Vector3(0, size.y * 0.5, 0), TTR("Depth (Z)"));
		add(SNAME("size"), 1, 1, 1, 1, 0, Vector3(), TTR("Height (Y)"));
	} else if (p_class == SNAME("FellowGasEmitter3D")) {
		add(SNAME("radius"), -1, 0, 1, 1, 0, Vector3(), TTR("Radius"));
	} else if (p_class == SNAME("FellowWater3D") || p_class == SNAME("FellowShallowWater3D")) {
		const StringName prop = p_class == SNAME("FellowWater3D") ? SNAME("extent") : SNAME("domain_size");
		const Vector2 e = p_properties.get(prop, Vector2());
		if (e.x > 0 && e.y > 0) { // a water body without an extent has no edge
			add(prop, 0, 0, 1, 2, 0, Vector3(), TTR("Width (X)"));
			add(prop, 1, 2, 1, 2, 0, Vector3(), TTR("Depth (Z)"));
		}
	} else if (p_class == SNAME("FellowSnowTracker3D")) {
		const real_t r = p_properties.get("radius", 0.1);
		add(SNAME("radius"), -1, 0, 1, 1, 0, Vector3(), TTR("Radius"));
		add(SNAME("length"), -1, 2, -1, 2, r, Vector3(), TTR("Length")); // at the capsule's front tip
	} else if (p_class == SNAME("FellowLightning3D")) {
		const Vector2 range = p_properties.get("distance_range", Vector2(400, 3000));
		add(SNAME("distance_range"), 0, 0, 1, 1, 0, Vector3(), TTR("Nearest Strike"));
		add(SNAME("distance_range"), 1, 0, 1, 1, 0, Vector3(), TTR("Farthest Strike"));
		add(SNAME("cloud_height"), -1, 1, 1, 1, 0, Vector3(range.x, 0, 0), TTR("Cloud Height"));
	}
	return handles;
}

Vector<Vector3> FellowGizmoPlugin::_handle_positions(const Node3D *p_node, const StringName &p_class) const {
	StringName size_property;
	switch (_handle_kind(p_node, p_class, size_property)) {
		case HANDLES_BOX:
			return helper->box_get_handles(p_node->get(size_property));
		case HANDLES_CYLINDER:
			return helper->cylinder_get_handles(p_node->get(SNAME("height")), p_node->get(SNAME("radius")));
		case HANDLES_BOUNDS: {
			const AABB b = p_node->get(SNAME("bounds"));
			Vector<Vector3> faces;
			for (int i = 0; i < 6; i++) {
				Vector3 c = b.get_center();
				c[i / 2] = (i % 2) ? b.position[i / 2] + b.size[i / 2] : b.position[i / 2];
				faces.push_back(c);
			}
			return faces;
		}
		case HANDLES_AXES: {
			Vector<Vector3> positions;
			for (const AxisHandle &h : axis_handles_for(p_class, _gizmo_properties(p_node, p_class))) {
				positions.push_back(h.position);
			}
			return positions;
		}
		default:
			return Vector<Vector3>();
	}
}

Dictionary FellowGizmoPlugin::_gizmo_properties(const Node3D *p_node, const StringName &p_class) {
	Dictionary props;
	for (const StringName &p : _fellow_gizmo_properties(p_class)) {
		props[p] = p_node->get(p);
	}
	return props;
}

String FellowGizmoPlugin::get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	const Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	StringName size_property;
	switch (_handle_kind(node, cls, size_property)) {
		case HANDLES_BOX:
			return helper->box_get_handle_name(p_id);
		case HANDLES_CYLINDER:
			return helper->cylinder_get_handle_name(p_id);
		case HANDLES_BOUNDS: {
			static const char *faces[6] = { "-X", "+X", "-Y", "+Y", "-Z", "+Z" };
			return vformat(TTR("Bounds %s"), faces[CLAMP(p_id, 0, 5)]);
		}
		case HANDLES_AXES: {
			const Vector<AxisHandle> handles = axis_handles_for(cls, _gizmo_properties(node, cls));
			return p_id >= 0 && p_id < handles.size() ? handles[p_id].name : String();
		}
		default:
			return String();
	}
}

Variant FellowGizmoPlugin::get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	const Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	StringName size_property;
	switch (_handle_kind(node, cls, size_property)) {
		case HANDLES_BOX:
			return node->get(size_property);
		case HANDLES_CYLINDER:
			return Vector2(node->get(SNAME("radius")), node->get(SNAME("height")));
		case HANDLES_BOUNDS:
			return node->get(SNAME("bounds"));
		case HANDLES_AXES: {
			const Vector<AxisHandle> handles = axis_handles_for(cls, _gizmo_properties(node, cls));
			return p_id >= 0 && p_id < handles.size() ? node->get(handles[p_id].property) : Variant();
		}
		default:
			return Variant();
	}
}

void FellowGizmoPlugin::begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) {
	helper->initialize_handle_action(get_handle_value(p_gizmo, p_id, p_secondary), p_gizmo->get_node_3d()->get_global_transform());
}

// The point of the line through p_base along p_axis nearest the mouse's ray (the node's space).
static Vector3 _fellow_on_axis(const Vector3 p_segment[2], const Vector3 &p_base, int p_axis) {
	Vector3 dir;
	dir[p_axis] = 1.0;
	Vector3 on_axis, on_ray;
	Geometry3D::get_closest_points_between_segments(p_base - dir * 16384.0, p_base + dir * 16384.0, p_segment[0], p_segment[1], on_axis, on_ray);
	return on_axis;
}

static real_t _fellow_snapped(real_t p_value) {
	if (Node3DEditor::get_singleton() && Node3DEditor::get_singleton()->is_snap_enabled()) {
		return Math::snapped(p_value, Node3DEditor::get_singleton()->get_translate_snap());
	}
	return p_value;
}

void FellowGizmoPlugin::set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	Vector3 sg[2];
	helper->get_segment(p_camera, p_point, sg);
	StringName size_property;
	switch (_handle_kind(node, cls, size_property)) {
		case HANDLES_BOX: {
			Vector3 size = node->get(size_property);
			Vector3 position;
			helper->box_set_handle(sg, p_id, size, position);
			node->set(size_property, size);
			node->set_global_position(position);
		} break;
		case HANDLES_CYLINDER: {
			real_t height = node->get(SNAME("height"));
			real_t radius = node->get(SNAME("radius"));
			Vector3 position;
			helper->cylinder_set_handle(sg, p_id, height, radius, position);
			node->set(SNAME("height"), height);
			node->set(SNAME("radius"), radius);
			node->set_global_position(position);
		} break;
		case HANDLES_BOUNDS: {
			AABB b = node->get(SNAME("bounds"));
			const int axis = CLAMP(p_id / 2, 0, 2);
			Vector3 base = b.get_center();
			base[axis] = 0.0;
			const real_t c = _fellow_snapped(_fellow_on_axis(sg, base, axis)[axis]);
			const real_t lo = b.position[axis];
			const real_t hi = lo + b.size[axis];
			if (p_id % 2) {
				b.size[axis] = MAX(c - lo, real_t(0.01));
			} else {
				const real_t new_lo = MIN(c, hi - real_t(0.01));
				b.position[axis] = new_lo;
				b.size[axis] = hi - new_lo;
			}
			node->set(SNAME("bounds"), b);
		} break;
		case HANDLES_AXES: {
			const Vector<AxisHandle> handles = axis_handles_for(cls, _gizmo_properties(node, cls));
			ERR_FAIL_INDEX(p_id, handles.size());
			const AxisHandle &h = handles[p_id];
			const real_t c = (_fellow_on_axis(sg, h.base, h.axis)[h.axis] - h.base[h.axis]) * h.sign;
			const real_t v = _fellow_snapped(MAX((c - h.offset) * h.scale, real_t(0.0)));
			const Variant current = node->get(h.property);
			if (h.component < 0) {
				node->set(h.property, v);
			} else if (current.get_type() == Variant::VECTOR3) {
				Vector3 value = current;
				value[h.component] = MAX(v, real_t(0.01)); // a gas volume's size must stay above 0
				node->set(h.property, value);
			} else {
				Vector2 value = current;
				value[h.component] = v;
				node->set(h.property, value);
			}
		} break;
		default:
			break;
	}
}

void FellowGizmoPlugin::commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	StringName size_property;
	StringName property;
	String action;
	switch (_handle_kind(node, cls, size_property)) {
		case HANDLES_BOX:
			helper->box_commit_handle(TTR("Change Emitter Size"), p_cancel, node, node, SNAME("global_position"), size_property);
			return;
		case HANDLES_CYLINDER:
			helper->cylinder_commit_handle(p_id, TTR("Change Emitter Radius"), TTR("Change Emitter Height"), p_cancel, node);
			return;
		case HANDLES_BOUNDS:
			property = SNAME("bounds");
			action = TTR("Change Cell Bounds");
			break;
		case HANDLES_AXES: {
			const Vector<AxisHandle> handles = axis_handles_for(cls, _gizmo_properties(node, cls));
			ERR_FAIL_INDEX(p_id, handles.size());
			property = handles[p_id].property;
			action = vformat(TTR("Change %s"), handles[p_id].name);
		} break;
		default:
			return;
	}
	if (p_cancel) {
		node->set(property, p_restore);
		return;
	}
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(action);
	ur->add_do_property(node, property, node->get(property));
	ur->add_undo_property(node, property, p_restore);
	ur->commit_action();
}

// ------------------------------------------------------------------ BVH importer

String FellowBVHImporter::get_importer_name() const {
	return "fellow.bvh";
}

String FellowBVHImporter::get_visible_name() const {
	return "BVH Motion Capture (FellowEngine)";
}

void FellowBVHImporter::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("bvh");
}

String FellowBVHImporter::get_save_extension() const {
	return "res";
}

String FellowBVHImporter::get_resource_type() const {
	return "Animation";
}

int FellowBVHImporter::get_preset_count() const {
	return 2;
}

String FellowBVHImporter::get_preset_name(int p_idx) const {
	return p_idx == 1 ? "CMU (Hahne's conversion)" : "Centimetres";
}

void FellowBVHImporter::get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset) const {
	// The BVH's units in metres: centimetres by default; the CMU database's conversion is in 1/0.45 inch.
	r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "scale", PROPERTY_HINT_RANGE, "0.0001,10,0.000001,or_greater"), p_preset == 1 ? 0.056444 : 0.01));
	// The first frame of many captures is a T-pose: left out of the animation.
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "skip_first_frame"), p_preset == 1));
}

bool FellowBVHImporter::get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	return true;
}

Error FellowBVHImporter::import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	MethodBind *import_bvh = ClassDB::get_method("FellowMotionDatabase", "import_bvh");
	ERR_FAIL_NULL_V_MSG(import_bvh, ERR_UNAVAILABLE, "BVH import needs FellowEngine's fellow_animation module.");
	const Variant path = p_source_file;
	const Variant scale = p_options.has("scale") ? p_options["scale"] : Variant(0.01);
	const Variant skip = p_options.has("skip_first_frame") ? p_options["skip_first_frame"] : Variant(false);
	const Variant *args[3] = { &path, &scale, &skip };
	Callable::CallError ce;
	const Dictionary result = import_bvh->call(nullptr, args, 3, ce);
	Ref<Animation> animation = result.get("animation", Variant());
	ERR_FAIL_COND_V_MSG(animation.is_null(), ERR_PARSE_ERROR, "Cannot read the BVH file " + p_source_file + ".");
	animation->set_name(p_source_file.get_file().get_basename());
	return ResourceSaver::save(animation, p_save_path + ".res");
}

// ------------------------------------------------------------------ the plugin

void FellowEditorPlugin::_split_menu() {
	Node *streamer = nullptr;
	for (Node *n : EditorInterface::get_singleton()->get_selection()->get_top_selected_node_list()) {
		if (n->is_class("FellowWorldStreamer3D")) {
			streamer = n;
			break;
		}
	}
	if (!streamer || !Object::cast_to<Node3D>(streamer->get_parent())) {
		EditorNode::get_singleton()->show_warning(TTR("Select a FellowWorldStreamer3D whose parent is the level's root: its parent's other 3D children are split into cells."));
		return;
	}
	split_streamer = streamer->get_instance_id();
	Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
	const String base = scene && !scene->get_scene_file_path().is_empty() ? scene->get_scene_file_path().get_basename() : String("res://level");
	split_directory->set_text(base + "_cells");
	split_dialog->popup_centered(Size2(460, 0) * EDSCALE);
}

void FellowEditorPlugin::_split_confirmed() {
	Node *streamer = ObjectDB::get_instance<Node>(split_streamer);
	ERR_FAIL_NULL(streamer);
	Node3D *root = Object::cast_to<Node3D>(streamer->get_parent());
	ERR_FAIL_NULL(root);
	Dictionary options;
	options["owner"] = EditorInterface::get_singleton()->get_edited_scene_root();
	options["split_terrains"] = true; // milestone 65: a terrain into regions, each a cell
	const Array cells = streamer->call("split_into_cells", root, split_cell_size->get_value(), split_directory->get_text(), options);
	EditorFileSystem::get_singleton()->scan_changes();
	EditorInterface::get_singleton()->mark_scene_as_unsaved();
	EditorNode::get_singleton()->show_accept(vformat(TTR("%d streaming cells made; their scenes are in %s."), cells.size(), split_directory->get_text()), TTR("OK"));
}

// Milestone 66 (decision 0174): proxies baked for the selected streamer's cells, the large meshes streamed optionally.
void FellowEditorPlugin::_proxies_menu() {
	Node *streamer = nullptr;
	for (Node *n : EditorInterface::get_singleton()->get_selection()->get_top_selected_node_list()) {
		if (n->is_class("FellowWorldStreamer3D")) {
			streamer = n;
			break;
		}
	}
	if (!streamer) {
		EditorNode::get_singleton()->show_warning(TTR("Select a FellowWorldStreamer3D: a proxy is baked for each of its cells."));
		return;
	}
	proxies_streamer = streamer->get_instance_id();
	Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
	const String base = scene && !scene->get_scene_file_path().is_empty() ? scene->get_scene_file_path().get_basename() : String("res://level");
	proxies_directory->set_text(base + "_proxies");
	proxies_dialog->popup_centered(Size2(460, 0) * EDSCALE);
}

void FellowEditorPlugin::_proxies_confirmed() {
	Node *streamer = ObjectDB::get_instance<Node>(proxies_streamer);
	ERR_FAIL_NULL(streamer);
	Dictionary options;
	options["owner"] = EditorInterface::get_singleton()->get_edited_scene_root();
	options["stream_meshes"] = proxies_stream_meshes->is_pressed();
	const Dictionary info = streamer->call("bake_proxies", proxies_directory->get_text(), options);
	EditorFileSystem::get_singleton()->scan_changes();
	EditorInterface::get_singleton()->mark_scene_as_unsaved();
	String text = vformat(TTR("%d proxies baked (%d triangles for %d); they are in %s."), int(info.get("proxies", 0)), int(info.get("proxy_triangles", 0)), int(info.get("source_triangles", 0)), proxies_directory->get_text());
	if (proxies_stream_meshes->is_pressed()) {
		text += "\n" + vformat(TTR("%d meshes streamed: %d triangles while far (%d near)."), int(info.get("streamed_meshes", 0)), int(info.get("stand_in_triangles", 0)), int(info.get("streamed_triangles", 0)));
	}
	EditorNode::get_singleton()->show_accept(text, TTR("OK"));
}

// FellowEngine's nodes animate in the editor (milestone 52), or stand still: the toolbar's toggle and the editor setting
// keep each other in step.
static const char *FELLOW_ANIMATE_SETTING = "editors/3d/fellow/animate_nodes";

void FellowEditorPlugin::_animate_toggled(bool p_animate) {
	Fellow::set_editor_animate(p_animate);
	EditorSettings::get_singleton()->set_setting(FELLOW_ANIMATE_SETTING, p_animate);
	EditorSettings::get_singleton()->mark_setting_changed(FELLOW_ANIMATE_SETTING);
}

void FellowEditorPlugin::_editor_settings_changed() {
	const bool animate = EDITOR_GET(FELLOW_ANIMATE_SETTING);
	Fellow::set_editor_animate(animate);
	if (animate_button && animate_button->is_pressed() != animate) {
		animate_button->set_pressed_no_signal(animate);
	}
}

// Ready-made setups (milestone 53), wired up so that none has a configuration warning: added to the selected Node3D
// (or the scene's root), undoable, selected. Nodes made by class name: the plugin needs none of the modules' headers.
static Node *_fellow_make(const StringName &p_class, const String &p_name, Node *p_parent, const Dictionary &p_properties = Dictionary()) {
	Node *n = Object::cast_to<Node>(ClassDB::instantiate(p_class));
	ERR_FAIL_NULL_V_MSG(n, nullptr, vformat("FellowEngine: no %s in this build.", p_class));
	n->set_name(p_name);
	for (const KeyValue<Variant, Variant> &kv : p_properties) {
		n->set(kv.key, kv.value);
	}
	if (p_parent) {
		p_parent->add_child(n);
	}
	return n;
}

static bool _fellow_scene_has(const Node *p_root, const String &p_class) {
	return p_root && (p_root->is_class(p_class) || !p_root->find_children("*", p_class, true, false).is_empty());
}

Node3D *FellowEditorPlugin::make_preset(int p_preset, const Node *p_root) {
	Node3D *top = nullptr;
	switch (p_preset) {
		case PRESET_WEATHER: {
			// The weather drives the scene's rain, wind and lightning; those it lacks are added beside it.
			top = memnew(Node3D);
			top->set_name("Weather");
			_fellow_make("FellowWeather3D", "FellowWeather3D", top);
			if (!_fellow_scene_has(p_root, "FellowRain3D")) {
				_fellow_make("FellowRain3D", "Rain", top);
			}
			if (!_fellow_scene_has(p_root, "FellowWind3D")) {
				_fellow_make("FellowWind3D", "Wind", top);
			}
			if (!_fellow_scene_has(p_root, "FellowLightning3D")) {
				_fellow_make("FellowLightning3D", "Lightning", top);
			}
		} break;
		case PRESET_FIRE:
		case PRESET_SMOKE: {
			// The volume's floor is on its node: the emitter just above it (milestone 54: below it before, emitting
			// nothing).
			top = Object::cast_to<Node3D>(_fellow_make("FellowGasVolume3D", p_preset == PRESET_FIRE ? "Fire" : "Smoke", nullptr, Dictionary{ { "size", Vector3(2, 4, 2) } }));
			ERR_FAIL_NULL_V(top, nullptr);
			Dictionary emitter{ { "position", Vector3(0, 0.4, 0) } };
			if (p_preset == PRESET_SMOKE) {
				emitter["fuel"] = 0.0;
				emitter["temperature"] = 400.0;
				emitter["smoke"] = 1.0;
			}
			_fellow_make("FellowGasEmitter3D", "Emitter", top, emitter);
		} break;
		case PRESET_BURNING_BOX: {
			// A box that catches fire from a small flame under it, its flames and smoke in a gas volume around it.
			top = memnew(Node3D);
			top->set_name("BurningBox");
			MeshInstance3D *box = memnew(MeshInstance3D);
			box->set_name("Box");
			Ref<BoxMesh> mesh;
			mesh.instantiate();
			box->set_mesh(mesh);
			box->set_position(Vector3(0, 0.5, 0));
			top->add_child(box);
			_fellow_make("FellowBurnable3D", "FellowBurnable3D", box);
			Node *volume = _fellow_make("FellowGasVolume3D", "GasVolume", top, Dictionary{ { "size", Vector3(3, 4, 3) } });
			_fellow_make("FellowGasEmitter3D", "Flame", volume, Dictionary{ { "position", Vector3(0, 0.15, 0.65) }, { "radius", 0.15 } });
		} break;
		case PRESET_OCEAN: {
			top = Object::cast_to<Node3D>(_fellow_make("FellowWater3D", "Ocean", nullptr)); // to the horizon
		} break;
		case PRESET_LAKE: {
			top = Object::cast_to<Node3D>(_fellow_make("FellowWater3D", "Lake", nullptr, Dictionary{ { "extent", Vector2(20, 20) } }));
		} break;
		case PRESET_POOL: {
			// Shallow water that flows and splashes, drawn by a water body over it.
			top = Object::cast_to<Node3D>(_fellow_make("FellowShallowWater3D", "Pool", nullptr, Dictionary{ { "domain_size", Vector2(8, 8) }, { "initial_water_level", 0.3 }, { "inflow_velocity", Vector2(1, 0) }, { "inflow_depth", 0.4 } }));
			ERR_FAIL_NULL_V(top, nullptr);
			_fellow_make("FellowWater3D", "Surface", top, Dictionary{ { "extent", Vector2(8, 8) }, { "simulation_path", NodePath("..") } });
		} break;
		case PRESET_TERRAIN: {
			// A flat 256 m square at 1 m, centred on its parent, a grass and a rock layer: to sculpt and paint with the
			// terrain's toolbar (milestone 58).
			Ref<Image> heights = Image::create_empty(257, 257, false, Image::FORMAT_RF);
			heights->fill(Color(0, 0, 0));
			Array layers;
			const Color colors[2] = { Color(0.3, 0.42, 0.2), Color(0.45, 0.43, 0.4) };
			for (int i = 0; i < 2; i++) {
				Ref<Resource> layer = Object::cast_to<Resource>(ClassDB::instantiate("FellowTerrainLayer"));
				ERR_FAIL_COND_V(layer.is_null(), nullptr);
				layer->set_name(i == 0 ? "Grass" : "Rock");
				layer->set("albedo_color", colors[i]);
				layer->set("triplanar", i == 1);
				layers.push_back(layer);
			}
			top = Object::cast_to<Node3D>(_fellow_make("FellowTerrain3D", "Terrain", nullptr, Dictionary{ { "position", Vector3(-128, 0, -128) }, { "heightmap", heights }, { "layers", layers } }));
		} break;
		case PRESET_PIPELINE_WARMUP: {
			top = Object::cast_to<Node3D>(_fellow_make("FellowPipelineWarmer3D", "PipelineWarmup", nullptr, Dictionary()));
		} break;
		default:
			break;
	}
	return top;
}

static void _fellow_set_owner_do(EditorUndoRedoManager *p_ur, Node *p_node, Node *p_owner) {
	p_ur->add_do_method(p_node, "set_owner", p_owner);
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_fellow_set_owner_do(p_ur, p_node->get_child(i), p_owner);
	}
}

void FellowEditorPlugin::_add_preset(int p_preset) {
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (!root) {
		EditorNode::get_singleton()->show_warning(TTR("Open or create a 3D scene first."));
		return;
	}
	Node *parent = root;
	EditorSelection *selection = EditorNode::get_singleton()->get_editor_selection();
	const List<Node *> selected = selection->get_top_selected_node_list();
	if (selected.size() == 1 && Object::cast_to<Node3D>(selected.front()->get())) {
		parent = selected.front()->get();
	}
	Node3D *made = make_preset(p_preset, root);
	ERR_FAIL_NULL(made);
	EditorUndoRedoManager *ur = EditorUndoRedoManager::get_singleton();
	ur->create_action(vformat(TTR("Add %s (FellowEngine)"), made->get_name()), UndoRedo::MERGE_DISABLE, root);
	ur->add_do_method(parent, "add_child", made, true);
	_fellow_set_owner_do(ur, made, root);
	ur->add_do_method(selection, "clear");
	ur->add_do_method(selection, "add_node", made);
	ur->add_do_reference(made);
	ur->add_undo_method(parent, "remove_child", made);
	ur->commit_action();
}

void FellowEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_tool_menu_item(TTR("Split into Streaming Cells (FellowEngine)..."), callable_mp(this, &FellowEditorPlugin::_split_menu));
		add_tool_menu_item(TTR("Bake Streaming Proxies (FellowEngine)..."), callable_mp(this, &FellowEditorPlugin::_proxies_menu));
		add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, add_menu);
		add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, animate_button);
		EditorSettings::get_singleton()->connect("settings_changed", callable_mp(this, &FellowEditorPlugin::_editor_settings_changed));
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		remove_tool_menu_item(TTR("Split into Streaming Cells (FellowEngine)..."));
		remove_tool_menu_item(TTR("Bake Streaming Proxies (FellowEngine)..."));
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, animate_button);
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, add_menu);
		EditorSettings::get_singleton()->disconnect("settings_changed", callable_mp(this, &FellowEditorPlugin::_editor_settings_changed));
	}
}

void FellowEditorPlugin::_update_animate_icon() {
	if (animate_button && animate_button->has_theme_icon(SNAME("Play"), EditorStringName(EditorIcons))) {
		animate_button->set_button_icon(animate_button->get_theme_icon(SNAME("Play"), EditorStringName(EditorIcons)));
	}
}

FellowEditorPlugin::FellowEditorPlugin() {
	// The gizmos and the importer from the start, as the editor's own plugins do (the importer before the first scan).
	gizmo_plugin.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(gizmo_plugin);
	bvh_importer.instantiate();
	ResourceFormatImporter::get_singleton()->add_importer(bvh_importer);

	split_dialog = memnew(ConfirmationDialog);
	split_dialog->set_title(TTR("Split into Streaming Cells"));
	VBoxContainer *vb = memnew(VBoxContainer);
	split_dialog->add_child(vb);
	Label *info = memnew(Label);
	info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	info->set_custom_minimum_size(Size2(420, 0) * EDSCALE);
	info->set_text(TTR("The streamer's parent's 3D children are grouped into cells on a grid, each cell saved as a scene and replaced by a FellowStreamingCell3D under the streamer. Cameras, directional lights, environments and nodes wider than two cells stay. This cannot be undone: save the scene first."));
	vb->add_child(info);
	split_cell_size = memnew(SpinBox);
	split_cell_size->set_min(1);
	split_cell_size->set_max(100000);
	split_cell_size->set_step(1);
	split_cell_size->set_value(64);
	split_cell_size->set_suffix("m");
	vb->add_margin_child(TTR("Cell size:"), split_cell_size);
	split_directory = memnew(LineEdit);
	vb->add_margin_child(TTR("Directory for the cells' scenes:"), split_directory);
	split_dialog->connect(SceneStringName(confirmed), callable_mp(this, &FellowEditorPlugin::_split_confirmed));
	EditorNode::get_singleton()->get_gui_base()->add_child(split_dialog);

	proxies_dialog = memnew(ConfirmationDialog);
	proxies_dialog->set_title(TTR("Bake Streaming Proxies"));
	VBoxContainer *pvb = memnew(VBoxContainer);
	proxies_dialog->add_child(pvb);
	Label *pinfo = memnew(Label);
	pinfo->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	pinfo->set_custom_minimum_size(Size2(420, 0) * EDSCALE);
	pinfo->set_text(TTR("Each cell of the selected streamer gets a proxy: its content merged and simplified into one mesh with vertex colours, drawn while the cell is not loaded. Bake again after changing the cells."));
	pvb->add_child(pinfo);
	proxies_stream_meshes = memnew(CheckBox);
	proxies_stream_meshes->set_text(TTR("Stream large meshes (simplified stand-ins in the cells' scenes, the full meshes loaded near)"));
	proxies_stream_meshes->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	pvb->add_child(proxies_stream_meshes);
	proxies_directory = memnew(LineEdit);
	pvb->add_margin_child(TTR("Directory for the proxies:"), proxies_directory);
	proxies_dialog->connect(SceneStringName(confirmed), callable_mp(this, &FellowEditorPlugin::_proxies_confirmed));
	EditorNode::get_singleton()->get_gui_base()->add_child(proxies_dialog);

	EDITOR_DEF(FELLOW_ANIMATE_SETTING, true);
	Fellow::set_editor_animate(EDITOR_GET(FELLOW_ANIMATE_SETTING));
	animate_button = memnew(Button);
	animate_button->set_name("FellowAnimateNodes");
	animate_button->set_theme_type_variation(SceneStringName(FlatButton));
	animate_button->set_toggle_mode(true);
	animate_button->set_pressed(Fellow::is_editor_animate());
	animate_button->set_tooltip_text(TTR("Animate FellowEngine's nodes in the editor: the water's waves, the rain, lightning, the wind, gas and shallow water, as in the running game.\nOff: they stand still, and the editor redraws only when something changes (less GPU and power use)."));
	animate_button->connect(SceneStringName(toggled), callable_mp(this, &FellowEditorPlugin::_animate_toggled));
	animate_button->connect(SceneStringName(theme_changed), callable_mp(this, &FellowEditorPlugin::_update_animate_icon));

	add_menu = memnew(MenuButton);
	add_menu->set_name("FellowAddMenu");
	add_menu->set_text(TTR("FellowEngine"));
	add_menu->set_flat(false);
	add_menu->set_theme_type_variation("FlatMenuButton");
	add_menu->set_tooltip_text(TTR("Add a FellowEngine setup, its nodes wired up, to the selected 3D node (or the scene's root)."));
	PopupMenu *popup = add_menu->get_popup();
	popup->add_item(TTR("Add Weather (Rain, Wind, Lightning)"), PRESET_WEATHER);
	popup->add_separator();
	popup->add_item(TTR("Add Fire"), PRESET_FIRE);
	popup->add_item(TTR("Add Smoke"), PRESET_SMOKE);
	popup->add_item(TTR("Add Burning Box"), PRESET_BURNING_BOX);
	popup->add_separator();
	popup->add_item(TTR("Add Ocean"), PRESET_OCEAN);
	popup->add_item(TTR("Add Lake (20 x 20 m)"), PRESET_LAKE);
	popup->add_item(TTR("Add Flowing Pool (8 x 8 m)"), PRESET_POOL);
	popup->add_separator();
	popup->add_item(TTR("Add Terrain (256 x 256 m, to sculpt)"), PRESET_TERRAIN);
	popup->add_item(TTR("Add Pipeline Warm-up (compiles while loading)"), PRESET_PIPELINE_WARMUP);
	popup->connect(SceneStringName(id_pressed), callable_mp(this, &FellowEditorPlugin::_add_preset));
}

FellowEditorPlugin::~FellowEditorPlugin() {
	if (animate_button && !animate_button->get_parent()) {
		memdelete(animate_button); // taken out of the toolbar on exit
	}
	if (add_menu && !add_menu->get_parent()) {
		memdelete(add_menu);
	}
	if (ResourceFormatImporter::get_singleton()) {
		ResourceFormatImporter::get_singleton()->remove_importer(bvh_importer);
	}
}
