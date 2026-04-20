// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QHash>
#include <QSharedPointer>
#include <QTextLayout>
#include <QTextStream>

#include "context_stack.h"
#include "context_switcher.h"
#include "style.h"

namespace Qutepart {

class Context;
typedef QSharedPointer<Context> ContextPtr;

class AbstractRule;
typedef QSharedPointer<AbstractRule> RulePtr;

class Language;
class TextToMatch;
class MatchResult;
class Theme;
class TextBlockUserData;

class Context {
  public:
    Context(const QString &name, const QString &attribute, const ContextSwitcher &lineEndContext,
            const ContextSwitcher &lineBeginContext, const ContextSwitcher &lineEmptyContext,
            const ContextSwitcher &fallthroughContext, bool dynamic, const QList<RulePtr> &rules);

    void printDescription(QTextStream &out) const;

    QString name() const;

    void setTheme(const Theme *theme);
    void setLanguage(QSharedPointer<Language> language);
    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error);
    void setKeywordParams(const QHash<QString, QStringList> &lists, const QString &deliminators,
                          bool caseSensitive, QString &error);
    void setStyles(const QHash<QString, Style> &styles, QString &error);

    inline bool dynamic() const { return _dynamic; }
    inline ContextSwitcher lineBeginContext() const { return _lineBeginContext; }
    inline ContextSwitcher lineEndContext() const { return _lineEndContext; }

    const ContextStack parseBlock(const ContextStack &contextStack, TextToMatch &textToMatch,
                                  QVector<QTextLayout::FormatRange> &formats, QString &textTypeMap,
                                  QVector<QSharedPointer<Language>> &languageMap, bool &lineContinue,
                                  TextBlockUserData *data) const;

    // Try to match textToMatch with nested rules
    MatchResult *tryMatch(const TextToMatch &textToMatch) const;

    QSharedPointer<Language> language;

  protected:
    void applyMatchResult(const TextToMatch &textToMatch, const MatchResult *matchRes,
                          const Context *context, QVector<QTextLayout::FormatRange> &formats,
                          QString &textTypeMap,
                          QVector<QSharedPointer<Language>> &languageMap) const;

    QString _name;
    QString attribute;
    ContextSwitcher _lineEndContext;
    ContextSwitcher _lineBeginContext;
    ContextSwitcher _lineEmptyContext;
    ContextSwitcher fallthroughContext;
    bool _dynamic;
    QList<RulePtr> rules;
    Style style;
};

} // namespace Qutepart
