#!/usr/bin/env python3
"""FellowEngine's fire sound (milestone 179, decision 0321): measures the cases fire_sound.gd rendered.

* flames: loudness (RMS, dBFS) against the heat release, the roar's spectral peak against 160 / sqrt(D) Hz, the
  puffing (the roar's envelope over 20 ms windows: its spectrum's peak from 0.2 to 8 Hz) against 1.5 / sqrt(D) Hz;
* crackle: cracks heard a second (onsets of 1 ms windows of the band above 2 kHz over six times their median) against
  those asked for, the
  power law of their peaks (the tail's slope of the survival function, log-log, the top 40 %: the synthesis's energies
  are Pareto 1.4, amplitudes their root — a slope of -2.8), the peak and the crest factor;
* the flare: the lows (under 150 Hz) in the 0.4 s after the gas catches over the 0.4 s before;
* the CPU's time a second of sound.

Writes <dir>/report.json, <dir>/spectrograms.png and <dir>/showreel.wav (the cases one after another, for listening).
With fire_demo.gd's recording (Movie Maker's .wav) as a second argument, its loudness, lows and cracks heard every 2 s.

    python benchmarks/tools/world_sound/fire_sound.py <dir> [<movie.wav>]
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
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2)))


def lowpass(x, fc, rate):
    a = 1.0 - np.exp(-2.0 * np.pi * fc / rate)
    from scipy.signal import lfilter

    return lfilter([a], [1.0, a - 1.0], x)


def flames_metrics(m, rate, size):
    y = m[int(1.0 * rate) :]
    # The roar's spectrum (Welch, 0.5 Hz... 8192 points), smoothed over a third of an octave: its peak 20 Hz-2 kHz.
    n = 8192
    seg = y[: (len(y) // n) * n].reshape(-1, n)
    p = np.mean(np.abs(np.fft.rfft(seg * np.hanning(n), axis=1)) ** 2, axis=0)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    band = (f > 20) & (f < 2000)
    fb = f[band]
    pb = p[band]
    sm = np.array([pb[(fb > x / 1.12) & (fb < x * 1.12)].mean() for x in fb])
    peak = float(fb[np.argmax(sm)])
    centroid = float((f * p).sum() / (p.sum() + 1e-30))
    # The puffing: the roar's envelope (20 ms windows of the signal under 1 kHz), its spectrum 0.2-8 Hz.
    low = lowpass(lowpass(y, 1000.0, rate), 1000.0, rate)
    k = int(0.02 * rate)
    env = np.sqrt(np.array([np.mean(low[i : i + k] ** 2) for i in range(0, len(low) - k, k)]))
    env = env - env.mean()
    ef = np.fft.rfftfreq(len(env), 0.02)
    es = np.abs(np.fft.rfft(env * np.hanning(len(env)))) ** 2
    eb = (ef > 0.2) & (ef < 8.0)
    puff = float(ef[eb][np.argmax(es[eb])])
    return {
        "rms_dbfs": db(rms(y)),
        "peak_hz": peak,
        "predicted_peak_hz": 160.0 / np.sqrt(size),
        "centroid_hz": centroid,
        "puff_hz": puff,
        "predicted_puff_hz": 1.5 / np.sqrt(size),
    }


def crack_band(m, rate):
    # The cracks' band: a fourth-order Butterworth high-pass at 2 kHz (the flames' roar is under it, as a listener's
    # ear separates them).
    from scipy.signal import butter, sosfilt

    return sosfilt(butter(4, 2000.0, "highpass", fs=rate, output="sos"), m)


def crackle_metrics(m, rate, asked, seconds):
    hp = crack_band(m, rate)
    k = int(0.001 * rate)
    win = np.sqrt(np.array([np.mean(hp[i : i + k] ** 2) for i in range(0, len(hp) - k, k)]))
    med = np.median(win) + 1e-9
    on = np.where((win[1:] > 6 * med) & (win[:-1] <= 6 * med))[0] + 1
    peaks = np.array([np.abs(hp[i * k : i * k + 6 * k]).max() for i in on]) if len(on) else np.zeros(0)
    slope = float("nan")
    if len(peaks) > 40:
        s = np.sort(peaks)[::-1]
        top = s[: int(len(s) * 0.4)]
        surv = np.arange(1, len(top) + 1) / len(s)
        slope = float(np.polyfit(np.log(top), np.log(surv), 1)[0])
    return {
        "cracks_per_s": len(on) / seconds,
        "asked_per_s": asked,
        "tail_slope": slope,
        "peak_dbfs": db(np.abs(m).max()),
        "crest_db": db(np.abs(m).max()) - db(rms(m)),
    }


def flare_metrics(m, rate, at):
    low = lowpass(lowpass(m, 150.0, rate), 150.0, rate)
    i = int(at * rate)
    w = int(0.4 * rate)
    before = rms(low[i - w : i]) + 1e-9
    after = rms(low[i : i + w])
    steady = rms(low[i + int(1.5 * rate) : i + int(2.5 * rate)])
    return {"lows_after_over_before_db": db(after / before), "lows_after_over_steady_db": db(after / (steady + 1e-9))}


def demo_metrics(path, rep):
    # fire_demo.gd's sound (Movie Maker's .wav): every 2 s its loudness, its lows, its peak and the cracks heard.
    w = wave.open(path)
    rate = w.getframerate()
    raw = w.readframes(w.getnframes())
    sw = w.getsampwidth()
    x = np.frombuffer(raw, dtype="<i4" if sw == 4 else "<i2").reshape(-1, w.getnchannels()).astype(np.float64)
    x /= 2147483648.0 if sw == 4 else 32768.0
    m = x.mean(axis=1)
    hp = crack_band(m, rate)
    k = int(0.001 * rate)
    win = np.sqrt(np.array([np.mean(hp[i : i + k] ** 2) for i in range(0, len(hp) - k, k)]))
    rows = []
    print("demo (every 2 s):")
    for s0 in range(0, int(len(m) / rate) - 1, 2):
        seg = m[s0 * rate : (s0 + 2) * rate]
        wv = win[s0 * 1000 : (s0 + 2) * 1000]
        med = np.median(wv) + 1e-9
        on = int(np.sum((wv[1:] > 6 * med) & (wv[:-1] <= 6 * med)))
        lo = lowpass(lowpass(seg, 300.0, rate), 300.0, rate)
        r = {"from_s": s0, "rms_dbfs": db(rms(seg)), "lows_dbfs": db(rms(lo)), "peak_dbfs": db(np.abs(seg).max()), "cracks_per_s": on / 2.0}
        rows.append(r)
        print("  %2d-%2d s: %6.1f dBFS, under 300 Hz %6.1f, peak %6.1f, cracks heard %4.1f/s" % (s0, s0 + 2, r["rms_dbfs"], r["lows_dbfs"], r["peak_dbfs"], r["cracks_per_s"]))
    rep["demo"] = {"peak_dbfs": db(np.abs(x).max()), "rms_dbfs": db(rms(x)), "every_2s": rows}


def main():
    d = sys.argv[1]
    idx = json.load(open(os.path.join(d, "cases.json")))
    rate = int(idx["rate"])
    cases = idx["cases"]
    rep = {"rate": rate, "flames": {}, "crackle": {}, "flare": {}, "cpu": {}, "counts": {}}
    for name, c in sorted(cases.items()):
        m = load(d, name)
        rep["cpu"][name] = c["usec_per_second"]
        rep["counts"][name] = {k: c[k] for k in ("cracks", "pops", "ticks", "flares")}
        if name.startswith("flames_"):
            r = flames_metrics(m, rate, c["size"])
            r["power"] = c["power"]
            rep["flames"][name] = r
        if c.get("crackle", 0) > 0:
            rep["crackle"][name] = crackle_metrics(m, rate, c["crackle"], c["seconds"])
            rep["crackle"][name]["rms_dbfs"] = db(rms(m))
        if name == "flare":
            rep["flare"][name] = flare_metrics(m, rate, c["ignite_at"])

    print("flames:")
    for n, r in rep["flames"].items():
        print(
            "  %-12s power %6.2f: %6.1f dBFS, roar's peak %4.0f Hz (160/sqrt(D) %4.0f), centroid %4.0f Hz, puffing %.2f Hz (1.5/sqrt(D) %.2f)"
            % (n, r["power"], r["rms_dbfs"], r["peak_hz"], r["predicted_peak_hz"], r["centroid_hz"], r["puff_hz"], r["predicted_puff_hz"])
        )
    print("crackle:")
    for n, r in rep["crackle"].items():
        print(
            "  %-12s cracks heard %5.1f/s (asked %5.1f), tail slope %5.2f, peak %5.1f dBFS, RMS %5.1f dBFS, crest %4.1f dB"
            % (n, r["cracks_per_s"], r["asked_per_s"], r["tail_slope"], r["peak_dbfs"], r["rms_dbfs"], r["crest_db"])
        )
    for n, r in rep["flare"].items():
        print("flare: lows in the 0.4 s after %+.1f dB over before, %+.1f dB over the fire burning" % (r["lows_after_over_before_db"], r["lows_after_over_steady_db"]))
    print("counts: " + ", ".join("%s %s" % (k, v) for k, v in sorted(rep["counts"].items())))
    if len(sys.argv) > 2:
        demo_metrics(sys.argv[2], rep)
    json.dump(rep, open(os.path.join(d, "report.json"), "w"), indent=1)
    print("CPU (µs a second of sound): " + ", ".join("%s %.0f" % kv for kv in sorted(rep["cpu"].items())))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    show = ["flames_0.25m", "flames_4.0m", "campfire", "bonfire", "flare", "embers"]
    fig, axes = plt.subplots(2, 3, figsize=(15, 7.5))
    for ax, name in zip(axes.flat, show):
        m = load(d, name)[: int(6 * rate)]
        ax.specgram(m + 1e-9, NFFT=2048, Fs=rate, noverlap=1536, cmap="magma", vmin=-150, vmax=-40)
        ax.set_ylim(0, 12000)
        ax.set_title(name)
        ax.set_xlabel("s")
        ax.set_ylabel("Hz")
    fig.tight_layout()
    fig.savefig(os.path.join(d, "spectrograms.png"), dpi=90)

    order = ["flames_0.25m", "flames_1.0m", "flames_4.0m", "campfire", "bonfire", "flare", "crackle", "embers"]
    gap = np.zeros(int(0.5 * rate))
    parts = []
    for k in order:
        parts += [load(d, k)[: int(5 * rate)], gap]
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
