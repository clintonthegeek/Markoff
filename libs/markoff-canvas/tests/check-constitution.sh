#!/usr/bin/env bash
# Constitution gate for the markoff-canvas spike.
#
# Enforces C1–C4 of docs/specs/2026-08-13-markoff-canvas-spike-design.md §6
# over libs/markoff-canvas/ ONLY. Any hit is a spike-level failure, not a
# lint nit: the premise under test is that this leaf needs none of the
# arbitration machinery the other leaves accumulated. If you are here
# because the gate is red, do not add an exception — stop, write the
# finding into the spec's §9, and report it.
#
# The gate is grep. Grep is not the constraint; the constraint is C1–C4.
# Renaming a re-entrance guard dodges the grep and still fails the spike
# (spec §6, C1: "grep pattern plus honest review"). T11 is the honest
# review.
#
# Usage: libs/markoff-canvas/tests/check-constitution.sh
# Exit:  0 clean, 1 violation(s) printed.

set -uo pipefail

LEAF="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Scan sources and build files. Prose (CLAUDE.md) and this script itself
# name the forbidden patterns by necessity and are not scanned.
mapfile -t FILES < <(
    find "$LEAF" \
        -type d \( -name 'build' -o -name 'build-*' -o -name '.git' \) -prune -o \
        -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \
                   -o -name '*.qml' -o -name 'CMakeLists.txt' \) -print \
    | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "check-constitution: no source files found under $LEAF" >&2
    exit 1
fi

fail=0

# Emit a file with whole-line comments blanked (line numbers preserved), so
# that prose *about* the constitution — this leaf's own rationale comments —
# does not read as a violation of it. A real violation is executable code or
# a build directive, which cannot live on a pure comment line.
#
# C++ preprocessor lines are NOT stripped: `#include <QQuickItem>` must still
# be caught. Only CMake gets `#` treated as a comment marker.
decomment() {
    local f="$1"
    if [[ "$(basename "$f")" == "CMakeLists.txt" ]]; then
        sed -E 's@^[[:space:]]*#.*$@@' "$f"
    else
        sed -E 's@^[[:space:]]*(//|\*|/\*).*$@@' "$f"
    fi
}

# check <rule> <summary> <extended-regex> [file...]
check() {
    local rule="$1" summary="$2" pattern="$3"
    shift 3
    local scan=("$@")
    [[ ${#scan[@]} -eq 0 ]] && scan=("${FILES[@]}")

    local hits="" f out
    for f in "${scan[@]}"; do
        out="$(decomment "$f" | grep -nE "$pattern")" || continue
        hits+="$(sed "s|^|  libs/markoff-canvas/${f#"$LEAF"/}:|" <<<"$out")"$'\n'
    done

    if [[ -n "${hits//[$'\n']/}" ]]; then
        echo "VIOLATION $rule — $summary"
        printf '%s' "$hits"
        echo
        fail=1
    fi
}

check "C1" "re-entrance guard (suppressing reaction to our own write)" \
      'm_applying|isApplying|m_inSet|m_updating'

check "C2" "view-side deferral (ordering problem escaped instead of solved)" \
      'singleShot\(0|callLater|QueuedConnection'

check "C3" "QTextDocument / QML text instance (second document model)" \
      'QTextDocument|QTextEdit|QPlainTextEdit|QQuick|import QtQuick'

check "C4" "flat/global coordinate space (cross-block byte arithmetic)" \
      'applyFlatEdit|flatView\(|widgetFlatView'

# C3, link-line half: the leaf may not link Quick or another view leaf.
CMAKE_FILES=()
for f in "${FILES[@]}"; do
    [[ "$(basename "$f")" == "CMakeLists.txt" ]] && CMAKE_FILES+=("$f")
done
if [[ ${#CMAKE_FILES[@]} -gt 0 ]]; then
    check "C3" "links Quick or another view leaf" \
          'Qt6::Quick|markoff_live|markoff_styled|markoff_source|Markoff::(Live|Styled|Source)' \
          "${CMAKE_FILES[@]}"
fi

if [[ $fail -ne 0 ]]; then
    echo "check-constitution: FAILED (see spec §6; log the finding in §9)" >&2
    exit 1
fi

echo "check-constitution: clean (C1–C4) over ${#FILES[@]} files"
exit 0
