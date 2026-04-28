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
};

QTEST_APPLESS_MAIN(TstFoundationSyntaxHighlightService)
#include "tst_foundation_syntax_highlight_service.moc"
