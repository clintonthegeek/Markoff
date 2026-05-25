// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

class MARKOFF_CORE_EXPORT CodeBlockProcessorRegistry : public QObject {
    Q_OBJECT
public:
    explicit CodeBlockProcessorRegistry(QObject *parent = nullptr);
    ~CodeBlockProcessorRegistry() override;

    void registerProcessor(std::shared_ptr<CodeBlockProcessor>);
    void unregisterProcessor(const QString &language);
    std::shared_ptr<CodeBlockProcessor> processorFor(const QString &language) const;
    QStringList registeredLanguages() const;

Q_SIGNALS:
    void processorRegistered(const QString &);
    void processorUnregistered(const QString &);

private:
    QHash<QString, std::shared_ptr<CodeBlockProcessor>> m_byLang;
};

}  // namespace Markoff
