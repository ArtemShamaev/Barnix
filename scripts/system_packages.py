#!/usr/bin/env python3
"""Barnix itself as brm packages.

Every system program becomes a package of the same name ("base": it may
replace the copy the system came with), Happy Gouse comes with its data and
the manuals, GCC is a bundle made from live-ext2-gcc.img. Two users:

* scripts/install_apps.py writes /etc/brm/base (and /etc/brm/files/NAME for
  bundles) into an image, so brm knows what the system came with;
* BarnixRepo's tools/build.py calls build_repository() to publish them.

A version is 0.3.YYYYMMDD.HHMM of the newest file (UTC), so an image and the
repository built from the same files agree, and a rebuilt program is newer.

Bundles (.bpz) are "BPZ1" and blocks of [u32 raw size][u32 packed size]
[raw deflate], at most 1 MiB raw each, which unpack to a BPK1 stream (the
format of Setup's packages). brm unpacks them while they download.
"""
import hashlib
import io
import json
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
BARNIX_VERSION = '0.3'
BLOCK = 1 << 20
GCC_TARGET = 'i686-pc-linux-gnu'
GCC_VERSION = '16.2.0'
GCC_LIB = f'/usr/lib/gcc/{GCC_TARGET}/{GCC_VERSION}'
# GCC links static programs by default: Barnix has no dynamic loader.
GCC_SPECS = ('*link_emulation:\nelf_i386\n\n'
             '*libgcc:\n-lgcc -lgcc_eh\n\n'
             '*self_spec:\n+ %{!shared:%{!static:-static}}\n\n')

# name: (category, description, description in Russian)
PROGRAMS = {
    'append': ('system', 'Append text to a file', 'Дописать текст в файл'),
    'barnocraft': ('games', 'Barnocraft: a voxel world to build in', 'Barnocraft: воксельный мир, где можно строить'),
    'bcc': ('devel', 'BCC, the tiny self-hosted C compiler', 'BCC, маленький компилятор C прямо в Barnix'),
    'beep': ('multimedia', 'Play a tone on the speaker or sound card', 'Сыграть звук на динамике или звуковой карте'),
    'bnm': ('network', 'Barnix Network Manager: cable and Wi-Fi', 'Сетевой менеджер Barnix: кабель и Wi-Fi'),
    'brm': ('system', 'Barnix Repository Manager (this program)', 'Менеджер пакетов Barnix (эта программа)'),
    'cat': ('system', 'Show a file', 'Показать файл'),
    'cd': ('system', 'Change the folder', 'Сменить папку'),
    'chmod': ('system', 'Give users rights to files', 'Выдать пользователям права на файлы'),
    'clear': ('system', 'Clear the screen', 'Очистить экран'),
    'cp': ('system', 'Copy files', 'Копировать файлы'),
    'devices': ('system', 'Disks, mice, keyboards and network cards', 'Диски, мыши, клавиатуры и сетевые карты'),
    'df': ('system', 'Free space on the disks', 'Свободное место на дисках'),
    'diskinfo': ('system', 'Information about the disk', 'Сведения о диске'),
    'echo': ('system', 'Print text', 'Напечатать текст'),
    'execute': ('desktop', 'Execute: the text-mode program launcher', 'Execute: текстовое меню программ'),
    'false': ('system', 'Exit with status 1', 'Завершиться с кодом 1'),
    'fdisk': ('system', 'Partition a disk', 'Разметить диск на разделы'),
    'format': ('system', 'Make an ext2 filesystem', 'Создать файловую систему ext2'),
    'get': ('network', 'Download a file over HTTP or HTTPS', 'Скачать файл по HTTP или HTTPS'),
    'git': ('devel', 'git clone over HTTPS', 'git clone по HTTPS'),
    'gldemo': ('graphics', 'OpenGL demo: a spinning scene', 'Демо OpenGL: вращающаяся сцена'),
    'glrunner': ('games', 'GL Runner: a 3D endless runner', 'GL Runner: трёхмерный бегун'),
    'help': ('system', 'Shell commands', 'Команды оболочки'),
    'init': ('system', 'Prepare a new disk for Barnix', 'Подготовить новый диск для Barnix'),
    'java': ('devel', 'BJVM: the Java virtual machine for JAR files', 'BJVM: виртуальная машина Java для JAR-файлов'),
    'ls': ('system', 'List files', 'Список файлов'),
    'macro': ('devel', 'Macro: the text editor of the shell', 'Macro: текстовый редактор оболочки'),
    'mkdir': ('system', 'Make a folder', 'Создать папку'),
    'mount': ('system', 'Attach a disk or USB stick', 'Подключить диск или флешку'),
    'mv': ('system', 'Move or rename files', 'Переместить или переименовать файлы'),
    'nova': ('devel', 'Nova: run Nova programs on BJVM', 'Nova: запуск программ на языке Nova в BJVM'),
    'paint': ('graphics', 'Paint for the text screen', 'Рисовалка для текстового экрана'),
    'panic': ('system', 'Stop the system (for testing)', 'Остановить систему (для проверки)'),
    'play': ('multimedia', 'Play a WAV file', 'Проиграть WAV-файл'),
    'pong': ('games', 'Pong for two players or against the computer', 'Пинг-понг вдвоём или с компьютером'),
    'pwd': ('system', 'Show the current folder', 'Показать текущую папку'),
    'rm': ('system', 'Delete files', 'Удалить файлы'),
    'rmdir': ('system', 'Delete an empty folder', 'Удалить пустую папку'),
    'snake': ('games', 'Snake in the text screen', 'Змейка на текстовом экране'),
    'stat': ('system', 'File details', 'Сведения о файле'),
    'su': ('system', 'Become another user', 'Войти другим пользователем'),
    'sudo': ('system', 'Run a command as root', 'Выполнить команду от имени root'),
    'sync': ('system', 'Write everything to the disk', 'Записать всё на диск'),
    'theme': ('desktop', 'Colors of the text screen', 'Цвета текстового экрана'),
    'touch': ('system', 'Make an empty file', 'Создать пустой файл'),
    'true': ('system', 'Exit with status 0', 'Завершиться с кодом 0'),
    'unmount': ('system', 'Detach a disk', 'Отключить диск'),
    'useradd': ('system', 'Add a user', 'Добавить пользователя'),
    'whoami': ('system', 'Show the current user', 'Показать текущего пользователя'),
    'write': ('system', 'Write text to a file', 'Записать текст в файл'),
}
ESSENTIAL = {'append', 'brm', 'cat', 'cd', 'chmod', 'clear', 'cp', 'echo', 'false', 'help', 'init', 'ls',
             'mkdir', 'mount', 'mv', 'pwd', 'rm', 'rmdir', 'stat', 'su', 'sudo', 'sync', 'touch', 'true',
             'unmount', 'useradd', 'whoami', 'write'}
GOUSE = ('desktop', 'Happy Gouse: windows, Gouse Web, IDE, BarnStore, games and manuals',
         'Весёлый Гусь: окна, Гусь.Веб, IDE, BarnStore, игры и справочник')
EXAMPLES = ('devel', 'Example programs: hello, fileio, asciiart, fortune...',
            'Примеры программ: hello, fileio, asciiart, fortune...')
PYTHON = ('devel', 'Python 3.9: the language with its standard library',
          'Python 3.9: язык и его стандартная библиотека')
PYTHON_ROOT = ROOT / 'tools/python/root'
GCC = ('devel', f'GCC {GCC_VERSION}: C and C++ compiler with glibc and libstdc++',
       f'GCC {GCC_VERSION}: компилятор C и C++ с glibc и libstdc++')


def version_of(paths):
    newest = max(pathlib.Path(p).stat().st_mtime for p in paths)
    return time.strftime(f'{BARNIX_VERSION}.%Y%m%d.%H%M', time.gmtime(newest))


def gouse_data():
    """Files Happy Gouse needs besides its program: {destination: source}."""
    files = {}
    font = ROOT / 'apps/gouse/web.bwf'
    if font.exists():
        files['/usr/share/gouse/web.bwf'] = font
    helper = ROOT / 'apps/gouse/debug.py'
    if helper.exists():
        files['/usr/share/gouse/debug.py'] = helper
    examples = ROOT / 'apps/gouse/examples'
    if examples.exists():
        for example in sorted(examples.iterdir()):
            if example.suffix in ('.html', '.js', '.txt'):
                files[f'/usr/share/gouse/examples/{example.name}'] = example
    for page in sorted((ROOT / 'docs/manual').glob('*')):
        if page.is_file():
            files[f'/usr/share/doc/barnix/{page.name}'] = page
    return files


def example_programs():
    import sys
    if str(ROOT / 'scripts') not in sys.path:
        sys.path.insert(0, str(ROOT / 'scripts'))
    from add_elf import complete_apps
    programs = {p.name: p for p in ROOT.glob('apps/*.elf')}
    for p in complete_apps(ROOT):
        programs[p.name] = p
    return [programs[name] for name in sorted(programs)]


class Package:
    def __init__(self, name, meta, files, flags='base'):
        self.name, (self.category, self.description, self.description_ru) = name, meta
        self.files = files          # {destination: (source path, executable)}
        self.flags = flags
        self.bundle = len(files) != 1 or next(iter(files)) != f'/usr/bin/{name}'
        self.version = version_of([source for source, _ in files.values()])

    def installed_kib(self):
        return (sum(source.stat().st_size for source, _ in self.files.values()) + 1023) // 1024

    def base_line(self):
        return '|'.join([self.name, self.version, self.description, self.description_ru, self.category, self.flags])


def system_packages(gcc_files=None):
    """The packages of this source tree (apps/bin must be built)."""
    packages = []
    for program in sorted((ROOT / 'apps/bin').iterdir()):
        name = program.name
        if name not in PROGRAMS and name != 'gouse':
            continue
        if name == 'gouse':
            files = {'/usr/bin/gouse': (program, True)}
            files.update({d: (s, False) for d, s in gouse_data().items()})
            packages.append(Package('gouse', GOUSE, files))
            continue
        flags = 'base,essential' if name in ESSENTIAL else 'base'
        packages.append(Package(name, PROGRAMS[name], {f'/usr/bin/{name}': (program, True)}, flags))
    examples = example_programs()
    if examples:
        packages.append(Package('examples', EXAMPLES, {f'/usr/bin/{p.name}': (p, True) for p in examples}))
    if gcc_files:
        packages.append(Package('gcc', GCC, gcc_files))
    if (PYTHON_ROOT / 'usr/bin/python3').is_file():   # tools/python/build.sh
        files = {}
        for source in sorted(PYTHON_ROOT.rglob('*')):
            if source.is_file() and not source.is_symlink():
                destination = '/' + str(source.relative_to(PYTHON_ROOT))
                files[destination] = (source, destination == '/usr/bin/python3')
        packages.append(Package('python', PYTHON, files))
    return packages


def gcc_files_from_image(image, scratch):
    """GCC's files in live-ext2-gcc.img (Barnix commands left out), with
    specs that link statically and an empty /tmp/.keep for its scratch folder."""
    scratch = pathlib.Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    subprocess.run(['debugfs', '-R', f'rdump /usr {scratch}', str(image)], check=True, capture_output=True, text=True)
    root = scratch / 'usr'
    barnix = {p.name for p in (ROOT / 'apps/bin').iterdir()} | {p.name for p in ROOT.glob('apps/*.elf')} | \
             {p.name for p in (ROOT / 'Setup/extra-apps').glob('*.elf')}
    files = {}
    for part in ('bin', 'include', 'lib', 'libexec'):
        for source in sorted((root / part).rglob('*')):
            if part == 'bin' and source.name in barnix:
                continue
            if source.is_file():
                files['/' + str(source.relative_to(scratch))] = (source, part in ('bin', 'libexec'))
    for required in ('/usr/bin/gcc', '/usr/bin/g++', '/usr/bin/as', '/usr/bin/ld'):
        if required not in files:
            raise FileNotFoundError(f'{image} lacks {required}')
    specs = scratch / 'specs'
    specs.write_text(GCC_SPECS)
    files[f'{GCC_LIB}/specs'] = (specs, False)
    keep = scratch / 'keep'
    keep.write_bytes(b'')
    files['/tmp/.keep'] = (keep, False)
    # The compiler's own age (cc1 in the image), not the moment of extraction
    # or the image's: other changes to the image must not look like a new GCC.
    stat = subprocess.run(['debugfs', '-R', f'stat /usr/libexec/gcc/{GCC_TARGET}/{GCC_VERSION}/cc1', str(image)],
                          capture_output=True, text=True).stdout
    import os, re
    found = re.search(r'mtime: 0x([0-9a-f]+)', stat)
    stamp = int(found.group(1), 16) if found else pathlib.Path(image).stat().st_mtime
    for source, _ in files.values():
        os.utime(source, (stamp, stamp))
    return files


def bpk_stream(files):
    """The BPK1 bytes of files, in pieces."""
    yield b'BPK1'
    for destination, (source, executable) in sorted(files.items()):
        encoded = destination.encode('utf-8')
        size = source.stat().st_size
        if not destination.startswith('/') or '..' in pathlib.PurePosixPath(destination).parts or len(encoded) > 255:
            raise ValueError(f'unsafe package entry: {destination}')
        yield struct.pack('<HII', len(encoded), 0o100755 if executable else 0o100644, size) + encoded
        with source.open('rb') as stream:
            for piece in iter(lambda: stream.read(BLOCK), b''):
                yield piece
    yield struct.pack('<HII', 0, 0, 0)


def write_bpz(path, files):
    pending = bytearray()
    with open(path, 'wb') as out:
        out.write(b'BPZ1')

        def block(raw):
            packer = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed = packer.compress(raw) + packer.flush()
            out.write(struct.pack('<II', len(raw), len(packed)) + packed)
        for piece in bpk_stream(files):
            pending += piece
            while len(pending) >= BLOCK:
                block(bytes(pending[:BLOCK]))
                del pending[:BLOCK]
        if pending:
            block(bytes(pending))
        out.write(struct.pack('<II', 0, 0))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for piece in iter(lambda: stream.read(1 << 20), b''):
            digest.update(piece)
    return digest.hexdigest()


def build_repository(output, gcc_image=None):
    """Writes output/packages/system/*.{elf,bpz}; returns index2 lines.
    Unchanged packages (same version, file present) are not packed again."""
    output = pathlib.Path(output)
    target = output / 'packages/system'
    target.mkdir(parents=True, exist_ok=True)
    state_path = target / 'state.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    lines = []
    with tempfile.TemporaryDirectory(prefix='barnix-system-') as scratch:
        gcc_files = gcc_files_from_image(gcc_image, scratch) if gcc_image else None
        packages = system_packages(gcc_files)
        if not gcc_image and 'gcc' in state:   # keep the published GCC
            old = state['gcc']
            lines.append(old['line'] if old['line'].count('|') >= 10 else old['line'] + '|3081')
        for package in packages:
            suffix = 'bpz' if package.bundle else 'elf'
            path = target / f'{package.name}.{suffix}'
            old = state.get(package.name)
            if not (old and old['version'] == package.version and path.exists()):
                if package.bundle:
                    write_bpz(path, package.files)
                else:
                    shutil.copyfile(next(iter(package.files.values()))[0], path)
            line = '|'.join([package.name, package.version, f'packages/system/{path.name}', str(path.stat().st_size),
                             sha256(path), package.description, package.description_ru, package.category,
                             package.flags, str(package.installed_kib()), str(len(package.files))])
            state[package.name] = {'version': package.version, 'line': line}
            lines.append(line)
            print(f'{package.name:12} {package.version:22} {path.stat().st_size:10} bytes')
    state_path.write_text(json.dumps(state, indent=1, ensure_ascii=False))
    return lines


def base_files(packages, part):
    """What an image or Setup component brings, for brm: {destination: text}.
    /etc/brm/base.d/PART lists the packages, /etc/brm/files/NAME a bundle's files."""
    out = {f'/etc/brm/base.d/{part}': '# brm: part of this system (name|version|description|description.ru|category|flags)\n' +
           ''.join(p.base_line() + '\n' for p in packages)}
    for p in packages:
        if p.bundle:
            out[f'/etc/brm/files/{p.name}'] = ''.join(d + '\n' for d in sorted(p.files))
    return out


def gcc_package(gcc_files):
    return Package('gcc', GCC, gcc_files)


if __name__ == '__main__':
    import argparse
    import sys
    sys.path.insert(0, str(ROOT / 'scripts'))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output', type=pathlib.Path, help='repository folder (BarnixRepo)')
    parser.add_argument('--gcc-image', type=pathlib.Path)
    arguments = parser.parse_args()
    for entry in build_repository(arguments.output, arguments.gcc_image):
        pass
