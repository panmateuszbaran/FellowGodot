def can_build(env, platform):
    # Needs the RenderingDevice (Forward+ / Mobile renderers); disabled together with 3D.
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "FellowShallowWater3D",
        "FellowWater3D",
        "FellowRain3D",
        "FellowWind3D",
        "FellowGasVolume3D",
        "FellowGasEmitter3D",
        "FellowBurnable3D",
        "FellowLightning3D",
        "FellowSnowTracker3D",
        "FellowWeather3D",
    ]


def get_doc_path():
    return "doc_classes"
