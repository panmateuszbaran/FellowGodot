/**************************************************************************/
/*  fellow_cell_proxies.h                                                 */
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

#include "core/variant/dictionary.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class Node;

// FellowEngine cell proxies and streamed meshes (docs/decisions/0174-cell-proxies.md): what a streaming cell shows
// from afar while its content is not loaded, and the stand-ins of its large meshes while they are far.
namespace FellowCellProxies {

// One mesh standing in for a cell's content: its visible meshes, MultiMeshes (up to a few hundred instances) and
// terrains merged in the content root's space, welded and simplified to about p_ratio of their triangles (a terrain
// region's edges kept), each corner coloured by its materials' average albedo under it (linear vertex colours, for
// proxy_material()). With p_texel_size (metres) over 0 and a material with a texture among them (and an editor build:
// the UV unwrapper), the proxy is unwrapped instead and drawn with a texture of what the content draws under it,
// texels of about p_texel_size (coarser to keep within p_max_texture_size), with a material of its own
// (docs/decisions/0251-textured-proxies.md). p_triangles over 0: about that many triangles instead of p_ratio. Null
// when there is nothing to draw. r_info: the source and proxy triangles, the simplifier's error, the texture's size,
// bytes, texels found and time.
Ref<ArrayMesh> bake_proxy(Node *p_content, float p_ratio, int p_terrain_quads, float p_texel_size, int p_max_texture_size, Dictionary &r_info, int p_triangles = 0);

// Frees the textures' levels read by textured bakes (kept from one cell to the next).
void clear_bake_cache();

// A mesh's stand-in with vertices of its own: each triangle surface simplified to about p_ratio of its triangles
// (materials and attributes kept). Null when it would not be at most half the triangles.
Ref<ArrayMesh> simplified_mesh(const Ref<Mesh> &p_mesh, float p_ratio);

// The material of the proxies: their vertex colours as albedo.
Ref<StandardMaterial3D> proxy_material();

// The mean linear colour of a texture (its smallest mips), cached by texture.
Color texture_average(const Ref<Texture2D> &p_texture);

int mesh_triangles(const Ref<Mesh> &p_mesh);

} // namespace FellowCellProxies
