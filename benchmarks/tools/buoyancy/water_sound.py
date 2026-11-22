#!/usr/bin/env python3
"""FellowEngine's water sound (milestone 177, decision 0319): measures the cases water_sound.gd rendered.

* single bubbles: the spectrum's peak against Minnaert's frequency (3.26 m/s / radius); the envelope's decay against
  the model's damping (0.13 / r + 0.0072 r^-1.5); the pitch from 0.15 to 1 decay time against the model's rise (xi 0.1);
* splashes: peak and loudness (RMS over 0.3 s, dBFS), the spectral centroid, the low share (< 300 Hz), how long they
  ring (10 ms windows within 40 dB of the loudest), against speed and size;
* a wake's loudness at each level; the CPU's time a second of sound.

Writes <dir>/report.json, <dir>/spectrograms.png and <dir>/showreel.wav (every case one after another, for listening).
--timing: sound_timing.gd's strikes against their onsets in Movie Maker's sound.

    python benchmarks/tools/buoyancy/water_sound.py <dir>
    python benchmarks/tools/buoyancy/water_sound.py --timing <movie.wav> <sound_timing.gd's output>
"""

import json
import os
import sys
import wave

import numpy as np


def load(d, name, rate):
    return np.fromfile(os.path.join(d, name + ".f32"), dtype="<f4")


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-12))


def envelope(x):
    from scipy.signal import hilbert

    return np.abs(hilbert(x))


def bubble_metrics(x, rate, radius, still):
    start = int(0.01 * rate)
    y = x[start:]
    n = 1 << 20
    spec = np.abs(np.fft.rfft(y * np.hanning(len(y)), n))
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    peak = float(freqs[int(np.argmax(spec))])
    f_model = 3.26 / radius
    d_model = 0.13 / radius + 0.0072 * radius**-1.5
    out = {"minnaert_hz": f_model, "peak_hz": peak}
    env = envelope(y)
    # The decay: log envelope's slope over its first 30 dB.
    e0 = env[: int(0.002 * rate) + 8].max()
    idx = np.where(env < e0 * 10 ** (-30 / 20))[0]
    end = int(idx[0]) if len(idx) else len(env)
    t = np.arange(len(env)) / rate
    lo = int(0.1 * end)
    if end - lo > 16:
        slope = np.polyfit(t[lo:end], np.log(np.maximum(env[lo:end], 1e-12)), 1)[0]
        out["damping_measured"] = float(-slope)
    out["damping_model"] = d_model
    if not still:
        # Instantaneous frequency over the first decay time, against 1 + xi d t at t = 1 / d.
        from scipy.signal import hilbert

        ph = np.unwrap(np.angle(hilbert(y)))
        inst = np.diff(ph) * rate / (2 * np.pi)
        k0 = int(0.15 / d_model * rate)
        k1 = int(1.0 / d_model * rate)
        if k1 > k0 + 4 and k1 < len(inst):
            w = max(2, int(0.1 / d_model * rate))
            f0 = float(np.median(inst[k0 : k0 + w]))
            f1 = float(np.median(inst[k1 - w // 2 : k1 + w // 2]))
            out["rise_ratio"] = f1 / f0
            out["rise_model"] = (1.0 + 0.1 * 1.0) / (1.0 + 0.1 * 0.15)
    return out


def splash_metrics(x, rate, t_event=0.05):
    a = int(t_event * rate)
    y = x[a:]
    peak = float(np.max(np.abs(y)))
    rms = float(np.sqrt(np.mean(y[: int(0.3 * rate)] ** 2)))
    spec = np.abs(np.fft.rfft(y)) ** 2
    freqs = np.fft.rfftfreq(len(y), 1.0 / rate)
    total = spec.sum() + 1e-30
    centroid = float((freqs * spec).sum() / total)
    low = float(spec[freqs < 300].sum() / total)
    # Ringing: the end of the last 10 ms window whose RMS is within 40 dB of the loudest window's.
    k = int(0.01 * rate)
    win = np.sqrt(np.array([np.mean(y[i : i + k] ** 2) for i in range(0, len(y) - k + 1, k)]))
    over = np.where(win > win.max() * 10 ** (-40 / 20))[0]
    ring = float((over[-1] + 1) * k / rate) if len(over) else 0.0
    return {"peak_dbfs": db(peak), "rms_dbfs": db(rms), "centroid_hz": centroid, "low_share": low, "ring_s": ring}


def timing(wav_path, log_path, fps=30.0):
    """Each strike's sound onset against the start of the frame it is drawn in (sound_timing.gd's log)."""
    import re

    with wave.open(wav_path, "rb") as w:
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
        width = w.getsampwidth()
        x = np.frombuffer(raw, dtype="<i2" if width == 2 else "<i4").astype(np.float64) / (
            32768.0 if width == 2 else 2147483648.0
        )
        x = x.reshape(-1, w.getnchannels()).mean(axis=1)
    k = int(0.001 * rate)
    win = np.sqrt(np.array([np.mean(x[i : i + k] ** 2) for i in range(0, len(x) - k, k)])) + 1e-12
    out = []
    for line in open(log_path):
        m = re.match(r"strike (\d+) frame (\d+) tick (\d+)", line.strip())
        if not m:
            continue
        frame = int(m.group(2))
        t = frame / fps
        a = max(0, int((t - 0.05) / 0.001))
        base = max(np.median(win[a : a + 40]), 1e-6)
        on = next((i for i in range(a + 40, min(len(win), a + 400)) if win[i] > base * 10), None)
        if on is not None:
            out.append((int(m.group(1)), t, on * 0.001, (on * 0.001 - t) * 1000.0))
    for n, t, o, lag in out:
        print("strike %d: drawn in the frame from %.3f s, heard from %.3f s: %+.0f ms" % (n, t, o, lag))
    return out


def main():
    if sys.argv[1] == "--timing":
        timing(sys.argv[2], sys.argv[3])
        return
    d = sys.argv[1]
    idx = json.load(open(os.path.join(d, "cases.json")))
    rate = int(idx["rate"])
    cases = idx["cases"]
    report = {"rate": rate, "bubbles": {}, "splashes": {}, "wake": {}, "cpu": {}}
    for name, c in sorted(cases.items()):
        x = load(d, name, rate)
        report["cpu"][name] = c["usec_per_second"]
        if name.startswith("bubble_"):
            report["bubbles"][name] = bubble_metrics(x, rate, c["radius"], name.endswith("_still"))
        elif name.startswith("splash_") or name.startswith("debris"):
            report["splashes"][name] = dict(splash_metrics(x, rate), speed=c.get("speed"), size=c.get("size"))
        elif name == "wake":
            for k, (t0, t1) in enumerate([(0.3, 1.0), (1.3, 2.0), (2.3, 3.0), (3.3, 3.5)]):
                seg = x[int(t0 * rate) : int(t1 * rate)]
                report["wake"][["0.3", "1.0", "3.0", "calm"][k]] = db(np.sqrt(np.mean(seg**2)))
    json.dump(report, open(os.path.join(d, "report.json"), "w"), indent=1)

    print("single bubbles (rise 0 for the damping, 0.1 for the rise):")
    for r in ["0.5", "1.0", "2.0", "5.0", "10.0"]:
        s = report["bubbles"]["bubble_%smm_still" % r]
        b = report["bubbles"]["bubble_%smm" % r]
        line = "  %5s mm: Minnaert %7.0f Hz, peak %7.0f Hz (%+.2f %%); damping %6.1f/s, measured %6.1f/s" % (
            r,
            s["minnaert_hz"],
            s["peak_hz"],
            100 * (s["peak_hz"] / s["minnaert_hz"] - 1),
            s["damping_model"],
            s.get("damping_measured", float("nan")),
        )
        if "rise_ratio" in b:
            line += "; pitch over one decay time x%.3f (model x%.3f)" % (b["rise_ratio"], b["rise_model"])
        print(line)
    print("splashes:")
    for name, s in sorted(report["splashes"].items(), key=lambda kv: (kv[0].split("_")[1], kv[1].get("speed") or 0)):
        print(
            "  %-18s peak %6.1f dBFS, loudness %6.1f dBFS, centroid %6.0f Hz, below 300 Hz %4.1f %%, rings %.2f s"
            % (name, s["peak_dbfs"], s["rms_dbfs"], s["centroid_hz"], 100 * s["low_share"], s["ring_s"])
        )
    print("wake loudness: " + ", ".join("%s: %.1f dBFS" % kv for kv in report["wake"].items()))
    print(
        "CPU: "
        + ", ".join("%s %.0f µs/s" % kv for kv in sorted(report["cpu"].items()) if not kv[0].startswith("bubble"))
    )

    # Spectrograms.
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    show = ["bubble_1.0mm", "bubble_5.0mm", "splash_0.3m_5.0ms", "splash_1.0m_5ms", "debris_20", "wake"]
    fig, axes = plt.subplots(2, 3, figsize=(15, 7.5))
    for ax, name in zip(axes.flat, show):
        x = load(d, name, rate)
        ax.specgram(x + 1e-9, NFFT=1024, Fs=rate, noverlap=896, cmap="magma", vmin=-140, vmax=-30)
        ax.set_ylim(0, 12000)
        ax.set_title(name)
        ax.set_xlabel("s")
        ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=90)

    # The showreel: every splash and the wake, 0.4 s apart.
    order = sorted(
        (k for k in cases if k.startswith("splash_0.3m") and not k.endswith("m_5ms")), key=lambda k: cases[k]["speed"]
    )
    order += sorted(
        (k for k in cases if k.startswith("splash_") and k.endswith("m_5ms")), key=lambda k: cases[k]["size"]
    )
    order += ["debris_20", "wake"]
    gap = np.zeros(int(0.4 * rate), dtype=np.float32)
    parts = []
    for k in order:
        parts += [load(d, k, rate), gap]
    reel = np.concatenate(parts)
    pcm = (np.clip(reel, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(d, "showreel.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())
    print("showreel: " + ", ".join(order))


if __name__ == "__main__":
    main()
