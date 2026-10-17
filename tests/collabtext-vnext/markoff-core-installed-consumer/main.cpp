// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTemporaryDir>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <filesystem>
#include <vector>

int main()
{
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const std::filesystem::path root(directory.path().toStdString());

    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("installed-core-consumer"), 1};
    std::vector<Markoff::BlockId> acceptedBlocks;
    {
        auto provider = Markoff::VNext::makeReferenceDocumentStorageProvider(root);
        if (!provider.ok()) return 2;
        auto document = Markoff::MarkoffDocument::open(*provider.value(), scope,
            Markoff::VNext::StorageMode::durable);
        if (!document.ok()) return 3;

        const auto imported = document.value()->importInitialMarkdown(
            QByteArrayLiteral("# Installed\n"));
        if (imported.state != Markoff::VNext::ActionPublicationState::committed)
            return 4;

        acceptedBlocks = document.value()->iterateBlocks();
        if (acceptedBlocks.size() != 1) return 5;
        const auto text = document.value()->blockText(acceptedBlocks.front());
        const auto edited = document.value()->applyBlockEdit(
            {acceptedBlocks.front(), static_cast<uint32_t>(text.size()), 0,
             QByteArrayLiteral(" consumer")});
        if (edited.state != Markoff::VNext::ActionPublicationState::committed)
            return 6;

        const auto snapshot = document.value()->snapshotForExport();
        if (!snapshot.ok()
            || !snapshot.value().markdown.contains("Installed consumer"))
            return 7;
    }

    {
        auto provider = Markoff::VNext::makeReferenceDocumentStorageProvider(root);
        if (!provider.ok()) return 8;
        auto document = Markoff::MarkoffDocument::open(*provider.value(), scope,
            Markoff::VNext::StorageMode::durable);
        if (!document.ok()) return 9;
        const auto reopenedBlocks = document.value()->iterateBlocks();
        if (reopenedBlocks != acceptedBlocks) return 10;
        const auto reopenedText = document.value()->blockText(reopenedBlocks.front());
        if (!reopenedText.contains(QByteArrayLiteral("Installed consumer"))) return 11;
        document.value()->undoD2();
        if (!document.value()->blockText(reopenedBlocks.front())
                .contains(QByteArrayLiteral("Installed"))
            || document.value()->blockText(reopenedBlocks.front())
                .contains(QByteArrayLiteral("consumer"))) return 12;
        document.value()->redoD2();
        if (!document.value()->blockText(reopenedBlocks.front())
                .contains(QByteArrayLiteral("Installed consumer"))) return 13;
    }
    return 0;
}
