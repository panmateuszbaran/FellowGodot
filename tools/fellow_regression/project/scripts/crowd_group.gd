extends Node3D
## The regression crowd's process thread group (Node.PROCESS_THREAD_GROUP_SUB_THREAD): its _process() runs on a worker
## thread and poses its characters [first, last); their skeletons' updates follow on the same thread (decision 0164).

var crowd: Node3D
var first := 0
var last := 0


func _process(_delta: float) -> void:
	crowd.apply(first, last)
