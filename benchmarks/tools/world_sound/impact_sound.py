#!/usr/bin/env python3
"""FellowEngine's contact sound (milestone 180, decision 0322): measures the cases impact_sound.gd rendered.

* each body struck in the hand and dropped on stone: its peak and loudness (the first 50 ms, dBFS: full scale is 120 dB
  SPL at a metre), its spectral centroid (the first 0.3 s), the contact's duration and the click's peak;
* the materials: the strongest mode's decay measured (a band of a sixth of an octave round it, the slope of its level)
  against the synthesis's (T60 = 6.9 / decay);
* the steel bar and pipe: their lowest mode against a free-free beam's (f1 = 22.37 / (2 pi) c kappa / L^2);
* where the plank is struck: its second mode over its first, at its end and in its middle (a node of the second);
* the crate dropped at 0.5-8 m/s: loudness (dB per doubling) and brightness; struck by metal and by rubber;
* sliding at 0.3-3 m/s: loudness against speed (dB a decade), centroid; the pipe rolling;
* the CPU's time a second of sound.

Writes <dir>/report.json, <dir>/spectrograms.png and <dir>/showreel.wav.

    python benchmarks/tools/world_sound/impact_sound.py <dir>
"""

import json
import os
import sys
import wave

import numpy as np


def load(d, name):
    return np.fromfile(os.path.join(d, name + ".f32"), dtype="<f4").reshape(-1, 2)[:, 0].astype(np.float64)


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2))) if len(x) else 0.0


def centroid(x, rate):
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / rate)
    return float((f * sp).sum() / (sp.sum() + 1e-30))


def band(x, rate, lo, hi):
    from scipy.signal import butter, sosfilt

    return sosfilt(butter(4, [lo, hi], "bandpass", fs=rate, output="sos"), x)


def mode_decay(x, rate, f0, start):
    # The level of a sixth of an octave round the mode in 20 ms windows, from its loudest (the band's filter takes a
    # while to fill) for 0.8 s or until 40 dB down: its slope, dB a second, as a T60.
    y = band(x, rate, f0 / 1.06, f0 * 1.06)
    k = int(0.02 * rate)
    i0 = int(start * rate)
    lv = np.array([db(rms(y[i : i + k])) for i in range(i0, len(y) - k, k)])
    top = int(np.argmax(lv[: int(0.5 / 0.02)]))
    lv = lv[top : top + int(0.8 / 0.02)]
    keep = lv > lv[0] - 40.0
    t = np.arange(len(lv))[keep] * 0.02
    slope = np.polyfit(t, lv[keep], 1)[0] if keep.sum() > 3 else float("nan")
    return -60.0 / slope if slope < 0 else float("nan")


def main():
    d = sys.argv[1]
    idx = json.load(open(os.path.join(d, "cases.json")))
    rate = int(idx["rate"])
    cases = idx["cases"]
    rep = {
        "rate": rate,
        "bodies": {},
        "decay": {},
        "beam": {},
        "position": {},
        "speed": {},
        "hardness": {},
        "slide": {},
        "cpu": {},
    }
    for name, c in sorted(cases.items()):
        rep["cpu"][name] = c["usec_per_second"]
    on = 0.05
    for name, c in sorted(cases.items()):
        if not (name.startswith("held_") or name.startswith("drop_")):
            continue
        x = load(d, name)
        i = int(on * rate)
        rep["bodies"][name] = {
            "peak_dbfs": db(np.abs(x).max()),
            "rms_50ms_dbfs": db(rms(x[i : i + int(0.05 * rate)])),
            "centroid_hz": centroid(x[i : i + int(0.3 * rate)], rate),
            "contact_ms": 1000.0 * c["contact_durations"][0],
            "click_dbfs": db(c["clicks"][0]),
            "lowest_modes_hz": c["modes_hz"][:4],
        }
        if name.startswith("held_") and c["modes_hz"]:
            # The strongest mode in the first 0.3 s.
            seg = x[i : i + int(0.3 * rate)]
            sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
            f = np.fft.rfftfreq(len(seg), 1.0 / rate)
            # (Not one with another mode within 2 %: two beating slowly in the band would bend the slope.)
            ms = c["modes_hz"]
            alone = [
                m
                for m in range(len(ms))
                if ms[m] < 4000.0 and all(abs(ms[m] - ms[j]) > 0.02 * ms[m] for j in range(len(ms)) if j != m)
            ]
            if alone:
                best = max(alone, key=lambda m: sp[np.argmin(np.abs(f - ms[m]))])
                f0 = c["modes_hz"][best]
                rep["decay"][name[5:]] = {
                    "mode_hz": f0,
                    "t60_measured_s": mode_decay(x, rate, f0, on),
                    "t60_synthesised_s": 6.9 / c["decays"][best],
                }
            else:
                rep["decay"][name[5:]] = {
                    "mode_hz": ms[0],
                    "t60_measured_s": float("nan"),
                    "t60_synthesised_s": 6.9 / c["decays"][0],
                    "note": "its modes in pairs (beating)",
                }
    for name, kappa, length, cl in (
        ("bar", 0.03 / 3.464, 1.0, (200e9 / 7850) ** 0.5),
        ("pipe", 0.06 / 2.83, 1.0, (200e9 / 7850) ** 0.5),
    ):
        c = cases["held_" + name]
        f1 = 4.730**2 / (2 * np.pi) * cl * kappa / length**2
        x = load(d, "held_" + name)
        seg = x[int(on * rate) : int((on + 1.0) * rate)]
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
        f = np.fft.rfftfreq(len(seg), 1.0 / rate)
        sel = (f > f1 * 0.8) & (f < f1 * 1.25)
        rep["beam"][name] = {
            "theory_hz": f1,
            "synthesised_hz": c["modes_hz"][0],
            "found_hz": float(f[sel][np.argmax(sp[sel])]),
        }
    for name in ("plank_end", "plank_middle"):
        c = cases[name]
        x = load(d, name)[int(on * rate) : int((on + 1.0) * rate)]
        m1, m2 = c["modes_hz"][0], c["modes_hz"][1]
        e1 = rms(band(x, rate, m1 / 1.06, m1 * 1.06))
        e2 = rms(band(x, rate, m2 / 1.06, m2 * 1.06))
        rep["position"][name] = {"mode1_hz": m1, "mode2_hz": m2, "second_over_first_db": db(e2) - db(e1)}
    speeds = []
    for v in (0.5, 1.0, 2.0, 4.0, 8.0):
        name = "speed_%s" % str(v)
        x = load(d, name)
        i = int(on * rate)
        r = {
            "speed": v,
            "peak_dbfs": db(np.abs(x).max()),
            "rms_50ms_dbfs": db(rms(x[i : i + int(0.05 * rate)])),
            "centroid_hz": centroid(x[i : i + int(0.3 * rate)], rate),
            "contact_ms": 1000.0 * cases[name]["contact_durations"][0],
        }
        rep["speed"][name] = r
        speeds.append(r)
    rep["speed_db_per_doubling"] = float(
        np.polyfit(np.log2([r["speed"] for r in speeds]), [r["rms_50ms_dbfs"] for r in speeds], 1)[0]
    )
    for name in ("crate_by_metal", "crate_by_rubber", "held_crate"):
        x = load(d, name)
        i = int(on * rate)
        rep["hardness"][name] = {
            "contact_ms": 1000.0 * cases[name]["contact_durations"][0],
            "centroid_hz": centroid(x[i : i + int(0.3 * rate)], rate),
            "rms_50ms_dbfs": db(rms(x[i : i + int(0.05 * rate)])),
        }
    sl = []
    for v in (0.3, 1.0, 3.0):
        name = "slide_%s" % str(v)
        x = load(d, name)[int(0.5 * rate) :]
        r = {"speed": v, "rms_dbfs": db(rms(x)), "centroid_hz": centroid(x[: int(1.0 * rate)], rate)}
        rep["slide"][name] = r
        sl.append(r)
    rep["slide_db_per_decade"] = float(
        np.polyfit(np.log10([r["speed"] for r in sl]), [r["rms_dbfs"] for r in sl], 1)[0]
    )
    x = load(d, "roll_pipe")[int(0.5 * rate) :]
    rep["slide"]["roll_pipe"] = {
        "speed": 1.0,
        "rms_dbfs": db(rms(x)),
        "centroid_hz": centroid(x[: int(1.0 * rate)], rate),
    }
    json.dump(rep, open(os.path.join(d, "report.json"), "w"), indent=1)

    print("bodies (struck at 2 m/s in the hand / dropped at 3 m/s on stone):")
    for n, r in rep["bodies"].items():
        print(
            "  %-12s peak %6.1f dBFS, 50 ms %6.1f, centroid %5.0f Hz, contact %5.2f ms, click %6.1f dBFS, modes %s"
            % (
                n,
                r["peak_dbfs"],
                r["rms_50ms_dbfs"],
                r["centroid_hz"],
                r["contact_ms"],
                r["click_dbfs"],
                ", ".join("%.0f" % f for f in r["lowest_modes_hz"]),
            )
        )
    print("decay of the strongest mode (T60 measured / synthesised):")
    for n, r in rep["decay"].items():
        print(
            "  %-8s %6.0f Hz: %5.2f s / %5.2f s %s"
            % (n, r["mode_hz"], r["t60_measured_s"], r["t60_synthesised_s"], r.get("note", ""))
        )
    for n, r in rep["beam"].items():
        print(
            "beam %s: lowest mode theory %.0f Hz, synthesised %.0f, found %.0f"
            % (n, r["theory_hz"], r["synthesised_hz"], r["found_hz"])
        )
    for n, r in rep["position"].items():
        print(
            "%s: mode 2 (%.0f Hz) over mode 1 (%.0f Hz) %+.1f dB"
            % (n, r["mode2_hz"], r["mode1_hz"], r["second_over_first_db"])
        )
    print(
        "crate dropped: "
        + ", ".join(
            "%.1f m/s %.1f dBFS %.0f Hz (contact %.2f ms)"
            % (r["speed"], r["rms_50ms_dbfs"], r["centroid_hz"], r["contact_ms"])
            for r in speeds
        )
        + "; %.1f dB a doubling" % rep["speed_db_per_doubling"]
    )
    print(
        "hardness: "
        + ", ".join(
            "%s contact %.2f ms centroid %.0f Hz %.1f dBFS" % (n, r["contact_ms"], r["centroid_hz"], r["rms_50ms_dbfs"])
            for n, r in rep["hardness"].items()
        )
    )
    print(
        "sliding: "
        + ", ".join("%.1f m/s %.1f dBFS %.0f Hz" % (r["speed"], r["rms_dbfs"], r["centroid_hz"]) for r in sl)
        + "; %.1f dB a decade" % rep["slide_db_per_decade"]
    )
    r = rep["slide"]["roll_pipe"]
    print("pipe rolling at 1 m/s: %.1f dBFS, %.0f Hz" % (r["rms_dbfs"], r["centroid_hz"]))
    print("CPU (µs a second of sound): " + ", ".join("%s %.0f" % kv for kv in sorted(rep["cpu"].items())))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    show = [
        ("held_crate", 3000),
        ("held_pipe", 8000),
        ("held_bottle", 12000),
        ("drop_stone", 12000),
        ("held_sheet", 3000),
        ("slide_1.0", 6000),
    ]
    fig, axes = plt.subplots(2, 3, figsize=(15, 7.5))
    for ax, (name, top) in zip(axes.flat, show):
        x = load(d, name)[: int(1.5 * rate)]
        ax.specgram(x + 1e-9, NFFT=1024, Fs=rate, noverlap=896, cmap="magma", vmin=-160, vmax=-30)
        ax.set_ylim(0, top)
        ax.set_title(name)
        ax.set_xlabel("s")
        ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=90)

    order = [
        "held_crate",
        "drop_crate",
        "held_plank",
        "plank_middle",
        "held_pipe",
        "drop_pipe",
        "held_bar",
        "held_bottle",
        "drop_bottle",
        "drop_stone",
        "drop_ball",
        "held_sheet",
    ]
    order += [
        "speed_0.5",
        "speed_2.0",
        "speed_8.0",
        "crate_by_metal",
        "crate_by_rubber",
        "slide_0.3",
        "slide_1.0",
        "slide_3.0",
        "roll_pipe",
    ]
    gap = np.zeros(int(0.4 * rate))
    parts = []
    for k in order:
        parts += [load(d, k)[: int(1.6 * rate)], gap]
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
