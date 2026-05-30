// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/styled/DocumentRenderer.h>

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QPainter>
#include <QString>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

#include "FormatPass.h"

namespace Markoff::Styled {

DocumentRenderer::DocumentRenderer() = default;
DocumentRenderer::~DocumentRenderer() = default;

void DocumentRenderer::setTheme(const Markoff::Theme *theme) { m_theme = theme; }
void DocumentRenderer::setFontScale(qreal s) { m_fontScale = s; }

void DocumentRenderer::renderInto(QTextDocument *target,
                                  const Markoff::MarkoffDocument *source) const {
    if (!target || !source) return;
    // Deterministic base font so headless metrics are stable and the golden
    // compare against the widget path is meaningful. QFont() resolves to the
    // application default font — identical to what the widget path inherits.
    target->setDefaultFont(
        m_theme ? m_theme->font(Markoff::Theme::FontRole::Body) : QFont());
    // Seed text in the single-"\n" widget coordinate space FormatPass expects.
    target->setPlainText(QString::fromUtf8(source->widgetFlatView()));
    FormatPass::Options opts;
    opts.fontScale = m_fontScale;
    opts.theme     = m_theme;
    opts.inferKind = false;  // read-only: never suggest/issue Cmd::changeKind
    FormatPass::apply(target, source, opts, /*gate=*/nullptr);
}

void DocumentRenderer::renderInto(QTextDocument *target,
                                  const QByteArray &markdownUtf8) const {
    if (!target) return;
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(markdownUtf8);
    renderInto(target, &doc);
}

qreal DocumentRenderer::idealHeight(const Markoff::MarkoffDocument *source,
                                    qreal width) const {
    if (!source) return 0;
    QTextDocument doc;
    renderInto(&doc, source);
    doc.setTextWidth(width);
    return doc.size().height();
}

void DocumentRenderer::paint(QPainter *painter, const QRectF &rect,
                             const Markoff::MarkoffDocument *source) const {
    if (!painter || !source) return;
    QTextDocument doc;
    renderInto(&doc, source);
    doc.setTextWidth(rect.width());
    painter->save();
    painter->translate(rect.topLeft());
    QAbstractTextDocumentLayout::PaintContext ctx;
    ctx.clip = QRectF(0, 0, rect.width(), rect.height());
    doc.documentLayout()->draw(painter, ctx);
    painter->restore();
}

}  // namespace Markoff::Styled
