#!/usr/bin/env python3
"""FellowEngine's footsteps (milestone 181, decision 0323): measures the cases footstep_sound.gd rendered.

* each ground (four walking steps, sneakers, 75 kg): the peak and RMS (dBFS at sound_unit_size), a step's loudness
  against a step on stone (its K-weighted energy, ITU-R BS.1770's filter: the grounds' balance, milestone 181b), the
  spectral centroid, the share of the sound under 300 Hz, 300 Hz-2 kHz, 2-6 kHz and over 6 kHz, the grains and
  bubbles a step;
* the shoes on stone and wood: the centroid, the share over 2 kHz (the click);
* sneaking, walking, running and landing on stone and gravel: their peaks against walking's;
* a light (50 kg) and a heavy (100 kg) walker on gravel; grass over dirt;
* a foot scuffing: on wood a sneaker's squeak (the strongest narrow peak between 1.2 and 2.4 kHz over the spectrum
  round it) against a boot's;
* the CPU's time a second of sound.

Writes <dir>/report.json, <dir>/spectrograms.png and <dir>/showreel.wav.

    python benchmarks/tools/world_sound/footstep_sound.py <dir>
"""

import json
import os
import sys
import wave

import numpy as np

GROUNDS = [
    "stone",
    "wood",
    "metal",
    "tile",
    "carpet",
    "gravel",
    "sand",
    "snow",
    "grass",
    "leaves",
    "dirt",
    "mud",
    "water",
]
SHOES = ["sneaker", "boot", "hard", "barefoot"]


def load(d, name):
    return np.fromfile(os.path.join(d, name + ".f32"), dtype="<f4").reshape(-1, 2)[:, 0].astype(np.float64)


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def spectrum(x, rate):
    sp = np.abs(np.fft.rfft(x)) ** 2
    return sp, np.fft.rfftfreq(len(x), 1.0 / rate)


def describe(x, rate):
    sp, f = spectrum(x, rate)
    tot = sp.sum() + 1e-30
    bands = [(0, 300), (300, 2000), (2000, 6000), (6000, rate / 2)]
    return {
        "peak_dbfs": db(np.abs(x).max()),
        "rms_dbfs": db(rms(x)),
        "centroid_hz": float((f * sp).sum() / tot),
        "bands": [float(sp[(f >= lo) & (f < hi)].sum() / tot) for lo, hi in bands],
    }


def _biquad(b, a, x):
    try:
        from scipy.signal import lfilter

        return lfilter(b, a, x)
    except ImportError:
        y = np.zeros_like(x)
        x1 = x2 = y1 = y2 = 0.0
        for i, v in enumerate(x):
            o = b[0] * v + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
            x2, x1, y2, y1 = x1, v, y1, o
            y[i] = o
        return y


def step_loudness(x, rate, steps, period=0.55, start=0.1):
    """A step's loudness (LUFS-like): its K-weighted energy (BS.1770's filter at 48 kHz) over a second, the mean over the steps."""
    k = _biquad([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585], x)
    k = _biquad([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621], k)
    e = []
    for i in range(steps):
        a = int((start + period * i - 0.01) * rate)
        b = int((start + period * (i + 1) - 0.01) * rate)
        e.append(10 * np.log10(np.sum(k[a:b] ** 2) / rate + 1e-30) - 0.691)
    return float(np.mean(e))


def squeak(x, rate):
    n = 8192
    seg = x[: (len(x) // n) * n].reshape(-1, n)
    p = np.mean(np.abs(np.fft.rfft(seg * np.hanning(n), axis=1)) ** 2, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    smooth = np.convolve(p, np.ones(41) / 41, mode="same")
    ratio = 10 * np.log10((p + 1e-30) / (smooth + 1e-30))
    band = (f > 1200) & (f < 2400)
    i = int(np.argmax(np.where(band, ratio, -1e9)))
    return {"found_hz": float(f[i]), "over_db": float(ratio[i])}


def main():
    d = sys.argv[1]
    idx = json.load(open(os.path.join(d, "cases.json")))
    rate = int(idx["rate"])
    cases = idx["cases"]
    rep = {"rate": rate, "grounds": {}, "shoes": {}, "gaits": {}, "weight": {}, "scuff": {}, "cpu": {}}
    for name, c in sorted(cases.items()):
        rep["cpu"][name] = c["usec_per_second"]
    for g in GROUNDS:
        c = cases["walk_" + g]
        x = load(d, "walk_" + g)
        r = describe(x, rate)
        r["step_loudness"] = step_loudness(x, rate, c["steps"])
        r["grains_per_step"] = c["grains"] / c["steps"]
        r["bubbles_per_step"] = c["bubbles"] / c["steps"]
        rep["grounds"][g] = r
    for g in GROUNDS:
        rep["grounds"][g]["over_stone_lu"] = (
            rep["grounds"][g]["step_loudness"] - rep["grounds"]["stone"]["step_loudness"]
        )
    rep["grounds"]["grass over dirt"] = describe(load(d, "grass_over_dirt"), rate)
    for s, shoe in enumerate(SHOES):
        for g in ("stone", "wood"):
            rep["shoes"]["%s on %s" % (shoe, g)] = describe(load(d, "shoe%d_%s" % (s, g)), rate)
    for g in ("stone", "gravel"):
        walk = db(np.abs(load(d, "walk_" + g)).max())
        for gait in ("sneak", "run", "land"):
            rep["gaits"]["%s on %s" % (gait, g)] = {
                "peak_dbfs": db(np.abs(load(d, "%s_%s" % (gait, g))).max()),
                "over_walking_db": db(np.abs(load(d, "%s_%s" % (gait, g))).max()) - walk,
            }
    light = rms(load(d, "light_gravel"))
    heavy = rms(load(d, "heavy_gravel"))
    rep["weight"] = {
        "light_rms_dbfs": db(light),
        "heavy_rms_dbfs": db(heavy),
        "heavy_over_light_db": db(heavy) - db(light),
    }
    for name in ("scuff_wood_shoe0", "scuff_wood_shoe1", "scuff_gravel_shoe0"):
        x = load(d, name)[int(0.2 * rate) : int(0.85 * rate)]
        r = describe(x, rate)
        r.update(squeak(x, rate))
        rep["scuff"][name] = r
    json.dump(rep, open(os.path.join(d, "report.json"), "w"), indent=1)

    print("grounds (four walking steps, sneakers, 75 kg; at sound_unit_size):")
    for g, r in rep["grounds"].items():
        extra = ""
        if "over_stone_lu" in r:
            extra += ", a step %+5.1f LU over stone's" % r["over_stone_lu"]
        if "grains_per_step" in r:
            extra += ", %.0f grains, %.0f bubbles a step" % (r["grains_per_step"], r["bubbles_per_step"])
        print(
            "  %-15s peak %6.1f dBFS, RMS %6.1f, centroid %5.0f Hz, under 300 Hz %3.0f %%, to 2 kHz %3.0f %%, to 6 kHz %3.0f %%, over %3.0f %%%s"
            % (
                g,
                r["peak_dbfs"],
                r["rms_dbfs"],
                r["centroid_hz"],
                100 * r["bands"][0],
                100 * r["bands"][1],
                100 * r["bands"][2],
                100 * r["bands"][3],
                extra,
            )
        )
    print("shoes:")
    for n, r in rep["shoes"].items():
        print(
            "  %-18s peak %6.1f dBFS, centroid %5.0f Hz, over 2 kHz %3.0f %%"
            % (n, r["peak_dbfs"], r["centroid_hz"], 100 * (r["bands"][2] + r["bands"][3]))
        )
    print("gaits: " + ", ".join("%s %+.1f dB" % (n, r["over_walking_db"]) for n, r in rep["gaits"].items()))
    print("weight on gravel: 100 kg over 50 kg %+.1f dB" % rep["weight"]["heavy_over_light_db"])
    for n, r in rep["scuff"].items():
        print(
            "scuff %s: RMS %.1f dBFS, centroid %.0f Hz, a narrow peak at %.0f Hz %+.1f dB over the spectrum round it"
            % (n, r["rms_dbfs"], r["centroid_hz"], r["found_hz"], r["over_db"])
        )
    print("CPU (µs a second of sound): " + ", ".join("%s %.0f" % kv for kv in sorted(rep["cpu"].items())))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    show = [
        "walk_stone",
        "shoe2_stone",
        "walk_wood",
        "walk_metal",
        "walk_gravel",
        "walk_snow",
        "walk_leaves",
        "walk_water",
    ]
    fig, axes = plt.subplots(2, 4, figsize=(16, 7.5))
    for ax, name in zip(axes.flat, show):
        x = load(d, name)[: int(1.3 * rate)]
        ax.specgram(x + 1e-9, NFFT=1024, Fs=rate, noverlap=896, cmap="magma", vmin=-170, vmax=-50)
        ax.set_ylim(0, 10000)
        ax.set_title(name)
        ax.set_xlabel("s")
        ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=80)

    order = ["walk_" + g for g in GROUNDS] + [
        "shoe1_stone",
        "shoe2_stone",
        "shoe3_stone",
        "sneak_gravel",
        "run_gravel",
        "land_gravel",
        "scuff_wood_shoe0",
    ]
    gap = np.zeros(int(0.4 * rate))
    parts = []
    for k in order:
        parts += [load(d, k)[: int(2.3 * rate)], gap]
    reel = np.concatenate(parts)
    pcm = (np.clip(np.repeat(reel[:, None], 2, axis=1), -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(d, "showreel.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    print("showreel: " + ", ".join(order))


if __name__ == "__main__":
    main()
