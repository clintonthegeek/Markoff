// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>

class QTextDocument;

namespace Markoff {
class MarkoffDocument;
class Theme;
}

namespace Markoff::Styled {

/// Subscribes to `MarkoffDocument::d2DocumentChanged`. On each fire,
/// walks `iterateBlocks()` and applies block + inline formats to
/// `QTextDocument` via `QTextCursor`. Re-entry-guarded; uses
/// `beginEditBlock`/`endEditBlock` to coalesce repaints.
class StyleApplier : public QObject {
    Q_OBJECT
public:
    explicit StyleApplier(QObject *parent = nullptr);
    ~StyleApplier() override;

    void setTextDocument(QTextDocument *doc);
    QTextDocument *textDocument() const noexcept { return m_textDocument; }

    void setMarkoffDocument(Markoff::MarkoffDocument *doc);
    Markoff::MarkoffDocument *markoffDocument() const noexcept { return m_markoffDocument; }

    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

    void setFontScale(qreal s);
    qreal fontScale() const noexcept { return m_fontScale; }

    /// Counter incremented on every restyle pass; tests assert progress.
    quint64 restyleCount() const noexcept { return m_restyleCount; }

    /// Force a restyle without waiting for `d2DocumentChanged`.
    void rerender();

private Q_SLOTS:
    void onD2Changed();

private:
    void applyFormats();

    QPointer<QTextDocument>            m_textDocument;
    Markoff::MarkoffDocument          *m_markoffDocument = nullptr;
    const Markoff::Theme              *m_theme           = nullptr;
    qreal                              m_fontScale       = 1.0;
    bool                               m_applyingFormats = false;
    quint64                            m_restyleCount    = 0;
};

}  // namespace Markoff::Styled
