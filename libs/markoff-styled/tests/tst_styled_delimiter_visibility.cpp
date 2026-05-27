// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledDelimiterVisibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void delimiters_render_visible_in_v0() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("**bold** text"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // v0: delimiters are visible. We just check the document text
        // still contains them — promoting to cursor-aware hide is v0.1.
        QCOMPARE(e.textEdit()->toPlainText(),
                 QStringLiteral("**bold** text"));
    }
};

QTEST_MAIN(TstStyledDelimiterVisibility)
#include "tst_styled_delimiter_visibility.moc"
