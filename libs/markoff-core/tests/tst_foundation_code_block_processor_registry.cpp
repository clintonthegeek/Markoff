// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/Theme.h>

using namespace Markoff;

namespace {
class FakeMermaidProcessor : public CodeBlockProcessor {
public:
    QString language() const override { return "mermaid"; }
    RenderedBlock render(const QByteArray &, const Theme &) override
    { RenderedBlock r; r.kind = RenderedBlock::Kind::Empty; return r; }
};
}

class TstFoundationCodeBlockRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void register_then_lookup() {
        CodeBlockProcessorRegistry r;
        QSignalSpy spy(&r, &CodeBlockProcessorRegistry::processorRegistered);
        r.registerProcessor(std::make_shared<FakeMermaidProcessor>());
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.processorFor("mermaid") != nullptr);
        QVERIFY(r.processorFor("plantuml") == nullptr);
    }

    void unregister_removes_processor() {
        CodeBlockProcessorRegistry r;
        r.registerProcessor(std::make_shared<FakeMermaidProcessor>());
        QSignalSpy spy(&r, &CodeBlockProcessorRegistry::processorUnregistered);
        r.unregisterProcessor("mermaid");
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.processorFor("mermaid") == nullptr);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCodeBlockRegistry)
#include "tst_foundation_code_block_processor_registry.moc"
