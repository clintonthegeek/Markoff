#!/usr/bin/env bash
# Backend thread-safety gate for markoff-embed.
#
# A backend's render() runs on the embed runtime's worker thread (the pool
# in EmbedRuntime::request). QPixmap is a GUI-thread resource — Qt's own
# rule — so a backend that constructs one renders on the wrong thread. It
# works on many platforms and breaks on others, which is the worst way for
# it to fail.
#
# QImage is the thread-safe raster and is what the contract wants: the
# artifact crosses back to the GUI thread as bytes.
#
# This is a grep, like the canvas leaf's constitution gate: grep is not the
# constraint, the constraint is "no QPixmap on a worker thread". Renaming a
# QPixmap dodges the grep and still breaks the contract.
#
# Usage: libs/markoff-embed/tests/check-backend-threads.sh
# Exit:  0 clean, 1 violation(s) printed.

set -uo pipefail

LEAF="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mapfile -t FILES < <(
    find "$LEAF/backends" \
        -type d \( -name build -o -name 'build-*' -o -name .git \) -prune -o \
        -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \) -print \
    | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "check-backend-threads: no backend sources found under $LEAF/backends" >&2
    exit 1
fi

fail=0

# Strip whole-line comments (the file's own rationale mentions QPixmap), so
# prose about the rule does not read as a violation of it. A real violation
# is executable code, which cannot live on a pure comment line.
decomment() {
    sed -E 's@//.*$@@' "$1"
}

for file in "${FILES[@]}"; do
    if decomment "$file" | grep -nE '\bQPixmap\b' > /dev/null; then
        echo "check-backend-threads: QPixmap in a backend source (worker thread): $file"
        decomment "$file" | grep -nE '\bQPixmap\b' | sed 's/^/    /'
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    echo "check-backend-threads: FAILED — a backend may not construct a QPixmap;"
    echo "    backends render on the embed runtime's worker thread. Use QImage."
    exit 1
fi

echo "check-backend-threads: clean (no QPixmap in ${#FILES[@]} backend sources)"
exit 0
