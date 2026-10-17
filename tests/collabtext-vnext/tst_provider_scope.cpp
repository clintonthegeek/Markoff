// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/ReplicatedMapStream.h>
#include <markoff/vnext/ScopedTextStream.h>
#include <markoff/vnext/StreamNames.h>

#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace CollabText;
using namespace Markoff::VNext;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T take(Result<T> result)
{
    if (!result.ok())
        throw std::runtime_error(std::string(to_string(result.error().code()))
                                 + ": " + std::string(result.error().message()));
    return std::move(result).value();
}

void providerSessionsAreTheOnlyAllocationPath()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "could not create provider-scope test directory");
    const DocumentStorageScope firstDocument{QStringLiteral("document-A"), 1};
    const DocumentStorageScope secondDocument{QStringLiteral("document-B"), 1};
    const DocumentStorageScope nextEpoch{QStringLiteral("document-A"), 2};
    auto provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temporary.path().toStdString()) / "document-A"));
    auto firstSession = take(provider->open(firstDocument, StorageMode::durable));
    const std::string firstRoute = take(blockTextStreamName({firstSession->actor(), 42}));
    const PositionScope firstScope{"document-A", firstRoute, Primitive::text, 1};
    auto first = take(ScopedTextStream::create(*firstSession, firstScope));
    const auto firstOperation = take(first->replaceUtf8(0, 0, QByteArrayLiteral("a")));
    first.reset();
    firstSession.reset();
    provider.reset();

    provider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temporary.path().toStdString()) / "document-A"));
    firstSession = take(provider->open(firstDocument, StorageMode::durable));
    auto reopened = take(ScopedTextStream::create(*firstSession, firstScope));
    require(take(reopened->applyRemote(
                {firstScope, firstOperation.identity, firstOperation.bytes})).kind()
                == ApplyOutcomeKind::applied,
            "provider reopen could not replay the accepted operation");
    const auto next = take(reopened->replaceUtf8(1, 1, QByteArrayLiteral("d")));
    require(next.identity.actor == firstOperation.identity.actor
                && next.identity.counter > firstOperation.identity.counter,
            "provider reopen reused a scoped text operation identity");
    auto wrongScope = ScopedTextStream::create(*firstSession,
        PositionScope{"document-B", firstRoute, Primitive::text, 1});
    require(!wrongScope.ok() && wrongScope.error().code() == ErrorCode::wrong_document,
            "provider session created a stream for another document scope");
    reopened.reset();
    firstSession.reset();
    provider.reset();

    auto secondProvider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temporary.path().toStdString()) / "document-B"));
    auto secondSession = take(secondProvider->open(secondDocument, StorageMode::durable));
    const MapScope secondMap{"document-B", "frontmatter", 1};
    auto map = take(ReplicatedMapStream::create(*secondSession, secondMap));
    require(map->completeReplay().ok(), "provider-backed map did not complete replay");
    const auto write = take(map->set(QByteArrayLiteral("title"), QByteArrayLiteral("B")));
    require(write.scope == secondMap && write.operation.counter == 1,
            "provider map allocation escaped its document scope");
    map.reset();
    secondSession.reset();
    secondProvider.reset();

    auto epochProvider = take(makeReferenceDocumentStorageProvider(
        std::filesystem::path(temporary.path().toStdString()) / "document-A-epoch-2"));
    auto epochSession = take(epochProvider->open(nextEpoch, StorageMode::durable));
    const std::string epochRoute = take(blockTextStreamName({epochSession->actor(), 42}));
    const PositionScope epochScope{"document-A", epochRoute, Primitive::text, 2};
    auto epochStream = take(ScopedTextStream::create(*epochSession, epochScope));
    const auto epochOperation = take(epochStream->replaceUtf8(
        0, 0, QByteArrayLiteral("c")));
    require(epochOperation.identity.counter == firstOperation.identity.counter,
            "provider counter did not begin independently in the next epoch");
}

} // namespace

int main()
{
    try {
        providerSessionsAreTheOnlyAllocationPath();
        std::cout << "Provider scope checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Provider scope checks failed: " << error.what() << '\n';
        return 1;
    }
}
