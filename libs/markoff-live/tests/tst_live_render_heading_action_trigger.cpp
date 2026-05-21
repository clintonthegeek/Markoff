// SPDX-License-Identifier: GPL-3.0-or-later
//
// Verifies that triggering the LiveActionController's heading{N}Action via
// QAction::trigger() (as a consumer's menu / shortcut would) actually
// invokes LiveFormatController::setHeadingLevel. Distinguishes the trigger-
// path bug from the direct-call path covered by
// tst_live_render_format_heading_level.

#include <QApplication>
#include <QTest>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveActionController.h>
#include <markoff/live/LiveListModelBinding.h>

class TestHeadingActionTrigger : public QObject {
    Q_OBJECT
private slots:
    void heading3Action_trigger_promotes_paragraph_to_h3() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Hello world\n");

        // Place cursor in the paragraph so setHeadingLevel has a target.
        binding.cursorState()->begin(0, 0);

        auto *lac = binding.actionController();
        QVERIFY2(lac, "actionController should be created under AllCapabilities");

        auto *act = lac->heading3Action();
        QVERIFY2(act, "heading3Action Q_PROPERTY returns a valid QAction");
        QVERIFY2(act->isEnabled(),
                 "heading3Action must be enabled when a doc is wired");

        act->trigger();
        QCoreApplication::processEvents();

        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);
        const auto attrs = doc.blockAttrs(id);
        const auto level = attrs.constFind(Markoff::AttrNames::Level);
        QVERIFY(level != attrs.cend());
        const int *p = std::get_if<int>(&level.value());
        QVERIFY(p);
        QCOMPARE(*p, 3);
    }

    void heading0Action_trigger_demotes_heading_to_paragraph() {
        Markoff::MarkoffDocument doc(quint16(42));
        Markoff::Live::LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("## Hello\n");

        binding.cursorState()->begin(0, 0);
        auto *lac = binding.actionController();
        QVERIFY(lac);
        lac->heading0Action()->trigger();
        QCoreApplication::processEvents();

        const Markoff::BlockId id = doc.iterateBlocks()[0];
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Paragraph);
        QCOMPARE(doc.blockText(id), QByteArray("Hello"));
    }
};

// Need QApplication (not QCoreApplication) because LiveActionController
// only creates the QActions under QGuiApplication.
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TestHeadingActionTrigger tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_live_render_heading_action_trigger.moc"
