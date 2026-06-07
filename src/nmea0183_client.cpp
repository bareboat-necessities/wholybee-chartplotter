#include "nmea0183_client.hpp"
#include "nav_data_store.hpp"

#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QDateTime>
#include <QStringList>
#include <cmath>
#include <optional>

namespace {
constexpr int kStaleMs     = 5000;   // no valid sentence for this long -> not decoding
constexpr int kReconnectMs = 3000;   // retry a failed/dropped TCP connection

// Verify the "*HH" XOR checksum if present. NMEA xors every byte between '$'/'!'
// and '*'. Sentences without a checksum are accepted (some gateways omit it).
bool checksumOk(const QByteArray& s) {
    const int star = s.lastIndexOf('*');
    if (star < 0) return true;                       // no checksum supplied
    if (star + 3 > s.size()) return false;           // "*HH" doesn't fit
    unsigned char x = 0;
    for (int i = 1; i < star; ++i) x ^= static_cast<unsigned char>(s[i]);
    bool ok = false;
    const int given = s.mid(star + 1, 2).toInt(&ok, 16);
    return ok && given == x;
}

// "ddmm.mmmm" + hemisphere -> signed decimal degrees. Works for both latitude
// (dd) and longitude (ddd) because the minutes are always two integer digits:
// degrees = floor(value / 100), minutes = value - degrees*100.
std::optional<double> parseCoord(const QString& field, const QString& hemi) {
    if (field.isEmpty()) return std::nullopt;
    bool ok = false;
    const double raw = field.toDouble(&ok);
    if (!ok) return std::nullopt;
    const double deg = std::floor(raw / 100.0);
    double v = deg + (raw - deg * 100.0) / 60.0;
    const QString h = hemi.toUpper();
    if (h == QLatin1String("S") || h == QLatin1String("W")) v = -v;
    return v;
}

std::optional<double> parseNum(const QString& field) {
    if (field.isEmpty()) return std::nullopt;
    bool ok = false;
    const double v = field.toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}
} // namespace

Nmea0183Client::Nmea0183Client(INavDataPublisher* publisher, QObject* parent)
    : QObject(parent), publisher_(publisher) {
    staleTimer_ = new QTimer(this);
    staleTimer_->setSingleShot(true);
    staleTimer_->setInterval(kStaleMs);
    connect(staleTimer_, &QTimer::timeout, this, &Nmea0183Client::onStaleTimeout);

    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setInterval(kReconnectMs);
    connect(reconnectTimer_, &QTimer::timeout, this, &Nmea0183Client::tryReconnect);
}

Nmea0183Client::~Nmea0183Client() { stop(); }

void Nmea0183Client::setConfig(NmeaTransport transport, const QString& host,
                               quint16 port, bool enabled) {
    transport_ = transport;
    host_ = host;
    port_ = port;
    enabled_ = enabled;
    stop();
    if (enabled_) start();
}

void Nmea0183Client::start() {
    buffer_.clear();
    if (transport_ == NmeaTransport::Tcp) {
        tcp_ = new QTcpSocket(this);
        connect(tcp_, &QTcpSocket::connected,  this, &Nmea0183Client::onTcpConnected);
        connect(tcp_, &QTcpSocket::readyRead,  this, &Nmea0183Client::onTcpReadyRead);
        // Drop -> retry while still enabled.
        connect(tcp_, &QTcpSocket::disconnected, this, [this] {
            setDecoding(false);
            if (enabled_) reconnectTimer_->start();
        });
        connect(tcp_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            setDecoding(false);
            if (enabled_ && !reconnectTimer_->isActive()) reconnectTimer_->start();
        });
        tcp_->connectToHost(host_, port_);
    } else {
        udp_ = new QUdpSocket(this);
        connect(udp_, &QUdpSocket::readyRead, this, &Nmea0183Client::onUdpReadyRead);
        // Share so multiple listeners on the box can coexist.
        udp_->bind(QHostAddress::AnyIPv4, port_,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }
}

void Nmea0183Client::stop() {
    reconnectTimer_->stop();
    staleTimer_->stop();
    if (tcp_) { tcp_->abort(); tcp_->deleteLater(); tcp_ = nullptr; }
    if (udp_) { udp_->close(); udp_->deleteLater(); udp_ = nullptr; }
    buffer_.clear();
    setDecoding(false);
}

void Nmea0183Client::tryReconnect() {
    if (!enabled_ || transport_ != NmeaTransport::Tcp || !tcp_) return;
    if (tcp_->state() == QAbstractSocket::UnconnectedState)
        tcp_->connectToHost(host_, port_);
}

void Nmea0183Client::onTcpConnected() {
    reconnectTimer_->stop();
}

void Nmea0183Client::onTcpReadyRead() {
    if (tcp_) feed(tcp_->readAll());
}

void Nmea0183Client::onUdpReadyRead() {
    while (udp_ && udp_->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(udp_->pendingDatagramSize()));
        udp_->readDatagram(dg.data(), dg.size());
        // A datagram may hold several sentences separated by CR/LF.
        for (const QByteArray& line : dg.split('\n'))
            processLine(line);
    }
}

// Reassemble a TCP byte stream into newline-delimited sentences, carrying any
// partial trailing line until the rest arrives.
void Nmea0183Client::feed(const QByteArray& bytes) {
    buffer_.append(bytes);
    int nl;
    while ((nl = buffer_.indexOf('\n')) >= 0) {
        processLine(buffer_.left(nl));
        buffer_.remove(0, nl + 1);
    }
    if (buffer_.size() > 4096) buffer_.clear();   // guard against a garbage stream
}

void Nmea0183Client::processLine(QByteArray line) {
    while (!line.isEmpty() && (line.endsWith('\r') || line.endsWith('\n') ||
                              line.endsWith(' '))) line.chop(1);
    if (line.isEmpty()) return;
    if (line.front() != '$' && line.front() != '!') return;
    if (!checksumOk(line)) return;
    handleSentence(QString::fromLatin1(line));
}

void Nmea0183Client::handleSentence(const QString& sentence) {
    // Strip the checksum, then split on commas. Address is "$ttSSS"; we match on
    // the 3-char sentence type (SSS), ignoring the talker id (tt).
    QString body = sentence;
    const int star = body.indexOf('*');
    if (star >= 0) body.truncate(star);
    const QStringList f = body.split(',');
    if (f.isEmpty() || f[0].size() < 4) return;
    const QString type = f[0].right(3).toUpper();
    if      (type == QLatin1String("RMC")) parseRmc(f);
    else if (type == QLatin1String("GLL")) parseGll(f);
}

// $--RMC,time,status,lat,N/S,lon,E/W,sog,cog,date,magvar,E/W,...
void Nmea0183Client::parseRmc(const QStringList& f) {
    if (f.size() < 9) return;
    if (f[2].toUpper() != QLatin1String("A")) return;      // V = void / no fix
    const auto lat = parseCoord(f[3], f[4]);
    const auto lon = parseCoord(f[5], f[6]);
    if (!lat || !lon) return;

    NavValueMeta m;
    m.source = QStringLiteral("nmea0183");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    publisher_->publishOwnshipPosition(*lat, *lon, m);

    const auto sog = parseNum(f[7]);
    const auto cog = parseNum(f[8]);
    if (sog && cog) publisher_->publishCogSog(*cog, *sog, m);

    setDecoding(true);
    staleTimer_->start();
}

// $--GLL,lat,N/S,lon,E/W,time,status,mode
void Nmea0183Client::parseGll(const QStringList& f) {
    if (f.size() < 5) return;
    if (f.size() >= 7 && f[6].toUpper() != QLatin1String("A")) return;  // status if present
    const auto lat = parseCoord(f[1], f[2]);
    const auto lon = parseCoord(f[3], f[4]);
    if (!lat || !lon) return;

    NavValueMeta m;
    m.source = QStringLiteral("nmea0183");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    publisher_->publishOwnshipPosition(*lat, *lon, m);

    setDecoding(true);
    staleTimer_->start();
}

void Nmea0183Client::setDecoding(bool on) {
    if (on == decoding_) return;
    decoding_ = on;
    emit decodingChanged(on);
}

void Nmea0183Client::onStaleTimeout() {
    setDecoding(false);
}
