// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <vector>

class InMemoryTransport : public QObject {
    Q_OBJECT
public:
    explicit InMemoryTransport(QString replicaName, QObject *parent = nullptr);

    using OnInboundFn = std::function<void(QString, QByteArray, quint16)>;

    struct ActionReceipt {
        QByteArray actionId;
        QByteArray acceptedRevision;
        friend bool operator==(const ActionReceipt&, const ActionReceipt&) = default;
    };
    using OnActionFn = std::function<std::vector<ActionReceipt>(
        QString, QByteArray, quint16)>;
    using OnReceiptFn = std::function<void(ActionReceipt)>;

    void push(QString streamName, QByteArray blob);
    void setOnInbound(OnInboundFn fn);
    /// Push one complete action envelope and remember it until an exact
    /// action-ID receipt returns from a peer.
    [[nodiscard]] bool pushAction(QString streamName, QByteArray actionId,
                                  QByteArray mabBytes);
    void setOnAction(OnActionFn fn);
    void setOnReceipt(OnReceiptFn fn);
    [[nodiscard]] qsizetype pendingActionCount() const noexcept;

    void connectPeer(InMemoryTransport *peer);
    void disconnectPeer(InMemoryTransport *peer);


    QString replicaName() const { return m_name; }

private:
    struct PendingAction {
        QString streamName;
        QByteArray mabBytes;
    };

    void deliverFromPeer(QString stream, QByteArray blob, quint16 producer);
    void deliverActionFromPeer(InMemoryTransport *source, QString stream,
                               QByteArray actionId, QByteArray mabBytes,
                               quint16 producer);
    void deliverReceiptFromPeer(InMemoryTransport *source, ActionReceipt receipt);

    QString m_name;
    QList<InMemoryTransport *> m_peers;
    OnInboundFn m_onInbound;
    OnActionFn m_onAction;
    OnReceiptFn m_onReceipt;
    QHash<QByteArray, PendingAction> m_pendingActions;
    QHash<QByteArray, QPointer<InMemoryTransport>> m_actionOrigins;
};
