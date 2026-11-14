"""Rebuild sounds/*.wav from the Kenney CC0 sound packs (www.kenney.nl).

Run once by a maintainer; the build only needs the WAV files it writes.
Usage: python3 scripts/import_sounds.py DIR
DIR holds the unzipped packs: kenney_impact-sounds, kenney_interface-sounds,
kenney_digital-audio and kenney_sci-fi-sounds (ffmpeg decodes their OGG files).
Each clip becomes 22050 Hz mono 16-bit: leading silence cut, the tail cut
where it falls 40 dB below the peak (or at the length limit) and faded out.
"""
import array
import math
import pathlib
import subprocess
import sys
import wave

RATE = 22050
ROOT = pathlib.Path(__file__).resolve().parent.parent

# output name: (pack, file, longest ms, gain)
CLIPS = {
    # Barnocraft: steps, digging and placing, as in the original (place = step)
    'bc_grass1': ('impact', 'footstep_grass_000', 220, 1.0),
    'bc_grass2': ('impact', 'footstep_grass_001', 220, 1.0),
    'bc_grass3': ('impact', 'footstep_grass_002', 220, 1.0),
    'bc_grass4': ('impact', 'footstep_grass_003', 220, 1.0),
    'bc_gravel1': ('impact', 'footstep_snow_000', 260, 1.0),
    'bc_gravel2': ('impact', 'footstep_snow_001', 260, 1.0),
    'bc_gravel3': ('impact', 'footstep_snow_002', 260, 1.0),
    'bc_stone1': ('impact', 'footstep_concrete_000', 120, 1.0),
    'bc_stone2': ('impact', 'footstep_concrete_001', 120, 1.0),
    'bc_stone3': ('impact', 'footstep_concrete_002', 120, 1.0),
    'bc_stone4': ('impact', 'footstep_concrete_003', 120, 1.0),
    'bc_wood1': ('impact', 'footstep_wood_000', 140, 1.0),
    'bc_wood2': ('impact', 'footstep_wood_001', 140, 1.0),
    'bc_wood3': ('impact', 'footstep_wood_002', 140, 1.0),
    'bc_wood4': ('impact', 'footstep_wood_003', 140, 1.0),
    'bc_pick1': ('impact', 'impactMining_000', 250, 0.9),
    'bc_pick2': ('impact', 'impactMining_001', 250, 0.9),
    'bc_pick3': ('impact', 'impactMining_004', 250, 0.9),
    'bc_chop1': ('impact', 'impactPlank_medium_000', 200, 0.9),
    'bc_chop2': ('impact', 'impactPlank_medium_001', 200, 0.9),
    'bc_chop3': ('impact', 'impactPlank_medium_002', 200, 0.9),
    'bc_glass1': ('impact', 'impactGlass_medium_000', 420, 0.9),
    'bc_glass2': ('impact', 'impactGlass_medium_001', 420, 0.9),
    'bc_land': ('impact', 'impactSoft_medium_000', 160, 1.0),
    # Happy Gouse
    'g_open': ('interface', 'open_002', 340, 0.6),
    'g_close': ('interface', 'close_002', 300, 0.6),
    'g_minimize': ('interface', 'minimize_001', 260, 0.5),
    'g_maximize': ('interface', 'maximize_001', 260, 0.5),
    'g_ding': ('interface', 'confirmation_001', 300, 0.7),
    'g_question': ('interface', 'question_002', 340, 0.7),
    'g_reveal': ('interface', 'tick_002', 40, 0.7),
    'g_flag': ('interface', 'toggle_001', 120, 0.7),
    'g_boom': ('scifi', 'explosionCrunch_000', 760, 0.9),
    'g_disc': ('interface', 'drop_002', 120, 0.8),
    'g_slide': ('interface', 'drop_001', 100, 0.7),
    # Barnix shell
    'sys_error': ('interface', 'error_006', 260, 0.7),
    # Snake and Pong
    'arc_eat': ('digital', 'powerUp2', 260, 0.6),
    'arc_die': ('digital', 'lowDown', 620, 0.6),
    'arc_hit': ('digital', 'tone1', 120, 0.6),
    'arc_score': ('digital', 'twoTone1', 260, 0.6),
}
PACKS = {'impact': 'kenney_impact-sounds', 'interface': 'kenney_interface-sounds',
         'digital': 'kenney_digital-audio', 'scifi': 'kenney_sci-fi-sounds'}


def decode(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-ac', '1', '-ar', str(RATE),
                          '-f', 's16le', '-'], check=True, capture_output=True).stdout
    return [s / 32768 for s in array.array('h', raw)]


def shape(samples, longest_ms, gain):
    peak = max(abs(s) for s in samples) or 1
    start = next(i for i, s in enumerate(samples) if abs(s) > peak * 0.02)
    samples = samples[max(0, start - RATE // 1000):]
    # the tail ends where a 10 ms window stays 40 dB below the peak
    window, end = RATE // 100, len(samples)
    for i in range(len(samples) - window, 0, -window):
        if max(abs(s) for s in samples[i:i + window]) > peak * 0.01:
            end = i + window
            break
    end = min(end, longest_ms * RATE // 1000)
    samples = samples[:end]
    fade = min(len(samples) // 3, RATE * 30 // 1000)
    for i in range(fade):
        samples[len(samples) - 1 - i] *= i / fade
    scale = gain * 0.95 / peak
    return [s * scale for s in samples]


def chime():
    """The Barnix start chime: plucked C major arpeggio over a soft fifth."""
    out = [0.0] * int(RATE * 1.5)
    for k, (note, at) in enumerate(((72, 0.0), (76, 0.11), (79, 0.22), (84, 0.33))):
        hz = 440 * 2 ** ((note - 69) / 12)
        period = int(RATE / hz)
        seed = 12345 + k
        line = []
        for _ in range(period):
            seed = (seed * 1103515245 + 12345) & 0x7fffffff
            line.append((seed / 0x7fffffff) * 2 - 1)
        begin = int(at * RATE)
        for i in range(len(out) - begin):
            j = i % period
            v = line[j]
            line[j] = (v + line[(j + 1) % period]) * 0.4985
            out[begin + i] += v * 0.35
    for note in (60, 67):   # sustained sine pad
        hz = 440 * 2 ** ((note - 69) / 12)
        for i in range(len(out)):
            t = i / RATE
            env = min(1, t / 0.08) * math.exp(-t * 2.2)
            out[i] += math.sin(2 * math.pi * hz * t) * env * 0.12
    # gentle low-pass
    y = 0
    for i in range(len(out)):
        y += (out[i] - y) * 0.55
        out[i] = y
    return shape(out, 1500, 0.8)


def write(name, samples):
    path = ROOT / 'sounds' / (name + '.wav')
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(array.array('h', (max(-32767, min(32767, int(s * 32767))) for s in samples)).tobytes())
    return len(samples)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    base = pathlib.Path(sys.argv[1])
    (ROOT / 'sounds').mkdir(exist_ok=True)
    total = 0
    for name, (pack, source, longest, gain) in CLIPS.items():
        path = next((base / PACKS[pack]).rglob(source + '.ogg'))
        total += write(name, shape(decode(path), longest, gain))
    total += write('sys_start', chime())
    print(f'{len(CLIPS) + 1} clips, {total / RATE:.1f} s')


if __name__ == '__main__':
    main()
