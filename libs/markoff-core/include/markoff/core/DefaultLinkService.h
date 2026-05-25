// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/LinkService.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT DefaultLinkService : public LinkService {
    Q_OBJECT
public:
    explicit DefaultLinkService(QObject *parent = nullptr);
    LinkKind classify(const QString &linkText) const override;
    QUrl resolve(const QString &linkText, const QString &fromContext = {}) const override;
};

}  // namespace Markoff
