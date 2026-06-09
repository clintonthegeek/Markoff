// SPDX-License-Identifier: GPL-3.0-or-later
// Contract tests for the MarkdownView base: default behaviors every
// leaf inherits. Spec: docs/specs/2026-06-09-markdownview-contract-v2-design.md §3.
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/EditorContext.h>
#include <markoff/core/FindController.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using namespace Markoff;

class TstMarkdownViewBase : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_routes_to_undoD2_and_respects_readOnly() {
        MarkdownView v;
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        v.setDocument(&doc);

        doc.applyFlatEdit(5, 5, QByteArray(" world"), Origin::UserEdit);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));

        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
        v.redo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));

        v.setReadOnly(true);
        v.undo();   // must NOT mutate while read-only
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world\n"));
        v.setReadOnly(false);
        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
    }

    void theme_and_fontScale_store_and_signal() {
        MarkdownView v;
        QSignalSpy themeSpy(&v, &MarkdownView::themeChanged);
        QSignalSpy scaleSpy(&v, &MarkdownView::fontScaleChanged);

        Theme t = Theme::defaultDark();
        v.setTheme(t);
        QCOMPARE(themeSpy.count(), 1);

        QCOMPARE(v.fontScale(), 1.0);
        v.setFontScale(1.5);
        QCOMPARE(v.fontScale(), 1.5);
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(1.5);             // no-op → no second signal
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(99.0);            // clamps to 4.0
        QCOMPARE(v.fontScale(), 4.0);
        v.setFontScale(0.0);             // clamps to 0.25
        QCOMPARE(v.fontScale(), 0.25);
    }

    void find_and_format_defaults_are_safe_noops() {
        MarkdownView v;
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("hello");
        v.setDocument(&doc);
        FindController fc(&doc);
        v.attachFindController(&fc);   // qWarning + no-op; must not crash
        v.detachFindController();
        v.toggleBold();
        v.toggleItalic();
        v.toggleStrikethrough();
        v.toggleInlineCode();
        v.insertLink();
        v.setHeadingLevel(2);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello\n"));
    }
};

QTEST_MAIN(TstMarkdownViewBase)
#include "tst_markdown_view_base.moc"
