/**************************************************************************/
/*  fellow_acoustics_3d.cpp                                               */
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

#include "fellow_acoustics_3d.h"

#include "fellow_acoustic_portal_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/audio/audio_server.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {

const float SPEED_OF_SOUND = 343.0f;
const float COMB_DELAY = 0.031f; // the reverb's combs' mean delay (s): its decay from their feedback
const char *SKIP_GROUP = "fellow_no_acoustics";

// The physics bodies a node sits in (its ancestors'): the rays from and to it pass through them.
void exclude_bodies(const Node *p_node, HashSet<RID> &r_exclude) {
	for (const Node *n = p_node; n; n = n->get_parent()) {
		if (const CollisionObject3D *co = Object::cast_to<CollisionObject3D>(n)) {
			r_exclude.insert(co->get_rid());
		}
	}
}

// Directions spread evenly over the sphere (a Fibonacci lattice).
Vector3 sphere_direction(int p_i, int p_n) {
	const float golden = float(Math::PI) * (3.0f - Math::sqrt(5.0f));
	const float y = 1.0f - (float(p_i) + 0.5f) * 2.0f / float(p_n);
	const float r = Math::sqrt(MAX(0.0f, 1.0f - y * y));
	const float a = golden * float(p_i);
	return Vector3(Math::cos(a) * r, y, Math::sin(a) * r);
}

} // namespace

FellowAcoustics3D *FellowAcoustics3D::current = nullptr;
FellowAcoustics3D::GlobalStats FellowAcoustics3D::global_stats;

void FellowAcoustics3D::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	if (!enabled) {
		_reset_sources();
	}
}

void FellowAcoustics3D::set_reverb(bool p_enabled) {
	if (reverb == p_enabled) {
		return;
	}
	reverb = p_enabled;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint() && current == this) {
		if (reverb) {
			_setup_reverb();
		} else {
			_release_reverb();
		}
	}
}

void FellowAcoustics3D::set_reverb_bus(const StringName &p_bus) {
	if (reverb_bus == p_bus) {
		return;
	}
	const bool live = is_inside_tree() && !Engine::get_singleton()->is_editor_hint() && current == this && reverb;
	if (live) {
		_release_reverb();
	}
	reverb_bus = p_bus;
	if (live) {
		_setup_reverb();
	}
}

void FellowAcoustics3D::_track(Node *p_node) {
	AudioStreamPlayer3D *player = Object::cast_to<AudioStreamPlayer3D>(p_node);
	if (!player || player->is_in_group(SKIP_GROUP)) {
		return;
	}
	const ObjectID id = player->get_instance_id();
	if (sources.has(id)) {
		return;
	}
	Source s;
	s.id = id;
	sources.insert(id, s);
	order.push_back(id);
}

void FellowAcoustics3D::_node_added(Node *p_node) {
	_track(p_node);
}

void FellowAcoustics3D::_node_removed(Node *p_node) {
	if (!Object::cast_to<AudioStreamPlayer3D>(p_node)) {
		return;
	}
	const ObjectID id = p_node->get_instance_id();
	if (sources.erase(id)) {
		order.erase(id);
	}
}

// The reverb the sources are sent to: the bus named reverb_bus (made, sending to Master, when there is none) and its
// first AudioEffectReverb (added, wet only, when it has none). What this node made it removes when it leaves.
void FellowAcoustics3D::_setup_reverb() {
	AudioServer *as = AudioServer::get_singleton();
	if (!as || reverb_bus == StringName() || effect.is_valid()) {
		return;
	}
	int idx = as->get_bus_index(reverb_bus);
	if (idx < 0) {
		as->add_bus();
		idx = as->get_bus_count() - 1;
		as->set_bus_name(idx, reverb_bus);
		as->set_bus_send(idx, SNAME("Master"));
		created_bus = true;
	}
	for (int i = 0; i < as->get_bus_effect_count(idx); i++) {
		Ref<AudioEffectReverb> r = as->get_bus_effect(idx, i);
		if (r.is_valid()) {
			effect = r;
			break;
		}
	}
	if (effect.is_null()) {
		effect.instantiate();
		effect->set_dry(0.0f);
		effect->set_wet(1.0f);
		as->add_bus_effect(idx, effect);
		added_effect = true;
	}
	room_measured = false;
	reverb_timer = 0.0f;
	_setup_room_buses(); // milestone 111
}

void FellowAcoustics3D::_release_reverb() {
	AudioServer *as = AudioServer::get_singleton();
	// The sends first: a send to a bus that is gone would play on Master.
	for (KeyValue<ObjectID, Source> &kv : sources) {
		if (AudioStreamPlayer3D *p = ObjectDB::get_instance<AudioStreamPlayer3D>(kv.key)) {
			AudioStreamPlayer3D::FellowAcoustics a = p->fellow_get_acoustics();
			a.send = 0.0f;
			a.send_bus = StringName();
			p->fellow_set_acoustics(a);
		}
		kv.value.room_bus = -1;
	}
	_release_room_buses(); // milestone 111
	if (as && effect.is_valid()) {
		const int idx = as->get_bus_index(reverb_bus);
		if (idx >= 0) {
			if (created_bus) {
				as->remove_bus(idx);
			} else if (added_effect) {
				for (int i = 0; i < as->get_bus_effect_count(idx); i++) {
					if (as->get_bus_effect(idx, i) == effect) {
						as->remove_bus_effect(idx, i);
						break;
					}
				}
			}
		}
	}
	effect.unref();
	created_bus = false;
	added_effect = false;
}

void FellowAcoustics3D::_reset_sources() {
	for (KeyValue<ObjectID, Source> &kv : sources) {
		if (AudioStreamPlayer3D *p = ObjectDB::get_instance<AudioStreamPlayer3D>(kv.key)) {
			p->fellow_set_acoustics(AudioStreamPlayer3D::FellowAcoustics());
		}
		kv.value.occlusion = 0.0f;
		kv.value.target = 0.0f;
		kv.value.occlusion_highs = 0.0f;
		kv.value.target_highs = 0.0f;
		kv.value.portal_target = 0.0f;
		kv.value.portal_weight = 0.0f;
		kv.value.tested = false;
	}
}

Node3D *FellowAcoustics3D::_listener() const {
	if (!listener_path.is_empty()) {
		return Object::cast_to<Node3D>(get_node_or_null(listener_path));
	}
	Viewport *vp = get_viewport();
	if (!vp) {
		return nullptr;
	}
	if (AudioListener3D *l = vp->get_audio_listener_3d()) {
		return l;
	}
	return vp->get_camera_3d();
}

// The reverb's decay from the room (Sabine's formula with the mean free path, 4 V / S, taken as the mean distance to
// the walls the rays found): RT60 = 0.161 V / (S a) = 0.04 l / a.
float FellowAcoustics3D::_rt60() const {
	return _rt60_for(room_distance);
}

float FellowAcoustics3D::_rt60_for(float p_distance) const {
	return CLAMP(0.04f * p_distance / absorption, 0.1f, 10.0f);
}

// Godot's reverb (Freeverb's combs) driven by a room: the feedback that decays in its RT60, from 0.7 (room size 0) to
// 0.98 (1); the predelay from the nearest wall; the damping from the absorption.
void FellowAcoustics3D::_drive_reverb(const Ref<AudioEffectReverb> &p_effect, const Room &p_room) const {
	const float g = Math::pow(10.0f, -3.0f * COMB_DELAY / _rt60_for(p_room.distance));
	p_effect->set_room_size(CLAMP((g - 0.7f) / 0.28f, 0.0f, 1.0f));
	p_effect->set_predelay_msec(CLAMP(2.0f * p_room.nearest / SPEED_OF_SOUND * 1000.0f, 1.0f, 150.0f));
	p_effect->set_damping(CLAMP(0.2f + absorption, 0.0f, 1.0f));
}

void FellowAcoustics3D::_measure_room(const Vector3 &p_at, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_exclude) {
	const Room r = _room_at(p_at, p_space, p_exclude);
	target_enclosure = r.enclosure;
	target_distance = r.distance;
	target_nearest = r.nearest;
	if (!room_measured) {
		room_enclosure = target_enclosure;
		room_distance = target_distance;
		room_nearest = target_nearest;
		room_measured = true;
	}
}

FellowAcoustics3D::Room FellowAcoustics3D::_room_at(const Vector3 &p_at, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_exclude) {
	int around = 0;
	int closed = 0;
	int hits = 0;
	float sum = 0.0f;
	float nearest = reverb_max_distance;
	for (int i = 0; i < reverb_rays; i++) {
		const Vector3 dir = sphere_direction(i, reverb_rays);
		PhysicsDirectSpaceState3D::RayParameters rq;
		rq.from = p_at;
		rq.to = p_at + dir * reverb_max_distance;
		rq.exclude = p_exclude;
		rq.collision_mask = collision_mask;
		PhysicsDirectSpaceState3D::RayResult rr;
		const bool hit = p_space->intersect_ray(rq, rr);
		last_rays++;
		const bool level = dir.y > 0.0f; // what closes over and round the listener (the ground below does not)
		around += level ? 1 : 0;
		if (hit) {
			const float d = p_at.distance_to(rr.position);
			hits++;
			sum += d;
			closed += level ? 1 : 0;
			if (level) {
				nearest = MIN(nearest, d);
			}
		}
	}
	Room r;
	r.enclosure = around > 0 ? float(closed) / float(around) : 0.0f;
	r.distance = hits > 0 ? sum / float(hits) : reverb_max_distance;
	r.nearest = nearest;
	return r;
}

// Milestone 111 (decision 0218): two points are in one room when the rays found it alike from both — as closed
// (within 0.2) and as large (mean distances within a factor of 1.5).
bool FellowAcoustics3D::_same_room(const Room &p_a, const Room &p_b) {
	const float lo = MAX(MIN(p_a.distance, p_b.distance), 0.01f);
	return Math::abs(p_a.enclosure - p_b.enclosure) <= 0.2f && MAX(p_a.distance, p_b.distance) / lo <= 1.5f;
}

// The reverbs for other rooms: buses named reverb_bus + "Room1", "Room2"... (made, sending to Master, when there are
// none), each with its first AudioEffectReverb (added, wet only, when it has none).
void FellowAcoustics3D::_setup_room_buses() {
	AudioServer *as = AudioServer::get_singleton();
	if (!as || source_rooms <= 0 || reverb_bus == StringName() || !room_buses.is_empty()) {
		return;
	}
	for (int i = 0; i < source_rooms; i++) {
		RoomBus rb;
		rb.bus = StringName(String(reverb_bus) + "Room" + itos(i + 1));
		int idx = as->get_bus_index(rb.bus);
		if (idx < 0) {
			as->add_bus();
			idx = as->get_bus_count() - 1;
			as->set_bus_name(idx, rb.bus);
			as->set_bus_send(idx, SNAME("Master"));
			rb.created_bus = true;
		}
		for (int e = 0; e < as->get_bus_effect_count(idx); e++) {
			Ref<AudioEffectReverb> r = as->get_bus_effect(idx, e);
			if (r.is_valid()) {
				rb.effect = r;
				break;
			}
		}
		if (rb.effect.is_null()) {
			rb.effect.instantiate();
			rb.effect->set_dry(0.0f);
			rb.effect->set_wet(1.0f);
			as->add_bus_effect(idx, rb.effect);
			rb.added_effect = true;
		}
		room_buses.push_back(rb);
	}
	room_cursor = 0;
}

void FellowAcoustics3D::_release_room_buses() {
	AudioServer *as = AudioServer::get_singleton();
	for (KeyValue<ObjectID, Source> &kv : sources) {
		if (kv.value.room_bus >= 0) {
			if (AudioStreamPlayer3D *p = ObjectDB::get_instance<AudioStreamPlayer3D>(kv.key)) {
				AudioStreamPlayer3D::FellowAcoustics a = p->fellow_get_acoustics();
				a.send = 0.0f;
				a.send_bus = StringName();
				p->fellow_set_acoustics(a);
			}
			kv.value.room_bus = -1;
		}
		kv.value.room_measured = false;
	}
	// From the last: removing a bus moves the ones after it.
	for (int i = int(room_buses.size()) - 1; i >= 0; i--) {
		const RoomBus &rb = room_buses[i];
		const int idx = as ? as->get_bus_index(rb.bus) : -1;
		if (idx < 0) {
			continue;
		}
		if (rb.created_bus) {
			as->remove_bus(idx);
		} else if (rb.added_effect) {
			for (int e = 0; e < as->get_bus_effect_count(idx); e++) {
				if (as->get_bus_effect(idx, e) == rb.effect) {
					as->remove_bus_effect(idx, e);
					break;
				}
			}
		}
	}
	room_buses.clear();
}

void FellowAcoustics3D::set_source_rooms(int p_count) {
	p_count = CLAMP(p_count, 0, 8);
	if (source_rooms == p_count) {
		return;
	}
	const bool live = is_inside_tree() && !Engine::get_singleton()->is_editor_hint() && current == this && reverb && effect.is_valid();
	if (live) {
		_release_room_buses();
	}
	source_rooms = p_count;
	if (live) {
		_setup_room_buses();
	}
}

// Milestone 111: one audible source's room measured a frame (in turn, within the rays left), then every source given a
// reverb — the listener's when its room is alike or it has none (enclosed under half), else the room bus already
// holding an alike room, a free one, or the nearest — and each room bus driven by the mean of its sources' rooms.
void FellowAcoustics3D::_source_rooms(float p_delta, const Room &p_listener, PhysicsDirectSpaceState3D *p_space, const HashSet<RID> &p_listener_exclude, int &r_budget) {
	last_room_measures = 0;
	const uint32_t count = order.size();
	for (uint32_t visited = 0; visited < count && r_budget >= reverb_rays; visited++) {
		room_cursor = room_cursor % count;
		const ObjectID id = order[room_cursor++];
		Source *s = sources.getptr(id);
		AudioStreamPlayer3D *player = ObjectDB::get_instance<AudioStreamPlayer3D>(id);
		if (!s || !player || !s->audible || !player->is_inside_tree()) {
			continue;
		}
		HashSet<RID> exclude;
		exclude = p_listener_exclude;
		exclude_bodies(player, exclude);
		s->room = _room_at(player->get_global_position(), p_space, exclude);
		s->room_measured = true;
		r_budget -= reverb_rays;
		last_room_measures++;
		break;
	}
	for (RoomBus &rb : room_buses) {
		rb.sources = 0;
	}
	LocalVector<Room> sums;
	sums.resize(room_buses.size());
	for (KeyValue<ObjectID, Source> &kv : sources) {
		Source &s = kv.value;
		// In the open or under little cover (an enclosure under 0.5) a source has no room of its own: the listener's
		// reverb, as without source_rooms.
		if (!s.audible || !s.room_measured || s.room.enclosure < 0.5f || _same_room(s.room, p_listener)) {
			s.room_bus = -1;
			continue;
		}
		int pick = -1;
		// Its bus of the last frame while it still holds an alike room, else one that does, else a free one, else the
		// one whose room is nearest.
		if (s.room_bus >= 0 && s.room_bus < int(room_buses.size()) && (room_buses[s.room_bus].sources == 0 || _same_room(room_buses[s.room_bus].target, s.room))) {
			pick = s.room_bus;
		}
		for (uint32_t i = 0; pick < 0 && i < room_buses.size(); i++) {
			if (room_buses[i].sources > 0 && _same_room(room_buses[i].target, s.room)) {
				pick = int(i);
			}
		}
		for (uint32_t i = 0; pick < 0 && i < room_buses.size(); i++) {
			if (room_buses[i].sources == 0) {
				pick = int(i);
			}
		}
		float best = 1e30f;
		for (uint32_t i = 0; pick < 0 && i < room_buses.size(); i++) {
			const float d = Math::abs(room_buses[i].target.enclosure - s.room.enclosure) + Math::abs(Math::log(MAX(room_buses[i].target.distance, 0.01f) / MAX(s.room.distance, 0.01f)));
			if (d < best) {
				best = d;
				pick = int(i);
			}
		}
		s.room_bus = pick;
		if (pick < 0) {
			continue;
		}
		RoomBus &rb = room_buses[pick];
		if (rb.sources == 0) {
			rb.target = s.room; // the first source sets the room the next ones are compared with
			sums[pick] = Room();
		}
		rb.sources++;
		sums[pick].enclosure += s.room.enclosure;
		sums[pick].distance += s.room.distance;
		sums[pick].nearest += s.room.nearest;
	}
	const float k = reverb_response > 0.0f ? 1.0f - Math::exp(-p_delta / reverb_response) : 1.0f;
	for (uint32_t i = 0; i < room_buses.size(); i++) {
		RoomBus &rb = room_buses[i];
		if (rb.sources == 0) {
			continue;
		}
		const float n = float(rb.sources);
		rb.target.enclosure = sums[i].enclosure / n;
		rb.target.distance = sums[i].distance / n;
		rb.target.nearest = sums[i].nearest / n;
		if (!rb.measured) {
			rb.room = rb.target;
			rb.measured = true;
		}
		rb.room.enclosure += (rb.target.enclosure - rb.room.enclosure) * k;
		rb.room.distance += (rb.target.distance - rb.room.distance) * k;
		rb.room.nearest += (rb.target.nearest - rb.room.nearest) * k;
		_drive_reverb(rb.effect, rb.room);
	}
}

// Milestone 99 (decision 0207): the path through an opening, for a source the rays found blocked. The open portals the
// line crosses (the listener and the source on its two sides), nearest path first: the line's crossing of the opening's
// plane brought 10 cm inside its edges, both legs tested clear. The first clear one is heard when it is louder than
// through the wall (the path's extra length, the bend through it at portal_diffraction_db a quarter turn, its
// transmission; the highs: the wall's shelf against the bend's): the source then heard from its direction at its
// length (the player's own distance attenuation), its occlusion the bend's and the transmission's.
void FellowAcoustics3D::_portal_path(Source &r_source, const Vector3 &p_at, const Vector3 &p_pos, float p_range, const HashSet<RID> &p_exclude, PhysicsDirectSpaceState3D *p_space, const World3D *p_world, int &r_budget) {
	struct Candidate {
		float length = 0.0f;
		Vector3 point;
		float transmission = 1.0f;
	};
	LocalVector<Candidate> candidates;
	for (const FellowAcousticPortal3D *portal : FellowAcousticPortal3D::get_portals()) {
		if (portal->get_transmission() <= 0.0f || portal->get_world_3d().ptr() != p_world) {
			continue;
		}
		const Transform3D xf = portal->get_global_transform();
		Vector3 ux = xf.basis.get_column(0);
		Vector3 uy = xf.basis.get_column(1);
		Vector3 n = xf.basis.get_column(2);
		const float sx = ux.length();
		const float sy = uy.length();
		const float sn = n.length();
		if (sx < 1e-6f || sy < 1e-6f || sn < 1e-6f) {
			continue;
		}
		ux /= sx;
		uy /= sy;
		n /= sn;
		const float dl = (p_at - xf.origin).dot(n);
		const float ds = (p_pos - xf.origin).dot(n);
		if (dl * ds >= 0.0f) {
			continue; // both on one side: the opening is not between them
		}
		const Vector3 crossing = p_at + (p_pos - p_at) * (dl / (dl - ds));
		const Vector2 half = portal->get_size() * 0.5f * Vector2(sx, sy);
		const float mx = MAX(0.0f, half.x - 0.1f);
		const float my = MAX(0.0f, half.y - 0.1f);
		const Vector3 local = crossing - xf.origin;
		Candidate c;
		c.point = xf.origin + ux * CLAMP(local.dot(ux), -mx, mx) + uy * CLAMP(local.dot(uy), -my, my);
		c.length = p_at.distance_to(c.point) + c.point.distance_to(p_pos);
		c.transmission = portal->get_transmission();
		if (c.length <= p_range) {
			candidates.push_back(c);
		}
	}
	for (uint32_t i = 1; i < candidates.size(); i++) {
		for (uint32_t j = i; j > 0 && candidates[j].length < candidates[j - 1].length; j--) {
			SWAP(candidates[j], candidates[j - 1]);
		}
	}
	const float direct = MAX(p_at.distance_to(p_pos), 0.01f);
	int tried = 0;
	for (const Candidate &c : candidates) {
		if (tried >= max_portals || r_budget < 2) {
			break;
		}
		tried++;
		r_budget -= 2;
		last_rays += 2;
		bool clear = true;
		for (int leg = 0; leg < 2 && clear; leg++) {
			PhysicsDirectSpaceState3D::RayParameters rq;
			rq.from = leg == 0 ? p_at : c.point;
			rq.to = leg == 0 ? c.point : p_pos;
			rq.exclude = p_exclude;
			rq.collision_mask = collision_mask;
			PhysicsDirectSpaceState3D::RayResult rr;
			clear = !p_space->intersect_ray(rq, rr);
		}
		if (!clear) {
			continue;
		}
		const Vector3 in = (c.point - p_at).normalized();
		const Vector3 out = (p_pos - c.point).normalized();
		const float bend = in.angle_to(out); // 0: straight through
		const float level_db = portal_diffraction_db * bend / float(Math::PI * 0.5) + Math::linear_to_db(MAX(c.transmission, 1e-4f));
		const float bend_highs = CLAMP(bend / float(Math::PI), 0.0f, 1.0f);
		const float path_db = Math::linear_to_db(direct / c.length) + level_db + 0.5f * bend_highs * occluded_shelf_db;
		const float wall_db = r_source.target * occluded_volume_db + 0.5f * r_source.target_highs * occluded_shelf_db;
		if (path_db > wall_db) {
			r_source.target = occluded_volume_db < -0.01f ? CLAMP(level_db / occluded_volume_db, 0.0f, 1.0f) : 0.0f;
			r_source.target_highs = bend_highs;
			r_source.portal_point = p_at + in * c.length;
			r_source.portal_target = 1.0f;
			last_portal_paths++;
		}
		return; // the nearest clear opening decides
	}
}

void FellowAcoustics3D::_step(float p_delta) {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	last_rays = 0;
	last_tests = 0;
	last_portal_paths = 0;
	Node3D *listener = _listener();
	Ref<World3D> world = listener ? listener->get_world_3d() : Ref<World3D>();
	PhysicsDirectSpaceState3D *space = world.is_valid() ? PhysicsServer3D::get_singleton()->space_get_direct_state(world->get_space()) : nullptr;
	if (!listener || !space) {
		return;
	}
	const Vector3 at = listener->get_global_position();
	HashSet<RID> listener_exclude;
	exclude_bodies(listener, listener_exclude);

	// The room around the listener, every reverb_interval.
	if (reverb && effect.is_valid()) {
		reverb_timer -= p_delta;
		if (reverb_timer <= 0.0f) {
			reverb_timer = reverb_interval;
			_measure_room(at, space, listener_exclude);
		}
		const float k = reverb_response > 0.0f ? 1.0f - Math::exp(-p_delta / reverb_response) : 1.0f;
		room_enclosure += (target_enclosure - room_enclosure) * k;
		room_distance += (target_distance - room_distance) * k;
		room_nearest += (target_nearest - room_nearest) * k;
		Room heard;
		heard.enclosure = room_enclosure;
		heard.distance = room_distance;
		heard.nearest = room_nearest;
		_drive_reverb(effect, heard);
	}
	const float send = reverb && effect.is_valid() ? reverb_level * room_enclosure : 0.0f;

	// The sources: rays to each audible one in turn, as many a frame as the budget allows.
	const uint32_t count = order.size();
	int budget = max_rays_per_frame;
	for (uint32_t visited = 0; visited < count && budget >= occlusion_rays; visited++) {
		cursor = cursor % count;
		const ObjectID id = order[cursor++];
		Source *s = sources.getptr(id);
		AudioStreamPlayer3D *player = ObjectDB::get_instance<AudioStreamPlayer3D>(id);
		if (!s || !player || !player->is_inside_tree()) {
			continue;
		}
		const Vector3 pos = player->get_global_position();
		s->distance = at.distance_to(pos);
		const float range = player->get_max_distance() > 0.0f ? MIN(max_distance, player->get_max_distance()) : max_distance;
		s->audible = player->is_playing() && s->distance <= range;
		if (!s->audible) {
			continue;
		}
		HashSet<RID> exclude;
		exclude = listener_exclude;
		exclude_bodies(player, exclude);
		// The source's point and points around it across the line to it (source_radius): paths past an edge.
		const Vector3 dir = s->distance > 1e-4f ? (pos - at) / s->distance : Vector3(0, 0, -1);
		Vector3 u = dir.cross(Math::abs(dir.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).normalized();
		Vector3 v = dir.cross(u).normalized();
		float blocked = 0.0f;
		float blocked_highs = 0.0f;
		for (int i = 0; i < occlusion_rays; i++) {
			Vector3 to = pos;
			if (i > 0) {
				const float a = float(Math::TAU) * float(i - 1) / float(occlusion_rays - 1);
				to += (u * Math::cos(a) + v * Math::sin(a)) * source_radius;
			}
			// Milestone 91 (decision 0199): through what lies on the path, up to three colliders, each blocking it by its
			// metadata "fellow_occlusion" (0 lets all through, 1 blocks as a wall; without it, 1), the shares added up
			// to a full block (a plain wall ends the path at once).
			HashSet<RID> path_exclude;
			path_exclude = exclude;
			Vector3 from = at;
			float path_block = 0.0f;
			float path_highs = 0.0f;
			for (int wall = 0; wall < 3 && (path_block < 1.0f || path_highs < 1.0f); wall++) {
				PhysicsDirectSpaceState3D::RayParameters rq;
				rq.from = from;
				rq.to = to;
				rq.exclude = path_exclude;
				rq.collision_mask = collision_mask;
				PhysicsDirectSpaceState3D::RayResult rr;
				if (!space->intersect_ray(rq, rr)) {
					break;
				}
				float b = 1.0f;
				if (rr.collider && rr.collider->has_meta(SNAME("fellow_occlusion"))) {
					b = CLAMP(float(rr.collider->get_meta(SNAME("fellow_occlusion"))), 0.0f, 1.0f);
				}
				// Milestone 92 (decision 0200): the highs blocked by "fellow_occlusion_highs" when it has one (a
				// curtain lets the level through and stops the highs), else as much as the level.
				float h = b;
				if (rr.collider && rr.collider->has_meta(SNAME("fellow_occlusion_highs"))) {
					h = CLAMP(float(rr.collider->get_meta(SNAME("fellow_occlusion_highs"))), 0.0f, 1.0f);
				}
				path_block += b;
				path_highs += h;
				path_exclude.insert(rr.rid);
				from = rr.position;
				if (b < 1.0f || h < 1.0f) {
					last_rays++; // a ray more to look past it
				}
			}
			blocked += MIN(path_block, 1.0f);
			blocked_highs += MIN(path_highs, 1.0f);
		}
		budget -= occlusion_rays;
		last_rays += occlusion_rays;
		last_tests++;
		s->target = blocked / float(occlusion_rays);
		s->target_highs = blocked_highs / float(occlusion_rays);
		s->portal_target = 0.0f;
		if (portals && s->target > 0.01f) {
			_portal_path(*s, at, pos, range, exclude, space, world.ptr(), budget);
		}
		if (!s->tested) {
			s->occlusion = s->target;
			s->occlusion_highs = s->target_highs;
			s->portal_weight = s->portal_target;
			s->apparent = s->portal_point;
			s->tested = true;
		}
	}

	// Milestone 111: the sources' own rooms.
	last_room_measures = 0;
	if (reverb && effect.is_valid() && !room_buses.is_empty()) {
		Room listener_room;
		listener_room.enclosure = target_enclosure;
		listener_room.distance = target_distance;
		listener_room.nearest = target_nearest;
		_source_rooms(p_delta, listener_room, space, listener_exclude, budget);
	}

	// Every source's sound, its occlusion eased towards what the rays found.
	const float k = response_time > 0.0f ? 1.0f - Math::exp(-p_delta / response_time) : 1.0f;
	for (KeyValue<ObjectID, Source> &kv : sources) {
		Source &s = kv.value;
		AudioStreamPlayer3D *player = ObjectDB::get_instance<AudioStreamPlayer3D>(kv.key);
		if (!player) {
			continue;
		}
		if (!s.audible) {
			s.target = 0.0f;
			s.target_highs = 0.0f;
			s.portal_target = 0.0f;
		}
		s.occlusion += (s.target - s.occlusion) * k;
		s.occlusion_highs += (s.target_highs - s.occlusion_highs) * k;
		// Milestone 99: where it is heard from moves to the opening's path and back with the same response.
		if (s.portal_target > 0.0f) {
			s.apparent = s.portal_weight < 0.02f ? s.portal_point : s.apparent.lerp(s.portal_point, k);
		}
		s.portal_weight += (s.portal_target - s.portal_weight) * k;
		AudioStreamPlayer3D::FellowAcoustics a;
		if (s.portal_weight > 1e-3f) {
			a.position = s.apparent;
			a.position_weight = MIN(s.portal_weight, 1.0f);
		}
		if (s.occlusion > 1e-4f || s.occlusion_highs > 1e-4f) {
			a.gain = Math::db_to_linear(s.occlusion * occluded_volume_db);
			a.shelf = Math::db_to_linear(s.occlusion_highs * occluded_shelf_db);
			const float own = player->get_attenuation_filter_cutoff_hz();
			a.cutoff_hz = Math::exp(Math::lerp(Math::log(MAX(own, 20.0f)), Math::log(MIN(occluded_cutoff_hz, own)), s.occlusion_highs));
		}
		if (s.room_bus >= 0 && s.room_bus < int(room_buses.size())) {
			// Milestone 111: a source in another room, sent to that room's reverb by how closed its room is (cut by
			// half the occlusion's decibels as below).
			a.send_bus = room_buses[s.room_bus].bus;
			a.send = MIN(1.0f, reverb_level * s.room.enclosure / Math::sqrt(MAX(a.gain, 1e-4f)));
		} else if (send > 0.0f) {
			// The reverb hears the source through the room more than through the wall: the send is cut by half the
			// occlusion's decibels.
			a.send_bus = reverb_bus;
			a.send = MIN(1.0f, send / Math::sqrt(MAX(a.gain, 1e-4f)));
		}
		s.send = a.send;
		player->fellow_set_acoustics(a);
	}

	last_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.frames++;
	global_stats.usec += last_usec;
	global_stats.rays += last_rays;
	global_stats.tests += last_tests;
}

void FellowAcoustics3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			if (current && current != this) {
				current->_reset_sources();
				current->_release_reverb();
			}
			current = this;
			SceneTree *tree = get_tree();
			tree->connect(SNAME("node_added"), callable_mp(this, &FellowAcoustics3D::_node_added));
			tree->connect(SNAME("node_removed"), callable_mp(this, &FellowAcoustics3D::_node_removed));
			TypedArray<Node> players = tree->get_root()->find_children("*", "AudioStreamPlayer3D", true, false);
			for (int i = 0; i < players.size(); i++) {
				_track(Object::cast_to<Node>(players[i]));
			}
			if (reverb) {
				_setup_reverb();
			}
			set_physics_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			SceneTree *tree = get_tree();
			tree->disconnect(SNAME("node_added"), callable_mp(this, &FellowAcoustics3D::_node_added));
			tree->disconnect(SNAME("node_removed"), callable_mp(this, &FellowAcoustics3D::_node_removed));
			_reset_sources();
			_release_reverb();
			sources.clear();
			order.clear();
			if (current == this) {
				current = nullptr;
			}
			set_physics_process_internal(false);
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (enabled && current == this) {
				_step(float(get_physics_process_delta_time()));
			}
		} break;
	}
}

float FellowAcoustics3D::get_portal_weight(Node *p_player) const {
	ERR_FAIL_NULL_V(p_player, 0.0f);
	const Source *s = sources.getptr(p_player->get_instance_id());
	return s ? s->portal_weight : 0.0f;
}

Vector3 FellowAcoustics3D::get_heard_position(Node *p_player) const {
	const Node3D *n = Object::cast_to<Node3D>(p_player);
	ERR_FAIL_NULL_V(n, Vector3());
	const Source *s = sources.getptr(p_player->get_instance_id());
	const Vector3 own = n->is_inside_tree() ? n->get_global_position() : Vector3();
	return s && s->portal_weight > 1e-3f ? own.lerp(s->apparent, MIN(s->portal_weight, 1.0f)) : own;
}

float FellowAcoustics3D::get_occlusion(Node *p_player) const {
	ERR_FAIL_NULL_V(p_player, 0.0f);
	const Source *s = sources.getptr(p_player->get_instance_id());
	return s ? s->occlusion : 0.0f;
}

float FellowAcoustics3D::get_occlusion_highs(Node *p_player) const {
	ERR_FAIL_NULL_V(p_player, 0.0f);
	const Source *s = sources.getptr(p_player->get_instance_id());
	return s ? s->occlusion_highs : 0.0f;
}

Dictionary FellowAcoustics3D::get_room() const {
	Dictionary d;
	d["enclosure"] = room_enclosure;
	d["mean_distance"] = room_distance;
	d["nearest"] = room_nearest;
	d["rt60"] = _rt60();
	d["room_size"] = effect.is_valid() ? effect->get_room_size() : 0.0f;
	d["predelay_msec"] = effect.is_valid() ? effect->get_predelay_msec() : 0.0f;
	d["send"] = reverb && effect.is_valid() ? reverb_level * room_enclosure : 0.0f;
	return d;
}

Dictionary FellowAcoustics3D::get_stats() const {
	Dictionary d;
	int audible = 0;
	int occluded = 0;
	for (const KeyValue<ObjectID, Source> &kv : sources) {
		audible += kv.value.audible ? 1 : 0;
		occluded += kv.value.audible && kv.value.occlusion > 0.5f ? 1 : 0;
	}
	d["sources"] = int(sources.size());
	d["audible"] = audible;
	d["occluded"] = occluded;
	d["rays"] = last_rays;
	d["tests"] = last_tests;
	d["usec"] = int64_t(last_usec);
	d["active"] = current == this;
	d["portal_paths"] = last_portal_paths; // milestone 99: sources heard through an opening, tested this frame
	d["portals"] = int(FellowAcousticPortal3D::get_portals().size());
	int rooms_used = 0;
	for (const RoomBus &rb : room_buses) {
		rooms_used += rb.sources > 0 ? 1 : 0;
	}
	d["room_buses"] = int(room_buses.size()); // milestone 111
	d["room_buses_used"] = rooms_used;
	d["room_measures"] = last_room_measures;
	return d;
}

Dictionary FellowAcoustics3D::get_source_room(Node *p_player) const {
	Dictionary d;
	ERR_FAIL_NULL_V(p_player, d);
	const Source *s = sources.getptr(p_player->get_instance_id());
	if (!s) {
		return d;
	}
	d["measured"] = s->room_measured;
	d["send"] = s->send;
	d["enclosure"] = s->room.enclosure;
	d["mean_distance"] = s->room.distance;
	d["nearest"] = s->room.nearest;
	d["rt60"] = _rt60_for(s->room.distance);
	const bool own = s->room_bus >= 0 && s->room_bus < int(room_buses.size());
	d["bus"] = own ? room_buses[s->room_bus].bus : (reverb && effect.is_valid() ? reverb_bus : StringName());
	d["bus_rt60"] = own ? _rt60_for(room_buses[s->room_bus].room.distance) : _rt60();
	d["bus_room_size"] = own ? room_buses[s->room_bus].effect->get_room_size() : (effect.is_valid() ? effect->get_room_size() : 0.0f);
	return d;
}

Dictionary FellowAcoustics3D::get_global_stats() {
	Dictionary d;
	d["frames"] = int64_t(global_stats.frames);
	d["usec"] = int64_t(global_stats.usec);
	d["rays"] = int64_t(global_stats.rays);
	d["tests"] = int64_t(global_stats.tests);
	return d;
}

void FellowAcoustics3D::reset_global_stats() {
	global_stats = GlobalStats();
}

PackedStringArray FellowAcoustics3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!listener_path.is_empty() && is_inside_tree() && !Object::cast_to<Node3D>(get_node_or_null(listener_path))) {
		warnings.push_back(RTR("Listener Path leads to no Node3D: the sounds are heard from the viewport's AudioListener3D or camera."));
	}
	return warnings;
}

void FellowAcoustics3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &FellowAcoustics3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &FellowAcoustics3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_listener_path", "path"), &FellowAcoustics3D::set_listener_path);
	ClassDB::bind_method(D_METHOD("get_listener_path"), &FellowAcoustics3D::get_listener_path);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowAcoustics3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowAcoustics3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_occlusion_rays", "rays"), &FellowAcoustics3D::set_occlusion_rays);
	ClassDB::bind_method(D_METHOD("get_occlusion_rays"), &FellowAcoustics3D::get_occlusion_rays);
	ClassDB::bind_method(D_METHOD("set_source_radius", "radius"), &FellowAcoustics3D::set_source_radius);
	ClassDB::bind_method(D_METHOD("get_source_radius"), &FellowAcoustics3D::get_source_radius);
	ClassDB::bind_method(D_METHOD("set_occluded_volume_db", "db"), &FellowAcoustics3D::set_occluded_volume_db);
	ClassDB::bind_method(D_METHOD("get_occluded_volume_db"), &FellowAcoustics3D::get_occluded_volume_db);
	ClassDB::bind_method(D_METHOD("set_occluded_shelf_db", "db"), &FellowAcoustics3D::set_occluded_shelf_db);
	ClassDB::bind_method(D_METHOD("get_occluded_shelf_db"), &FellowAcoustics3D::get_occluded_shelf_db);
	ClassDB::bind_method(D_METHOD("set_occluded_cutoff_hz", "hz"), &FellowAcoustics3D::set_occluded_cutoff_hz);
	ClassDB::bind_method(D_METHOD("get_occluded_cutoff_hz"), &FellowAcoustics3D::get_occluded_cutoff_hz);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &FellowAcoustics3D::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &FellowAcoustics3D::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_max_rays_per_frame", "rays"), &FellowAcoustics3D::set_max_rays_per_frame);
	ClassDB::bind_method(D_METHOD("get_max_rays_per_frame"), &FellowAcoustics3D::get_max_rays_per_frame);
	ClassDB::bind_method(D_METHOD("set_response_time", "time"), &FellowAcoustics3D::set_response_time);
	ClassDB::bind_method(D_METHOD("get_response_time"), &FellowAcoustics3D::get_response_time);
	ClassDB::bind_method(D_METHOD("set_reverb", "enabled"), &FellowAcoustics3D::set_reverb);
	ClassDB::bind_method(D_METHOD("has_reverb"), &FellowAcoustics3D::has_reverb);
	ClassDB::bind_method(D_METHOD("set_reverb_bus", "bus"), &FellowAcoustics3D::set_reverb_bus);
	ClassDB::bind_method(D_METHOD("get_reverb_bus"), &FellowAcoustics3D::get_reverb_bus);
	ClassDB::bind_method(D_METHOD("set_reverb_rays", "rays"), &FellowAcoustics3D::set_reverb_rays);
	ClassDB::bind_method(D_METHOD("get_reverb_rays"), &FellowAcoustics3D::get_reverb_rays);
	ClassDB::bind_method(D_METHOD("set_reverb_interval", "interval"), &FellowAcoustics3D::set_reverb_interval);
	ClassDB::bind_method(D_METHOD("get_reverb_interval"), &FellowAcoustics3D::get_reverb_interval);
	ClassDB::bind_method(D_METHOD("set_reverb_max_distance", "distance"), &FellowAcoustics3D::set_reverb_max_distance);
	ClassDB::bind_method(D_METHOD("get_reverb_max_distance"), &FellowAcoustics3D::get_reverb_max_distance);
	ClassDB::bind_method(D_METHOD("set_absorption", "absorption"), &FellowAcoustics3D::set_absorption);
	ClassDB::bind_method(D_METHOD("get_absorption"), &FellowAcoustics3D::get_absorption);
	ClassDB::bind_method(D_METHOD("set_reverb_level", "level"), &FellowAcoustics3D::set_reverb_level);
	ClassDB::bind_method(D_METHOD("get_reverb_level"), &FellowAcoustics3D::get_reverb_level);
	ClassDB::bind_method(D_METHOD("set_reverb_response", "time"), &FellowAcoustics3D::set_reverb_response);
	ClassDB::bind_method(D_METHOD("get_reverb_response"), &FellowAcoustics3D::get_reverb_response);
	ClassDB::bind_method(D_METHOD("set_portals", "enabled"), &FellowAcoustics3D::set_portals);
	ClassDB::bind_method(D_METHOD("has_portals"), &FellowAcoustics3D::has_portals);
	ClassDB::bind_method(D_METHOD("set_portal_diffraction_db", "db"), &FellowAcoustics3D::set_portal_diffraction_db);
	ClassDB::bind_method(D_METHOD("get_portal_diffraction_db"), &FellowAcoustics3D::get_portal_diffraction_db);
	ClassDB::bind_method(D_METHOD("set_max_portals", "count"), &FellowAcoustics3D::set_max_portals);
	ClassDB::bind_method(D_METHOD("get_max_portals"), &FellowAcoustics3D::get_max_portals);
	ClassDB::bind_method(D_METHOD("set_source_rooms", "count"), &FellowAcoustics3D::set_source_rooms);
	ClassDB::bind_method(D_METHOD("get_source_rooms"), &FellowAcoustics3D::get_source_rooms);
	ClassDB::bind_method(D_METHOD("get_source_room", "player"), &FellowAcoustics3D::get_source_room);
	ClassDB::bind_method(D_METHOD("get_portal_weight", "player"), &FellowAcoustics3D::get_portal_weight);
	ClassDB::bind_method(D_METHOD("get_heard_position", "player"), &FellowAcoustics3D::get_heard_position);
	ClassDB::bind_method(D_METHOD("get_occlusion", "player"), &FellowAcoustics3D::get_occlusion);
	ClassDB::bind_method(D_METHOD("get_occlusion_highs", "player"), &FellowAcoustics3D::get_occlusion_highs);
	ClassDB::bind_method(D_METHOD("get_room"), &FellowAcoustics3D::get_room);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowAcoustics3D::get_stats);
	ClassDB::bind_static_method("FellowAcoustics3D", D_METHOD("get_global_stats"), &FellowAcoustics3D::get_global_stats);
	ClassDB::bind_static_method("FellowAcoustics3D", D_METHOD("reset_global_stats"), &FellowAcoustics3D::reset_global_stats);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "listener_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_listener_path", "get_listener_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_GROUP("Occlusion", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_rays", PROPERTY_HINT_RANGE, "1,16,1"), "set_occlusion_rays", "get_occlusion_rays");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "source_radius", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:m"), "set_source_radius", "get_source_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occluded_volume_db", PROPERTY_HINT_RANGE, "-60,0,0.1,suffix:dB"), "set_occluded_volume_db", "get_occluded_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occluded_shelf_db", PROPERTY_HINT_RANGE, "-60,0,0.1,suffix:dB"), "set_occluded_shelf_db", "get_occluded_shelf_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occluded_cutoff_hz", PROPERTY_HINT_RANGE, "20,20000,1,suffix:Hz"), "set_occluded_cutoff_hz", "get_occluded_cutoff_hz");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_max_distance", "get_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_rays_per_frame", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), "set_max_rays_per_frame", "get_max_rays_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "response_time", PROPERTY_HINT_RANGE, "0,2,0.01,suffix:s"), "set_response_time", "get_response_time");
	ADD_GROUP("Portals", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "portals"), "set_portals", "has_portals");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "portal_diffraction_db", PROPERTY_HINT_RANGE, "-40,0,0.1,suffix:dB"), "set_portal_diffraction_db", "get_portal_diffraction_db");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_portals", PROPERTY_HINT_RANGE, "1,16,1"), "set_max_portals", "get_max_portals");
	ADD_GROUP("Reverb", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reverb"), "set_reverb", "has_reverb");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "reverb_bus"), "set_reverb_bus", "get_reverb_bus");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reverb_rays", PROPERTY_HINT_RANGE, "4,256,1"), "set_reverb_rays", "get_reverb_rays");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_interval", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:s"), "set_reverb_interval", "get_reverb_interval");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_max_distance", PROPERTY_HINT_RANGE, "1,500,0.1,or_greater,suffix:m"), "set_reverb_max_distance", "get_reverb_max_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "absorption", PROPERTY_HINT_RANGE, "0.01,1,0.01"), "set_absorption", "get_absorption");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_level", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reverb_level", "get_reverb_level");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reverb_response", PROPERTY_HINT_RANGE, "0,5,0.01,suffix:s"), "set_reverb_response", "get_reverb_response");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "source_rooms", PROPERTY_HINT_RANGE, "0,8,1"), "set_source_rooms", "get_source_rooms");
}

FellowAcoustics3D::FellowAcoustics3D() {
}

FellowAcoustics3D::~FellowAcoustics3D() {
	if (current == this) {
		current = nullptr;
	}
}
