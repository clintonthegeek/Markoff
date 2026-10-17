// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>
#include <stdexcept>

namespace Markoff::Styled::Tests {
struct CandidateDocumentFixture {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};
inline CandidateDocumentFixture openCandidateDocument(const QByteArray& markdown)
{
    static int serial = 0;
    auto provider = VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocumentFixture fixture{std::move(provider).value(), {}};
    auto opened = MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("styled-test-%1").arg(++serial), 1},
        VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    const auto imported = fixture.document->importInitialMarkdown(markdown);
    if (imported.state != VNext::ActionPublicationState::committed
        || !fixture.document->canStartOrdinaryAction())
        throw std::runtime_error("provider-backed initial import failed");
    return fixture;
}
} // namespace Markoff::Styled::Tests
