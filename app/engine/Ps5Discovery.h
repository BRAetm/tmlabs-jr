#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>

class QUdpSocket;
class QTimer;

namespace Labs {

// Sony's UDP discovery probe — broadcasts SRCH * HTTP/1.1 to port 9302 and
// listens for any PS4/PS5 on the LAN to respond with its IP + name + status.
// Async, non-blocking, harmless if nothing replies.
class Ps5Discovery : public QObject {
    Q_OBJECT
public:
    struct Host {
        QString ip;
        QString name;
        QString type;        // "PS5" / "PS4"
        QString hostId;      // some consoles include a stable host-id
        QString statusCode;  // 200 = on, 620 = standby, etc.
    };

    explicit Ps5Discovery(QObject* parent = nullptr);
    ~Ps5Discovery() override;

    // Fire one round of discovery. Emits hostsFound() after timeoutMs.
    void discover(int timeoutMs = 2500);

signals:
    void hostsFound(QList<Labs::Ps5Discovery::Host> hosts);

private slots:
    void onReadyRead();
    void onTimeout();

private:
    QUdpSocket* m_sock     = nullptr;
    QTimer*     m_timer    = nullptr;
    QHash<QString, Host> m_seen;  // de-dupe by IP
};

} // namespace Labs
