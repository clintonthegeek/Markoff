// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockWalker.h"

#include <markoff/view/qml/BlockKind.h>

#include <QRegularExpression>
#include <QStringList>

namespace Markoff::View::Qml {

namespace {

bool isFenceOpen(const QString &line, QString &lang)
{
    static const QRegularExpression re(QStringLiteral("^```\\s*(\\S*)\\s*$"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return false;
    lang = m.captured(1);
    return true;
}

bool isFenceClose(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^```\\s*$"));
    return re.match(line).hasMatch();
}

bool isHorizontalRule(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^[ \\t]*([-*_])(\\s*\\1){2,}[ \\t]*$"));
    return re.match(line).hasMatch();
}

bool isImageOnly(const QString &block, BlockRecord &rec)
{
    static const QRegularExpression re(
        QStringLiteral("^!\\[(.*?)\\]\\((\\S+?)(?:\\s+\"(.*?)\")?\\)\\s*$"));
    const auto m = re.match(block.trimmed());
    if (!m.hasMatch()) return false;
    rec.kind = BlockKind::Image;
    rec.imageAlt = m.captured(1);
    rec.imageSrc = m.captured(2);
    rec.imageTitle = m.captured(3);
    rec.text = block;
    rec.source = block;
    return true;
}

int headingLevel(const QString &line, QString &headingText)
{
    static const QRegularExpression re(QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return 0;
    headingText = m.captured(2);
    return m.captured(1).length();
}

}  // namespace

QList<BlockRecord> BlockWalker::walk(const QString &source)
{
    QList<BlockRecord> result;
    if (source.isEmpty()) return result;

    const QStringList lines = source.split(QChar('\n'));
    int i = 0;
    const int n = lines.size();

    while (i < n) {
        // Skip blank lines.
        while (i < n && lines[i].trimmed().isEmpty()) ++i;
        if (i >= n) break;

        BlockRecord rec;

        QString fenceLang;
        if (isFenceOpen(lines[i], fenceLang)) {
            // Fenced code block. Capture body until fence-close or EOF.
            const int startIdx = i;
            ++i;
            QStringList bodyLines;
            while (i < n && !isFenceClose(lines[i])) {
                bodyLines << lines[i];
                ++i;
            }
            const bool closed = (i < n && isFenceClose(lines[i]));
            const int endIdx = closed ? i : (n - 1);
            if (closed) ++i;  // consume closing fence

            rec.kind = BlockKind::CodeBlock;
            rec.codeLanguage = fenceLang;
            rec.codeText = bodyLines.join(QChar('\n'));
            if (!bodyLines.isEmpty() || closed) rec.codeText += QChar('\n');

            QStringList allLines;
            for (int k = startIdx; k <= endIdx && k < n; ++k) allLines << lines[k];
            rec.source = allLines.join(QChar('\n'));
            rec.text = rec.source;
            result.append(rec);
            continue;
        }

        // Non-fence: collect lines until next blank line.
        const int startIdx = i;
        QStringList blockLines;
        while (i < n && !lines[i].trimmed().isEmpty()) {
            blockLines << lines[i];
            ++i;
        }
        const QString blockSource = blockLines.join(QChar('\n'));

        // Classify.
        if (blockLines.size() == 1) {
            const QString single = blockLines.first();
            QString headingText;
            const int level = headingLevel(single, headingText);
            if (level > 0) {
                rec.kind = BlockKind::Heading;
                rec.headingLevel = level;
                rec.text = headingText;
                rec.source = blockSource;
                result.append(rec);
                continue;
            }
            if (isHorizontalRule(single)) {
                rec.kind = BlockKind::HorizontalRule;
                rec.source = blockSource;
                rec.text = blockSource;
                result.append(rec);
                continue;
            }
            BlockRecord img;
            if (isImageOnly(single, img)) {
                result.append(img);
                continue;
            }
        }

        // Default: paragraph.
        rec.kind = BlockKind::Paragraph;
        rec.source = blockSource;
        rec.text = blockSource;
        result.append(rec);

        Q_UNUSED(startIdx);
    }

    return result;
}

}  // namespace Markoff::View::Qml
