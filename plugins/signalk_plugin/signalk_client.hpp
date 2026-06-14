#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;

// WebSocket transport to a Signal K server.
//
// Connects to ws://host:port/signalk/v1/stream?subscribe=self and forwards every
// text frame (one Signal K delta JSON object per frame) up to the plugin via
// messageReceived(). Mirrors Nmea0183Client's lifecycle:
//   - setConfig() applies new settings and (re)opens the connection while enabled
//   - a 5-second stale timer flips isDecoding() / emits decodingChanged()
//   - a 3-second reconnect timer retries a dropped or refused link
//
// "Decoding" tracks whether messages keep arriving, not socket state per se, so
// the status dot follows the same convention as the NMEA sources: green while
// data flows, off when the stream goes quiet.
class SignalKClient : public QObject {
    Q_OBJECT
public:
    explicit SignalKClient(QObject* parent = nullptr);
    ~SignalKClient() override;

    bool isDecoding() const { return decoding_; }

public slots:
    // Apply a new configuration. Tears down any existing connection and, if
    // enabled, starts a fresh one. Safe to call repeatedly.
    void setConfig(const QString& host, quint16 port, bool enabled);

signals:
    // True while messages are arriving (within the timeout).
    void decodingChanged(bool on);
    // One Signal K delta JSON object as text (we forward whatever the server
    // sends, even if it batches multiple updates in a single frame).
    void messageReceived(const QString& json);
    // The full vessel tree at connection time, fetched from the REST API.
    // Signal K servers only emit deltas when source data changes, so vessel
    // names cached in the tree (from earlier AIS Type 5 / Type 24 messages) do
    // not flow over the stream — the snapshot fills them in once at startup.
    void snapshotReceived(const QString& json);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& text);
    void onStaleTimeout();
    void tryReconnect();

private:
    void start();
    void stop();
    void setDecoding(bool on);
    void markDecoding();   // mark active + restart the stale timer
    void fetchSnapshot();  // GET /signalk/v1/api/vessels once after WS connects
    QUrl buildUrl() const;

    QString host_;
    quint16 port_    = 80;
    bool    enabled_ = false;

    QWebSocket* ws_             = nullptr;
    QTimer*     staleTimer_     = nullptr;   // fires when data stops (5 s)
    QTimer*     reconnectTimer_ = nullptr;   // retry a dropped link (3 s)
    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply*         snapshotReply_ = nullptr;
    bool        decoding_       = false;
};
