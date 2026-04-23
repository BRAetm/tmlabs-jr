#include "Ps5Discovery.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

namespace Labs {

static constexpr quint16 kDiscoveryPort = 9302;

static const QByteArray kProbePs5 =
    "SRCH * HTTP/1.1\n"
    "device-discovery-protocol-version:00030010\n";
static const QByteArray kProbePs4 =
    "SRCH * HTTP/1.1\n"
    "device-discovery-protocol-version:00010010\n";


Ps5Discovery::Ps5Discovery(QObject* parent)
    : QObject(parent)
{}

Ps5Discovery::~Ps5Discovery() = default;

void Ps5Discovery::discover(int timeoutMs)
{
    m_seen.clear();

    if (!m_sock) {
        m_sock = new QUdpSocket(this);
        connect(m_sock, &QUdpSocket::readyRead, this, &Ps5Discovery::onReadyRead);
    }
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, &Ps5Discovery::onTimeout);
    }

    if (m_sock->state() != QAbstractSocket::BoundState) {
        if (!m_sock->bind(QHostAddress::AnyIPv4, 0,
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            qWarning() << "Ps5Discovery: bind failed:" << m_sock->errorString();
            emit hostsFound({});
            return;
        }
    }

    // Broadcast on every interface that has a v4 broadcast address.
    int sent = 0;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))      continue;
        if ((iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QHostAddress bcast = entry.broadcast();
            if (bcast.isNull()) continue;
            m_sock->writeDatagram(kProbePs5, bcast, kDiscoveryPort);
            m_sock->writeDatagram(kProbePs4, bcast, kDiscoveryPort);
            sent += 2;
        }
    }
    // Belt + suspenders: also fire at the limited broadcast.
    m_sock->writeDatagram(kProbePs5, QHostAddress::Broadcast, kDiscoveryPort);
    m_sock->writeDatagram(kProbePs4, QHostAddress::Broadcast, kDiscoveryPort);

    qInfo() << "Ps5Discovery: probed" << sent << "interface broadcasts + 2 limited";
    m_timer->start(timeoutMs);
}

static QString headerValue(const QByteArray& body, const QByteArray& key)
{
    // Body is HTTP-like: "key:value\n" lines. Case-insensitive lookup.
    int i = 0;
    while (i < body.size()) {
        int eol = body.indexOf('\n', i);
        if (eol < 0) eol = body.size();
        const QByteArray line = body.mid(i, eol - i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0) {
            const QByteArray k = line.left(colon).trimmed();
            if (QString::fromLatin1(k).compare(QString::fromLatin1(key), Qt::CaseInsensitive) == 0)
                return QString::fromUtf8(line.mid(colon + 1)).trimmed();
        }
        i = eol + 1;
    }
    return {};
}

void Ps5Discovery::onReadyRead()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_sock->receiveDatagram();
        if (dg.data().isEmpty()) continue;

        Host h;
        h.ip         = dg.senderAddress().toString();
        h.name       = headerValue(dg.data(), "host-name");
        h.type       = headerValue(dg.data(), "host-type");
        h.hostId     = headerValue(dg.data(), "host-id");
        h.statusCode = headerValue(dg.data(), "status_code");
        if (h.statusCode.isEmpty()) {
            // Some consoles put the status in the first line: "HTTP/1.1 200 Ok"
            const QByteArray first = dg.data().split('\n').value(0);
            if (first.contains("200")) h.statusCode = "200";
            else if (first.contains("620")) h.statusCode = "620";
        }
        m_seen.insert(h.ip, h);
    }
}

void Ps5Discovery::onTimeout()
{
    QList<Host> out;
    out.reserve(m_seen.size());
    for (const Host& h : std::as_const(m_seen)) out << h;
    qInfo() << "Ps5Discovery: found" << out.size() << "console(s)";
    emit hostsFound(out);
}

} // namespace Labs
