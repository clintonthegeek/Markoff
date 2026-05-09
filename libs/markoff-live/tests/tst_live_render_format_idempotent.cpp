// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QCoreApplication>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveFormatController.h>

class TestFormatIdempotent : public QObject {
    Q_OBJECT
private slots:
    void bold_twice_restores_original() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello world\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        // First bold: select "world" at qtPos 6–11 → produces "hello **world**\n".
        binding.selectionView()->begin(0, 6);
        binding.selectionView()->extend(0, 11);
        fc.toggleBold();

        const QByteArray afterFirst = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(afterFirst.contains("**world**"),
                 ("after first bold: " + afterFirst).constData());

        // Pump the event loop so the model's text records catch up with the edit
        // (applyFlatEdit schedules d2DocumentChanged via QTimer::singleShot(0)).
        QCoreApplication::processEvents();

        // Model text is now "hello **world**" (no '\n').
        // "world" is at qtPos 8–13 (shifted by 2 for leading "**").
        // Selecting qtPos 8..13 and calling toggleBold should UNWRAP.
        binding.selectionView()->begin(0, 8);
        binding.selectionView()->extend(0, 13);
        fc.toggleBold();

        const QByteArray result = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(!result.contains("**"),
                 ("after second bold (unwrap), expected no **: " + result).constData());
        QVERIFY2(result.contains("world"),
                 ("after unwrap, expected 'world' in: " + result).constData());
        QVERIFY(result.contains("hello "));
    }

    void italic_twice_restores_original() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("foo\n");

        Markoff::Live::LiveFormatController fc;
        fc.setDocument(&doc);
        fc.setSelectionView(binding.selectionView());
        fc.setModel(binding.model());

        // First italic: select "foo" at qtPos 0–3 → produces "_foo_\n".
        binding.selectionView()->begin(0, 0);
        binding.selectionView()->extend(0, 3);
        fc.toggleItalic();

        const QByteArray afterFirst = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(afterFirst.contains("_foo_"),
                 ("after first italic: " + afterFirst).constData());

        // Pump the event loop so the model's text records catch up with the edit.
        QCoreApplication::processEvents();

        // Model text is now "_foo_"; "foo" is at qtPos 1–4 (shifted by 1).
        binding.selectionView()->begin(0, 1);
        binding.selectionView()->extend(0, 4);
        fc.toggleItalic();

        const QByteArray result = doc.blockText(doc.iterateBlocks()[0]);
        QVERIFY2(!result.contains("_"),
                 ("after second italic (unwrap), expected no _: " + result).constData());
        QVERIFY2(result.contains("foo"),
                 ("after unwrap, expected 'foo' in: " + result).constData());
    }
};

QTEST_MAIN(TestFormatIdempotent)
#include "tst_live_render_format_idempotent.moc"
