// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/CompletionCandidate.h>
#include <markoff-foundation/CompletionContext.h>
#include <markoff-foundation/CompletionTrigger.h>

using namespace Markoff;

class TstFoundationCompletionTypes : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void context_default_is_inactive() {
        CompletionContext ctx;
        QVERIFY(!ctx.isActive());
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void context_active_when_trigger_set() {
        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::WikiLink;
        QVERIFY(ctx.isActive());
    }

    void candidate_carries_fields() {
        CompletionCandidate c;
        c.display = ":smile:";
        c.insertion = ":smile:";
        c.detail = "smiling face";
        QCOMPARE(c.priority, 0);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionTypes)
#include "tst_foundation_completion_types.moc"
