#!/usr/bin/env python3
"""Podglad dzwiekow alarmu - generuje pliki WAV (fala prostokatna jak buzzer piezo).

Odwzorowuje dokladnie melodie z firmware (tools/../main.cpp: ALARM_MELODIES).
Fala prostokatna + krotka obwiednia = zblizony dzwiek buzera pasywnego.

    python3 tools/make_alarm_audio.py [katalog_wyjsciowy]

Domyslnie zapisuje alarm-melodia-*.wav oraz alarm-melodie.wav w /workspace/share.
"""
import math
import os
import struct
import sys
import wave

RATE = 22050
UNIT_MS = 150          # ta sama jednostka co ALARM_UNIT_MS w firmware
AMP = 0.28             # amplituda (buzzer i tak jest glosny)
FADE_MS = 4            # obwiednia antyklik
OCTAVE_UP = 2          # musi byc rowne BUZZER_OCTAVE_UP w main.cpp (podbicie dla piezo)

MELODIES = {
    "budzik":  [(880, 2), (0, 1), (880, 2), (0, 1), (880, 3), (0, 2), (1047, 4), (0, 4)],
    "dzwonek": [(1319, 3), (0, 1), (988, 3), (0, 3), (1319, 3), (0, 1), (988, 3), (0, 4)],
    "pobudka": [(523, 1), (659, 1), (784, 1), (1047, 2), (0, 1), (1047, 1), (784, 1), (1047, 3), (0, 3)],
    "pikanie": [(1568, 1), (0, 1), (1568, 1), (0, 1), (1568, 1), (0, 1), (1568, 1), (0, 3)],
    "mors-cq": [(700, 3), (0, 1), (700, 1), (0, 1), (700, 3), (0, 1), (700, 1), (0, 3),
                (700, 3), (0, 1), (700, 3), (0, 1), (700, 1), (0, 1), (700, 3), (0, 4)],
    "mors-sos": [(700, 1), (0, 1), (700, 1), (0, 1), (700, 1), (0, 3),
                 (700, 3), (0, 1), (700, 3), (0, 1), (700, 3), (0, 3),
                 (700, 1), (0, 1), (700, 1), (0, 1), (700, 1), (0, 4)],
    "syrena": [(400, 1), (550, 1), (700, 1), (850, 1), (1000, 1), (1150, 1), (1300, 1),
               (1300, 1), (1150, 1), (1000, 1), (850, 1), (700, 1), (550, 1), (400, 2), (0, 2)],
    "nokia": [(659, 1), (587, 1), (370, 2), (415, 2), (554, 1), (494, 1), (587, 2), (659, 2),
              (440, 1), (554, 1), (659, 1), (880, 2), (0, 3)],
    "tetris": [(659, 2), (494, 1), (523, 1), (587, 2), (523, 1), (494, 1), (440, 2), (440, 1), (523, 1),
               (659, 2), (587, 1), (523, 1), (494, 3), (523, 1), (587, 2), (659, 2), (523, 2), (440, 2), (440, 2), (0, 3)],
    "kukulka": [(880, 1), (660, 2), (0, 2), (880, 1), (660, 2), (0, 4)],
    "pips": [(1000, 1), (0, 1), (1000, 1), (0, 1), (1000, 1), (0, 1), (1000, 1), (0, 1),
             (1000, 1), (0, 1), (1000, 3), (0, 4)],
    "kurant": [(660, 3), (0, 1), (880, 3), (0, 1), (660, 3), (0, 1), (660, 4), (0, 4)],
}

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else "/workspace/share"


def tone_samples(freq, ms):
    n = int(RATE * ms / 1000)
    fade = max(1, int(RATE * FADE_MS / 1000))
    out = []
    if freq <= 0:
        return [0] * n
    period = RATE / freq
    for i in range(n):
        s = AMP if (i % period) < (period / 2) else -AMP
        # obwiednia na poczatku/koncu, zeby nie bylo trzaskow
        if i < fade:
            s *= i / fade
        elif i > n - fade:
            s *= (n - i) / fade
        out.append(s)
    return out


def render(notes):
    samples = []
    for freq, units in notes:
        if freq > 0:
            freq <<= OCTAVE_UP  # to samo podbicie oktawy co w firmware
        samples.extend(tone_samples(freq, units * UNIT_MS))
    return samples


def write_wav(path, samples):
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples)
        w.writeframes(frames)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    combined = []
    for name, notes in MELODIES.items():
        samples = render(notes)
        path = os.path.join(OUT_DIR, "alarm-melodia-%s.wav" % name)
        write_wav(path, samples)
        print("zapisano", path)
        combined.extend(samples)
        combined.extend([0] * int(RATE * 0.6))  # 0.6 s przerwy miedzy motywami
    path = os.path.join(OUT_DIR, "alarm-melodie.wav")
    write_wav(path, combined)
    print("zapisano", path)


if __name__ == "__main__":
    main()
