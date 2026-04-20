// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 1 smoke test for qutepart-corbomite.
//
// Proves the vendored widget compiles, instantiates, and round-trips plain
// text through the public API. This is the only test we ship in Phase 1;
// upstream's full test suite was deleted because it covers features we're
// about to trim (Kate-XML highlighting, multi-language indent, bundled
// themes). New tests are authored fresh as the library is shaped.

#include <QObject>
#include <QString>
#include <QTest>

#include <qutepart.h>

class QutepartSmokeTest : public QObject {
    Q_OBJECT

  private slots:
    void roundTripPlainText() {
        Qutepart::Qutepart widget;
        const QString input = QStringLiteral("one\ntwo\nthree\nfour");
        widget.setPlainText(input);
        QCOMPARE(widget.toPlainText(), input);
    }

    void cursorPositionApi() {
        Qutepart::Qutepart widget;
        widget.setPlainText(QStringLiteral("one\ntwo\nthree\nfour"));
        widget.goTo(2);
        QCOMPARE(widget.textCursorPosition(), Qutepart::TextCursorPosition(2, 0));
        widget.goTo({2, 1});
        QCOMPARE(widget.textCursorPosition(), Qutepart::TextCursorPosition(2, 1));
    }
};

QTEST_MAIN(QutepartSmokeTest)
#include "tst_qutepart_smoke.moc"
