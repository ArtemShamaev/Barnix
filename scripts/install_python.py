"""Put Python (tools/python/root, made by tools/python/build.sh) into an image,
registered for brm like a package that came with the system."""
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from system_packages import system_packages, base_files, PYTHON_ROOT  # noqa: E402
from install_apps import write_text_files  # noqa: E402


def install(image):
    package = next((p for p in system_packages() if p.name == 'python'), None)
    if not package:
        print('Python: not built (sh tools/python/build.sh)')
        return False
    commands, made = [], {'/'}
    for destination, (source, executable) in sorted(package.files.items()):
        parts = destination.split('/')[1:-1]
        for i in range(1, len(parts) + 1):
            directory = '/' + '/'.join(parts[:i])
            if directory not in made:
                commands.append(f'mkdir {directory}')
                made.add(directory)
        commands += [f'rm {destination}', f'write "{source}" {destination}']
        if executable:
            commands.append(f'set_inode_field {destination} mode 0100755')
    with tempfile.NamedTemporaryFile('w', suffix='.debugfs', delete=False) as script:
        script.write('\n'.join(commands) + '\n')
    subprocess.run(['debugfs', '-w', '-f', script.name, str(image)], check=True, capture_output=True)
    pathlib.Path(script.name).unlink()

    def debug(command):
        return subprocess.run(['debugfs', '-w', '-R', command, str(image)], check=True, capture_output=True, text=True)
    with tempfile.TemporaryDirectory() as scratch:
        write_text_files(debug, pathlib.Path(scratch), base_files([package], 'python'))
    subprocess.run(['e2fsck', '-fn', str(image)], check=True, capture_output=True)
    print(f'Python: {len(package.files)} files')
    return True


if __name__ == '__main__':
    install(pathlib.Path(sys.argv[1]))
