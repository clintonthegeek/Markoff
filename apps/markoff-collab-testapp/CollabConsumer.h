// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>

namespace Markoff { class MarkoffDocument; }
class InMemoryTransport;

class CollabConsumer : public QObject {
    Q_OBJECT
public:
    CollabConsumer(Markoff::MarkoffDocument *doc,
                   InMemoryTransport *transport,
                   QObject *parent = nullptr);
private:
    Markoff::MarkoffDocument *m_doc;
    InMemoryTransport        *m_transport;
};
