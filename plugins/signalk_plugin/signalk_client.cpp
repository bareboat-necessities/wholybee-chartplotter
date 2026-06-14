#include "signalk_client.hpp"

#include <QWebSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

namespace {
constexpr int kStaleMs     = 5000;   // no message for 5 s -> not decoding
constexpr int kReconnectMs = 3000;   // retry every 3 s while enabled
}  // namespace

SignalKClient::SignalKClient(QObject* parent) : QObject(parent) {
    staleTimer_ = new QTimer(this);
    staleTimer_->setSingleShot(true);
    staleTimer_->setInterval(kStaleMs);
    connect(staleTimer_, &QTimer::timeout, this, &SignalKClient::onStaleTimeout);

    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(false);
    reconnectTimer_->setInterval(kReconnectMs);
    connect(reconnectTimer_, &QTimer::timeout, this, &SignalKClient::tryReconnect);

    nam_ = new QNetworkAccessManager(this);
}

SignalKClient::~SignalKClient() { stop(); }

void SignalKClient::setConfig(const QString& host, quint16 port, bool enabled) {
    host_    = host;
    port_    = port;
    enabled_ = enabled;
    stop();
    if (enabled_ && !host_.isEmpty() && port_ > 0) start();
}

QUrl SignalKClient::buildUrl() const {
    QUrl u;
    u.setScheme(QStringLiteral("ws"));
    u.setHost(host_);
    u.setPort(port_);
    u.setPath(QStringLiteral("/signalk/v1/stream"));
    // `subscribe=all` asks for every vessel's deltas, not just our own — needed
    // for AIS targets to come through. The decoder uses the hello message's
    // `self` field to tell own-ship deltas apart from AIS contacts.
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("subscribe"), QStringLiteral("all"));
    u.setQuery(q);
    return u;
}

void SignalKClient::start() {
    if (ws_) return;
    ws_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(ws_, &QWebSocket::connected,    this, &SignalKClient::onConnected);
    connect(ws_, &QWebSocket::disconnected, this, &SignalKClient::onDisconnected);
    connect(ws_, &QWebSocket::textMessageReceived,
            this, &SignalKClient::onTextMessage);
    // Treat any error as a closed link: stop talking, let the reconnect timer
    // try again — same recovery the NMEA TCP client uses.
    connect(ws_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (ws_) ws_->abort();
    });
    ws_->open(buildUrl());
    if (!reconnectTimer_->isActive()) reconnectTimer_->start();
}

void SignalKClient::stop() {
    reconnectTimer_->stop();
    staleTimer_->stop();
    setDecoding(false);
    if (snapshotReply_) {
        snapshotReply_->abort();
        snapshotReply_->deleteLater();
        snapshotReply_ = nullptr;
    }
    if (ws_) {
        ws_->disconnect(this);
        ws_->abort();
        ws_->deleteLater();
        ws_ = nullptr;
    }
}

void SignalKClient::onConnected() {
    // Connection up, but only flip the dot once the server starts streaming.
    // Kick off a one-shot REST GET of the full vessel tree so any names already
    // cached server-side flow through immediately, without waiting for the next
    // AIS static report (every 6 min for Class A, irregular for Class B).
    fetchSnapshot();
}

void SignalKClient::fetchSnapshot() {
    if (!nam_) return;
    if (snapshotReply_) { snapshotReply_->abort(); snapshotReply_->deleteLater(); snapshotReply_ = nullptr; }
    QUrl u;
    u.setScheme(QStringLiteral("http"));
    u.setHost(host_);
    u.setPort(port_);
    u.setPath(QStringLiteral("/signalk/v1/api/vessels"));
    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("chartplotter-signalk"));
    snapshotReply_ = nam_->get(req);
    connect(snapshotReply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = snapshotReply_;
        if (!r) return;
        snapshotReply_ = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;   // silent: snapshot is best-effort
        const QByteArray body = r->readAll();
        if (body.isEmpty()) return;
        emit snapshotReceived(QString::fromUtf8(body));
    });
}

void SignalKClient::onDisconnected() {
    setDecoding(false);
    // Drop the socket and let reconnect rebuild it.
    if (ws_) { ws_->deleteLater(); ws_ = nullptr; }
}

void SignalKClient::onTextMessage(const QString& text) {
    markDecoding();
    emit messageReceived(text);
}

void SignalKClient::onStaleTimeout() { setDecoding(false); }

void SignalKClient::tryReconnect() {
    if (!enabled_) { reconnectTimer_->stop(); return; }
    if (ws_) {
        // Active connection: nothing to do.
        if (ws_->state() == QAbstractSocket::ConnectedState
            || ws_->state() == QAbstractSocket::ConnectingState)
            return;
    }
    // Rebuild from scratch — simpler than poking at intermediate states.
    if (ws_) { ws_->deleteLater(); ws_ = nullptr; }
    start();
}

void SignalKClient::markDecoding() {
    setDecoding(true);
    staleTimer_->start();
}

void SignalKClient::setDecoding(bool on) {
    if (on == decoding_) return;
    decoding_ = on;
    emit decodingChanged(on);
}
