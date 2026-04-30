#!/usr/bin/env bash
# Perf-record a 30-second typing window in markoff-view-qml-app.
#
# Usage (TWO terminals):
#
#   Terminal A (GUI app, started first):
#     ./build-dev/bin/markoff-view-qml-app docs/specs/2026-04-28-foundation-design.md
#
#   Terminal B (perf record, started after the app's window is visible
#   and you've clicked into the editor area):
#     ./scripts/perf-record-typing.sh stage1-2-after
#
# The script will:
#   1. Find the markoff-view-qml-app PID.
#   2. Run `pkexec perf record -F 99 -g` against it for 30 seconds.
#   3. Chown the resulting data file back to $USER.
#   4. Print a brief top-30 summary so the trend is visible immediately.
#
# Type continuously in Terminal A's editor window during the 30-second
# perf window. The script prints "TYPE NOW" / "DONE — releasing perf"
# markers so you know when to start and stop.
#
# Output: /tmp/perf-<label>.data (full perf data; analyze later with
# `perf report -i ...`).

set -euo pipefail

label="${1:-untagged}"
out="/tmp/perf-${label}.data"

pid="$(pgrep -f markoff-view-qml-app | head -1 || true)"
if [[ -z "$pid" ]]; then
    echo "ERROR: no running markoff-view-qml-app found." >&2
    echo "Start it in another terminal first:" >&2
    echo "  ./build-dev/bin/markoff-view-qml-app docs/specs/2026-04-28-foundation-design.md" >&2
    exit 1
fi

echo "Targeting markoff-view-qml-app PID: $pid"
echo "Output: $out"
echo
echo ">>> TYPE NOW into the editor window — perf will record for 30 seconds <<<"
echo

pkexec perf record -F 99 -p "$pid" -g -o "$out" -- sleep 30 || {
    echo "perf record failed (perhaps you cancelled the auth prompt?)" >&2
    exit 2
}

echo
echo ">>> DONE — releasing perf, chowning data file back to $USER <<<"
pkexec chown "$USER:$USER" "$out"

echo
echo "Top-30 leaf symbols (--no-children, percent_limit 0.5):"
echo
perf report -i "$out" --no-children --stdio --percent-limit 0.5 2>/dev/null \
    | head -60

echo
echo "Full data: $out"
echo "Reanalyze with: perf report -i $out --no-children --stdio --percent-limit 0.5"
