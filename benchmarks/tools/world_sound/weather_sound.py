#!/usr/bin/env python3
"""FellowEngine's weather sound (milestone 178, decision 0320): measures the cases weather_sound.gd rendered.

* rain: loudness (RMS, dBFS), spectral centroid, the share above 9 kHz (small bubbles on water) and below 500 Hz (a
  roof's drumming), drops heard a second (1 ms windows over four times the median), the channels' correlation;
* wind: loudness against speed, centroid, the narrow peaks (edges whistling) against 0.2 v / d for d = 3, 7, 15 mm, a
  gust's loudness following its speed;
* thunder: its onset against the distance / 343 m/s, how long it rolls (50 ms windows within 30 dB of the loudest), its
  peak, the centroid of its first 0.25 s (the crack) and of all of it, the channels' balance (a strike to the right);
* the CPU's time a second of sound.

Writes <dir>/report.json, <dir>/spectrograms.png and <dir>/showreel.wav (the cases one after another, for listening).

    python benchmarks/tools/world_sound/weather_sound.py <dir>
"""

import json
import os
import sys
import wave

import numpy as np


def load(d, name):
    x = np.fromfile(os.path.join(d, name + ".f32"), dtype="<f4")
    return x.reshape(-1, 2)


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-12))


def rms(x):
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def centroid(m, rate):
    spec = np.abs(np.fft.rfft(m)) ** 2
    f = np.fft.rfftfreq(len(m), 1.0 / rate)
    return float((f * spec).sum() / (spec.sum() + 1e-30)), spec, f


def rain_metrics(x, rate):
    y = x[int(0.5 * rate) :]
    m = y.mean(axis=1)
    c, spec, f = centroid(m, rate)
    total = spec.sum() + 1e-30
    k = int(0.001 * rate)
    win = np.sqrt(np.array([np.mean(m[i : i + k] ** 2) for i in range(0, len(m) - k, k)]))
    med = np.median(win) + 1e-12
    hits = int(np.sum((win[1:] > 4 * med) & (win[:-1] <= 4 * med)))
    return {
        "rms_dbfs": db(rms(y)),
        "centroid_hz": c,
        "above_9k": float(spec[f > 9000].sum() / total),
        "below_500": float(spec[f < 500].sum() / total),
        "drops_per_s": hits / (len(m) / rate),
        "lr_correlation": float(np.corrcoef(y[:, 0], y[:, 1])[0, 1]),
    }


def wind_metrics(x, rate, v):
    y = x[int(0.8 * rate) :]
    m = y.mean(axis=1)
    c, _, _ = centroid(m, rate)
    out = {"rms_dbfs": db(rms(y)), "centroid_hz": c}
    # Whistles: the spectrum (2 Hz bins) smoothed over 60 Hz; peaks standing 8 dB over it.
    n = rate // 2
    seg = m[: (len(m) // n) * n].reshape(-1, n)
    p = np.mean(np.abs(np.fft.rfft(seg * np.hanning(n), axis=1)) ** 2, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    smooth = np.convolve(p, np.ones(31) / 31, mode="same")
    ratio = 10 * np.log10((p + 1e-30) / (smooth + 1e-30))
    peaks = []
    for d_mm in (3, 7, 15):
        fp = 0.2 * v / (d_mm / 1000.0)
        band = (f > fp * 0.75) & (f < fp * 1.25)
        if band.any():
            i = np.argmax(np.where(band, p, 0))
            peaks.append({"edge_mm": d_mm, "predicted_hz": fp, "found_hz": float(f[i]), "over_db": float(ratio[i])})
    out["whistles"] = peaks
    return out


def thunder_metrics(x, rate, distance):
    m = x.mean(axis=1)
    a = np.abs(m)
    onset_i = int(np.argmax(a > a.max() * 1e-3))
    onset = onset_i / rate
    k = int(0.05 * rate)
    win = np.sqrt(np.array([np.mean(m[i : i + k] ** 2) for i in range(0, len(m) - k, k)])) + 1e-12
    over = np.where(win > win.max() * 10 ** (-30 / 20))[0]
    rolls = float((over[-1] + 1) * k / rate - onset) if len(over) else 0.0
    crack, _, _ = centroid(m[onset_i : onset_i + int(0.25 * rate)], rate)
    whole, _, _ = centroid(m[onset_i:], rate)
    lr = db(rms(x[onset_i:, 1])) - db(rms(x[onset_i:, 0]))
    return {
        "onset_s": onset,
        "expected_onset_s": distance / 343.0,
        "rolls_s": rolls,
        "peak_dbfs": db(a.max()),
        "rms_dbfs": db(rms(x[onset_i : onset_i + int(3 * rate)])),
        "crack_centroid_hz": crack,
        "centroid_hz": whole,
        "right_minus_left_db": lr,
    }


def main():
    d = sys.argv[1]
    idx = json.load(open(os.path.join(d, "cases.json")))
    rate = int(idx["rate"])
    cases = idx["cases"]
    rep = {"rate": rate, "rain": {}, "wind": {}, "thunder": {}, "cpu": {}}
    for name, c in sorted(cases.items()):
        x = load(d, name)
        rep["cpu"][name] = c["usec_per_second"]
        if name.startswith("rain_"):
            rep["rain"][name] = rain_metrics(x, rate)
        elif name == "wind_gust":
            m = x.mean(axis=1)
            k = int(0.1 * rate)
            rep["wind"][name] = {"rms_100ms_dbfs": [db(rms(m[i : i + k])) for i in range(0, len(m) - k, k)]}
        elif name.startswith("wind_"):
            rep["wind"][name] = wind_metrics(x, rate, c["speed"])
        elif name.startswith("thunder_"):
            rep["thunder"][name] = thunder_metrics(x, rate, c["distance"])
    json.dump(rep, open(os.path.join(d, "report.json"), "w"), indent=1)

    print("rain:")
    for n, r in rep["rain"].items():
        print(
            "  %-16s %6.1f dBFS, centroid %5.0f Hz, above 9 kHz %4.1f %%, below 500 Hz %4.1f %%, drops heard %4.0f/s, L/R correlation %.2f"
            % (
                n,
                r["rms_dbfs"],
                r["centroid_hz"],
                100 * r["above_9k"],
                100 * r["below_500"],
                r["drops_per_s"],
                r["lr_correlation"],
            )
        )
    print("wind:")
    for n, r in rep["wind"].items():
        if n == "wind_gust":
            env = r["rms_100ms_dbfs"]
            print("  %-16s loudness every 0.5 s: %s" % (n, " ".join("%.0f" % v for v in env[::5])))
            continue
        wh = ", ".join(
            "%d mm %.0f Hz (found %.0f, +%.0f dB)" % (w["edge_mm"], w["predicted_hz"], w["found_hz"], w["over_db"])
            for w in r["whistles"]
        )
        print("  %-16s %6.1f dBFS, centroid %5.0f Hz; whistles: %s" % (n, r["rms_dbfs"], r["centroid_hz"], wh))
    print("thunder:")
    for n, r in rep["thunder"].items():
        print(
            "  %-19s heard at %.3f s (distance / 343: %.3f), rolls %.1f s, peak %5.1f dBFS, 3 s RMS %5.1f dBFS, crack centroid %5.0f Hz, all %4.0f Hz, right - left %+.1f dB"
            % (
                n,
                r["onset_s"],
                r["expected_onset_s"],
                r["rolls_s"],
                r["peak_dbfs"],
                r["rms_dbfs"],
                r["crack_centroid_hz"],
                r["centroid_hz"],
                r["right_minus_left_db"],
            )
        )
    print("CPU (µs a second of sound): " + ", ".join("%s %.0f" % kv for kv in sorted(rep["cpu"].items())))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    show = ["rain_ground_0.5", "rain_water_0.5", "rain_roof_0.5", "wind_gust", "thunder_300m", "thunder_3000m"]
    fig, axes = plt.subplots(2, 3, figsize=(15, 7.5))
    for ax, name in zip(axes.flat, show):
        m = load(d, name).mean(axis=1)
        ax.specgram(m + 1e-9, NFFT=2048, Fs=rate, noverlap=1536, cmap="magma", vmin=-150, vmax=-40)
        ax.set_ylim(0, 20000 if name.startswith("rain") else 6000)
        ax.set_title(name)
        ax.set_xlabel("s")
        ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=90)

    order = ["rain_ground_0.2", "rain_ground_0.5", "rain_ground_1.0", "rain_water_0.5", "rain_roof_0.5"]
    order += ["wind_3.0ms", "wind_8.0ms", "wind_14.0ms", "wind_20.0ms", "wind_gust"]
    order += ["thunder_300m", "thunder_800m", "thunder_800m_ahead", "thunder_3000m"]
    gap = np.zeros((int(0.5 * rate), 2), dtype=np.float32)
    parts = []
    for k in order:
        x = load(d, k)
        if k.startswith("thunder"):
            on = int(rep["thunder"][k]["onset_s"] * rate)
            x = x[max(0, on - int(0.4 * rate)) :]
        elif k.startswith("rain") or (k.startswith("wind") and k != "wind_gust"):
            x = x[: int(2.5 * rate)]
        parts += [x, gap]
    reel = np.concatenate(parts)
    pcm = (np.clip(reel, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(d, "showreel.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    print("showreel: " + ", ".join(order))


if __name__ == "__main__":
    main()
