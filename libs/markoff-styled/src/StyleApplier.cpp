// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyleApplier.h"

#include "FormatPass.h"

#include <QPointer>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>

#include <memory>

#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

namespace Markoff::Styled {

StyleApplier::StyleApplier(QObject *parent) : QObject(parent) {}
StyleApplier::~StyleApplier() = default;

void StyleApplier::setTextDocument(QTextDocument *doc) {
    if (m_textDocument == doc) return;
    m_textDocument = doc;
    rerender();
}

void StyleApplier::setMarkoffDocument(Markoff::MarkoffDocument *doc) {
    if (m_markoffDocument == doc) return;
    if (m_markoffDocument) {
        disconnect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                   this, &StyleApplier::onD2Changed);
    }
    m_markoffDocument = doc;
    if (m_markoffDocument) {
        connect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &StyleApplier::onD2Changed);
    }
    rerender();
}

void StyleApplier::setTheme(const Markoff::Theme *theme) {
    // M7: store the theme BY VALUE and compare by content. The old shape
    // stored a pointer and pointer-compared, so a changed Theme written
    // into the same address (a leaf's own Theme member, reassigned) was
    // invisible and the leaf never restyled.
    if (m_theme) {
        if (!theme) {
            m_theme.reset();
            rerender();
            return;
        }
        if (*m_theme == *theme) return;
    } else if (!theme) {
        return;   // still no theme: nothing changed
    }
    m_theme = std::make_unique<Markoff::Theme>(*theme);
    rerender();
}

void StyleApplier::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    rerender();
}

void StyleApplier::setTextEdit(QTextEdit *edit) {
    m_textEdit = edit;
}

void StyleApplier::captureScrollBeforeEdit() {
    if (!m_textEdit || !m_textEdit->verticalScrollBar()) return;
    m_pendingScrollCapture = m_textEdit->verticalScrollBar()->value();
}

void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    m_blockHashes.clear();  // Force every block to apply on next pass.
    applyFormats();
}

void StyleApplier::onD2Changed() {
    applyFormats();
}

void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;

    // Snapshot scroll for in-place-edit preservation. Prefer the pre-captured
    // value from captureScrollBeforeEdit() (which fires before the binding's
    // setPlainText resets the bar), falling back to an in-place snapshot.
    // Restore is deferred (see QTimer::singleShot below) because Qt's layout
    // signals fire after endEditBlock and would override a synchronous setValue.
    const int savedScroll = (m_pendingScrollCapture >= 0)
        ? m_pendingScrollCapture
        : ((m_textEdit && m_textEdit->verticalScrollBar())
           ? m_textEdit->verticalScrollBar()->value() : -1);
    m_pendingScrollCapture = -1;  // consume; next pass needs a fresh capture.

    // Capture "did we have blocks before this pass" before FormatPass mutates
    // the gate — drives the scroll-restore guard (first pass = top is correct).
    const bool hadPrevBlocks = !m_blockHashes.isEmpty();

    FormatPass::Options opts;
    opts.fontScale = m_fontScale;
    opts.theme     = m_theme.get();   // by-value store (M7); null = headless fallback
    opts.inferKind = true;
    const FormatPass::Result r =
        FormatPass::apply(m_textDocument, m_markoffDocument, opts, &m_blockHashes);

    m_hashSkipsLastPass = r.hashSkips;

    // StyleApplier is the sole actor that issues Cmd::changeKind. FormatPass
    // only suggests; we queue for deferred dispatch (avoids synchronous
    // re-entry into d2DocumentChanged). INVARIANTS §2/§3.
    for (const auto &sug : r.kindSuggestions)
        m_pendingKindChanges.push_back({sug.id, sug.newKind});

    // Deferred scroll restore. Skip on: structural change (block set changed —
    // natural scroll is correct); first pass (no prior blocks — top is
    // correct); no scroll handle.
    if (!r.structural && hadPrevBlocks && savedScroll >= 0 && m_textEdit) {
        QPointer<QTextEdit> editPtr = m_textEdit;
        QTimer::singleShot(0, this, [editPtr, savedScroll]() {
            if (editPtr && editPtr->verticalScrollBar()) {
                editPtr->verticalScrollBar()->setValue(savedScroll);
            }
        });
    }

    ++m_restyleCount;
    m_applyingFormats = false;

    if (!m_pendingKindChanges.empty()) {
        QTimer::singleShot(0, this, &StyleApplier::applyPendingKindChanges);
    }
}

void StyleApplier::applyPendingKindChanges() {
    if (!m_markoffDocument) {
        m_pendingKindChanges.clear();
        return;
    }
    auto changes = std::move(m_pendingKindChanges);
    m_pendingKindChanges.clear();
    for (const auto &chg : changes) {
        Markoff::Cmd::changeKind(*m_markoffDocument, chg.id, chg.newKind);
    }
}

}  // namespace Markoff::Styled
