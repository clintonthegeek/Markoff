// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledDogfoodInvariants : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void hash_gate_skips_unchanged_blocks() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // 10 paragraph blocks separated by blank lines.
        doc.loadFromMarkdown(QByteArrayLiteral(
            "a\n\nb\n\nc\n\nd\n\ne\n\nf\n\ng\n\nh\n\ni\n\nj"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // After initial load, the styler has run at least once and
        // populated the hash for every block. Counter starts at 0.
        // Tickle the document with a single-character edit in block 0.
        const quint64 skipsBefore = e.styleApplierHashSkips();
        Q_UNUSED(skipsBefore);

        // Append "X" to the first block (block 0 byte range is [0,1)).
        doc.applyFlatEdit(1, 1, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);

        // Spin the event loop so the debounced d2DocumentChanged fires
        // and StyleApplier::applyFormats runs.
        QTRY_VERIFY(e.styleApplierHashSkips() > 0);
        // 9 of 10 blocks should be hash-skipped on this pass.
        QCOMPARE(e.styleApplierHashSkips(), quint64(9));
    }
};

QTEST_MAIN(TstStyledDogfoodInvariants)
#include "tst_styled_dogfood_invariants.moc"
