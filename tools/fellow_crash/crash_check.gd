## FellowEngine milestone 97 check (decision 0205): FellowCrashReporter — run by tools/fellow_crash/crash_check.py in a
## project of its own (debug/fellow/crash_reports/enabled), one process per mode:
##
##   godot --headless --path <project> -s res://crash_check.gd -- --mode=crash --kind=segfault|abort|trap|stack_overflow|thread|divide
##       [--delay=<frames>]
##   godot --headless --path <project> -s res://crash_check.gd -- --mode=report|pending|cost|hang|freeze [--stall_ms=N]
##   godot --headless --path <project> -s res://crash_check.gd -- --mode=upload [--url=<collector>] | --mode=makecert --key=<path> --cert=<path>
##
## crash: breadcrumbs, custom values and a marker printed, then a crash on purpose two script functions deep (the
## report should hold all of them and the script's backtrace; --delay: a scene made current and frames gone by first). report: a report written by the game. pending: the
## earlier run's report found, then acknowledged. cost: what printing, breadcrumbs and values cost. hang (milestone
## 98): the main thread stalled for --stall_ms two script functions deep, then going on; freeze: in a loop that never
## ends (the checker kills it). upload (milestone 102): the reports sent to a collector. One JSON line:
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
		"hang", "freeze":
			# Milestone 98: the main thread stalled two script functions deep — for --stall_ms and then going on, or
			# (freeze) in a loop that never ends.
			r.add_breadcrumb("before the stall", "game")
			for i in 20:
				await process_frame
			if mode == "hang":
				_stall_one(int(args.get("stall_ms", "3500")))
				for i in 10:
					await process_frame
				print("CRASH_CHECK_AFTER_STALL")
				_out("HANG", r.get_stats())
			else:
				_spin_one()
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
		"upload":
			# Milestone 102: the reports sent to --url (none: the project's upload_url, and with upload_automatically
			# the reporter has started on its own); each report_uploaded signal and the uploads_finished one.
			var uploaded := []
			var finished := []
			r.report_uploaded.connect(func(path, ok, code): uploaded.append({"file": path.get_file(), "ok": ok, "code": code}))
			r.uploads_finished.connect(func(sent, failed): finished.append([sent, failed]))
			var before := r.get_reports().size()
			var started := OK
			if args.has("url"):
				started = r.upload_reports(args["url"])
			var t0 := Time.get_ticks_msec()
			while finished.is_empty() and Time.get_ticks_msec() - t0 < 60000:
				await process_frame
			_out("UPLOAD", {
				"started": started,
				"before": before,
				"uploaded": uploaded,
				"finished": finished,
				"uploading_after": r.is_uploading(),
				"reports_after": Array(r.get_reports()).map(func(p): return p.get_file()),
				"msec": Time.get_ticks_msec() - t0,
			})
		"makecert":
			# A self-signed certificate for localhost, for the TLS upload's server.
			var crypto := Crypto.new()
			var key := crypto.generate_rsa(2048)
			var cert := crypto.generate_self_signed_certificate(key, "CN=localhost,O=FellowEngine,C=PL", "20250101000000", "20350101000000")
			key.save(args["key"])
			cert.save(args["cert"])
			_out("MAKECERT", {"ok": FileAccess.file_exists(args["key"]) and FileAccess.file_exists(args["cert"])})
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


func _stall_one(ms: int) -> void:
	_stall_two(ms)


func _stall_two(ms: int) -> void:
	OS.delay_msec(ms)


func _spin_one() -> void:
	_spin_two()


func _spin_two() -> void:
	var n := 0
	while true:
		n += 1


func _out(mode: String, d: Dictionary) -> void:
	print("CRASH_CHECK_%s %s" % [mode, JSON.stringify(d, "", true)])
