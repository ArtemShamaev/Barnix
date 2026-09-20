"""Test publication using local bare repositories; never contacts GitHub."""
import os
import pathlib
import shutil
import subprocess
import tempfile

script = pathlib.Path('scripts/update-repo.sh').resolve()
git = shutil.which('git')


def command(*args, cwd=None, env=None, ok=True):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True, text=True)
    if ok and result.returncode:
        raise AssertionError((args, result.stdout, result.stderr))
    if not ok:
        assert result.returncode, (args, 'expected failure')
    return result.stdout.strip()


def g(*args, cwd):
    return command(git, *args, cwd=cwd)


with tempfile.TemporaryDirectory(prefix='barnix-publish-') as tmp:
    root = pathlib.Path(tmp)
    remote, checkout, peer = [root / x for x in ('remote.git', 'checkout', 'peer')]
    command(git, 'init', '--bare', '--initial-branch=main', str(remote))
    command(git, 'clone', str(remote), str(checkout))
    g('config', 'user.name', 'Barnix Test', cwd=checkout)
    g('config', 'user.email', 'test@example.invalid', cwd=checkout)
    (checkout / 'scripts').mkdir()
    shutil.copy2(script, checkout / 'scripts/update-repo.sh')
    (checkout / '.gitignore').write_text('*.iso\n')
    g('add', '.', cwd=checkout)
    g('commit', '-m', 'Initial', cwd=checkout)
    g('push', 'origin', 'main', cwd=checkout)

    # Only the URL validation is mocked: fetch/commit/push use real local Git.
    binaries = root / 'bin'
    binaries.mkdir()
    wrapper = binaries / 'git'
    wrapper.write_text('#!/usr/bin/env python3\n'
                       'import os, sys\n'
                       'if sys.argv[1:3] == ["remote", "get-url"] and not os.getenv("TEST_BAD_URL"):\n'
                       '    print("https://github.com/ArtemShamaev/Barnix.git")\n'
                       'else:\n'
                       f'    os.execv({git!r}, [{git!r}] + sys.argv[1:])\n')
    wrapper.chmod(0o755)
    environment = dict(os.environ, PATH=str(binaries) + os.pathsep + os.environ['PATH'])
    publish = str(checkout / 'scripts/update-repo.sh')
    (checkout / 'feature.txt').write_text('new work')
    (checkout / 'artifact.iso').write_bytes(b'ignored')
    command(publish, 'Add feature', cwd=root, env=environment)
    assert g('log', '-1', '--format=%s', cwd=remote) == 'Add feature'
    assert g('show', 'main:feature.txt', cwd=remote) == 'new work'
    assert 'artifact.iso' not in g('ls-tree', '-r', '--name-only', 'main', cwd=remote)
    head = g('rev-parse', 'HEAD', cwd=checkout)
    command(publish, cwd=root, env=environment)
    assert g('rev-parse', 'HEAD', cwd=checkout) == head

    # A remote update must be detected before staging or committing local work.
    command(git, 'clone', str(remote), str(peer))
    g('config', 'user.name', 'Peer', cwd=peer)
    g('config', 'user.email', 'peer@example.invalid', cwd=peer)
    (peer / 'peer.txt').write_text('remote work')
    g('add', '.', cwd=peer)
    g('commit', '-m', 'Peer update', cwd=peer)
    g('push', cwd=peer)
    (checkout / 'local.txt').write_text('keep me')
    command(publish, cwd=root, env=environment, ok=False)
    assert g('rev-parse', 'HEAD', cwd=checkout) == head
    assert g('diff', '--cached', '--name-only', cwd=checkout) == ''
    assert (checkout / 'local.txt').read_text() == 'keep me'
    g('add', 'local.txt', cwd=checkout)
    g('commit', '-m', 'Divergent local commit', cwd=checkout)
    divergent = g('rev-parse', 'HEAD', cwd=checkout)
    command(publish, cwd=root, env=environment, ok=False)
    assert g('rev-parse', 'HEAD', cwd=checkout) == divergent

    # New branches are published without modifying main.
    g('switch', '-c', 'test-branch', cwd=checkout)
    command(publish, cwd=root, env=environment)
    assert g('rev-parse', 'refs/heads/test-branch', cwd=remote) == divergent
    assert g('log', '-1', '--format=%s', 'main', cwd=remote) == 'Peer update'
    bad_url = dict(environment, TEST_BAD_URL='1')
    command(publish, cwd=root, env=bad_url, ok=False)
    g('checkout', '--detach', cwd=checkout)
    command(publish, cwd=root, env=environment, ok=False)
    print('Repository publication tests passed (local remotes only)')
