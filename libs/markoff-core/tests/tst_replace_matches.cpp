// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SearchEngine.h>

using namespace Markoff;

class TstReplaceMatches : public QObject {
    Q_OBJECT
    static QByteArray afterReplace(const char *md, const QString &needle,
                                   const QString &repl) {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(md);
        doc.replaceMatches(SearchEngine::findByBlock(doc, needle), repl);
        return doc.serializeForSave();
    }
private slots:
    void single_match() {
        QCOMPARE(afterReplace("Find me here\n", "me", "you"),
                 QByteArray("Find you here\n"));
    }
    void multiple_matches_one_block() {
        QCOMPARE(afterReplace("foo foo foo\n", "foo", "bar"),
                 QByteArray("bar bar bar\n"));
    }
    void matches_across_blocks() {
        QCOMPARE(afterReplace("alpha\n\nbeta alpha\n", "alpha", "X"),
                 QByteArray("X\n\nbeta X\n"));
    }
    void length_changing_replacement_keeps_offsets() {
        QCOMPARE(afterReplace("ab ab\n", "ab", "abcd"),
                 QByteArray("abcd abcd\n"));
    }
    void replace_all_is_one_undo() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("foo foo foo\n");
        doc.replaceMatches(SearchEngine::findByBlock(doc, "foo"), "bar");
        QCOMPARE(doc.serializeForSave(), QByteArray("bar bar bar\n"));
        doc.undoD2();
        QCOMPARE(doc.serializeForSave(), QByteArray("foo foo foo\n"));
    }
    void empty_list_is_noop() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown("nothing here\n");
        doc.replaceMatches({}, "x");
        QCOMPARE(doc.serializeForSave(), QByteArray("nothing here\n"));
    }
};

QTEST_MAIN(TstReplaceMatches)
#include "tst_replace_matches.moc"
