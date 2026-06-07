#pragma once
#include <QDialog>
#include "nmea0183_client.hpp"   // NmeaTransport

class QRadioButton;
class QLineEdit;
class QCheckBox;
class QWidget;
class TouchSpinBox;

// Configures the network connection to a NMEA 0183 WiFi gateway: TCP or UDP, a
// port, and (TCP only) the gateway IP address, plus an enable switch. Edits a
// working copy; the caller reads the getters after acceptance and persists
// through Settings.
class Nmea0183Dialog : public QDialog {
    Q_OBJECT
public:
    Nmea0183Dialog(NmeaTransport transport, const QString& host, quint16 port,
                   bool enabled, QWidget* parent = nullptr);

    NmeaTransport transport() const;
    QString  host() const;
    quint16  port() const;
    bool     enabled() const;

private:
    void updateHostVisibility();

    QRadioButton* tcpRadio_ = nullptr;
    QRadioButton* udpRadio_ = nullptr;
    QWidget*      hostRow_ = nullptr;
    QLineEdit*    hostEdit_ = nullptr;
    TouchSpinBox* portBox_ = nullptr;
    QCheckBox*    enableBox_ = nullptr;
};
