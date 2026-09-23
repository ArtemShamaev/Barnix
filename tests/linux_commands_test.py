"""Run the installed command binaries on Linux itself; no Barnix emulator API."""
import pathlib
import subprocess
import tempfile

binaries = pathlib.Path('apps/bin').resolve()
with tempfile.TemporaryDirectory(prefix='barnix-commands-') as temporary:
    directory = pathlib.Path(temporary)
    def run(name, *args, status=0, stdin=b''):
        program = binaries / name
        assert program.read_bytes()[7] in (0, 3), (name, 'not Linux ELF')
        result = subprocess.run([str(program), *args], input=stdin, capture_output=True,
                                cwd=directory, env={})
        assert result.returncode == status, (name, args, result.returncode, result.stderr)
        return result.stdout

    assert run('echo', 'hello', 'Linux') == b'hello Linux\n'
    assert run('pwd') == str(directory).encode() + b'\n'
    run('true')
    run('false', status=1)
    run('touch', 'empty')
    assert (directory / 'empty').read_bytes() == b''
    run('write', 'file', 'hello', 'world')
    run('touch', 'file')
    run('append', 'file', '!')
    assert run('cat', 'file') == b'hello world!'
    payload = b'zero\x00byte\xff\n' + 'Привет'.encode() + bytes(range(256)) * 32
    (directory / 'binary').write_bytes(payload)
    assert run('cat', 'binary') == payload
    assert run('cat', stdin=payload) == payload
    run('cp', 'binary', 'copy')
    assert (directory / 'copy').read_bytes() == payload
    run('cp', 'file', 'copy', status=1)
    assert (directory / 'copy').read_bytes() == payload
    (directory / 'subdir').mkdir()
    assert set(run('ls').splitlines()) == {b'empty', b'file', b'binary', b'copy', b'subdir/'}
    assert run('ls', 'subdir') == b''
    run('ls', 'file', status=1)
    run('cat', 'missing', status=1)
print('All 10 migrated command binaries passed on Linux')
