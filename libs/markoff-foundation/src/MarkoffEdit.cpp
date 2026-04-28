// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffEdit.h>

#include <QJsonValue>
#include <QString>

namespace Markoff {

QJsonObject MarkoffEdit::toJson() const
{
    QJsonObject obj;
    obj.insert("oldStart", static_cast<qint64>(oldStart));
    obj.insert("oldEnd", static_cast<qint64>(oldEnd));
    // newText is UTF-8 bytes; encode as base64 for binary-safe JSON.
    obj.insert("newText", QString::fromLatin1(newText.toBase64()));
    return obj;
}

MarkoffEdit MarkoffEdit::fromJson(const QJsonObject &obj)
{
    MarkoffEdit e;
    e.oldStart = static_cast<quint32>(obj.value("oldStart").toInteger());
    e.oldEnd = static_cast<quint32>(obj.value("oldEnd").toInteger());
    e.newText = QByteArray::fromBase64(obj.value("newText").toString().toLatin1());
    return e;
}

}  // namespace Markoff
