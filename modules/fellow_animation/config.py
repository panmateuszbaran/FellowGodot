def can_build(env, platform):
    # Character animation (motion matching); disabled together with 3D.
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowMotionDatabase",
        "FellowMotionMatcher3D",
    ]


def get_doc_path():
    return "doc_classes"
