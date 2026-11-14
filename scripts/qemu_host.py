#!/usr/bin/env python3
"""QEMU options for running Barnix on this host: KVM when /dev/kvm is usable,
all cores (up to 8), a window that captures the mouse, and sound.

Environment overrides: QEMU_DEFAULT_DISPLAY=1 keeps QEMU's own display,
BARNIX_NO_KVM=1 forces TCG, BARNIX_NO_AUDIO=1 leaves out the sound card.
"""
import os
import subprocess
import sys


def listing(binary, option):
    try:
        with subprocess.Popen([binary, option, 'help'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              text=True) as process:
            out = process.communicate(timeout=10)[0]
    except (OSError, subprocess.SubprocessError):
        return set()
    return {line.strip() for line in out.splitlines()}


def audio_driver(drivers):
    runtime = os.environ.get('XDG_RUNTIME_DIR', '')
    if 'pipewire' in drivers and runtime and os.path.exists(os.path.join(runtime, 'pipewire-0')):
        return 'pipewire'
    if 'pa' in drivers and runtime and os.path.exists(os.path.join(runtime, 'pulse', 'native')):
        return 'pa'
    if 'sdl' in drivers and (os.environ.get('WAYLAND_DISPLAY') or os.environ.get('DISPLAY')):
        return 'sdl'
    return 'none'


def performance_options():
    options = []
    if not os.environ.get('BARNIX_NO_KVM') and os.access('/dev/kvm', os.R_OK | os.W_OK):
        options += ['-accel', 'kvm', '-cpu', 'host']
    else:
        options += ['-accel', 'tcg,thread=multi', '-cpu', 'max']
    options += ['-smp', str(max(1, min(os.cpu_count() or 1, 8)))]
    return options


def host_options(binary):
    options = performance_options()
    if not os.environ.get('QEMU_DEFAULT_DISPLAY'):
        displays = listing(binary, '-display')
        # SDL locks the pointer after a click, also on Wayland, where QEMU's GTK
        # window cannot confine it. Ctrl+Alt+G releases the mouse.
        if 'sdl' in displays:
            options += ['-display', 'sdl']
        elif 'gtk' in displays:
            options += ['-display', 'gtk,grab-on-hover=on']
    if not os.environ.get('BARNIX_NO_AUDIO'):
        driver = audio_driver(listing(binary, '-audiodev'))
        options += ['-audiodev', f'{driver},id=snd0', '-machine', 'pcspk-audiodev=snd0',
                    '-device', 'intel-hda', '-device', 'hda-duplex,audiodev=snd0']
    return options


if __name__ == '__main__':
    print(' '.join(host_options(sys.argv[1] if len(sys.argv) > 1 else 'qemu-system-i386')))
