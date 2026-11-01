#!/usr/bin/env bash
# Builds a self-contained Windows folder from an MSYS2 UCRT64 build:
# the app, Qt (via windeployqt), libmpv, FFmpeg and every DLL they load.
set -euo pipefail

build=${1:-build}
dist=${2:-dist/mxCut}
prefix=$(cygpath -u "$MINGW_PREFIX")

rm -rf "$dist"
mkdir -p "$dist"
cp "$build/mxcut.exe" "$dist/"
cp "$prefix/bin/ffmpeg.exe" "$prefix/bin/ffprobe.exe" "$dist/"

deployqt=$(command -v windeployqt6 || command -v windeployqt)
"$deployqt" --release --no-translations --qmldir qml --qmldir packaging/qml-deploy "$dist/mxcut.exe"

# Copy every DLL from the MSYS2 prefix that anything in the folder links to,
# repeating until nothing new turns up.
while :; do
    added=0
    while IFS= read -r dll; do
        name=$(basename "$dll")
        if [ ! -e "$dist/$name" ]; then
            cp "$dll" "$dist/"
            added=1
        fi
    done < <(find "$dist" -iname '*.exe' -o -iname '*.dll' | xargs -d '\n' ldd 2>/dev/null \
                | awk '{print $3}' | grep -i "^$prefix/bin/" | sort -u)
    [ "$added" = 0 ] && break
done

# The updater talks to GitHub over HTTPS, which needs a TLS plugin.
if ! ls "$dist"/tls/*.dll >/dev/null 2>&1; then
    echo "No Qt TLS plugin was deployed; update checks would fail" >&2
    exit 1
fi

echo "Packaged $(find "$dist" -type f | wc -l) files, $(du -sh "$dist" | cut -f1)"
