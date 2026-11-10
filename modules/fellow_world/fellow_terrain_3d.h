/**************************************************************************/
/*  fellow_terrain_3d.h                                                   */
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

#include "core/io/image.h"
#include "core/io/resource.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class Camera3D;
class FellowImpostorBaker;

// One material of a FellowTerrain3D: its textures and how they tile (docs/decisions/0158-terrain.md).
class FellowTerrainLayer : public Resource {
	GDCLASS(FellowTerrainLayer, Resource);

	Ref<Texture2D> albedo_texture;
	Color albedo_color = Color(1, 1, 1);
	bool height_in_albedo_alpha = false;
	Ref<Texture2D> normal_texture;
	float normal_strength = 1.0f;
	float roughness = 0.9f;
	float texture_size = 4.0f; // metres a texture repeat covers
	bool triplanar = false;

protected:
	static void _bind_methods();

public:
	void set_albedo_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_albedo_texture() const { return albedo_texture; }
	void set_albedo_color(const Color &p_color);
	Color get_albedo_color() const { return albedo_color; }
	void set_height_in_albedo_alpha(bool p_enable);
	bool is_height_in_albedo_alpha() const { return height_in_albedo_alpha; }
	void set_normal_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_normal_texture() const { return normal_texture; }
	void set_normal_strength(float p_strength);
	float get_normal_strength() const { return normal_strength; }
	void set_roughness(float p_roughness);
	float get_roughness() const { return roughness; }
	void set_texture_size(float p_metres);
	float get_texture_size() const { return texture_size; }
	void set_triplanar(bool p_enable);
	bool is_triplanar() const { return triplanar; }
};

// A kind of plant or rock a FellowTerrain3D scatters over one of its layers (docs/decisions/0159-terrain-foliage.md).
class FellowTerrainFoliage : public Resource {
	GDCLASS(FellowTerrainFoliage, Resource);

	Ref<Mesh> mesh;
	int layer = 0;
	float density = 1.0f; // instances per square metre where the layer's weight is 1
	float min_weight = 0.3f;
	float range = 100.0f;
	float cell_size = 0.0f; // 0: a quarter of the range, 8-256 m
	float slope_min = 0.0f; // degrees
	float slope_max = 90.0f;
	float height_min = -100000.0f;
	float height_max = 100000.0f;
	float scale_min = 0.8f;
	float scale_max = 1.2f;
	bool random_yaw = true;
	float align_to_normal = 0.0f;
	float sink = 0.0f; // metres below the ground (at scale 1)
	bool cast_shadows = true;
	int seed = 0;
	float fade_distance = 0.0f; // metres before the range's end over which the instances shrink out (decision 0172)
	Ref<Mesh> impostor; // a baked impostor (FellowTerrain3D.bake_foliage_impostors()), drawn from impostor_distance on
	float impostor_distance = 0.0f;

protected:
	static void _bind_methods();

public:
	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const { return mesh; }
	void set_layer(int p_layer);
	int get_layer() const { return layer; }
	void set_density(float p_density);
	float get_density() const { return density; }
	void set_min_weight(float p_weight);
	float get_min_weight() const { return min_weight; }
	void set_range(float p_range);
	float get_range() const { return range; }
	void set_cell_size(float p_size);
	float get_cell_size() const { return cell_size; }
	float get_effective_cell_size() const { return cell_size > 0.0f ? cell_size : CLAMP(range * 0.25f, 8.0f, 256.0f); }
	void set_slope_min(float p_degrees);
	float get_slope_min() const { return slope_min; }
	void set_slope_max(float p_degrees);
	float get_slope_max() const { return slope_max; }
	void set_height_min(float p_height);
	float get_height_min() const { return height_min; }
	void set_height_max(float p_height);
	float get_height_max() const { return height_max; }
	void set_scale_min(float p_scale);
	float get_scale_min() const { return scale_min; }
	void set_scale_max(float p_scale);
	float get_scale_max() const { return scale_max; }
	void set_random_yaw(bool p_enable);
	bool is_random_yaw() const { return random_yaw; }
	void set_align_to_normal(float p_amount);
	float get_align_to_normal() const { return align_to_normal; }
	void set_sink(float p_metres);
	float get_sink() const { return sink; }
	void set_cast_shadows(bool p_enable);
	bool is_casting_shadows() const { return cast_shadows; }
	void set_seed(int p_seed);
	int get_seed() const { return seed; }
	void set_fade_distance(float p_metres);
	float get_fade_distance() const { return fade_distance; }
	void set_impostor(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_impostor() const { return impostor; }
	void set_impostor_distance(float p_metres);
	float get_impostor_distance() const { return impostor_distance; }
};

// FellowEngine terrain (docs/decisions/0158-terrain.md): a height field drawn with continuous distance-dependent levels
// of detail (CDLOD: a quadtree of square patches whose vertices morph to the next coarser grid before it takes over, so
// neighbouring levels meet without cracks or popping), up to 9 materials blended by two splat maps and their heights,
// collision through height-map shapes, and height, normal and material queries for gameplay.
//
// The heights are an Image (FORMAT_RF, metres; one texel a vertex, `vertex_spacing` metres apart). The patches are the
// instances of one MultiMesh (one draw a pass; FellowEngine's MultiMesh culling culls them one by one): written into a
// second MultiMesh, hidden, when the selection changes and shown three frames later, so that the motion vectors of a
// patch always come from its own transforms (TAA and FSR see a static ground).
class FellowTerrain3D : public Node3D {
	GDCLASS(FellowTerrain3D, Node3D);

public:
	static void fellow_bake_shader_codes(Vector<String> &r_codes); // milestone 61: the terrain's code
	static constexpr int MAX_LAYERS = 9;
	static constexpr int MAX_LEVELS = 16;

	enum SculptMode {
		SCULPT_RAISE, // up by the strength in metres at the brush's centre
		SCULPT_LOWER,
		SCULPT_SMOOTH, // towards the neighbours' mean, by the strength (0..1)
		SCULPT_FLATTEN, // towards the target height, by the strength (0..1)
	};

	enum DebugView {
		DEBUG_VIEW_OFF,
		DEBUG_VIEW_LEVELS, // the ground coloured by its level of detail, blended as it morphs to the next
	};

private:
	Ref<Image> heightmap;
	Ref<Image> splat_map;
	Ref<Image> splat_map_2;
	Ref<Image> hole_map; // decision 0172: a texel above half is a hole at that vertex (the triangles touching it)
	TypedArray<FellowTerrainLayer> layers;
	float vertex_spacing = 1.0f;
	int patch_size = 16; // quads along a patch's side
	float lod_distance = 0.0f; // the finest level's range (0: three times its node's size)
	float morph_ratio = 0.7f;
	float height_blend = 0.3f;
	float macro_variation = 0.15f;
	bool cast_shadows = true;
	uint32_t render_layer_mask = 1;
	DebugView debug_view = DEBUG_VIEW_OFF;
	bool collision_enabled = true;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	int collision_region_size = 256;

	// CPU copies.
	int map_w = 0;
	int map_h = 0;
	LocalVector<float> heights; // map_w * map_h
	float height_min = 0.0f;
	float height_max = 0.0f;
	LocalVector<LocalVector<Vector2>> minmax; // per block level k (blocks of patch_size << k texels): (min, max)
	LocalVector<Vector2i> minmax_size;
	int top_level = 0;
	// Holes (decision 0172): per vertex (empty without a hole map), and per block level as minmax: 1 where a block's
	// triangles touch a hole (its patches are drawn with the material that discards them).
	LocalVector<uint8_t> holes;
	LocalVector<LocalVector<uint8_t>> hole_blocks;
	int hole_count = 0;
	float ranges[MAX_LEVELS] = {};

	// Rendering.
	Ref<ImageTexture> height_texture;
	Ref<Image> height_image; // the textures' images (with their mips), kept for edits of a region
	Ref<Image> normal_image;
	Ref<Image> splat_image[2];
	Rect2i edit_heights; // edited since the frame's start: textures and collision shapes to update at its end
	bool edit_splat[2] = {};
	Rect2i edit_splat_rect[2]; // the splat maps' texels edited since the frame's start
	bool edit_flush_queued = false;
	uint64_t edit_flush_usec = 0; // the last update at a frame's end
	Ref<ImageTexture> normal_texture;
	Ref<ImageTexture> splat_texture;
	Ref<ImageTexture> splat_texture_2;
	Ref<ArrayMesh> patch_mesh;
	Ref<ShaderMaterial> material;
	Ref<ShaderMaterial> hole_material; // the same with holes discarded, for the patches over them
	Ref<Image> hole_image;
	Ref<ImageTexture> hole_texture;
	Rect2i edit_holes; // the hole map's texels edited since the frame's start
	bool data_dirty = true;
	bool material_dirty = true;

	struct Patch {
		int32_t x = 0; // block coordinates at the patch's level
		int32_t z = 0;
		int32_t level = 0;
		bool operator==(const Patch &p_other) const { return x == p_other.x && z == p_other.z && level == p_other.level; }
	};
	// The patches: two MultiMeshes, one shown, the other written when the selection changes and shown three drawn
	// frames later.
	struct LodSet {
		RID multimesh[2];
		RID instance[2];
		int capacity[2] = { 0, 0 };
		// The patches over holes, drawn with hole_material (made with the hole map).
		RID hole_multimesh[2];
		RID hole_instance[2];
		int hole_capacity[2] = { 0, 0 };
		int hole_patches[2] = { 0, 0 };
		int shown = -1;
		int pending = -1;
		uint64_t pending_frame = 0;
		Vector3 selected_camera;
		bool selection_valid = false;
		LocalVector<Patch> patches[2];
	};
	LodSet set;
	LocalVector<Patch> scratch;
	LocalVector<float> buffer;

	// Collision.
	RID body;
	LocalVector<RID> shapes;

	// Foliage (decision 0159): per kind, cells around the camera, each a MultiMesh made once on a worker thread.
	TypedArray<FellowTerrainFoliage> foliage;
	int foliage_cells_per_frame = 8;
	struct FoliageKind {
		RID mesh;
		int layer = 0;
		float density = 1.0f;
		float min_weight = 0.3f;
		float range = 100.0f;
		float cell = 25.0f;
		float cos_slope_min = 1.0f; // the normal's y at the smallest slope
		float cos_slope_max = 0.0f;
		float height_min = -100000.0f;
		float height_max = 100000.0f;
		float scale_min = 0.8f;
		float scale_max = 1.2f;
		bool random_yaw = true;
		float align = 0.0f;
		float sink = 0.0f;
		bool shadows = true;
		uint32_t seed = 0;
		float fade = 0.0f;
		RID impostor; // the impostor's quad mesh (its range from impostor_distance)
		float impostor_distance = 0.0f;
	};
	LocalVector<FoliageKind> foliage_kinds;
	struct FoliageCell {
		int kind = 0;
		int cx = 0;
		int cz = 0;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		Vector<float> buffer; // written by the task
		int count = 0;
		bool ready = false;
		uint64_t seen = 0;
		RID multimesh;
		RID instance;
		RID impostor_multimesh;
		RID impostor_instance;
		// What its task reads, copied for it on the main thread when it starts (decision 0160): the terrain's edits and
		// rebuilds never race it, and nothing waits for it.
		FoliageKind k;
		int map_w = 0;
		int map_h = 0;
		float spacing = 1.0f;
		Rect2i heights_rect; // in the heightmap's texels
		LocalVector<float> heights; // NaN at holes
		Vector2i splat_size[2];
		Rect2i splat_rect[2]; // in the splat maps' texels
		LocalVector<uint8_t> splat[2];
	};
	HashMap<uint64_t, FoliageCell *> foliage_cells;
	// Cells dropped while their task was still to run: freed once it has (the main thread never waits for one: a
	// wait may run other queued work on it, a pipeline's compilation among them).
	LocalVector<FoliageCell *> foliage_retired;
	// A hidden instance of each kind's mesh from the start: its pipelines compile when the terrain is set up, not when
	// the kind's first cell appears.
	LocalVector<RID> foliage_warm;
	LocalVector<uint8_t> splat_cpu[2]; // RGBA8 copies of the splat maps (the queries; the foliage cells' copies)
	Vector2i splat_cpu_size[2];
	bool foliage_dirty = true;
	uint64_t foliage_frame = 0;
	int foliage_instances = 0;
	int foliage_impostor_instances = 0;

	// Baking the foliage's impostors (decision 0172): one kind after another, saved as resources.
	FellowImpostorBaker *impostor_baker = nullptr;
	int impostor_bakes_pending = 0;
	PackedStringArray impostor_bake_paths;
	void _foliage_impostor_baked(const Ref<ArrayMesh> &p_quad, int p_kind, const String &p_path);

	void _foliage_changed();
	void _update_foliage_kinds();
	void _clear_foliage(bool p_wait);
	void _copy_foliage_data(FoliageCell *p_cell) const;
	void _process_foliage(const Vector3 &p_camera);
	void _make_foliage_cell_visible(FoliageCell *p_cell);
	void _free_foliage_cell(FoliageCell *p_cell);
	static void _generate_foliage_cell(void *p_cell);
	static float _cell_height(const FoliageCell &p_cell, float p_x, float p_z);
	static float _cell_weight(const FoliageCell &p_cell, int p_layer, float p_x, float p_z);
	float _layer_weight_local(int p_layer, float p_x, float p_z) const;

	// Statistics.
	uint64_t selections = 0;
	uint64_t swaps = 0;
	uint64_t last_select_usec = 0;
	uint64_t process_usec_total = 0; // the main thread's time in the terrain's frames (levels and foliage)
	uint64_t process_frames = 0;
	uint64_t process_usec_max = 0;
	uint64_t process_usec_last = 0;

	void _rebuild_data();
	void _build_splat(int p_map);
	void _build_holes();
	void _build_hole_blocks();
	void _update_hole_blocks(const Rect2i &p_rect);
	void _update_holes_region(const Rect2i &p_rect);
	bool _make_hole_map_editable();
	void _holes_changed(const Rect2i &p_rect, bool p_rebuilt);
	bool _patch_has_holes(const Patch &p_patch) const;
	void _create_hole_instances();
	void _free_hole_instances();
	bool _is_hole_triangle(float p_fx, float p_fz) const;
	void _set_material_parameter(const StringName &p_name, const Variant &p_value);
	void _update_heights_region(const Rect2i &p_rect);
	void _update_splat_region(int p_map, const Rect2i &p_rect);
	void _drop_foliage_in(const Rect2 &p_local);
	void _make_heightmap_editable();
	void _queue_edit_flush();
	void _flush_edits();
	void _upload_texture(const Ref<ImageTexture> &p_texture, const Ref<Image> &p_image, const Rect2i &p_rect);
	Rect2i _brush_texels(const Vector3 &p_global, float p_radius, Vector2 &r_centre, float &r_radius) const;
	void _ensure_data() const;
	void _build_minmax();
	void _build_normals(Ref<Image> &r_normals) const;
	void _build_mesh();
	void _update_material();
	void _update_collision();
	Dictionary _collision_region_data(int p_x0, int p_z0, int p_w, int p_d) const;
	void _free_collision();
	void _create_instances();
	void _free_instances();
	void _update_instance_settings();
	void _update_transforms();
	void _process_lod();
	Camera3D *_find_camera() const;
	bool _select_node(int p_level, int p_x, int p_z, const Vector3 &p_camera, LocalVector<Patch> &r_out) const;
	void _select(const Vector3 &p_camera, LocalVector<Patch> &r_out) const;
	void _write_patches(int p_which, const LocalVector<Patch> &p_patches, const Vector3 &p_camera);
	AABB _node_box(int p_level, int p_x, int p_z) const;
	void _layer_changed();
	float _height_local(float p_x, float p_z) const;
	float _node_size(int p_level) const { return float(patch_size << (p_level + 1)) * vertex_spacing; }

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_heightmap(const Ref<Image> &p_image);
	Ref<Image> get_heightmap() const { return heightmap; }
	void set_splat_map(const Ref<Image> &p_image);
	Ref<Image> get_splat_map() const { return splat_map; }
	void set_splat_map_2(const Ref<Image> &p_image);
	Ref<Image> get_splat_map_2() const { return splat_map_2; }
	void set_hole_map(const Ref<Image> &p_image);
	Ref<Image> get_hole_map() const { return hole_map; }
	void set_layers(const TypedArray<FellowTerrainLayer> &p_layers);
	TypedArray<FellowTerrainLayer> get_layers() const { return layers; }
	void set_vertex_spacing(float p_spacing);
	float get_vertex_spacing() const { return vertex_spacing; }
	void set_patch_size(int p_size);
	int get_patch_size() const { return patch_size; }
	void set_lod_distance(float p_distance);
	float get_lod_distance() const { return lod_distance; }
	void set_morph_ratio(float p_ratio);
	float get_morph_ratio() const { return morph_ratio; }
	void set_height_blend(float p_blend);
	float get_height_blend() const { return height_blend; }
	void set_macro_variation(float p_variation);
	float get_macro_variation() const { return macro_variation; }
	void set_debug_view(DebugView p_view);
	DebugView get_debug_view() const { return debug_view; }
	void set_cast_shadows(bool p_enable);
	bool is_casting_shadows() const { return cast_shadows; }
	void set_render_layer_mask(uint32_t p_mask);
	uint32_t get_render_layer_mask() const { return render_layer_mask; }
	void set_collision_enabled(bool p_enable);
	bool is_collision_enabled() const { return collision_enabled; }
	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const { return collision_layer; }
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_collision_region_size(int p_size);
	int get_collision_region_size() const { return collision_region_size; }
	void set_foliage(const TypedArray<FellowTerrainFoliage> &p_foliage);
	TypedArray<FellowTerrainFoliage> get_foliage() const { return foliage; }
	void set_foliage_cells_per_frame(int p_cells);
	int get_foliage_cells_per_frame() const { return foliage_cells_per_frame; }

	void update_heightmap();
	void sculpt(const Vector3 &p_global, float p_radius, float p_strength, SculptMode p_mode, float p_target_height = 0.0f);
	void paint(const Vector3 &p_global, float p_radius, int p_layer, float p_strength);
	void blit_heightmap_region(const Ref<Image> &p_region, const Vector2i &p_position);
	void blit_splat_region(int p_map, const Ref<Image> &p_region, const Vector2i &p_position);
	void paint_holes(const Vector3 &p_global, float p_radius, bool p_hole);
	void blit_hole_region(const Ref<Image> &p_region, const Vector2i &p_position);
	bool is_hole(const Vector3 &p_global) const;
	int bake_foliage_impostors(const String &p_directory, int p_frames = 8, int p_resolution = 128);
	bool is_baking_impostors() const { return impostor_bakes_pending > 0; }
	float get_height(const Vector3 &p_global) const;
	Vector3 get_normal(const Vector3 &p_global) const;
	PackedFloat32Array get_layer_weights(const Vector3 &p_global) const;
	AABB get_aabb() const;
	Ref<ShaderMaterial> get_material() const { return material; }
	Dictionary get_stats() const;

	static Ref<Image> heightmap_from_image(const Ref<Image> &p_image, float p_min_height, float p_max_height);

	PackedStringArray get_configuration_warnings() const override;

	FellowTerrain3D();
	~FellowTerrain3D();
};

VARIANT_ENUM_CAST(FellowTerrain3D::DebugView);
VARIANT_ENUM_CAST(FellowTerrain3D::SculptMode);
