/**************************************************************************/
/*  fellow_audio_director.h                                               */
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
#include "core/math/random_pcg.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"
#include "servers/audio/audio_stream.h"

class Node3D;

// FellowEngine (milestone 82, decision 0190): a sound as a game asks for it — the variations it picks from, how much
// its level and pitch vary, how many may play at once, how soon it may play again, how important it is, the bus it
// plays on and how far it carries. Played by a FellowAudioDirector.
class FellowSoundEvent : public Resource {
	GDCLASS(FellowSoundEvent, Resource);

public:
	enum Selection {
		SELECTION_RANDOM_NO_REPEAT,
		SELECTION_RANDOM,
		SELECTION_SEQUENCE,
	};

private:
	TypedArray<AudioStream> streams;
	Selection selection = SELECTION_RANDOM_NO_REPEAT;
	float volume_db = 0.0f;
	float volume_random_db = 0.0f; // ± uniform
	float pitch_scale = 1.0f;
	float pitch_random_semitones = 0.0f; // ± uniform
	int max_instances = 0; // 0: no limit
	float cooldown = 0.0f; // s between two starts
	int priority = 128; // 0 to 255: higher keeps its voice
	StringName bus = StringName("Master");
	bool spatial = true;
	float unit_size = 10.0f;
	float max_distance = 0.0f; // m: farther, not played (0: no limit)
	int attenuation_model = 0; // AudioStreamPlayer3D::AttenuationModel
	bool virtual_voice = false; // milestone 85: kept and resumed when out of voices or of reach
	bool steal_oldest = false; // milestone 85: over max_instances, the oldest gives way

protected:
	static void _bind_methods();

public:
	void set_streams(const TypedArray<AudioStream> &p_streams) { streams = p_streams; }
	TypedArray<AudioStream> get_streams() const { return streams; }
	void set_selection(Selection p_selection) { selection = p_selection; }
	Selection get_selection() const { return selection; }
	void set_volume_db(float p_db) { volume_db = p_db; }
	float get_volume_db() const { return volume_db; }
	void set_volume_random_db(float p_db) { volume_random_db = MAX(0.0f, p_db); }
	float get_volume_random_db() const { return volume_random_db; }
	void set_pitch_scale(float p_scale) { pitch_scale = MAX(0.01f, p_scale); }
	float get_pitch_scale() const { return pitch_scale; }
	void set_pitch_random_semitones(float p_semitones) { pitch_random_semitones = MAX(0.0f, p_semitones); }
	float get_pitch_random_semitones() const { return pitch_random_semitones; }
	void set_max_instances(int p_count) { max_instances = MAX(0, p_count); }
	int get_max_instances() const { return max_instances; }
	void set_cooldown(float p_seconds) { cooldown = MAX(0.0f, p_seconds); }
	float get_cooldown() const { return cooldown; }
	void set_priority(int p_priority) { priority = CLAMP(p_priority, 0, 255); }
	int get_priority() const { return priority; }
	void set_bus(const StringName &p_bus) { bus = p_bus; }
	StringName get_bus() const { return bus; }
	void set_spatial(bool p_spatial) { spatial = p_spatial; }
	bool is_spatial() const { return spatial; }
	void set_unit_size(float p_size) { unit_size = MAX(0.1f, p_size); }
	float get_unit_size() const { return unit_size; }
	void set_max_distance(float p_distance) { max_distance = MAX(0.0f, p_distance); }
	float get_max_distance() const { return max_distance; }
	void set_attenuation_model(int p_model) { attenuation_model = CLAMP(p_model, 0, 3); }
	int get_attenuation_model() const { return attenuation_model; }
	void set_virtual_voice(bool p_enabled) { virtual_voice = p_enabled; }
	bool is_virtual_voice() const { return virtual_voice; }
	void set_steal_oldest(bool p_enabled) { steal_oldest = p_enabled; }
	bool is_steal_oldest() const { return steal_oldest; }

	// The level a listener p_distance away hears it at (dB, before the variation): its volume and the attenuation.
	float get_audibility_db(float p_distance) const;
};

// FellowEngine (milestone 82, decision 0190): a mix state — the buses' levels it changes (dB added to each), how fast
// it comes in and goes out; pushed and popped by the game, or active while a voice plays on its trigger bus (a duck:
// the music lowered while someone speaks).
class FellowMixSnapshot : public Resource {
	GDCLASS(FellowMixSnapshot, Resource);

	Dictionary bus_volumes; // StringName -> dB added
	float fade_in = 0.3f;
	float fade_out = 0.5f;
	StringName trigger_bus;

protected:
	static void _bind_methods();

public:
	void set_bus_volumes(const Dictionary &p_volumes) { bus_volumes = p_volumes; }
	Dictionary get_bus_volumes() const { return bus_volumes; }
	void set_fade_in(float p_seconds) { fade_in = MAX(0.0f, p_seconds); }
	float get_fade_in() const { return fade_in; }
	void set_fade_out(float p_seconds) { fade_out = MAX(0.0f, p_seconds); }
	float get_fade_out() const { return fade_out; }
	void set_trigger_bus(const StringName &p_bus) { trigger_bus = p_bus; }
	StringName get_trigger_bus() const { return trigger_bus; }
};

// FellowEngine (milestone 82, decision 0190): plays FellowSoundEvents from a pool of players within a budget of
// voices — a variation picked, its level and pitch varied, the event's instance limit and cooldown kept, sounds beyond
// their distance not started; when the voices are all taken, the least important and quietest (as the listener hears
// it now) gives its voice to a more audible new one, faded out in 30 ms, or the new one is not played. Mix snapshots
// fade the buses' levels in and out, pushed by the game or while a voice plays on their trigger bus.
class FellowAudioDirector : public Node {
	GDCLASS(FellowAudioDirector, Node);

	struct Voice {
		int id = 0;
		Ref<FellowSoundEvent> event;
		Node *player = nullptr; // AudioStreamPlayer3D or AudioStreamPlayer, a child of the director
		bool spatial = true;
		ObjectID follow;
		Vector3 position;
		float volume_db = 0.0f; // as played (the variation in)
		float audibility = 0.0f; // dB at the listener
		uint64_t order = 0; // started, in order
		bool fading = false;
		float fade_time = 0.0f;
		float fade_left = 0.0f;
		bool stolen = false;
		// Milestone 85: a virtual voice (no player: out of voices or of reach) goes on in time and is resumed there.
		bool is_virtual = false;
		bool to_virtual = false; // fading out into a virtual voice
		double started = 0.0; // the director's clock at its start
		float pitch = 1.0f;
		Ref<AudioStream> stream;
	};
	struct EventState {
		double last_start = -1e9;
		int last_index = -1;
		int next_index = 0;
	};
	struct SnapshotState {
		Ref<FellowMixSnapshot> snapshot;
		bool pushed = false;
		float weight = 0.0f;
	};
	struct Stats {
		uint64_t started = 0;
		uint64_t stolen = 0;
		uint64_t rejected_voices = 0;
		uint64_t rejected_instances = 0;
		uint64_t rejected_cooldown = 0;
		uint64_t culled = 0;
		uint64_t finished = 0;
		uint64_t virtualized = 0;
		uint64_t realized = 0;
		uint64_t peak = 0;
		uint64_t frames = 0;
		uint64_t usec = 0;
		uint64_t play_usec = 0;
	};

	int max_voices = 32;
	bool steal_voices = true;
	float steal_fade = 0.03f;
	TypedArray<FellowMixSnapshot> snapshots;
	int64_t seed = 0;
	NodePath listener_path;

	LocalVector<Voice> voices;
	LocalVector<Node *> free_players_3d;
	LocalVector<Node *> free_players;
	HashMap<ObjectID, EventState> event_states;
	LocalVector<SnapshotState> snapshot_states;
	HashMap<StringName, float> bus_base; // the buses' levels before the snapshots
	RandomPCG rng;
	double clock = 0.0;
	int next_id = 1;
	int active_count = 0; // voices with a player, not fading out
	uint64_t next_order = 0;
	Stats stats;

	Node3D *_listener() const;
	Vector3 _listener_position() const;
	float _audibility(const Voice &p_voice, const Vector3 &p_listener) const;
	int _active_voices() const;
	Node *_take_player(bool p_spatial);
	void _release(uint32_t p_index);
	void _release_player_to_virtual(uint32_t p_index);
	SnapshotState *_snapshot_state(const Ref<FellowMixSnapshot> &p_snapshot, bool p_create);
	void _update(double p_delta);
	void _restore_buses();
	int _play(const Ref<FellowSoundEvent> &p_event, const Vector3 &p_position, Node3D *p_follow);
	void _realize(Voice &p_voice, float p_from);
	float _virtual_position(const Voice &p_voice, bool &r_ended) const;
	bool _in_reach(const Voice &p_voice, const Vector3 &p_listener, float p_margin) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_max_voices(int p_count) { max_voices = CLAMP(p_count, 1, 1024); }
	int get_max_voices() const { return max_voices; }
	void set_steal_voices(bool p_enabled) { steal_voices = p_enabled; }
	bool is_steal_voices() const { return steal_voices; }
	void set_steal_fade(float p_seconds) { steal_fade = MAX(0.0f, p_seconds); }
	float get_steal_fade() const { return steal_fade; }
	void set_snapshots(const TypedArray<FellowMixSnapshot> &p_snapshots);
	TypedArray<FellowMixSnapshot> get_snapshots() const { return snapshots; }
	void set_seed(int64_t p_seed);
	int64_t get_seed() const { return seed; }
	void set_listener_path(const NodePath &p_path) { listener_path = p_path; }
	NodePath get_listener_path() const { return listener_path; }

	int play(const Ref<FellowSoundEvent> &p_event, const Vector3 &p_position = Vector3());
	int play_on(const Ref<FellowSoundEvent> &p_event, Node3D *p_node);
	void stop(int p_voice, float p_fade = 0.0f);
	void stop_event(const Ref<FellowSoundEvent> &p_event, float p_fade = 0.0f);
	void stop_all(float p_fade = 0.0f);
	bool is_playing(int p_voice) const;
	bool is_virtual(int p_voice) const;
	int get_voice_count() const { return _active_voices(); }
	int get_event_voice_count(const Ref<FellowSoundEvent> &p_event) const;
	Node *get_voice_player(int p_voice) const;
	Array get_voices() const;

	void push_snapshot(const Ref<FellowMixSnapshot> &p_snapshot);
	void pop_snapshot(const Ref<FellowMixSnapshot> &p_snapshot);
	float get_snapshot_weight(const Ref<FellowMixSnapshot> &p_snapshot) const;

	Dictionary get_stats() const;
	void reset_stats() { stats = Stats(); }

	PackedStringArray get_configuration_warnings() const override;

	FellowAudioDirector();
	~FellowAudioDirector();
};

VARIANT_ENUM_CAST(FellowSoundEvent::Selection);
