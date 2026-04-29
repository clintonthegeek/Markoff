// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include <markoff-foundation/CompletionTrigger.h>
#include <markoff-foundation/EmojiCompletionProvider.h>
#include <markoff/view/qml/CompletionPopupModel.h>

using namespace Markoff::View::Qml;

class TstViewQmlCompletionPopup : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void default_constructed_is_empty() {
        CompletionPopupModel m;
        QCOMPARE(m.rowCount(), 0);
    }

    void emoji_provider_returns_smile_for_smi_prefix() {
        CompletionPopupModel m;
        m.registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());

        QSignalSpy spy(&m, &CompletionPopupModel::countChanged);
        m.requestCompletions(int(Markoff::CompletionTrigger::Emoji),
                             QStringLiteral("smi"));

        QVERIFY(spy.count() >= 1);
        QVERIFY(m.rowCount() > 0);

        // Find at least one candidate whose display contains "smile" (case-insensitive).
        bool foundSmile = false;
        for (int i = 0; i < m.rowCount(); ++i) {
            const QVariant v = m.data(m.index(i, 0), CompletionPopupModel::DisplayRole);
            if (v.toString().contains("smile", Qt::CaseInsensitive)) {
                foundSmile = true; break;
            }
        }
        QVERIFY(foundSmile);
    }

    void clear_resets_model() {
        CompletionPopupModel m;
        m.registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());
        m.requestCompletions(int(Markoff::CompletionTrigger::Emoji), QStringLiteral("smi"));
        QVERIFY(m.rowCount() > 0);

        m.clear();
        QCOMPARE(m.rowCount(), 0);
    }

    void role_names_expose_qml_friendly_keys() {
        CompletionPopupModel m;
        const QHash<int, QByteArray> names = m.roleNames();
        QVERIFY(names.values().contains("display"));
        QVERIFY(names.values().contains("insertion"));
        QVERIFY(names.values().contains("detail"));
    }

    void wrong_trigger_returns_no_candidates() {
        CompletionPopupModel m;
        m.registerProvider(std::make_shared<Markoff::EmojiCompletionProvider>());
        // EmojiCompletionProvider only handles Emoji trigger; Tag should yield nothing.
        m.requestCompletions(int(Markoff::CompletionTrigger::Tag), QStringLiteral("foo"));
        QCOMPARE(m.rowCount(), 0);
    }
};

QTEST_MAIN(TstViewQmlCompletionPopup)
#include "tst_view_qml_completion_popup.moc"
