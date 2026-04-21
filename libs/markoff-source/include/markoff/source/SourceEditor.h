// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <QTextDocument>

#include <markoff/MarkdownView.h>

namespace Qutepart { class Qutepart; }

namespace Markoff {
class MarkoffDocument;
class Theme;
class ResourceProvider;
class LinkResolver;
}

namespace Markoff::Source {

class SourceSearchAdapter;

/// Plain-text markdown editor built on Qutepart. MarkdownView-compatible
/// leaf widget for the tri-view family.
class SourceEditor : public Markoff::MarkdownView {
    Q_OBJECT
public:
    explicit SourceEditor(QWidget *parent = nullptr);
    ~SourceEditor() override;

    // MarkdownView contract (Phase A: forwarding-only).
    void setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::MarkoffDocument *document() const override;
    void setViewTheme(const Markoff::Theme &) override;
    void setViewResourceProvider(Markoff::ResourceProvider *) override;
    void setViewLinkResolver(Markoff::LinkResolver *) override;
    float scrollPosition() const override;
    void setScrollPosition(float visualLine) override;
    void zoomIn() override;
    void zoomOut() override;
    void resetZoom() override;
    QJsonObject ephemeralState() const override;
    void setEphemeralState(const QJsonObject &) override;
    Markoff::SearchAdapter *searchAdapter() override;
    bool hasCursor() const override { return true; }
    bool hasEditing() const override { return true; }
    bool hasFold() const override { return true; }
    Markoff::CursorPos cursorPosition() const override;
    bool setCursorPosition(Markoff::CursorPos) override;
    bool setReadOnly(bool) override;
    bool isReadOnly() const override;
    QVector<Markoff::FoldSpec> foldedHeadings() const override;
    void setFoldedHeadings(const QVector<Markoff::FoldSpec> &) override;

    // Internal hooks exposed for SourceSearchAdapter and tests
    Qutepart::Qutepart *qutepart() { return m_qutepart; }

    /// Returns Qutepart's inner QTextDocument. Exposed for tests that need
    /// to drive edits via QTextCursor without pulling in the full Qutepart
    /// header — preferred over qutepart()->document() in test code.
    QTextDocument *innerDocument() const;

    /// Convenience: plain text content of Qutepart's internal buffer.
    /// Mirrors QPlainTextEdit::toPlainText() for test/host access without
    /// exposing Qutepart directly.
    QString toPlainText() const;

private:
    Qutepart::Qutepart *m_qutepart = nullptr;
    Markoff::MarkoffDocument *m_markoffDoc = nullptr;
    std::unique_ptr<SourceSearchAdapter> m_searchAdapter;
    int m_baseFontPt = 0;  // captured on construction for resetZoom
    bool m_applyingCanonicalDelta = false;

    void onCanonicalContentsChanged(qsizetype offset, qsizetype removed, qsizetype inserted);
    void onCanonicalReloaded();
    void onLocalContentsChange(int position, int charsRemoved, int charsAdded);
};

}  // namespace Markoff::Source
