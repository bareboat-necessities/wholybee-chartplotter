#include "nmea0183_dialog.hpp"
#include "touch_spin_box.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

Nmea0183Dialog::Nmea0183Dialog(NmeaTransport transport, const QString& host,
                               quint16 port, bool enabled, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("NMEA 0183"));
    resize(460, 420);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Connect to a WiFi gateway that broadcasts NMEA 0183 data over the "
        "network."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    // ---- Transport ----
    auto* typeBox = new QGroupBox(QStringLiteral("Connection Type"));
    auto* typeRow = new QHBoxLayout(typeBox);
    tcpRadio_ = new QRadioButton(QStringLiteral("TCP"));
    udpRadio_ = new QRadioButton(QStringLiteral("UDP"));
    for (QRadioButton* r : {tcpRadio_, udpRadio_}) {
        r->setMinimumHeight(40);
        r->setStyleSheet(QStringLiteral("QRadioButton{ font-size:15px; }"));
    }
    (transport == NmeaTransport::Udp ? udpRadio_ : tcpRadio_)->setChecked(true);
    typeRow->addWidget(tcpRadio_);
    typeRow->addWidget(udpRadio_);
    typeRow->addStretch(1);
    col->addWidget(typeBox);

    // ---- Host (TCP only) ----
    hostRow_ = new QWidget;
    auto* hostCol = new QVBoxLayout(hostRow_);
    hostCol->setContentsMargins(0, 0, 0, 0);
    hostCol->setSpacing(4);
    auto* hostCap = new QLabel(QStringLiteral("Gateway IP address:"));
    hostCap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    hostEdit_ = new QLineEdit(host);
    hostEdit_->setPlaceholderText(QStringLiteral("e.g. 192.168.4.1"));
    hostEdit_->setMinimumHeight(40);
    hostEdit_->setStyleSheet(QStringLiteral("QLineEdit{ font-size:16px; padding:4px 8px; }"));
    hostCol->addWidget(hostCap);
    hostCol->addWidget(hostEdit_);
    col->addWidget(hostRow_);

    // ---- Port ----
    auto* portCap = new QLabel(QStringLiteral("Port:"));
    portCap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    col->addWidget(portCap);
    portBox_ = new TouchSpinBox;
    portBox_->setRange(1, 65535);
    portBox_->setSingleStep(1);
    portBox_->setDecimals(0);
    portBox_->setValue(port > 0 ? double(port) : 10110.0);   // 10110 = IANA NMEA-0183
    col->addWidget(portBox_);

    // ---- Enable ----
    enableBox_ = new QCheckBox(QStringLiteral("Enable connection"));
    enableBox_->setChecked(enabled);
    enableBox_->setMinimumHeight(40);
    enableBox_->setStyleSheet(QStringLiteral(
        "QCheckBox{ font-size:16px; }"
        "QCheckBox::indicator{ width:24px; height:24px; }"));
    col->addWidget(enableBox_);

    col->addStretch(1);

    auto* row = new QHBoxLayout;
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"));
    auto* okBtn     = new QPushButton(QStringLiteral("OK"));
    for (QPushButton* b : {cancelBtn, okBtn}) b->setMinimumHeight(44);
    okBtn->setDefault(true);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(okBtn);
    col->addLayout(row);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn,     &QPushButton::clicked, this, &QDialog::accept);
    connect(tcpRadio_, &QRadioButton::toggled, this, [this] { updateHostVisibility(); });

    updateHostVisibility();
}

void Nmea0183Dialog::updateHostVisibility() {
    // UDP only listens on a port; the IP address is meaningless for a listener.
    hostRow_->setVisible(tcpRadio_->isChecked());
}

NmeaTransport Nmea0183Dialog::transport() const {
    return udpRadio_->isChecked() ? NmeaTransport::Udp : NmeaTransport::Tcp;
}
QString  Nmea0183Dialog::host() const    { return hostEdit_->text().trimmed(); }
quint16  Nmea0183Dialog::port() const    { return quint16(portBox_->value()); }
bool     Nmea0183Dialog::enabled() const { return enableBox_->isChecked(); }
