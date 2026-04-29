// SPDX-License-Identifier: GPL-3.0-or-later
#include <QByteArray>
#include <QRandomGenerator>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

class TstFoundationMarkoffDocumentProperty : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void random_edit_sequences_match_reference() {
        const int kSequences = 100;
        const int kEditsPerSeq = 30;
        for (int seed = 0; seed < kSequences; ++seed) {
            QRandomGenerator rng(static_cast<quint32>(seed));
            MarkoffDocument doc(1);
            QByteArray ref;

            for (int step = 0; step < kEditsPerSeq; ++step) {
                const quint32 len = static_cast<quint32>(ref.size());
                const quint32 a = rng.bounded(len + 1);
                const quint32 b = rng.bounded(len + 1);
                const quint32 lo = std::min(a, b);
                const quint32 hi = std::max(a, b);

                QByteArray ins;
                if (rng.bounded(3) != 0) {
                    const int n = rng.bounded(5);
                    for (int k = 0; k < n; ++k)
                        ins.append(static_cast<char>('a' + rng.bounded(26)));
                }

                MarkoffEdit e;
                e.oldStart = lo; e.oldEnd = hi; e.newText = ins;
                doc.applyLocalEdit({ e });

                ref.replace(static_cast<int>(lo),
                            static_cast<int>(hi - lo), ins);

                QCOMPARE(doc.toMarkdownUtf8(), ref);
            }
        }
    }
};

QTEST_APPLESS_MAIN(TstFoundationMarkoffDocumentProperty)
#include "tst_foundation_markoff_document_property.moc"
