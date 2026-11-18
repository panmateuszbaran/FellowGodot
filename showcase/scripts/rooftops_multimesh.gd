extends MultiMeshInstance3D
## A MultiMesh made when the scene starts from its instances (a scripts/rooftops_instances.gd resource the builder
## computes: a MultiMesh's own instances are kept by the rendering server, which a headless build has none of).

@export var mesh: Mesh
@export var instances: Resource


func _ready() -> void:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.mesh = mesh
	var n: int = instances.get("count") if instances else 0
	mm.instance_count = n
	if n > 0:
		mm.buffer = instances.get("data")
	multimesh = mm
