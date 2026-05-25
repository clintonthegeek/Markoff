#!/usr/bin/env bash
# Run Markoff tests without stealing focus.
#
# Default mode: QT_QPA_PLATFORM=offscreen — tests render to memory buffers,
# no window appears on your screen.
#
# Usage:
#   scripts/run-tests.sh                       # full suite, offscreen
#   scripts/run-tests.sh -R 'cursor'           # ctest pattern, offscreen
#   scripts/run-tests.sh --bin tst_block_id    # one test binary, offscreen
#   scripts/run-tests.sh --bin tst_foo -- -v2  # one binary with QtTest args
#   scripts/run-tests.sh --nested              # nested Weston compositor
#                                              #   (single visible window,
#                                              #   does not steal focus)
#   scripts/run-tests.sh --direct              # against current Wayland/X11
#                                              #   session — requires
#                                              #   MARKOFF_ALLOW_DIRECT=1
#
# Caps parallelism at -j 8 per project convention (see CLAUDE.md).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MARKOFF_BUILD_DIR:-$REPO_ROOT/build-dev}"

mode="offscreen"
bin=""
ctest_args=()
bin_args=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --nested)   mode="nested"; shift ;;
        --direct)   mode="direct"; shift ;;
        --offscreen) mode="offscreen"; shift ;;
        --bin)      bin="$2"; shift 2 ;;
        --)         shift; bin_args=("$@"); break ;;
        *)          ctest_args+=("$1"); shift ;;
    esac
done

case "$mode" in
    offscreen)
        export QT_QPA_PLATFORM=offscreen
        ;;
    direct)
        if [[ "${MARKOFF_ALLOW_DIRECT:-0}" != "1" ]]; then
            echo "error: --direct will spawn test windows on your real session." >&2
            echo "       Set MARKOFF_ALLOW_DIRECT=1 to confirm." >&2
            exit 2
        fi
        unset QT_QPA_PLATFORM
        echo "warning: running tests against the real display; expect focus interruptions." >&2
        ;;
    nested)
        if ! command -v weston >/dev/null 2>&1; then
            echo "error: weston not installed. Try: sudo pacman -S weston" >&2
            exit 2
        fi
        runtime_dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
        sock="markoff-test-$$"
        weston --backend=wayland --width=1280 --height=800 --socket="$sock" >/dev/null 2>&1 &
        weston_pid=$!
        trap 'kill $weston_pid 2>/dev/null || true' EXIT
        # Wait for socket to appear (up to 5s).
        for _ in {1..50}; do
            [[ -S "$runtime_dir/$sock" ]] && break
            sleep 0.1
        done
        if [[ ! -S "$runtime_dir/$sock" ]]; then
            echo "error: nested weston failed to start (socket $sock not found)" >&2
            exit 2
        fi
        export WAYLAND_DISPLAY="$sock"
        export QT_QPA_PLATFORM=wayland
        ;;
esac

if [[ -n "$bin" ]]; then
    exec "$BUILD_DIR/bin/$bin" "${bin_args[@]}"
else
    exec ctest --test-dir "$BUILD_DIR" --output-on-failure -j 8 "${ctest_args[@]}"
fi
