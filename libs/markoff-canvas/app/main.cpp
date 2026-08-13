// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manual harness for the canvas spike. Not a product app — it exists so
// a human can look at the leaf without a test runner in the way.
//
//   QT_QPA_PLATFORM=offscreen build-dev/bin/markoff-canvas-app   # smoke
//   build-dev/bin/markoff-canvas-app                             # look at it

#include <QApplication>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# Canvas spike\n"
        "\n"
        "A projection view over MarkoffDocument.\n"
        "\n"
        "- one QTextLayout per block\n"
        "- no second document model\n"
        "\n"
        "## Kinds\n"
        "\n"
        "> A block quote, rendered with a bar and its own colour.\n"
        "\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n"
        "\n"
        "---\n"
        "\n"
        "A closing paragraph long enough to wrap across more than one line "
        "so that word wrapping is visible in the grab as well.\n");

    Markoff::Canvas::View view;
    view.setDocument(&doc);
    view.setWindowTitle(QStringLiteral("markoff-canvas (spike)"));
    view.resize(800, 600);
    view.show();

    // Offscreen smoke run: construct, drain the pending paint, exit — so
    // headless checks don't hang on an event loop nobody will quit.
    // Deliberately NOT a zero-timer quit: C2 forbids singleShot(0) in
    // this leaf, and the gate does not make exceptions for demo code.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen")) {
        QCoreApplication::processEvents();
        // MARKOFF_CANVAS_GRAB=<path.png> renders the widget to a file, so
        // the leaf can be eyeballed without a visible session
        // (scripts/run-tests.sh --direct needs per-task permission).
        const QString grabPath = qEnvironmentVariable("MARKOFF_CANVAS_GRAB");
        if (!grabPath.isEmpty() && !view.grab().save(grabPath)) {
            qWarning("failed to write grab to %s", qUtf8Printable(grabPath));
            return 1;
        }
        return 0;
    }

    return app.exec();
}
