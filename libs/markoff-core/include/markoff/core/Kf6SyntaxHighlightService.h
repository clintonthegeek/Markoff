// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <markoff/core/SyntaxHighlightService.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT Kf6SyntaxHighlightService
    : public SyntaxHighlightService
{
    Q_OBJECT
public:
    explicit Kf6SyntaxHighlightService(QObject *parent = nullptr);
    ~Kf6SyntaxHighlightService() override;

    QList<CodeSpan> highlight(const QString &language,
                                const QByteArray &contentUtf8) const override;
    QStringList     availableLanguages() const override;
    bool            supportsLanguage(const QString &lang) const override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
