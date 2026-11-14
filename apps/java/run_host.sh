#!/bin/sh
# Run the host BJVM build with a private home (obj/home), so programs that
# keep data in ~/.minecraft or similar never touch the real home directory.
# Usage: sh run_host.sh [java options] class|-jar file [args]
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$here/obj/home"
exec "$here/obj/java-host" -Duser.home="$here/obj/home" "$@"
