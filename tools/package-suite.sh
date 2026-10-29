#!/usr/bin/env bash
# package-suite.sh <version> <parts-dir> <out-dir>
# Builds director-<version>-windows-x64.zip from the per-plugin CI zips: the four plugin folders
# (each <plugin>/bin/64bit + <plugin>/data), INSTALL.txt, LICENSE and SHA256SUMS.txt.
set -euo pipefail
v=$1; parts=$2; out=$3
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d); mkdir -p "$out"
name="director-${v}-windows-x64"
mkdir -p "$stage/$name"
n=0
for z in $(find "$parts" -name "*-${v}-windows-x64.zip" | sort); do
	unzip -q -o "$z" -d "$stage/$name"
	n=$((n + 1))
done
want=$(ls -d "$root"/plugins/*/ | wc -l)
[ "$n" = "$want" ] || { echo "expected $want plugin zips, got $n"; exit 1; }
for d in "$stage/$name"/*/; do
	[ -d "$d/bin/64bit" ] || { echo "bad layout: $d"; exit 1; }
done
cp "$root/INSTALL.txt" "$root/LICENSE" "$stage/$name/"
(cd "$stage/$name" && find . -type f ! -name SHA256SUMS.txt -printf '%P\n' | sort | xargs -d '\n' sha256sum > SHA256SUMS.txt)
(cd "$stage" && zip -q -r -X "$name.zip" "$name")
cp "$stage/$name.zip" "$out/"
(cd "$out" && sha256sum "$name.zip" > SHA256SUMS.txt)
cp "$root/INSTALL.txt" "$out/"
ls -l "$out"; unzip -l "$out/$name.zip" | tail -n +1 | awk '{print $4}' | grep -E '/$|\.dll$|\.txt$|LICENSE' | head -40
