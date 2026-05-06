// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockKindDescriptor.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockSerializerRegistry.h>

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
        QVERIFY(reg.find(BlockKind::ListItem) != nullptr);
        QVERIFY(reg.find(BlockKind::Blockquote) != nullptr);
        QVERIFY(reg.find(BlockKind::Math) != nullptr);
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
        QVERIFY(kinds.contains(BlockKind::ListItem));
        QVERIFY(kinds.contains(BlockKind::Blockquote));
        QVERIFY(kinds.contains(BlockKind::Math));
    }

    void paragraph_descriptor_consumes_structural_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::Paragraph);
        QVERIFY(d);
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void heading_descriptor_consumes_structural_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::Heading);
        QVERIFY(d);
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void code_block_descriptor_consumes_only_edge_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::CodeBlock);
        QVERIFY(d);
        QVERIFY(!d->consumedStructuralKeys.contains(Qt::Key_Return));
        QVERIFY(!d->consumedStructuralKeys.contains(Qt::Key_Enter));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
    }

    void image_descriptor_has_no_structural_keys() {
        BlockKindRegistry r;
        QVERIFY(r.find(BlockKind::Image)->consumedStructuralKeys.isEmpty());
    }

    void hr_descriptor_consumes_structural_keys() {
        BlockKindRegistry r;
        const auto *d = r.find(BlockKind::HorizontalRule);
        QVERIFY(d);
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Delete));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Backspace));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Up));
        QVERIFY(d->consumedStructuralKeys.contains(Qt::Key_Down));
    }

    void blockKindRegistry_implements_serializer_interface() {
        BlockKindRegistry reg;
        const Markoff::BlockSerializerRegistry *iface = &reg;
        QVERIFY(iface != nullptr);
    }

    void serialize_paragraph_returns_content() {
        BlockKindRegistry reg;
        QByteArray result = reg.serialize(Markoff::BlockKind::Paragraph, {}, "hello world");
        QCOMPARE(result, QByteArray("hello world"));
    }

    void serialize_list_item_returns_content() {
        BlockKindRegistry reg;
        QByteArray result = reg.serialize(Markoff::BlockKind::ListItem, {}, "- item");
        QCOMPARE(result, QByteArray("- item"));
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderRegistry)
#include "tst_live_render_registry.moc"
