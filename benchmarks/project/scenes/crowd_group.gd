extends Node3D
## Crowd Lab --crowd_groups=N: one of N process thread groups (Node.PROCESS_THREAD_GROUP_SUB_THREAD). Its _process() runs
## on a worker thread, in parallel with the other groups, and poses its characters [first, last) at the crowd's time;
## their skeletons' updates (deferred thread-group notifications) follow on the same thread.

var crowd: Node3D
var first := 0
var last := 0


func _process(_delta: float) -> void:
	crowd.apply_time(first, last)
