// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

#include <functional>

class QMimeData;

namespace Markoff::ClipboardCodec {

/// MIME type for structured block JSON (same payload LiveClipboardController
/// already writes). Shared so every leaf round-trips through one name.
inline constexpr const char *kBlocksMime = "application/x-markoff-blocks";
inline constexpr const char *kMarkdownMime = "text/markdown";
inline constexpr const char *kRtfMime = "text/rtf";

/// Visible text with markup characters stripped (`**bold**` → `bold`,
/// `# Title` → `Title`). List markers are kept (`- `/`1. `).
MARKOFF_CORE_EXPORT QString markdownToPlain(const QByteArray &markdown);

/// Semantic HTML fragment (`<strong>`, `<em>`, `<h1>`…, `<a href>`).
/// Not themed Qt `QTextDocument::toHtml()` output.
MARKOFF_CORE_EXPORT QString markdownToHtml(const QByteArray &markdown);

/// Semantic RTF subset (`\b \i \strike \par \bullet`). Emitted directly;
/// Qt has no RTF writer backend on Linux.
MARKOFF_CORE_EXPORT QByteArray markdownToRtf(const QByteArray &markdown);

/// Lossy HTML → markdown. Walks a `QTextDocument` after `setHtml`.
MARKOFF_CORE_EXPORT QByteArray htmlToMarkdown(const QString &html);

/// Lossy RTF → markdown. Subset of control words (`\b \i \strike \par`
/// `\bullet` + `HYPERLINK` fields). Full RTF is not a goal.
MARKOFF_CORE_EXPORT QByteArray rtfToMarkdown(const QByteArray &rtf);

MARKOFF_CORE_EXPORT QString htmlToPlain(const QString &html);
MARKOFF_CORE_EXPORT QString rtfToPlain(const QByteArray &rtf);

/// Restore the list/quote display markers a leaf paints outside the text
/// (native bullets, quote margins, gutter markers), so a selection exported as
/// Markdown/HTML/RTF keeps its List/BlockQuote structure instead of re-parsing
/// into plain paragraphs.
///
/// `selection` is QTextDocument selection text, whose paragraph separators are
/// U+2029. `markerAtLine(i)` returns the bytes to prefix to line `i` (empty for
/// a line that continues a block or has no marker); the caller owns the
/// line → block mapping because it differs per leaf. The U+2029 separators are
/// preserved in the result.
MARKOFF_CORE_EXPORT QString restoreBlockMarkers(
    const QString &selection,
    const std::function<QByteArray(qsizetype lineIndex)> &markerAtLine);

/// Default Copy writes every flavor; "Copy as X" writes one. `text/plain`
/// on `All`/`Markdown` is the raw markdown (so round-trip paste keeps
/// markup). Stripped text is `Flavor::Plain` only.
enum class Flavor { All, Markdown, Plain, Html, Rtf };

/// Caller owns the returned object. `blocksPayload` may be empty — the
/// structured MIME type is omitted then.
MARKOFF_CORE_EXPORT QMimeData *mimeFromMarkdown(const QByteArray &markdown,
                                                const QJsonDocument &blocksPayload,
                                                Flavor flavor);

enum class PasteMode { Smart, Plain };

/// Smart: markoff-blocks → text/markdown → HTML → RTF → text/plain.
/// Plain: text/plain if present, else stripped HTML/RTF.
MARKOFF_CORE_EXPORT QByteArray markdownFromMime(const QMimeData *mime,
                                                PasteMode mode);

}  // namespace Markoff::ClipboardCodec
