// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression test for the E2.6 dogfood-surfaced dark-toggle bug.
//
// The bug: `LiveListModelBinding::setTheme` updated an internal Theme value
// in place, so `theme()` always returned the same pointer. QML's property
// binding system compares old-vs-new values when re-evaluating bindings and
// skips downstream writes when the value is unchanged, so the QML expression
//   `InlineHighlighterAttached.theme: liveBinding.theme`
// re-evaluated to the same pointer on `themeChanged`, no setter was called,
// and `InlineHighlighter::rehighlight()` never ran. Visible symptom:
// Ctrl+Shift+D fired the action chain but nothing on screen changed.
//
// The fix: setTheme alternates between two internal Theme buffers so the
// pointer always changes across calls. These slots pin that invariant.

#include <QTest>
#include <QGuiApplication>
#include <QSignalSpy>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>
#include <markoff/live/LiveListModelBinding.h>

using namespace Markoff;
using namespace Markoff::Live;

class TstThemeTogglePropagation : public QObject {
    Q_OBJECT
private slots:
    void theme_pointer_changes_on_every_set_theme_call() {
        LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
        const Theme *p0 = b.theme();
        QVERIFY(p0 != nullptr);

        Theme dark = Theme::defaultDark();
        b.setTheme(&dark);
        const Theme *p1 = b.theme();
        QVERIFY(p1 != nullptr);
        QVERIFY2(p1 != p0,
                 "theme pointer must change so QML's value-equality "
                 "optimization re-propagates downstream");

        Theme light = Theme::defaultLight();
        b.setTheme(&light);
        const Theme *p2 = b.theme();
        QVERIFY2(p2 != p1,
                 "theme pointer must change again on the round trip back to light");
    }

    void apply_default_theme_emits_themeChanged_and_swaps_pointer() {
        LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
        QSignalSpy spy(&b, &LiveListModelBinding::themeChanged);

        const Theme *pBefore = b.theme();
        b.applyDefaultTheme(/*dark=*/true);
        QCOMPARE(spy.count(), 1);
        const Theme *pAfter = b.theme();
        QVERIFY(pAfter != pBefore);

        // Colours are actually different — confirms the swap carried payload.
        const QColor bgLight = Theme::defaultLight()
            .color(Theme::Slot::EditorBackground);
        const QColor bgDark  = Theme::defaultDark()
            .color(Theme::Slot::EditorBackground);
        QVERIFY(bgLight != bgDark);
        QCOMPARE(pAfter->color(Theme::Slot::EditorBackground), bgDark);

        b.applyDefaultTheme(/*dark=*/false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(b.theme()->color(Theme::Slot::EditorBackground), bgLight);
    }

    void invokable_proxies_reflect_active_theme_after_toggle() {
        // `themePixelSizeFor` / `themeFamilyFor` / `themeIsBold` /
        // `themeIsItalic` must read from the active buffer, not the stale
        // one. Belt-and-suspenders for the swap implementation.
        LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
        b.applyDefaultTheme(/*dark=*/true);
        const Theme *active = b.theme();
        QCOMPARE(b.themePixelSizeFor(0 /*TextDefault*/),
                 active->pixelSizeFor(Theme::Slot::TextDefault));

        b.applyDefaultTheme(/*dark=*/false);
        const Theme *activeLight = b.theme();
        QVERIFY(activeLight != active);
        QCOMPARE(b.themePixelSizeFor(0),
                 activeLight->pixelSizeFor(Theme::Slot::TextDefault));
    }

    void themeColorFor_reflects_active_buffer() {
        LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
        const int kEditorBackground =
            static_cast<int>(Theme::Slot::EditorBackground);

        // Initial buffer = defaultLight.
        QCOMPARE(b.themeColorFor(kEditorBackground),
                 Theme::defaultLight().color(Theme::Slot::EditorBackground));

        b.applyDefaultTheme(/*dark=*/true);
        QCOMPARE(b.themeColorFor(kEditorBackground),
                 Theme::defaultDark().color(Theme::Slot::EditorBackground));

        b.applyDefaultTheme(/*dark=*/false);
        QCOMPARE(b.themeColorFor(kEditorBackground),
                 Theme::defaultLight().color(Theme::Slot::EditorBackground));
    }

    void themeColorFor_invalid_slot_returns_invalid_qcolor() {
        LiveListModelBinding b(LiveListModelBinding::AllCapabilities);
        QVERIFY(!b.themeColorFor(99999).isValid());
    }
};

QTEST_MAIN(TstThemeTogglePropagation)
#include "tst_live_render_theme_toggle_propagation.moc"
