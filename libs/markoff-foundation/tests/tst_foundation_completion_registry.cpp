// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/CompletionContext.h>
#include <markoff-foundation/CompletionProvider.h>
#include <markoff-foundation/CompletionRegistry.h>

using namespace Markoff;

namespace {
class FakeProvider : public CompletionProvider {
public:
    QSet<CompletionTrigger> handledTriggers() const override
    { return { CompletionTrigger::Emoji }; }
    QList<CompletionCandidate> candidatesFor(const CompletionContext &c, quint64) override
    {
        QList<CompletionCandidate> out;
        if (c.trigger == CompletionTrigger::Emoji) {
            CompletionCandidate cc;
            cc.display = ":" + c.prefix + ":";
            cc.insertion = cc.display;
            out << cc;
        }
        return out;
    }
};
}

class TstFoundationCompletionRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void gather_returns_synchronous_candidates() {
        CompletionRegistry r;
        r.registerProvider(std::make_shared<FakeProvider>());

        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = "smile";
        const auto cands = r.gather(ctx, 1);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands.first().display, QStringLiteral(":smile:"));
    }

    void gather_filters_by_handled_triggers() {
        CompletionRegistry r;
        r.registerProvider(std::make_shared<FakeProvider>());

        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::Tag;
        ctx.prefix  = "x";
        QCOMPARE(r.gather(ctx, 1).size(), 0);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionRegistry)
#include "tst_foundation_completion_registry.moc"
