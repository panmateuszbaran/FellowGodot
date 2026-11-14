/**************************************************************************/
/*  fellow_acoustics_3d.h                                                 */
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

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "servers/audio/effects/audio_effect_reverb.h"

class AudioStreamPlayer3D;
class PhysicsDirectSpaceState3D;

// FellowEngine (milestone 78, decision 0186): sound through the scene's geometry. Every AudioStreamPlayer3D in the
// tree (but those in the group "fellow_no_acoustics") is heard through what lies between it and the listener: rays
// from the listener to the source and to points around it on the physics world give the share of its paths that are
// blocked, which lowers the direct sound and cuts its highs (the player's own gain and high shelf, through a hook in
// AudioStreamPlayer3D); rays from the listener in every direction measure the room it is in (how closed, how large),
// which sets a reverb (its decay from the room's size and absorption, its predelay from the nearest walls) that every
// source is sent to, more in a closed room. One works at a time (the last to enter the tree); where it is does not
// matter. Milestone 111 (decision 0218): with source_rooms, a source in another room than the listener's (measured the
// same way from it) is sent to a reverb of its own room's (a few more buses, shared by sources in alike rooms).
class FellowAcoustics3D : public Node3D {
	GDCLASS(FellowAcoustics3D, Node3D);

	// A room as rays measure it: how closed over and round the point (0-1), the mean distance to what they met, the
	// nearest wall.
	struct Room {
		float enclosure = 0.0f;
		float distance = 0.0f;
		float nearest = 0.0f;
	};

	struct Source {
		ObjectID id;
		float occlusion = 0.0f; // smoothed, 0 open to 1 blocked
		float target = 0.0f;
		float occlusion_highs = 0.0f; // milestone 92: the highs' (the shelf and the cutoff), by the materials'
		float target_highs = 0.0f;
		float distance = 0.0f;
		bool audible = false;
		bool tested = false;
		// Milestone 99 (decision 0207): a path through an opening, heard from its direction at its length.
		float portal_target = 0.0f; // 1 while the path through a portal is the one heard
		float portal_weight = 0.0f; // smoothed
		Vector3 portal_point; // the apparent position (from the listener towards the opening, at the path's length)
		Vector3 apparent;
		// Milestone 111 (decision 0218): the source's own room, and the reverb it is sent to (-1: the listener's).
		Room room;
		bool room_measured = false;
		int room_bus = -1;
		float send = 0.0f; // the send given last
	};

	// Milestone 111: a reverb for sources in rooms other than the listener's.
	struct RoomBus {
		StringName bus;
		Ref<AudioEffectReverb> effect;
		bool created_bus = false;
		bool added_effect = false;
		Room target; // the mean of its sources' rooms
		Room room; // smoothed
		bool measured = false;
		int sources = 0; // this frame
	};

	struct GlobalStats {
		uint64_t frames = 0;
		uint64_t usec = 0;
		uint64_t rays = 0;
		uint64_t tests = 0;
	};

	static FellowAcoustics3D *current;
	static GlobalStats global_stats;

	bool enabled = true;
	NodePath listener_path;
	uint32_t collision_mask = 1;
	int occlusion_rays = 5;
	float source_radius = 0.5f;
	float occluded_volume_db = -12.0f;
	float occluded_shelf_db = -18.0f;
	float occluded_cutoff_hz = 1000.0f;
	float max_distance = 80.0f;
	int max_rays_per_frame = 512;
	float response_time = 0.12f;
	bool reverb = true;
	StringName reverb_bus = StringName("FellowReverb");
	int reverb_rays = 32;
	float reverb_interval = 0.25f;
	float reverb_max_distance = 40.0f;
	float absorption = 0.3f;
	float reverb_level = 0.5f;
	float reverb_response = 0.5f;
	bool portals = true;
	float portal_diffraction_db = -6.0f;
	int max_portals = 2;
	int source_rooms = 0; // milestone 111: the reverbs for other rooms (0: every source sent to the listener's)
	LocalVector<RoomBus> room_buses;
	uint32_t room_cursor = 0;
	int last_room_measures = 0;

	HashMap<ObjectID, Source> sources;
	LocalVector<ObjectID> order;
	uint32_t cursor = 0;

	// The listener's room: measured (target) and smoothed.
	float room_enclosure = 0.0f;
	float room_distance = 0.0f;
	float room_nearest = 0.0f;
	float target_enclosure = 0.0f;
	float target_distance = 0.0f;
	float target_nearest = 0.0f;
	bool room_measured = false;
	float reverb_timer = 0.0f;
	Ref<AudioEffectReverb> effect;
	bool created_bus = false;
	bool added_effect = false;

	int last_rays = 0;
	int last_tests = 0;
	int last_portal_paths = 0;
	uint64_t last_usec = 0;

	void _node_added(Node *p_node);
	void _node_removed(Node *p_node);
	void _track(Node *p_node);
	void _setup_reverb();
	void _release_reverb();
	void _reset_sources();
	Node3D *_listener() const;
	void _step(float p_delta);
	void _measure_room(const Vector3 &p_at, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_exclude);
	Room _room_at(const Vector3 &p_at, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_exclude);
	float _rt60() const;
	float _rt60_for(float p_distance) const;
	void _drive_reverb(const Ref<AudioEffectReverb> &p_effect, const Room &p_room) const;
	static bool _same_room(const Room &p_a, const Room &p_b);
	void _setup_room_buses();
	void _release_room_buses();
	void _source_rooms(float p_delta, const Room &p_listener, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_listener_exclude, int &r_budget);
	void _portal_path(Source &r_source, const Vector3 &p_at, const Vector3 &p_pos, float p_range, const HashSet<RID> &p_exclude, PhysicsDirectSpaceState3D *p_space, const World3D *p_world, int &r_budget);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void set_listener_path(const NodePath &p_path) { listener_path = p_path; }
	NodePath get_listener_path() const { return listener_path; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_occlusion_rays(int p_rays) { occlusion_rays = CLAMP(p_rays, 1, 16); }
	int get_occlusion_rays() const { return occlusion_rays; }
	void set_source_radius(float p_radius) { source_radius = MAX(0.0f, p_radius); }
	float get_source_radius() const { return source_radius; }
	void set_occluded_volume_db(float p_db) { occluded_volume_db = MIN(0.0f, p_db); }
	float get_occluded_volume_db() const { return occluded_volume_db; }
	void set_occluded_shelf_db(float p_db) { occluded_shelf_db = MIN(0.0f, p_db); }
	float get_occluded_shelf_db() const { return occluded_shelf_db; }
	void set_occluded_cutoff_hz(float p_hz) { occluded_cutoff_hz = CLAMP(p_hz, 20.0f, 20000.0f); }
	float get_occluded_cutoff_hz() const { return occluded_cutoff_hz; }
	void set_max_distance(float p_distance) { max_distance = MAX(0.0f, p_distance); }
	float get_max_distance() const { return max_distance; }
	void set_max_rays_per_frame(int p_rays) { max_rays_per_frame = MAX(1, p_rays); }
	int get_max_rays_per_frame() const { return max_rays_per_frame; }
	void set_response_time(float p_time) { response_time = MAX(0.0f, p_time); }
	float get_response_time() const { return response_time; }
	void set_reverb(bool p_enabled);
	bool has_reverb() const { return reverb; }
	void set_reverb_bus(const StringName &p_bus);
	StringName get_reverb_bus() const { return reverb_bus; }
	void set_reverb_rays(int p_rays) { reverb_rays = CLAMP(p_rays, 4, 256); }
	int get_reverb_rays() const { return reverb_rays; }
	void set_reverb_interval(float p_interval) { reverb_interval = MAX(0.0f, p_interval); }
	float get_reverb_interval() const { return reverb_interval; }
	void set_reverb_max_distance(float p_distance) { reverb_max_distance = MAX(1.0f, p_distance); }
	float get_reverb_max_distance() const { return reverb_max_distance; }
	void set_absorption(float p_absorption) { absorption = CLAMP(p_absorption, 0.01f, 1.0f); }
	float get_absorption() const { return absorption; }
	void set_reverb_level(float p_level) { reverb_level = CLAMP(p_level, 0.0f, 1.0f); }
	float get_reverb_level() const { return reverb_level; }
	void set_reverb_response(float p_time) { reverb_response = MAX(0.0f, p_time); }
	float get_reverb_response() const { return reverb_response; }

	void set_portals(bool p_enabled) { portals = p_enabled; }
	bool has_portals() const { return portals; }
	void set_portal_diffraction_db(float p_db) { portal_diffraction_db = CLAMP(p_db, -40.0f, 0.0f); }
	float get_portal_diffraction_db() const { return portal_diffraction_db; }
	void set_max_portals(int p_count) { max_portals = CLAMP(p_count, 1, 16); }
	int get_max_portals() const { return max_portals; }
	void set_source_rooms(int p_count);
	int get_source_rooms() const { return source_rooms; }
	Dictionary get_source_room(Node *p_player) const;
	float get_portal_weight(Node *p_player) const;
	Vector3 get_heard_position(Node *p_player) const;
	static FellowAcoustics3D *get_current() { return current; }

	float get_occlusion(Node *p_player) const;
	float get_occlusion_highs(Node *p_player) const;
	Dictionary get_room() const;
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();

	PackedStringArray get_configuration_warnings() const override;

	FellowAcoustics3D();
	~FellowAcoustics3D();
};
