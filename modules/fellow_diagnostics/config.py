def can_build(env, platform):
    # Milestone 97 (decision 0205): crash reports for shipped games. No dependencies; on every platform (the fatal
    # handlers on Linux/BSD, macOS and Windows; elsewhere only the reports a game writes itself).
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowCrashReporter",
    ]


def get_doc_path():
    return "doc_classes"
