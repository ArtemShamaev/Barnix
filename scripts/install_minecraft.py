"""Put the user's Minecraft Alpha into a Barnix ext2 image as /mc.

/mc/mc.jar is the a1.2.6 client, /mc/mod.jar the jar mod (МТКЕВЯ), and
/mc/minecraft the game's own folder (the Alpha Launcher starts the game with
user.home=/mc). They come from the local FreesmLauncher instance, or from
BARNIX_MC_JAR / BARNIX_MOD_JAR; BARNIX_NO_MINECRAFT=1 leaves them out.
Minecraft belongs to Mojang: an image made with it is for the owner's own
machines, never for publishing.
"""
import os
import pathlib
import subprocess

INSTANCE = pathlib.Path.home() / '.local/share/FreesmLauncher/instances/MTKEVYa'


def sources():
    game = os.environ.get('BARNIX_MC_JAR')
    game = pathlib.Path(game) if game else INSTANCE / 'libraries/customjar-1.jar'
    mod = os.environ.get('BARNIX_MOD_JAR')
    if mod:
        mod = pathlib.Path(mod)
    else:
        mods = sorted((INSTANCE / 'jarmods').glob('*.jar'), key=lambda p: p.stat().st_mtime)
        mod = mods[-1] if mods else None
    return (game if game.is_file() else None), (mod if mod and mod.is_file() else None)


def install(image):
    if os.environ.get('BARNIX_NO_MINECRAFT'):
        return False
    game, mod = sources()
    if not game:
        print('Minecraft Alpha: no client jar found, /mc left out (set BARNIX_MC_JAR)')
        return False

    def debug(command):
        return subprocess.run(['debugfs', '-w', '-R', command, str(image)], check=True, capture_output=True, text=True)
    for directory in ('/mc', '/mc/minecraft'):
        if 'Inode:' not in debug(f'stat {directory}').stdout:
            debug(f'mkdir {directory}')
    for source, name in ((game, 'mc.jar'), (mod, 'mod.jar')):
        if not source:
            continue
        debug(f'rm /mc/{name}')
        if 'Allocated inode:' not in debug(f'write "{source}" /mc/{name}').stdout:
            raise RuntimeError(f'cannot write /mc/{name}')
    print(f'Minecraft Alpha: /mc/mc.jar{" and /mc/mod.jar (" + mod.name + ")" if mod else ""}')
    return True


if __name__ == '__main__':
    import sys
    install(pathlib.Path(sys.argv[1]))
