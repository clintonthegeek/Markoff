// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/EmbedDepthGuard.h>
#include <markoff/core/EmbedRegistry.h>
#include <markoff/core/MarkdownRenderChild.h>

using namespace Markoff;

namespace {
std::unique_ptr<MarkdownRenderChild> makeChild(const QString &text)
{
    auto child = std::make_unique<MarkdownRenderChild>();
    child->setRenderedText(text);
    return child;
}
}

class TstFoundationEmbedRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void hasExtension_false_before_register_true_after() {
        EmbedRegistry reg;
        QVERIFY(!reg.hasExtension("png"));
        reg.registerExtension("png", [](const EmbedRequest &) {
            return makeChild("png-embed");
        });
        QVERIFY(reg.hasExtension("png"));
    }

    void hasExtension_is_case_insensitive() {
        EmbedRegistry reg;
        reg.registerExtension("PDF", [](const EmbedRequest &) {
            return makeChild("pdf-embed");
        });
        QVERIFY(reg.hasExtension("pdf"));
        QVERIFY(reg.hasExtension("Pdf"));
    }

    void unregisterExtension_removes_factory() {
        EmbedRegistry reg;
        reg.registerExtension("png", [](const EmbedRequest &) {
            return makeChild("png-embed");
        });
        QVERIFY(reg.hasExtension("png"));
        reg.unregisterExtension("png");
        QVERIFY(!reg.hasExtension("png"));
    }

    void dispatch_routes_by_extension_case_insensitively() {
        EmbedRegistry reg;
        reg.registerExtension("md", [](const EmbedRequest &req) {
            return makeChild("md:" + req.targetPath);
        });

        EmbedRequest req;
        req.targetPath = "Notes/Page.MD";
        auto child = reg.dispatch(req);
        QVERIFY(child != nullptr);
        QCOMPARE(child->renderedText(), QStringLiteral("md:Notes/Page.MD"));
    }

    void dispatch_returns_null_for_unregistered_extension() {
        EmbedRegistry reg;
        reg.registerExtension("png", [](const EmbedRequest &) {
            return makeChild("png-embed");
        });

        EmbedRequest req;
        req.targetPath = "Notes/Page.pdf";
        QVERIFY(reg.dispatch(req) == nullptr);
    }

    void dispatch_returns_null_when_target_has_no_extension() {
        EmbedRegistry reg;
        reg.registerExtension("md", [](const EmbedRequest &) {
            return makeChild("md-embed");
        });

        EmbedRequest req;
        req.targetPath = "Notes/Page";
        QVERIFY(reg.dispatch(req) == nullptr);
    }

    void dispatch_passes_request_through_to_factory() {
        EmbedRegistry reg;
        EmbedRequest seen;
        reg.registerExtension("png", [&seen](const EmbedRequest &req) {
            seen = req;
            return makeChild("ok");
        });

        EmbedRequest req;
        req.targetPath = "img.png";
        req.subpath = "^blockref";
        req.depth = 2;
        reg.dispatch(req);

        QCOMPARE(seen.targetPath, req.targetPath);
        QCOMPARE(seen.subpath, req.subpath);
        QCOMPARE(seen.depth, req.depth);
    }

    void depthGuard_allows_up_to_max_depth_then_rejects() {
        EmbedDepthGuard guard;
        for (int d = 0; d < EmbedDepthGuard::kMaxDepth; ++d)
            QVERIFY(guard.allow(d));
        QVERIFY(!guard.allow(EmbedDepthGuard::kMaxDepth));
        QVERIFY(!guard.allow(EmbedDepthGuard::kMaxDepth + 1));
    }

    void depthGuard_placeholder_round_trips_target() {
        const QString target = "Notes/Deep Page.md";
        const QString ph = EmbedDepthGuard::placeholder(target);
        QVERIFY(ph.contains(target));
        QCOMPARE(EmbedDepthGuard::placeholderTarget(ph), target);
    }

    void depthGuard_placeholderTarget_returns_empty_for_non_placeholder_text() {
        QCOMPARE(EmbedDepthGuard::placeholderTarget("just some text"), QString());
        QCOMPARE(EmbedDepthGuard::placeholderTarget("[embed depth cap: no closing bracket"), QString());
    }
};

QTEST_APPLESS_MAIN(TstFoundationEmbedRegistry)
#include "tst_foundation_embed_registry.moc"
