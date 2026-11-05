def can_build(env, platform):
    # FellowEngine's editor support (milestone 50): gizmos, the BVH importer, the cell splitter. Editor builds only.
    return env.editor_build and not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return ["FellowEditorTools"]


def get_doc_path():
    return "doc_classes"
