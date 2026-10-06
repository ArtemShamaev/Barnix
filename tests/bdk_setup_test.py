"""Install/reinstall the shipped Bash setup into an isolated prefix and template hive."""
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='barnix-bdk-setup-') as temporary:
        work = Path(temporary)
        package = work / 'package with spaces'
        with zipfile.ZipFile(ROOT / 'BDK/dist/Barnino.Systems.BDK-0.1.0.zip') as archive:
            archive.extractall(package)
        env = dict(os.environ, DOTNET_CLI_HOME=str(work / 'dotnet'),
                   DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_TELEMETRY_OPTOUT='1',
                   DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
        prefix = work / 'installed BDK'
        command = ['bash', str(package / 'setup.sh'), '--prefix', str(prefix), '--no-deps',
                   '--no-profile', '--template-hive', str(work / 'hive')]
        profiles = {p: p.read_bytes() if p.exists() else None for p in
                    (Path.home() / '.profile', Path.home() / '.bashrc')}
        subprocess.run(command, check=True, env=env)
        assert (prefix / 'sdk/include/barnix_api.h').is_file()
        assert (prefix / 'sdk/include/barnix_api').is_file()
        assert (prefix / 'env.sh').is_file()
        sentinel = prefix / 'keep-project.txt'; sentinel.write_text('keep')
        subprocess.run([*command, '--no-smoke-test'], check=True, env=env)
        assert sentinel.read_text() == 'keep'
        for path, before in profiles.items():
            assert (path.read_bytes() if path.exists() else None) == before
        listed = subprocess.check_output(['dotnet', 'new', 'list', 'bca', '--debug:custom-hive', str(work / 'hive')],
                                         env=env, text=True)
        assert 'bca' in listed and 'bcawms' in listed
        assert 'bca_cs' in listed and 'bcawms_cs' in listed
        assert 'bga' in listed and 'bga_cs' in listed
        invalid = subprocess.run(['bash', str(package / 'setup.sh'), '--prefix'], capture_output=True, text=True)
        assert invalid.returncode == 2
        assert 'winget' in (package / 'windows/setup.ps1').read_text()
        assert '#define BDKDist ".."' in (package / 'windows/bdk.iss').read_text()
    print('Bash setup, C/C++ smoke builds, repeat installation and packaged Windows setup passed')


if __name__ == '__main__':
    main()
