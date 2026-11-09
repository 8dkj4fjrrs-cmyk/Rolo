"""Makes The Lantern Keeper's sounds and music. Original, MIT like the rest.

    python3 make_sounds.py <folder>

Every sound is synthesised here from sine waves and noise, so there are no
samples from anywhere else. Writes into <folder>:
  flame.wav        a lost flame found: a rising sparkle
  rest.wav         resting at the wayshrine (saving): two soft bell notes
  gate.wav         the mine gate swinging open: a creak and a thud
  lantern_lit.ogg  the great lantern lighting up: a swell and a bright chord
  night.ogg        music for Emberwick and Whisperwood: a music box over warm
                   chords, 24 seconds, looping without a seam
  mine.ogg         music for the old mine: a low hum and far-off drips, 24 s

Needs NumPy, and ffmpeg (with libvorbis) for the OGG files. The results are
committed; run it again only to change the sounds.
"""
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 44100
rng = np.random.default_rng(7)   # fixed: the same sounds every time


def seconds(n):
    return np.arange(int(n * RATE)) / RATE


def bell(freq, length, decay=3.0, bright=0.35):
    """A struck bell or music-box tine: a sine with a few quieter overtones."""
    t = seconds(length)
    tone = np.sin(2 * np.pi * freq * t) + bright * np.sin(2 * np.pi * freq * 2.0 * t) * np.exp(-t * 6)
    tone += 0.12 * np.sin(2 * np.pi * freq * 3.01 * t) * np.exp(-t * 9)
    attack = np.minimum(1.0, t / 0.004)              # no click at the start...
    release = np.minimum(1.0, (length - t) / 0.08)   # ...or at the end
    return tone * attack * release * np.exp(-t * decay)


def pad(freqs, length, fade=1.5):
    """Soft held chord: slightly detuned sines that swell and fade."""
    t = seconds(length)
    out = np.zeros_like(t)
    for f in freqs:
        for detune in (-0.6, 0.6):
            out += np.sin(2 * np.pi * (f + detune) * t + rng.uniform(0, 6.28))
    env = np.minimum(1.0, t / fade) * np.minimum(1.0, (length - t) / fade)
    return out / (2 * len(freqs)) * env


def noise(length):
    return rng.uniform(-1.0, 1.0, int(length * RATE))


def lowpass(x, cutoff, passes=3):
    """Low-pass filter (one-pole, run several times for a steeper slope):
    keeps the rumble, takes away the hiss."""
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.asarray(x, dtype=float)
    for _ in range(passes):
        out = np.empty_like(y)
        acc = 0.0
        for i, v in enumerate(y):
            acc = (1 - a) * v + a * acc
            out[i] = acc
        y = out
    return y


def fade_out(x, length=0.05):
    """Ends a sound at silence, so cutting it off does not click."""
    n = int(length * RATE)
    x = x.copy()
    x[-n:] *= np.linspace(1.0, 0.0, n)
    return x


def loopable(x, overlap=1.0):
    """Joins the end of `x` smoothly onto its start: the last `overlap`
    seconds are faded into the first ones, and cut off."""
    n = int(overlap * RATE)
    body = x[:-n].copy()
    ramp = np.linspace(0.0, 1.0, n)
    body[:n] = body[:n] * ramp + x[-n:] * (1 - ramp)
    return body


def place(track, sound, at):
    """Adds `sound` into `track` from `at` seconds, wrapping round the end (for loops)."""
    start = int(at * RATE)
    for i in range(0, len(sound), len(track)):
        part = sound[i:i + len(track)]
        idx = (start + i + np.arange(len(part))) % len(track)
        np.add.at(track, idx, part)


def echo(x, delay, gain, times=4, loop=False):
    out = x.copy()
    for k in range(1, times + 1):
        shift = int(delay * k * RATE)
        if loop:
            out += np.roll(x, shift) * gain ** k
        else:
            out[shift:] += x[:len(x) - shift] * gain ** k
    return out


def note(name):
    """Frequency of a note like "C5" or "F#4"."""
    names = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    return 440.0 * 2 ** ((names[name[:-1]] + 12 * (int(name[-1]) + 1) - 69) / 12)


def normalise(x, peak):
    return x / np.max(np.abs(x)) * peak


# --- Effects ------------------------------------------------------------------------------------------------

def flame():
    out = np.zeros(int(0.9 * RATE))
    for i, n in enumerate(["G5", "B5", "D6", "G6"]):
        place(out, bell(note(n), 0.9 - i * 0.06, decay=5.0) * (0.8 + 0.1 * i), i * 0.06)
    shimmer = noise(0.9) * np.exp(-seconds(0.9) * 7) * 0.05
    return normalise(fade_out(echo(out + shimmer, 0.09, 0.3, 3)), 0.6)


def rest():
    out = np.zeros(int(1.6 * RATE))
    place(out, bell(note("E5"), 1.6, decay=2.5, bright=0.2), 0.0)
    place(out, bell(note("A5"), 1.4, decay=2.5, bright=0.2), 0.18)
    return normalise(fade_out(echo(out, 0.14, 0.25, 3)), 0.5)


def gate():
    length = 1.1
    t = seconds(length)
    # The creak: a buzzy tone sliding up, rough with noise.
    freq = 180 + 160 * t
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    creak = np.sign(np.sin(phase)) * 0.3 + np.sin(phase * 2.01) * 0.3
    creak *= (0.5 + 0.5 * np.sin(2 * np.pi * 23 * t)) * np.clip(1 - np.abs(t - 0.35) / 0.35, 0, 1)
    creak = lowpass(creak + noise(length) * 0.15, 2200)
    # The thud as it stops: a low knock.
    thud = np.zeros_like(t)
    tt = seconds(length - 0.72)
    thud[int(0.72 * RATE):] = np.sin(2 * np.pi * 70 * tt) * np.exp(-tt * 14) + lowpass(noise(length - 0.72), 400) * np.exp(-tt * 20) * 2
    return normalise(fade_out(creak * 0.6 + thud), 0.7)


def lantern_lit():
    length = 4.0
    t = seconds(length)
    swell = lowpass(noise(length), 500) * np.clip(t / 1.0, 0, 1) * np.exp(-np.maximum(t - 1.0, 0) * 4) * 6
    out = swell
    chord = np.zeros_like(t)
    for i, n in enumerate(["C4", "E4", "G4", "C5", "E5", "G5", "C6"]):
        place(chord, bell(note(n), length - 0.9, decay=1.2, bright=0.3) * (1.0 - i * 0.08), 0.9 + i * 0.03)
    out = out + chord * 0.6
    return normalise(fade_out(echo(out, 0.21, 0.35, 4), 0.3), 0.75)


# --- Music --------------------------------------------------------------------------------------------------

def night():
    """24 s: four chords of six seconds, a music-box tune above them, then round again."""
    length = 24.0
    beat = 0.5                         # 120 beats a minute, a quiet waltz-ish lilt
    track = np.zeros(int(length * RATE))
    chords = [["C3", "G3", "E4"], ["A2", "E3", "C4"], ["F2", "C3", "A3"], ["G2", "D3", "B3"]]
    for i, chord in enumerate(chords):
        place(track, pad([note(n) for n in chord], 6.6, fade=1.6) * 0.5, i * 6.0 - 0.3)
    tune = ["E5", "G5", "A5", "G5", "E5", "D5", "C5", "D5", "E5", None, "G5", "E5",
            "C5", "E5", "A5", "G5", "E5", "C5", "A4", "C5", "E5", None, "D5", "C5",
            "C5", "F5", "A5", "G5", "F5", "C5", "A4", "C5", "F5", None, "E5", "C5",
            "B4", "D5", "G5", "F5", "D5", "B4", "G4", "B4", "D5", None, "D5", None]
    for i, n in enumerate(tune):
        if n:
            place(track, bell(note(n), 1.6, decay=2.8, bright=0.45) * 0.35, i * beat)
    track = echo(track, beat * 1.5, 0.28, 4, loop=True)   # wraps round: no seam at the loop
    return normalise(track, 0.55)


def mine():
    length = 24.0
    t = seconds(length)
    # A low hum that breathes slowly (whole cycles in 24 s, so it loops).
    hum = (np.sin(2 * np.pi * 55 * t) + 0.5 * np.sin(2 * np.pi * 82.5 * t + 1.0)) * (0.6 + 0.4 * np.sin(2 * np.pi * t / 12))
    hum += loopable(lowpass(noise(length + 1.0), 160), 1.0) * 30
    track = hum * 0.25
    drips = [(1.3, "E6"), (4.1, "B5"), (6.0, "G6"), (9.7, "E6"), (12.2, "D6"), (15.8, "B5"), (18.4, "E6"), (21.1, "G5")]
    for at, n in drips:
        place(track, bell(note(n), 1.2, decay=6.0, bright=0.1) * 0.25, at)
    for at, n in [(0.0, "E3"), (12.0, "C3")]:
        place(track, pad([note(n), note(n) * 1.5], 12.6, fade=3.0) * 0.35, at - 0.3)
    track = echo(track, 0.37, 0.4, 5, loop=True)
    return normalise(track, 0.5)


# --- Writing ------------------------------------------------------------------------------------------------

def write_wav(path, samples):
    data = (np.clip(samples, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


def write_ogg(path, samples):
    temp = path.with_suffix(".tmp.wav")
    write_wav(temp, samples)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(temp), "-c:a", "libvorbis", "-q:a", "3",
                    "-map_metadata", "-1", "-fflags", "+bitexact", str(path)], check=True)
    temp.unlink()


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    write_wav(out / "flame.wav", flame())
    write_wav(out / "rest.wav", rest())
    write_wav(out / "gate.wav", gate())
    write_ogg(out / "lantern_lit.ogg", lantern_lit())
    write_ogg(out / "night.ogg", night())
    write_ogg(out / "mine.ogg", mine())


if __name__ == "__main__":
    main()
