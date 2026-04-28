// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/Kf6SyntaxHighlightService.h>

using namespace Markoff;

class TstFoundationSyntaxHighlightService : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void available_languages_includes_cpp() {
        Kf6SyntaxHighlightService s;
        const QStringList langs = s.availableLanguages();
        bool found = false;
        for (const QString &l : langs)
            if (l.compare("c++", Qt::CaseInsensitive) == 0
                || l.compare("cpp", Qt::CaseInsensitive) == 0)
            { found = true; break; }
        QVERIFY(found);
    }

    void supports_language_returns_true_for_known() {
        Kf6SyntaxHighlightService s;
        QVERIFY(s.supportsLanguage("c++") || s.supportsLanguage("cpp"));
    }

    void highlight_yields_some_spans() {
        Kf6SyntaxHighlightService s;
        const QString lang = s.supportsLanguage("c++") ? "c++" : "cpp";
        const auto spans = s.highlight(lang,
            QByteArray("int main() { return 0; }"));
        QVERIFY(!spans.isEmpty());
    }

    void highlight_offsets_are_utf8_bytes_for_non_ascii() {
        Kf6SyntaxHighlightService s;
        const QString lang = s.supportsLanguage("c++") ? "c++" : "cpp";
        // "// é\n" = 2+1+2+1 = 6 UTF-8 bytes; second line starts at byte offset 6.
        // "int" on the second line must produce a span with offset >= 6.
        const QByteArray src = QString::fromUtf8("// é\nint main() { return 0; }").toUtf8();
        const auto spans = s.highlight(lang, src);
        QVERIFY(!spans.isEmpty());
        bool found_second_line = false;
        for (const auto &sp : spans)
            if (sp.offset >= 6) { found_second_line = true; break; }
        QVERIFY(found_second_line);
    }
};

QTEST_APPLESS_MAIN(TstFoundationSyntaxHighlightService)
#include "tst_foundation_syntax_highlight_service.moc"
