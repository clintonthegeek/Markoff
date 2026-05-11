#!/bin/bash
# Spec §8.5 — assert that forceActiveFocus() in QML delegates appears
# only inside takeFocus() bodies or sub-cursor edit-mode entry points
# (enterEditMode, enterAltEdit, or their sub-cursor UI event handlers
# identified by: latexEdit, altInput, langInput).
set -euo pipefail

ROOT="${1:?usage: $0 <repo-root>}"
DELEGATES="$ROOT/libs/markoff-live/qml/delegates"

# Only check delegate files. LiveView.qml's forceActiveFocus calls are
# fallback/miss-case handlers that are not part of the chokepoint path.
HITS=$(grep -rn "forceActiveFocus" "$DELEGATES" || true)

VIOLATIONS=""
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    file="${line%%:*}"
    lineno="${line#*:}"; lineno="${lineno%%:*}"

    # Scan backward from the hit to find the nearest enclosing 'function'
    # declaration. Also collect a small forward+backward window to catch
    # inline sub-cursor identifiers (latexEdit, altInput, langInput).
    enclosing_fn=""
    if [[ -f "$file" ]]; then
        enclosing_fn=$(awk -v target="$lineno" '
            NR <= target && /function [a-zA-Z]/ { last = $0 }
            NR == target { print last; exit }
        ' "$file")
    fi

    # Near-window (20 lines back, 1 forward) for sub-cursor variable detection.
    # Must reach back to the enclosing Item's id declaration (e.g. id: langInput).
    start=$(( lineno - 20 )); [[ $start -lt 1 ]] && start=1
    near=$(sed -n "${start},$((lineno + 1))p" "$file" 2>/dev/null || true)

    # Allowlisted contexts:
    #   - function takeFocus       (chokepoint entry point)
    #   - function enter*          (sub-cursor mode: enterEditMode, enterAltEdit)
    #   - latexEdit / altInput / langInput  (sub-cursor UI identifiers)
    if echo "$enclosing_fn" | grep -qE "function takeFocus|function enter"; then
        continue
    fi
    if echo "$near" | grep -qE "latexEdit|altInput|langInput"; then
        continue
    fi

    VIOLATIONS+="$line"$'\n'
done <<< "$HITS"

if [[ -n "$VIOLATIONS" ]]; then
    echo "FAIL: forceActiveFocus() outside takeFocus() or sub-cursor entry:"
    echo "$VIOLATIONS"
    exit 1
fi

echo "PASS: focus path exits through chokepoint"
