// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include "support/RecordingLinkService.h"

#include <memory>
#include <stdexcept>

namespace {
QPoint pointForChar(QTextEdit *edit, int charPos) {
    QTextCursor c = edit->textCursor();
    c.setPosition(charPos);
    edit->setTextCursor(c);
    const QRect r = edit->cursorRect();
    return r.center() + QPoint(3, 0);
}

struct CandidateDocument {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString& key, const QByteArray& markdown)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {key, 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(markdown).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("document import failed");
    return fixture;
}
}  // namespace

class TstStyledLinkInteraction : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void click_inside_link_calls_activate() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-click"),
            QByteArrayLiteral("a [text](http://x.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // 'e' of 'text' is at char 4: "a [t[e]xt](...)" — index 4.
        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTRY_COMPARE(svc.activates.size(), 1);
    }

    void click_outside_link_does_not_activate() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-outside"),
            QByteArrayLiteral("just plain text"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 3);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTest::qWait(50);
        QCOMPARE(svc.activates.size(), 0);
    }

    void editor_provides_default_link_service_when_none_set() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-default"),
            QByteArrayLiteral("a [text](http://x.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // Do NOT call setLinkService — exercise the lazy default path.
        QVERIFY(e.linkService() != nullptr);

        // A click on a link should not crash (the default service is a no-op
        // for activate, just emits the signal).
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);
        QTest::qWait(50);
        // No assertion on the service's state — DefaultLinkService doesn't
        // expose recording surface. The test verifies no crash + non-null.
    }

    void hover_inside_link_calls_notify_hover() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover"),
            QByteArrayLiteral("a [text](http://x.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseMove(e.textEdit()->viewport(), p);

        QTRY_COMPARE(svc.hovers.size(), 1);
    }

    void hover_idempotent_within_same_link() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover-idempotent"),
            QByteArrayLiteral("a [text](http://x.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Ensure the mouse starts from a neutral position so the first move
        // is a real position change regardless of prior-test mouse state.
        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);
        // Move 1 char within the same link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 5));
        QTest::qWait(50);
        QCOMPARE(svc.hovers.size(), 1);
    }

    void hover_off_link_then_on_emits_left_and_new_hover() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover-transition"),
            QByteArrayLiteral("a [first](http://1.test) b [second](http://2.test) c"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(800, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        // Move into plain text between links.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 26));
        QTRY_COMPARE(svc.hoverLefts.size(), 1);

        // Move into the second link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 31));
        QTRY_COMPARE(svc.hovers.size(), 2);
    }

    void leave_event_clears_hover() {
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover-leave"),
            QByteArrayLiteral("a [text](http://x.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(e.textEdit()->viewport(), &leaveEvent);

        QTRY_COMPARE(svc.hoverLefts.size(), 1);
    }

    void hover_resolves_the_new_target_after_the_block_is_edited() {
        // The per-block hover cache is keyed on the block's edit sequence: a
        // text change to the hovered block must re-resolve its spans, not
        // serve the pre-edit link (ASK-M17 remaining half).
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover-stale"),
            QByteArrayLiteral("a [one](http://1.test) b"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);
        QCOMPARE(svc.hovers.back().activation.rawText, QStringLiteral("http://1.test"));

        // Change the URL in place (same length, so char positions hold):
        // 'a [one](http://[1].test) b' — the '1' is byte 15 of the block.
        const auto block = doc.iterateBlocks().front();
        QCOMPARE(doc.applyBlockEdit({block, 15, 1, QByteArrayLiteral("2")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QTest::qWait(50);

        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 2);
        QCOMPARE(svc.hovers.back().activation.rawText, QStringLiteral("http://2.test"));
    }

    void hover_survives_an_edit_in_another_block() {
        // An edit elsewhere must not invalidate the hovered block's cache —
        // resolution stays correct across the unrelated change.
        Markoff::Styled::Editor e;
        auto fixture = openCandidate(QStringLiteral("link-hover-other-block"),
            QByteArrayLiteral("a [one](http://1.test) b\n\nsecond block"));
        auto& doc = *fixture.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        QCOMPARE(doc.applyBlockEdit({blocks[1], 0, 0, QByteArrayLiteral("X")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QTest::qWait(50);

        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 2);
        QCOMPARE(svc.hovers.back().activation.rawText, QStringLiteral("http://1.test"));
    }
};

QTEST_MAIN(TstStyledLinkInteraction)
#include "tst_styled_link_interaction.moc"
