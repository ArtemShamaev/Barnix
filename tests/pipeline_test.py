"""Run Bssh's Linux pipeline helper natively and through keyboard input in QEMU."""
import pathlib
import shutil
import subprocess
import tempfile
from qemu_shell import run

runner = pathlib.Path('userland/bssh-run.elf').resolve()
programs = {name: pathlib.Path(f'apps/bin/{name}').resolve()
            for name in ['echo', 'cat', 'true', 'false']}
large = bytes(range(256)) * 512 + b'last byte\x00\xff'


def native(command, directory, reference=False):
    invocation = ['/bin/sh', '-c', command] if reference else [str(runner), command]
    return subprocess.run(invocation, cwd=directory, env={}, capture_output=True, timeout=30)


with tempfile.TemporaryDirectory(prefix='barnix-pipeline-') as temporary:
    directory = pathlib.Path(temporary)
    native_dirs = [directory / 'runner', directory / 'reference']
    for work in native_dirs:
        work.mkdir()
        for name, program in programs.items():
            (work / name).symlink_to(program)
        (work / 'large').write_bytes(large)
    commands = [
        './echo "a | b"|./cat',
        './echo "a > b"',
        './echo a\\ b|./cat >result',
        './echo first>result',
        './echo second>>result',
        './cat<result|./cat>copied',
        './echo overwrite>first>second',
        './echo diverted>diverted|./cat>empty',
        './echo stale>empty',
        './missing>empty',
        './cat large|./cat>large-copy',
        './cat large|./cat<result>override',
        './echo "">"two words"',
        './false|./true',
        './true|./false',
        './missing|./true',
        './true|./missing',
        './cat missing|./cat',
        './cat<missing>absent',
    ]
    for command in commands:
        actual = native(command, native_dirs[0])
        reference = native(command, native_dirs[1], reference=True)
        assert (actual.returncode, actual.stdout) == (reference.returncode, reference.stdout), (
            command, actual.returncode, actual.stdout, actual.stderr, reference.returncode, reference.stdout)
        for file in ['result', 'copied', 'first', 'second', 'diverted', 'empty', 'large-copy', 'override', 'two words', 'absent']:
            paths = [work / file for work in native_dirs]
            assert paths[0].exists() == paths[1].exists(), (command, file)
            if paths[0].exists():
                assert paths[0].read_bytes() == paths[1].read_bytes(), (command, file)
    for command in ['./echo bad>untouched|', './echo bad|./cat|./cat', './echo "bad', './echo 2>untouched']:
        result = native(command, native_dirs[0])
        assert result.returncode == 2 and not result.stdout, (command, result)
        assert not (native_dirs[0] / 'untouched').exists()
    (native_dirs[0] / 'hello.elf').symlink_to(pathlib.Path('apps/hello.elf').resolve())
    result = native('./hello.elf>untouched', native_dirs[0])
    assert result.returncode == 126 and b'require Linux programs' in result.stderr, result
    assert not (native_dirs[0] / 'untouched').exists()
    print('Native pipeline helper matches sh for supported pipes, redirects and exit statuses', flush=True)

    disk = directory / 'pipeline.img'
    shutil.copyfile('live-ext2.img', disk)
    subprocess.run(['python3', 'scripts/install_apps.py', str(disk), 'userland/linux-probe.elf'], check=True)
    payload = directory / 'large'
    payload.write_bytes(large)
    subprocess.run(['debugfs', '-w', '-R', f'write {payload} /large', str(disk)], check=True, capture_output=True)
    run(disk, directory, [
        ('echo "literal | >"', '\nliteral | >\n'),
        ('echo hello|cat', 'program exited: 0'),
        ('echo first>result', 'program exited: 0'),
        ('echo second>>result', 'program exited: 0'),
        ('cat<result|cat>copied', 'program exited: 0'),
        ('echo a\\ b|cat>escaped', 'program exited: 0'),
        ('echo quoted>"two words"', 'program exited: 0'),
        ('cat large|cat>large-copy', 'program exited: 0'),
        ('echo diverted>diverted|cat>empty', 'program exited: 0'),
        ('echo stale>empty', 'program exited: 0'),
        ('missing>empty', 'program exited: 127'),
        ('cat large|cat<result>override', 'program exited: 0'),
        ('false|true', 'program exited: 0'),
        ('true|false', 'program exited: 1'),
        ('missing|true', 'program exited: 0'),
        ('true|missing', 'program exited: 127'),
        ('cat<missing', 'program exited: 1'),
        ('echo bad>result|', 'missing command'),
        ('echo bad|cat|cat', 'at most two commands per pipeline'),
        ('echo "bad', 'unclosed quote'),
        ('echo 2>result', 'descriptor redirection is unsupported'),
        ('hello.elf>result', 'program exited: 126'),
        ('echo x|hello.elf', 'program exited: 126'),
        ('true|./linux-probe.elf fault', 'program exited: 139'),
        ('linux /bin/echo explicit|cat>explicit', 'program exited: 0'),
        ('mkdir docs', 'program exited: 0'),
        ('cd docs', 'program exited: 0'),
        ('pwd|cat>cwd', 'program exited: 0'),
        ('pwd', 'bssh> pwd\n/docs'),
        ('cd ..', 'program exited: 0'),
    ] + [('echo repeat|cat>repeat', 'program exited: 0')] * 8 + [
        ('hello.elf after', 'program exited: 7'),
    ], command_timeout=45)
    subprocess.run(['e2fsck', '-fn', str(disk)], check=True)
    for name, expected in {
        'result': b'first\nsecond\n', 'copied': b'first\nsecond\n',
        'escaped': b'a b\n', 'large-copy': large, 'diverted': b'diverted\n',
        'empty': b'', 'override': b'first\nsecond\n', 'explicit': b'explicit\n',
        'docs/cwd': b'/docs\n', 'repeat': b'repeat\n',
        'two words': b'quoted\n',
    }.items():
        result = subprocess.run(['debugfs', '-R', f'cat "/{name}"', str(disk)], capture_output=True, check=True)
        assert result.stdout == expected, (name, len(result.stdout), result.stdout[:128])
    run(None, directory, [('echo live|cat>live', 'program exited: 0'),
                           ('cat live', 'live')])
print('Bssh pipelines passed: ISO, live boot, ATA bytes, failure recovery, cwd and descriptor reuse')
