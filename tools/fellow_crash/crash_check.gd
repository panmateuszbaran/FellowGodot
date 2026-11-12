## FellowEngine milestone 97 check (decision 0205): FellowCrashReporter — run by tools/fellow_crash/crash_check.py in a
## project of its own (debug/fellow/crash_reports/enabled), one process per mode:
##
##   godot --headless --path <project> -s res://crash_check.gd -- --mode=crash --kind=segfault|abort|trap|stack_overflow|thread|divide
##       [--delay=<frames>]
##   godot --headless --path <project> -s res://crash_check.gd -- --mode=report|pending|cost
##
## crash: breadcrumbs, custom values and a marker printed, then a crash on purpose two script functions deep (the
## report should hold all of them and the script's backtrace; --delay: a scene made current and frames gone by first). report: a report written by the game. pending: the
## earlier run's report found, then acknowledged. cost: what printing, breadcrumbs and values cost. One JSON line:
## CRASH_CHECK_<MODE> {...}.
extends SceneTree

var args := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and a.contains("="):
			args[a.substr(2, a.find("=") - 2)] = a.substr(a.find("=") + 1)
	var mode: String = args.get("mode", "report")
	var r := FellowCrashReporter
	match mode:
		"crash":
			r.add_breadcrumb("level loaded", "game")
			r.set_custom_value("level", "crash_check")
			r.set_custom_value("player_position", Vector3(1.5, 2.0, -3.25))
			print("CRASH_CHECK_MARKER 4242")
			push_warning("CRASH_CHECK_WARNING a warning before the crash")
			var delay := int(args.get("delay", "0"))
			if delay > 0:
				# Later in a game: a scene current, frames gone by.
				var scene := Node3D.new()
				scene.name = "Harbour"
				root.add_child(scene)
				current_scene = scene
				for i in delay:
					await process_frame
			_level_one(args.get("kind", "segfault"))
		"report":
			r.add_breadcrumb("before the report", "game")
			r.set_custom_value("checkpoint", 7)
			var path := _write("a soft failure")
			var d := FellowCrashReporter.read_report(path)
			_out("REPORT", {
				"path": path,
				"kind": d.get("kind"),
				"reason": d.get("reason"),
				"frames": d.get("frames", []).size(),
				"breadcrumbs": d.get("breadcrumbs", []).map(func(c): return c["text"]),
				"custom": d.get("custom", {}),
				"script_backtraces": d.get("script_backtraces", []),
				"log_tail": d.get("log", []).slice(-3),
				"context": d.get("context", {}),
				"stats": r.get_stats(),
			})
		"pending":
			var p := r.get_pending_report()
			var summary := {"found": not p.is_empty()}
			if not p.is_empty():
				summary["path"] = p.get("path")
				summary["kind"] = p.get("kind")
				summary["reason"] = p.get("reason")
			r.acknowledge_reports()
			summary["after_acknowledge"] = not r.get_pending_report().is_empty()
			summary["reports"] = r.get_reports().size()
			_out("PENDING", summary)
		"cost":
			var n := int(args.get("lines", "20000"))
			var t0 := Time.get_ticks_usec()
			for i in n:
				print("cost line %d of the crash reporter's check, with a little text after it" % i)
			var t1 := Time.get_ticks_usec()
			for i in 10000:
				r.add_breadcrumb("breadcrumb %d" % i, "cost")
			var t2 := Time.get_ticks_usec()
			for i in 10000:
				r.set_custom_value("value_%d" % (i % 16), i)
			var t3 := Time.get_ticks_usec()
			_out("COST", {
				"enabled": r.is_enabled(),
				"print_ns": (t1 - t0) * 1000.0 / n,
				"breadcrumb_ns": (t2 - t1) * 1000.0 / 10000,
				"custom_value_ns": (t3 - t2) * 1000.0 / 10000,
				"stats": r.get_stats(),
			})
	quit()


func _write(reason: String) -> String:
	return FellowCrashReporter.write_report(reason)


func _level_one(kind: String) -> void:
	FellowCrashReporter.add_breadcrumb("level one", "game")
	_level_two(kind)


func _level_two(kind: String) -> void:
	FellowCrashReporter.add_breadcrumb("level two", "game")
	print("CRASH_CHECK_ARMED " + kind)
	FellowCrashReporter.debug_crash(kind)


func _out(mode: String, d: Dictionary) -> void:
	print("CRASH_CHECK_%s %s" % [mode, JSON.stringify(d, "", true)])
