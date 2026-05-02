// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKindRegistry.h>

using namespace Markoff::LiveRender;

class TstLiveRenderRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void builtins_registered() {
        BlockKindRegistry reg;
        QVERIFY(reg.find(BlockKind::Paragraph) != nullptr);
        QVERIFY(reg.find(BlockKind::Heading) != nullptr);
        QVERIFY(reg.find(BlockKind::CodeBlock) != nullptr);
        QVERIFY(reg.find(BlockKind::HorizontalRule) != nullptr);
        QVERIFY(reg.find(BlockKind::Image) != nullptr);
    }

    void unknown_kind_returns_nullptr() {
        BlockKindRegistry reg;
        QVERIFY(reg.find(QStringLiteral("nonexistent-kind")) == nullptr);
    }

    void paragraph_accepts_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::Paragraph);
        QVERIFY(d != nullptr);
        QVERIFY(d->acceptsTextRoleUpdates);
    }

    void hr_does_not_accept_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::HorizontalRule);
        QVERIFY(d != nullptr);
        QVERIFY(!d->acceptsTextRoleUpdates);
    }

    void image_does_not_accept_text_updates() {
        BlockKindRegistry reg;
        const auto *d = reg.find(BlockKind::Image);
        QVERIFY(d != nullptr);
        QVERIFY(!d->acceptsTextRoleUpdates);
    }

    void plugin_kind_registration() {
        BlockKindRegistry reg;
        BlockKindDescriptor custom;
        custom.id = QStringLiteral("my-custom-block");
        custom.acceptsTextRoleUpdates = false;
        reg.register_(custom);
        const auto *d = reg.find(QStringLiteral("my-custom-block"));
        QVERIFY(d != nullptr);
        QCOMPARE(d->id, QStringLiteral("my-custom-block"));
    }

    void kinds_list_contains_all_builtins() {
        BlockKindRegistry reg;
        const auto kinds = reg.kinds();
        QVERIFY(kinds.contains(BlockKind::Paragraph));
        QVERIFY(kinds.contains(BlockKind::Heading));
        QVERIFY(kinds.contains(BlockKind::CodeBlock));
        QVERIFY(kinds.contains(BlockKind::HorizontalRule));
        QVERIFY(kinds.contains(BlockKind::Image));
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderRegistry)
#include "tst_live_render_registry.moc"
