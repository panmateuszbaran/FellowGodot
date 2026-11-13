/**************************************************************************/
/*  fellow_camera_relative.h                                              */
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

#include "core/math/projection.h"
#include "core/math/transform_3d.h"

// Milestone 103 (decision 0212): FellowEngine's screen effects work relative to the camera. A position far from the
// origin (100 km: a float's step is 8 mm there) reconstructed from depth in world space and then the camera's
// position subtracted from it lost most of its digits on the GPU — the fire's volume in blocks, the clouds and the
// rainbow in steps. The matrices below are made on the CPU (real_t: double with precision=double), so the GPU only
// sees positions near zero.

// NDC to the position relative to the camera: the inverse of the view projection with the camera at the origin.
inline Projection fellow_inverse_view_projection_relative(const Projection &p_view_projection, const Vector3 &p_camera) {
	Projection t;
	t.columns[3] = Vector4(p_camera.x, p_camera.y, p_camera.z, 1.0);
	return (p_view_projection * t).inverse();
}

// A view projection (another frame's) taking positions relative to p_camera.
inline Projection fellow_view_projection_relative(const Projection &p_view_projection, const Vector3 &p_camera) {
	Projection t;
	t.columns[3] = Vector4(p_camera.x, p_camera.y, p_camera.z, 1.0);
	return p_view_projection * t;
}

// A world transform (world to something) taking positions relative to p_camera.
inline Transform3D fellow_transform_relative(const Transform3D &p_from_world, const Vector3 &p_camera) {
	return Transform3D(p_from_world.basis, p_from_world.xform(p_camera));
}
