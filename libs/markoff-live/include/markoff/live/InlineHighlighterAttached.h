// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/Theme.h>
#include <markoff/parser/SourceSpan.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QQuickTextDocument>
#include <qqmlintegration.h>

namespace Markoff::Live {

class InlineHighlighter;

/// QML-property wrapper around InlineHighlighter. Wire in a delegate's TextEdit:
///
///   TextEdit {
///       id: edit
///       InlineHighlighterAttached {
///           target: edit
///           spans: model.inlineSpans
///           theme: root.liveBinding ? root.liveBinding.theme : null
///       }
///   }
class MARKOFF_LIVE_EXPORT InlineHighlighterAttached : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantList spans          READ spans  WRITE setSpans  NOTIFY spansChanged)
    Q_PROPERTY(const Markoff::Theme *theme READ theme  WRITE setTheme  NOTIFY themeChanged)
public:
    explicit InlineHighlighterAttached(QObject *parent = nullptr);
    ~InlineHighlighterAttached() override;

    QQuickTextDocument *target() const noexcept { return m_target; }
    void setTarget(QQuickTextDocument *target);

    QVariantList spans() const;
    void setSpans(const QVariantList &v);

    const Markoff::Theme *theme() const noexcept { return m_theme; }
    void setTheme(const Markoff::Theme *theme);

Q_SIGNALS:
    void targetChanged();
    void spansChanged();
    void themeChanged();

private:
    void rebuildHighlighter();

    QPointer<QQuickTextDocument> m_target;
    InlineHighlighter           *m_highlighter = nullptr;
    QList<Markoff::SourceSpan>   m_spans;
    const Markoff::Theme        *m_theme = nullptr;
};

}  // namespace Markoff::Live
