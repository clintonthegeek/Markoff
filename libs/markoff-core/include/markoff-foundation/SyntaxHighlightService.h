// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <markoff-foundation/CodeSpan.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT SyntaxHighlightService : public QObject {
    Q_OBJECT
public:
    explicit SyntaxHighlightService(QObject *parent = nullptr);
    ~SyntaxHighlightService() override;

    virtual QList<CodeSpan> highlight(const QString &language,
                                      const QByteArray &contentUtf8) const = 0;
    virtual QStringList     availableLanguages() const = 0;
    virtual bool            supportsLanguage(const QString &lang) const = 0;
};

}  // namespace Markoff
