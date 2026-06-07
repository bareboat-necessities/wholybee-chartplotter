#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>

class INavDataPublisher;
class QTcpSocket;
class QUdpSocket;
class QTimer;

// Network transport for the NMEA 0183 gateway.
enum class NmeaTransport { Tcp, Udp };

// Reads NMEA 0183 sentences from a WiFi gateway over TCP or UDP, decodes the
// position sentences we care about (RMC, GLL), and publishes them into the
// NavDataStore through the INavDataPublisher contract — the same path the
// simulator uses.
//
// "Decoding" status: while valid position sentences keep arriving, isDecoding()
// is true; if none arrive for the timeout (5 s) it flips false. The UI shows a
// green dot next to the menu item while decoding.
//
// TCP connects to host:port and reassembles the byte stream into lines. UDP
// binds to the port and reads datagrams (IP is irrelevant for a listener).
// Reconnection is attempted periodically while enabled.
class Nmea0183Client : public QObject {
    Q_OBJECT
public:
    explicit Nmea0183Client(INavDataPublisher* publisher, QObject* parent = nullptr);
    ~Nmea0183Client() override;

    bool isDecoding() const { return decoding_; }

public slots:
    // Apply a new configuration. Tears down any existing connection and, if
    // enabled, starts a fresh one. Safe to call repeatedly.
    void setConfig(NmeaTransport transport, const QString& host,
                   quint16 port, bool enabled);

signals:
    // True while valid position sentences are arriving (within the timeout).
    void decodingChanged(bool on);

private slots:
    void onTcpConnected();
    void onTcpReadyRead();
    void onUdpReadyRead();
    void onStaleTimeout();
    void tryReconnect();

private:
    void start();
    void stop();
    void setDecoding(bool on);
    void feed(const QByteArray& bytes);      // assemble + dispatch lines (TCP)
    void processLine(QByteArray line);       // one raw sentence
    void handleSentence(const QString& sentence);
    void parseRmc(const QStringList& f);
    void parseGll(const QStringList& f);

    INavDataPublisher* publisher_ = nullptr;

    NmeaTransport transport_ = NmeaTransport::Tcp;
    QString  host_;
    quint16  port_ = 0;
    bool     enabled_ = false;

    QTcpSocket* tcp_ = nullptr;
    QUdpSocket* udp_ = nullptr;
    QByteArray  buffer_;                      // partial TCP line carry-over
    QTimer*     staleTimer_ = nullptr;        // fires when data stops (5 s)
    QTimer*     reconnectTimer_ = nullptr;    // retry a dropped/failed TCP link
    bool        decoding_ = false;
};
