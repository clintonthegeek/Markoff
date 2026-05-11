#!/bin/bash
# Spec §8.5 — assert that no Qt.callLater appears in focus-related
# blocks of text-bearing delegates. The Math delegate's latexEdit
# Qt.callLater (BlockInternalEdit sub-cursor, spec §2.2) is the
# one exception and is explicitly allowlisted by line context.
set -euo pipefail

ROOT="${1:?usage: $0 <repo-root>}"
DELEGATES="$ROOT/libs/markoff-live/qml/delegates"

# Find Qt.callLater hits in text-bearing delegates, EXCLUDING the
# MathDelegate latexEdit-popup line (BlockInternalEdit sub-cursor).
HITS=$(grep -rn "Qt\.callLater" \
       "$DELEGATES/ParagraphDelegate.qml" \
       "$DELEGATES/HeadingDelegate.qml" \
       "$DELEGATES/BlockquoteDelegate.qml" \
       "$DELEGATES/CodeBlockDelegate.qml" \
       "$DELEGATES/ListItemDelegate.qml" \
       "$DELEGATES/MathDelegate.qml" \
       | grep -v "latexEdit\.forceActiveFocus" || true)

if [[ -n "$HITS" ]]; then
    echo "FAIL: Qt.callLater found in focus path:"
    echo "$HITS"
    exit 1
fi

echo "PASS: no Qt.callLater in focus path"
