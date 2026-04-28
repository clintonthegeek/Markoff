// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Kf6SyntaxHighlightService.h>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

namespace Markoff {

namespace {
CodeTokenKind mapStyle(KSyntaxHighlighting::Theme::TextStyle s)
{
    using TS = KSyntaxHighlighting::Theme::TextStyle;
    switch (s) {
    case TS::Keyword:        return CodeTokenKind::Keyword;
    case TS::ControlFlow:    return CodeTokenKind::ControlFlow;
    case TS::BuiltIn:        return CodeTokenKind::Builtin;
    case TS::DataType:       return CodeTokenKind::Type;
    case TS::Function:       return CodeTokenKind::Function;
    case TS::Variable:       return CodeTokenKind::Variable;
    case TS::Constant:       return CodeTokenKind::Constant;
    case TS::Operator:       return CodeTokenKind::Operator;
    case TS::String:         return CodeTokenKind::String;
    case TS::DecVal:
    case TS::BaseN:
    case TS::Float:          return CodeTokenKind::Number;
    case TS::Comment:        return CodeTokenKind::Comment;
    case TS::Documentation:  return CodeTokenKind::Documentation;
    case TS::Preprocessor:   return CodeTokenKind::Preprocessor;
    case TS::Annotation:     return CodeTokenKind::Annotation;
    default:                 return CodeTokenKind::Default;
    }
}

class CollectingHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
public:
    QList<CodeSpan> spans;
    quint32         lineByteBase = 0;

    KSyntaxHighlighting::State highlight(QStringView text,
                                         const KSyntaxHighlighting::State &state)
    {
        return highlightLine(text, state);
    }

protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &fmt) override
    {
        CodeSpan sp;
        sp.offset = lineByteBase + static_cast<quint32>(offset);
        sp.length = static_cast<quint32>(length);
        sp.kind   = mapStyle(fmt.textStyle());
        spans << sp;
    }
};
}

struct Kf6SyntaxHighlightService::Private {
    KSyntaxHighlighting::Repository repo;
};

Kf6SyntaxHighlightService::Kf6SyntaxHighlightService(QObject *parent)
    : SyntaxHighlightService(parent)
    , d(std::make_unique<Private>())
{
}
Kf6SyntaxHighlightService::~Kf6SyntaxHighlightService() = default;

QList<CodeSpan> Kf6SyntaxHighlightService::highlight(const QString &language,
                                                       const QByteArray &contentUtf8) const
{
    auto def = d->repo.definitionForName(language);
    if (!def.isValid()) return {};

    CollectingHighlighter h;
    h.setDefinition(def);
    h.setTheme(d->repo.defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    KSyntaxHighlighting::State state;
    const QString text = QString::fromUtf8(contentUtf8);
    quint32 byteBase = 0;
    for (const QString &line : text.split('\n')) {
        h.lineByteBase = byteBase;
        state = h.highlight(line, state);
        byteBase += static_cast<quint32>(line.toUtf8().size()) + 1;
    }
    return h.spans;
}

QStringList Kf6SyntaxHighlightService::availableLanguages() const
{
    QStringList out;
    for (const auto &def : d->repo.definitions()) out << def.name();
    return out;
}

bool Kf6SyntaxHighlightService::supportsLanguage(const QString &lang) const
{
    return d->repo.definitionForName(lang).isValid();
}

}  // namespace Markoff
