#include "nmea2000_plugin.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

Nmea2000Plugin::Nmea2000Plugin() = default;
Nmea2000Plugin::~Nmea2000Plugin() = default;

void Nmea2000Plugin::initialize(ICoreApi* core) {
    core_ = core;
    settings_ = core_->pluginSettings(QStringLiteral("nmea2000"));

    // Hooks into the same nav + AIS publisher contracts the 0183 client uses,
    // so per-value source/timestamp arbitration, AIS target store updates,
    // and Data Priority all work without any extra wiring.
    client_ = std::make_unique<Nmea2000Client>(core_->navPublisher(),
                                               core_->aisPublisher());

    dataSource_ = core_->registerDataSource(QStringLiteral("nmea2000"),
                                            QStringLiteral("NMEA 2000"),
                                            [this] { core_->showSettingsPage(this); });

    QObject::connect(client_.get(), &Nmea2000Client::decodingChanged, client_.get(),
                     [this](bool on) { if (dataSource_) dataSource_->setActive(on); });

    loadConfig();
    client_->setConfig(transport_, format_, host_, port_, enabled_);
}

void Nmea2000Plugin::shutdown() {
    if (dataSource_) dataSource_->setActive(false);
    client_.reset();
}

void Nmea2000Plugin::loadConfig() {
    transport_ = (settings_->value(QStringLiteral("transport"), QStringLiteral("tcp")).toString()
                      == QLatin1String("udp")) ? N2kTransport::Udp : N2kTransport::Tcp;
    // Only one format today; the stored string lets us add more without a
    // schema change.
    const QString fmt = settings_->value(QStringLiteral("format"),
                                         QStringLiteral("actisense-ascii")).toString();
    format_ = (fmt == QLatin1String("actisense-ascii")) ? N2kFormat::ActisenseAscii
                                                       : N2kFormat::ActisenseAscii;
    host_     = settings_->value(QStringLiteral("host")).toString();
    port_     = quint16(settings_->value(QStringLiteral("port"), 2598).toUInt());
    enabled_  = settings_->value(QStringLiteral("enabled"), false).toBool();
}

void Nmea2000Plugin::applyConfig(N2kTransport transport, N2kFormat format,
                                 const QString& host, quint16 port, bool enabled) {
    transport_ = transport;
    format_    = format;
    host_      = host;
    port_      = port;
    enabled_   = enabled;
    settings_->setValue(QStringLiteral("transport"),
                        transport == N2kTransport::Udp ? QStringLiteral("udp")
                                                       : QStringLiteral("tcp"));
    settings_->setValue(QStringLiteral("format"),
                        format == N2kFormat::ActisenseAscii
                            ? QStringLiteral("actisense-ascii")
                            : QStringLiteral("actisense-ascii"));
    settings_->setValue(QStringLiteral("host"), host);
    settings_->setValue(QStringLiteral("port"), int(port));
    settings_->setValue(QStringLiteral("enabled"), enabled);
    if (client_) client_->setConfig(transport, format, host, port, enabled);
}

QWidget* Nmea2000Plugin::createSettingsPage(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Connect to an NMEA 2000 gateway (e.g. Actisense W2K-1) that streams "
        "PGN data over the network."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    // Wire format. Only one option today; laid out so a second radio drops in
    // alongside without rework.
    auto* fmtBox = new QGroupBox(QStringLiteral("Data Format"));
    auto* fmtCol = new QVBoxLayout(fmtBox);
    auto* actisenseAscii = new QRadioButton(QStringLiteral("Actisense N2K ASCII"));
    actisenseAscii->setMinimumHeight(40);
    actisenseAscii->setStyleSheet(QStringLiteral("QRadioButton{ font-size:15px; }"));
    actisenseAscii->setChecked(format_ == N2kFormat::ActisenseAscii);
    fmtCol->addWidget(actisenseAscii);
    col->addWidget(fmtBox);

    // Transport.
    auto* typeBox = new QGroupBox(QStringLiteral("Connection Type"));
    auto* typeRow = new QHBoxLayout(typeBox);
    auto* tcp = new QRadioButton(QStringLiteral("TCP"));
    auto* udp = new QRadioButton(QStringLiteral("UDP"));
    for (QRadioButton* r : {tcp, udp}) {
        r->setMinimumHeight(40);
        r->setStyleSheet(QStringLiteral("QRadioButton{ font-size:15px; }"));
    }
    (transport_ == N2kTransport::Udp ? udp : tcp)->setChecked(true);
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
    hostCap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
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
    portCap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(portCap);
    auto* portBox = new TouchSpinBox;
    portBox->setRange(1, 65535);
    portBox->setSingleStep(1);
    portBox->setDecimals(0);
    portBox->setValue(port_ > 0 ? double(port_) : 2598.0);
    col->addWidget(portBox);

    // Enable.
    auto* enableBox = new QCheckBox(QStringLiteral("Enable connection"));
    enableBox->setChecked(enabled_);
    enableBox->setMinimumHeight(40);
    enableBox->setStyleSheet(QStringLiteral(
        "QCheckBox{ font-size:16px; }"
        "QCheckBox::indicator{ width:24px; height:24px; }"));
    col->addWidget(enableBox);

    // Apply.
    auto* btnRow = new QHBoxLayout;
    auto* applyBtn = new QPushButton(QStringLiteral("Apply"));
    applyBtn->setMinimumHeight(44);
    btnRow->addWidget(applyBtn);
    btnRow->addStretch(1);
    col->addLayout(btnRow);

    QObject::connect(applyBtn, &QPushButton::clicked, page,
                     [this, tcp, udp, actisenseAscii, hostEdit, portBox, enableBox] {
        const N2kTransport t = udp->isChecked() ? N2kTransport::Udp : N2kTransport::Tcp;
        const N2kFormat f = actisenseAscii->isChecked() ? N2kFormat::ActisenseAscii
                                                        : N2kFormat::ActisenseAscii;
        applyConfig(t, f, hostEdit->text().trimmed(),
                    quint16(portBox->value()), enableBox->isChecked());
    });

    return page;
}
