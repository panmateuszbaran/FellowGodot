def can_build(env, platform):
    # Scene-level world tools (automatic HLOD, streaming, terrain); disabled together with 3D. The terrain's shader
    # comes from fellow_fluid's shared shaders.
    env.module_add_dependencies("fellow_world", ["fellow_fluid"])
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowDestructible3D",
        "FellowHLOD3D",
        "FellowPipelineWarmer3D",
        "FellowStreamingCell3D",
        "FellowTerrain3D",
        "FellowTerrainFoliage",
        "FellowTerrainLayer",
        "FellowWorldStreamer3D",
    ]


def get_doc_path():
    return "doc_classes"
