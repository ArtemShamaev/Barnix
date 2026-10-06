#!/usr/bin/env bash
# Barnino Systems BDK: Linux x86_64 installer. Run as your regular user.
set -Eeuo pipefail
trap 'printf "BDK: installation failed at line %s. Fix the error above and rerun setup.\n" "$LINENO" >&2' ERR
bdk_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
bdk_prefix="${HOME}/.local/share/barnino-bdk"
bdk_deps=1
bdk_profile=1
bdk_smoke=1
bdk_hive=""
usage() {
    cat <<'HELP'
Usage: bash setup.sh [--prefix DIRECTORY] [--no-deps] [--no-profile]
                     [--no-smoke-test] [--template-hive DIRECTORY]
Installs build dependencies if missing, .NET SDK 10 if missing, the SDK and
bca/bcawms templates; then builds test projects. sudo is used only for OS packages.
--no-deps       Offline mode; fail if dependencies are missing.
--no-profile    Do not modify ~/.profile or ~/.bashrc; source PREFIX/env.sh yourself.
--no-smoke-test Skip final example builds.
--template-hive Isolated dotnet template registry (for testing).
HELP
}
while (($#)); do
    case "$1" in
        --prefix|--template-hive)
            [[ $# -ge 2 && -n "$2" ]] || { usage >&2; exit 2; }
            if [[ "$1" == --prefix ]]; then bdk_prefix=$2; else bdk_hive=$2; fi
            shift 2 ;;
        --no-deps) bdk_deps=0; shift ;;
        --no-profile) bdk_profile=0; shift ;;
        --no-smoke-test) bdk_smoke=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'BDK setup supports Linux x86_64. Use the Windows installer on Windows.' >&2; exit 1; }
[[ "$bdk_prefix" != *$'\n'* && "$bdk_prefix" != *$'\r'* ]] || exit 2
mkdir -p -- "$bdk_prefix"
bdk_prefix=$(cd -- "$bdk_prefix" && pwd)
bdk_work=$(mktemp -d -t barnino-bdk-setup-XXXXXXXX)
trap 'rm -rf -- "$bdk_work"' EXIT
bdk_root() {
    if [[ $EUID == 0 ]]; then "$@"; else sudo -- "$@"; fi
}
bdk_need=0
for bdk_tool in python3 gcc g++ ld curl tar gzip; do
    command -v "$bdk_tool" >/dev/null || bdk_need=1
done
if command -v python3 >/dev/null && ! python3 -c 'import sys; assert sys.version_info >= (3,9)' 2>/dev/null; then bdk_need=1; fi
if ! command -v dotnet >/dev/null && [[ ! -x "$bdk_prefix/tools/dotnet/dotnet" ]]; then bdk_need=1; fi
if ((bdk_need)); then
    ((bdk_deps)) || { echo 'Missing Python 3.9+, GCC/G++, binutils, curl, tar or gzip (--no-deps).' >&2; exit 1; }
    echo 'Installing missing build prerequisites (sudo may ask for your password)...'
    if command -v apt-get >/dev/null; then
        bdk_root apt-get update
        bdk_root apt-get install -y gcc g++ binutils python3 curl ca-certificates tar gzip libicu-dev libssl-dev zlib1g
    elif command -v dnf >/dev/null; then
        bdk_root dnf install -y gcc gcc-c++ binutils python3 curl ca-certificates tar gzip libicu openssl-libs zlib
    elif command -v pacman >/dev/null; then
        bdk_root pacman -S --needed --noconfirm gcc binutils python curl ca-certificates tar gzip icu openssl zlib
    elif command -v zypper >/dev/null; then
        bdk_root zypper --non-interactive install gcc gcc-c++ binutils python3 curl ca-certificates tar gzip libicu-devel libopenssl-devel zlib
    else
        echo 'Unsupported package manager. Install prerequisites and run setup again.' >&2; exit 1
    fi
fi
python3 -c 'import sys; assert sys.version_info >= (3,9), "Python 3.9+ is required"'
if [[ ! -d "$bdk_source/sdk" ]]; then
    [[ -f "$bdk_source/package.py" ]] || { echo 'Incomplete BDK package: sdk/ is missing.' >&2; exit 1; }
    python3 "$bdk_source/package.py"
    bdk_source="$bdk_source/dist"
fi
bdk_dotnet=$(command -v dotnet || true)
if [[ -x "$bdk_prefix/tools/dotnet/dotnet" ]]; then bdk_dotnet="$bdk_prefix/tools/dotnet/dotnet"; fi
if [[ -z "$bdk_dotnet" ]] || ! "$bdk_dotnet" --list-sdks | grep -q '^10\.'; then
    ((bdk_deps)) || { echo '.NET SDK 10 is missing (--no-deps).' >&2; exit 1; }
    echo 'Installing .NET SDK 10 from Microsoft into the BDK user directory...'
    curl --fail --location --proto '=https' --tlsv1.2 --retry 3 https://dot.net/v1/dotnet-install.sh -o "$bdk_work/dotnet-install.sh"
    bash "$bdk_work/dotnet-install.sh" --channel 10.0 --install-dir "$bdk_prefix/tools/dotnet" --no-path
    bdk_dotnet="$bdk_prefix/tools/dotnet/dotnet"
fi
"$bdk_dotnet" --list-sdks | grep -q '^10\.' || { echo '.NET SDK 10 is not usable.' >&2; exit 1; }
bdk_args=(--prefix "$bdk_prefix" --dotnet "$bdk_dotnet")
bdk_new=()
if [[ -n "$bdk_hive" ]]; then bdk_args+=(--template-hive "$bdk_hive"); bdk_new+=(--debug:custom-hive "$bdk_hive"); fi
python3 "$bdk_source/install.py" "${bdk_args[@]}"
mkdir -p "$bdk_prefix/bin"
ln -sfn -- "$bdk_prefix/sdk/tools/bcc" "$bdk_prefix/bin/bcc"
if [[ "$bdk_dotnet" == "$bdk_prefix/tools/dotnet/dotnet" ]]; then
    ln -sfn -- "$bdk_dotnet" "$bdk_prefix/bin/dotnet"
fi
{
    printf '# Barnino Systems BDK environment\n'
    printf 'export BDK_HOME=%q\n' "$bdk_prefix"
    printf 'export PATH=%q:"$PATH"\n' "$bdk_prefix/bin"
    if [[ "$bdk_dotnet" == "$bdk_prefix/tools/dotnet/dotnet" ]]; then printf 'export DOTNET_ROOT=%q\n' "$bdk_prefix/tools/dotnet"; fi
} > "$bdk_prefix/env.sh"
if ((bdk_profile)); then
    printf -v bdk_line '. %q # Barnino Systems BDK' "$bdk_prefix/env.sh"
    for bdk_rc in "$HOME/.profile" "$HOME/.bashrc"; do
        if ! grep -Fqx -- "$bdk_line" "$bdk_rc" 2>/dev/null; then printf '\n%s\n' "$bdk_line" >> "$bdk_rc"; fi
    done
fi
if ((bdk_smoke)); then
    echo 'Checking C/C++/C# templates and ELF builds...'
    for bdk_template in bca bcawms bca_cs bcawms_cs bga bga_cs; do
        bdk_project="$bdk_work/$bdk_template"
        bdk_language=()
        if [[ "$bdk_template" == bcawms ]]; then bdk_language=(--c); fi
        "$bdk_dotnet" new "$bdk_template" -o "$bdk_project" --no-update-check "${bdk_new[@]}" "${bdk_language[@]}"
        "$bdk_dotnet" run --project "$bdk_project"
        [[ -s "$bdk_project/elf/app.elf" ]] || { echo 'BDK smoke build did not produce app.elf.' >&2; exit 1; }
    done
fi
printf '\nBDK installation complete. In this terminal run:\n  source %q\n' "$bdk_prefix/env.sh"
echo 'Then: dotnet new bca -o MyApp && cd MyApp && dotnet run'
