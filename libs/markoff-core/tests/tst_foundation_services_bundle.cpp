// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff-foundation/CodeBlockProcessorRegistry.h>
#include <markoff-foundation/CompletionRegistry.h>
#include <markoff-foundation/DefaultLinkService.h>
#include <markoff-foundation/Kf6SyntaxHighlightService.h>
#include <markoff-foundation/MarkoffServices.h>

using namespace Markoff;

class TstFoundationServicesBundle : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bundle_holds_non_owning_pointers() {
        Kf6SyntaxHighlightService syntax;
        CodeBlockProcessorRegistry procs;
        DefaultLinkService links;
        CompletionRegistry completion;

        MarkoffServices s;
        s.syntax = &syntax;
        s.codeProcessors = &procs;
        s.links = &links;
        s.completion = &completion;

        QVERIFY(s.syntax != nullptr);
        QVERIFY(s.codeProcessors != nullptr);
        QVERIFY(s.links != nullptr);
        QVERIFY(s.completion != nullptr);
    }
};

QTEST_APPLESS_MAIN(TstFoundationServicesBundle)
#include "tst_foundation_services_bundle.moc"
