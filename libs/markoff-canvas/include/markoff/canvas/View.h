// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractScrollArea>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::Canvas {

/**
 * Projection view leaf: renders a MarkoffDocument directly, one
 * QTextLayout per block, with its own input pipeline.
 *
 * Authority model (spec §2): the document wins totally. This widget
 * holds no editable text state — every layout it builds is a derived
 * cache keyed by (BlockId, blockEditSequence), rebuilt from
 * blockText(), never patched in place.
 *
 * T0 scaffold: constructs, scrolls nothing, paints nothing. T1 adds
 * the block layout cache and paint path.
 */
class View : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit View(QWidget *parent = nullptr);
    ~View() override;

    /// Non-owning. Null is legal (an unbound view paints an empty page).
    void setDocument(MarkoffDocument *doc);
    MarkoffDocument *document() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    MarkoffDocument *m_doc = nullptr;
};

}  // namespace Markoff::Canvas
