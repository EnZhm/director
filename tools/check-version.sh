#!/usr/bin/env bash
# check-version.sh: every plugin's buildspec.json carries the suite version from VERSION.
# set-version: tools/check-version.sh --set  (rewrites the buildspecs from VERSION)
set -euo pipefail
cd "$(dirname "$0")/.."
v=$(tr -d '[:space:]' < VERSION)
[[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-(beta|rc)[0-9]+)?$ ]] || { echo "VERSION '$v' is not X.Y.Z"; exit 1; }
fail=0
for b in plugins/*/buildspec.json; do
	if [ "${1:-}" = --set ]; then
		tmp=$(mktemp); jq --indent 4 --arg v "$v" '.version = $v' "$b" > "$tmp" && mv "$tmp" "$b"
	fi
	got=$(jq -r .version "$b")
	if [ "$got" = "$v" ]; then echo "ok   $b $got"; else echo "FAIL $b $got (VERSION $v)"; fail=1; fi
done
exit $fail
