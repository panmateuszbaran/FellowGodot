def can_build(env, platform):
    # Needs the RenderingDevice (a local device of its own); disabled together with 3D.
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowMotionModel",
        "FellowMotionCharacter3D",
    ]


def get_doc_path():
    return "doc_classes"
