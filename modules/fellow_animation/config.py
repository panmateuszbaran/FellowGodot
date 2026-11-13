def can_build(env, platform):
    # Character animation (motion matching, traversal); disabled together with 3D.
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowFaceAnimator",
        "FellowHeadMotion3D",
        "FellowLadder3D",
        "FellowMotionDatabase",
        "FellowMotionMatcher3D",
        "FellowTraversal3D",
        "FellowTraversalPose3D",
    ]


def get_doc_path():
    return "doc_classes"
