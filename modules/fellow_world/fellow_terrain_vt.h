/**************************************************************************/
/*  fellow_terrain_vt.h                                                   */
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

#include "core/math/plane.h"
#include "core/math/rect2.h"
#include "core/math/vector3.h"
#include "core/math/vector4.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/dictionary.h"
#include "scene/resources/image_texture.h"

class Texture2DRD;

// FellowEngine milestone 70 (docs/decisions/0178-terrain-virtual-texture.md): a FellowTerrain3D's material baked into
// pages of a virtual texture and drawn from them, instead of blending its layers in every fragment.
//
// The virtual texture covers the terrain's map with pages of PAGE x PAGE texels (texels_per_metre at mip 0, each mip
// coarser by two); a page is baked, with BORDER texels around it, into a slot of an atlas shared by every terrain (two
// RGBA8 textures: the albedo in sRGB and the roughness; the normal in terrain space). A page table per terrain (an
// RGBA8 texture with a level per mip) maps each page to its slot or, while it is not baked, to its nearest baked
// ancestor's. The coarsest mip (one page over the whole map) is not a page but the terrain's overview: two textures of
// PAGE x PAGE texels with their own mips, baked level by level, read where a fragment covers more than its texels (far
// away, at grazing angles) and where no page is baked yet. The pages a view wants are chosen on the CPU each frame from
// the camera's distance to them (a quadtree from the coarsest page down, the frustum's pages finer), and the missing
// ones baked by a compute kernel within a budget of pages a frame, the coarsest and nearest first; the least recently
// wanted pages give their slots up.
class FellowTerrainVT {
public:
	static constexpr int PAGE = 128;
	static constexpr int BORDER = 4; // bilinear filtering and up to 4:1 anisotropy within a page
	static constexpr int PADDED = PAGE + 2 * BORDER;
	static constexpr int MAX_MIPS = 16;
	static constexpr int HEIGHT_MIP = 3; // the coarsest mip whose pages keep their height range (finer ones use it)
	static constexpr int OVERVIEW_MIPS = 8; // the overview's levels: PAGE texels down to 1
	static constexpr int REVISIT_FRAMES = 4; // the quadtree at least every this many frames near the camera (staggered by terrain)

	struct View {
		Vector3 camera; // terrain-local
		LocalVector<Plane> frustum; // terrain-local, outward normals
		float pixel_angle = 0.001f; // radians per pixel at the screen's centre (the 3D frame's resolution)
	};

	// A page to bake: the slot and the page (the kernel's buffer: ivec4(slot x, slot y, mip, page x | page z << 16)).
	struct Job {
		int32_t slot_x = 0;
		int32_t slot_y = 0;
		int32_t mip = 0;
		int32_t page = 0;
	};

private:
	// The atlas shared by every terrain (main thread), its RenderingDevice textures made on the render thread.
	struct Slot {
		FellowTerrainVT *owner = nullptr;
		uint64_t key = 0;
		uint64_t used = 0; // the frame its page was last wanted
	};
	static LocalVector<Slot> slots;
	static LocalVector<int> free_slots;
	static int slots_per_side;
	static int atlas_texels;
	static int users;
	static uint64_t frame;
	static int budget_left;
	static int wanted_this_frame; // pages wanted by every terrain this frame
	static int wanted_last_frame;
	static float pressure_bias; // added to every terrain's lod bias while the atlas is nearly full
	static Ref<Texture2DRD> atlas_albedo;
	static Ref<Texture2DRD> atlas_normal;
	static uint64_t stat_baked;
	static uint64_t stat_evicted;
	static uint64_t stat_wanted_missing;

	static void _begin_frame();
	static int _take_slot(FellowTerrainVT *p_for);

	// This terrain's virtual texture.
	uint64_t id = 0; // the render thread's kernel inputs for it
	Vector2 extent; // metres
	float texels_per_metre = 32.0f;
	int mips = 0;
	Vector2i pages0; // pages along x and z at mip 0 (powers of two)
	HashMap<uint64_t, int> resident; // page key -> slot
	HashMap<uint64_t, bool> stale; // resident pages to bake again (their content kept meanwhile)
	LocalVector<LocalVector<uint32_t>> table; // per mip: RGBA8 entries (slot x, slot y, mapped mip, 255) or 0
	Rect2i table_dirty[MAX_MIPS];
	Ref<Image> table_image;
	Ref<ImageTexture> table_texture;
	// Height ranges of the pages at HEIGHT_MIP (or the coarsest mip, if fewer): (min, max) in metres.
	LocalVector<Vector2> height_ranges;
	int height_mip = 0;
	Vector2 height_all;
	uint64_t want_frame = 0;
	LocalVector<uint64_t> wanted; // this frame's pages (keys)
	struct Missing {
		uint64_t key;
		float priority; // smaller first
		bool operator<(const Missing &p_o) const { return priority < p_o.priority; }
	};
	LocalVector<Missing> missing;
	// The overview (the coarsest mip with its own mips), made with the first configuration.
	Ref<Texture2DRD> overview_albedo;
	Ref<Texture2DRD> overview_normal;
	bool overview_stale = false; // to bake again
	bool overview_due = false; // baked with this frame's jobs
	uint64_t overview_baked = 0;
	int last_wanted = 0;
	int last_missing = 0;
	// Milestone 71: the last visit's choice kept while the view barely changes (the quadtree every few frames, or when the
	// camera has moved, pages are missing or stale, the bias changed); its slots kept recent meanwhile.
	bool visited = false;
	Vector3 visit_camera;
	float visit_bias = 0.0f;
	float visit_near = 0.0f; // the camera's distance to the map's box at the visit
	LocalVector<int> wanted_slots;
	uint64_t start_frame = UINT64_MAX;
	uint64_t visits = 0;
	uint64_t updates = 0;
	bool _start(bool p_may_keep, const Vector3 &p_camera, float p_lod_bias);
	uint64_t baked = 0;

	static uint64_t _key(int p_mip, int p_x, int p_z) { return (uint64_t(p_mip) << 48) | (uint64_t(uint32_t(p_z)) << 24) | uint64_t(uint32_t(p_x)); }
	static int _key_mip(uint64_t p_key) { return int(p_key >> 48); }
	static int _key_x(uint64_t p_key) { return int(p_key & 0xFFFFFF); }
	static int _key_z(uint64_t p_key) { return int((p_key >> 24) & 0xFFFFFF); }
	Vector2i _pages(int p_mip) const { return Vector2i(MAX(pages0.x >> p_mip, 1), MAX(pages0.y >> p_mip, 1)); }
	Rect2 _page_rect(int p_mip, int p_x, int p_z) const;
	Vector2 _page_heights(int p_mip, int p_x, int p_z) const;
	void _visit(const View &p_view, float p_bias, int p_mip, int p_x, int p_z);
	void _want(uint64_t p_key, float p_priority);
	void _set_entry_tree(int p_mip, int p_x, int p_z);
	void _evict(uint64_t p_key);

public:
	FellowTerrainVT();
	~FellowTerrainVT();

	// The page geometry for a map of p_extent metres; the tables empty, every slot of this terrain given back.
	void configure(const Vector2 &p_extent, float p_texels_per_metre);
	bool is_configured() const { return mips > 0; }
	int get_mip_count() const { return mips; }
	Vector2i get_pages0() const { return pages0; }
	float get_texels_per_metre() const { return texels_per_metre; }
	// The terrain's heights (row-major, p_w x p_h texels p_spacing apart) over a rectangle of texels: the pages' height
	// ranges there (the whole map with an empty rectangle).
	void update_heights(const float *p_heights, int p_w, int p_h, float p_spacing, const Rect2i &p_texels = Rect2i());

	// A frame, first: true when the last frame's choice of pages still holds for a camera there (terrain-local), kept
	// (milestone 71); then update() is not needed this frame.
	bool keep(const Vector3 &p_camera, float p_lod_bias);
	// A frame: the pages p_view wants, and the missing or stale ones to bake now (within the budget every terrain
	// shares). Their slots are this terrain's from now on and the page table says so (upload_page_table()).
	void update(const View &p_view, float p_lod_bias, LocalVector<Job> &r_jobs);
	// Pages over a terrain-local rectangle (metres) or all of them: baked again when next wanted, kept meanwhile.
	void invalidate(const Rect2 &p_local);
	void invalidate_all();
	// The page table's changed texels sent to its texture.
	void upload_page_table();
	Ref<ImageTexture> get_page_table() const { return table_texture; }
	// The shader's parameters: (texels per metre, mips, 1 / the atlas's texels, -), and the pages along x and z at mip 0.
	Vector4 get_shader_params() const;
	Ref<Texture2D> get_overview_albedo() const;
	Ref<Texture2D> get_overview_normal() const;
	bool is_overview_due() const { return overview_due; }
	uint64_t get_overview_baked_count() const { return overview_baked; }

	int get_resident_count() const { return resident.size(); }
	int get_wanted_count() const { return last_wanted; }
	int get_missing_count() const { return last_missing; }
	uint64_t get_baked_count() const { return baked; }
	uint64_t get_visit_count() const { return visits; }
	uint64_t get_update_count() const { return updates; }

	// What a terrain's pages are baked from: its RenderingServer textures and its layers (FellowTerrain3D's shader
	// parameters, the same arithmetic).
	static constexpr int LAYERS = 9;
	struct BakeParams {
		RID heightmap;
		RID normalmap;
		RID splat[2];
		RID albedo[LAYERS];
		RID normal[LAYERS];
		Vector4 layer_color[LAYERS];
		Vector4 layer_params[LAYERS];
		Vector4 layer_flags[LAYERS];
		Vector2 map_texels;
		Vector2 world_offset;
		float spacing = 1.0f;
		float texels_per_metre = 32.0f;
		float height_blend = 0.3f;
		float macro_variation = 0.15f;
	};
	// This frame's jobs (and the overview's levels when update() made it due) dispatched on the render thread before the
	// frame is drawn.
	void bake(const LocalVector<Job> &p_jobs, const BakeParams &p_params);

	// The shared atlas: made when the first terrain needs it (its size from the project setting
	// rendering/fellow/terrain/virtual_texture_atlas_size), freed with the last.
	static void add_user();
	static void remove_user();
	static Ref<Texture2D> get_atlas_albedo();
	static Ref<Texture2D> get_atlas_normal();
	static int get_atlas_slots() { return slots_per_side; }
	static int get_atlas_texels() { return atlas_texels; }
	static Dictionary get_cache_stats();
};
