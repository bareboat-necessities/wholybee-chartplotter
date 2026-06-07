#include "nmea0183_plugin.hpp"
#include "nmea0183_debug_window.hpp"
#include "touch_spin_box.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

Nmea0183Plugin::Nmea0183Plugin() = default;
Nmea0183Plugin::~Nmea0183Plugin() = default;

void Nmea0183Plugin::initialize(ICoreApi* core) {
    core_ = core;
    settings_ = core_->pluginSettings(QStringLiteral("nmea0183"));

    client_ = std::make_unique<Nmea0183Client>(core_->navPublisher());

    // Register as a data source: Data Connections item + Data Priority entry,
    // clicking opens our settings page.
    dataSource_ = core_->registerDataSource(QStringLiteral("nmea0183"),
                                            QStringLiteral("NMEA 0183"),
                                            [this] { core_->showSettingsPage(this); });

    // The green dot follows the decoding status.
    QObject::connect(client_.get(), &Nmea0183Client::decodingChanged, client_.get(),
                     [this](bool on) { if (dataSource_) dataSource_->setActive(on); });

    loadConfig();
    client_->setConfig(transport_, host_, port_, enabled_);
}

void Nmea0183Plugin::shutdown() {
    if (dataSource_) dataSource_->setActive(false);
    client_.reset();      // stops the socket; disconnects everything
    if (debug_) debug_->deleteLater();
}

void Nmea0183Plugin::loadConfig() {
    // One-time migration from the old core Settings keys ("nmea0183/*") so a
    // previously configured gateway keeps working after the refactor.
    if (!settings_->value(QStringLiteral("transport")).isValid()) {
        QSettings legacy;
        if (legacy.contains(QStringLiteral("nmea0183/transport"))) {
            settings_->setValue(QStringLiteral("transport"),
                                legacy.value(QStringLiteral("nmea0183/transport")));
            settings_->setValue(QStringLiteral("host"),
                                legacy.value(QStringLiteral("nmea0183/host")));
            settings_->setValue(QStringLiteral("port"),
                                legacy.value(QStringLiteral("nmea0183/port"), 10110));
            settings_->setValue(QStringLiteral("enabled"),
                                legacy.value(QStringLiteral("nmea0183/enabled"), false));
        }
    }
    transport_ = (settings_->value(QStringLiteral("transport"), QStringLiteral("tcp")).toString()
                      == QLatin1String("udp")) ? NmeaTransport::Udp : NmeaTransport::Tcp;
    host_     = settings_->value(QStringLiteral("host")).toString();
    port_     = quint16(settings_->value(QStringLiteral("port"), 10110).toUInt());
    enabled_  = settings_->value(QStringLiteral("enabled"), false).toBool();
}

void Nmea0183Plugin::applyConfig(NmeaTransport transport, const QString& host,
                                 quint16 port, bool enabled) {
    transport_ = transport;
    host_ = host;
    port_ = port;
    enabled_ = enabled;
    settings_->setValue(QStringLiteral("transport"),
                        transport == NmeaTransport::Udp ? QStringLiteral("udp")
                                                        : QStringLiteral("tcp"));
    settings_->setValue(QStringLiteral("host"), host);
    settings_->setValue(QStringLiteral("port"), int(port));
    settings_->setValue(QStringLiteral("enabled"), enabled);
    if (client_) client_->setConfig(transport, host, port, enabled);
}

void Nmea0183Plugin::showDebugWindow() {
    if (!debug_) {
        debug_ = new Nmea0183DebugWindow(core_->dialogParent());
        QObject::connect(client_.get(), &Nmea0183Client::sentenceReceived,
                         debug_.data(), &Nmea0183DebugWindow::appendLine);
    }
    debug_->show();
    debug_->raise();
    debug_->activateWindow();
}

QWidget* Nmea0183Plugin::createSettingsPage(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Connect to a WiFi gateway that broadcasts NMEA 0183 data over the network."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    // Connection type.
    auto* typeBox = new QGroupBox(QStringLiteral("Connection Type"));
    auto* typeRow = new QHBoxLayout(typeBox);
    auto* tcp = new QRadioButton(QStringLiteral("TCP"));
    auto* udp = new QRadioButton(QStringLiteral("UDP"));
    for (QRadioButton* r : {tcp, udp}) {
        r->setMinimumHeight(40);
        r->setStyleSheet(QStringLiteral("QRadioButton{ font-size:15px; }"));
    }
    (transport_ == NmeaTransport::Udp ? udp : tcp)->setChecked(true);
    typeRow->addWidget(tcp);
    typeRow->addWidget(udp);
    typeRow->addStretch(1);
    col->addWidget(typeBox);

    // Host (TCP only).
    auto* hostRow = new QWidget;
    auto* hostCol = new QVBoxLayout(hostRow);
    hostCol->setContentsMargins(0, 0, 0, 0);
    hostCol->setSpacing(4);
    auto* hostCap = new QLabel(QStringLiteral("Gateway IP address:"));
    hostCap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    auto* hostEdit = new QLineEdit(host_);
    hostEdit->setPlaceholderText(QStringLiteral("e.g. 192.168.4.1"));
    hostEdit->setMinimumHeight(40);
    hostEdit->setStyleSheet(QStringLiteral("QLineEdit{ font-size:16px; padding:4px 8px; }"));
    hostCol->addWidget(hostCap);
    hostCol->addWidget(hostEdit);
    col->addWidget(hostRow);
    hostRow->setVisible(tcp->isChecked());
    QObject::connect(tcp, &QRadioButton::toggled, hostRow,
                     [hostRow](bool on) { hostRow->setVisible(on); });

    // Port.
    auto* portCap = new QLabel(QStringLiteral("Port:"));
    portCap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    col->addWidget(portCap);
    auto* portBox = new TouchSpinBox;
    portBox->setRange(1, 65535);
    portBox->setSingleStep(1);
    portBox->setDecimals(0);
    portBox->setValue(port_ > 0 ? double(port_) : 10110.0);
    col->addWidget(portBox);

    // Enable.
    auto* enableBox = new QCheckBox(QStringLiteral("Enable connection"));
    enableBox->setChecked(enabled_);
    enableBox->setMinimumHeight(40);
    enableBox->setStyleSheet(QStringLiteral(
        "QCheckBox{ font-size:16px; }"
        "QCheckBox::indicator{ width:24px; height:24px; }"));
    col->addWidget(enableBox);

    // Actions.
    auto* btnRow = new QHBoxLayout;
    auto* applyBtn = new QPushButton(QStringLiteral("Apply"));
    auto* rawBtn   = new QPushButton(QStringLiteral("Show Raw Data…"));
    for (QPushButton* b : {applyBtn, rawBtn}) b->setMinimumHeight(44);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(rawBtn);
    btnRow->addStretch(1);
    col->addLayout(btnRow);

    QObject::connect(applyBtn, &QPushButton::clicked, page,
                     [this, tcp, udp, hostEdit, portBox, enableBox] {
        const NmeaTransport t = udp->isChecked() ? NmeaTransport::Udp : NmeaTransport::Tcp;
        applyConfig(t, hostEdit->text().trimmed(),
                    quint16(portBox->value()), enableBox->isChecked());
    });
    QObject::connect(rawBtn, &QPushButton::clicked, page, [this] { showDebugWindow(); });

    return page;
}
