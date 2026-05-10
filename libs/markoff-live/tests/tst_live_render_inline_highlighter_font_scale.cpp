// SPDX-License-Identifier: GPL-3.0-or-later
// Stub — real tests land in Phase 3A when InlineHighlighter gains setFontScale.
#include <QTest>
class TstInlineHighlighterFontScaleStub : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void placeholder() { QVERIFY(true); }
};
QTEST_GUILESS_MAIN(TstInlineHighlighterFontScaleStub)
#include "tst_live_render_inline_highlighter_font_scale.moc"
