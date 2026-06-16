#include "signalk_plugin.hpp"
#include "signalk_client.hpp"
#include "signalk_decoder.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

namespace {
constexpr quint16 kDefaultPort = 80;
const QString kSourceId = QStringLiteral("signalk");
}  // namespace

SignalKPlugin::SignalKPlugin() = default;
SignalKPlugin::~SignalKPlugin() = default;

void SignalKPlugin::initialize(ICoreApi* core) {
    core_     = core;
    settings_ = core_->pluginSettings(kSourceId);

    client_  = std::make_unique<SignalKClient>();
    decoder_ = std::make_unique<SignalKDecoder>(core_->navPublisher(),
                                                 core_->aisPublisher(),
                                                 kSourceId);
    // Each delta frame from the server goes through the decoder, which
    // dispatches to the nav and AIS publishers.
    QObject::connect(client_.get(), &SignalKClient::messageReceived,
                     decoder_.get(), &SignalKDecoder::handleMessage);
    // One-shot snapshot from the REST API at connect time so AIS names cached
    // server-side appear immediately instead of waiting for the next static
    // report (Type 5 every 6 min; Type 24 less frequent for Class B).
    QObject::connect(client_.get(), &SignalKClient::snapshotReceived,
                     decoder_.get(), &SignalKDecoder::handleSnapshot);

    // Data Connections item (with priority entry); clicking opens our page.
    dataSource_ = core_->registerDataSource(kSourceId, QStringLiteral("Signal K"),
                                            [this] { core_->showSettingsPage(this); });

    QObject::connect(client_.get(), &SignalKClient::decodingChanged, client_.get(),
                     [this](bool on) { if (dataSource_) dataSource_->setActive(on); });

    loadConfig();
    client_->setConfig(host_, port_, enabled_);
}

void SignalKPlugin::shutdown() {
    if (dataSource_) dataSource_->setActive(false);
    client_.reset();      // stops the socket
    decoder_.reset();
}

void SignalKPlugin::loadConfig() {
    host_    = settings_->value(QStringLiteral("host")).toString();
    port_    = quint16(settings_->value(QStringLiteral("port"),
                                        int(kDefaultPort)).toUInt());
    enabled_ = settings_->value(QStringLiteral("enabled"), false).toBool();
}

void SignalKPlugin::applyConfig(const QString& host, quint16 port, bool enabled) {
    host_    = host;
    port_    = port;
    enabled_ = enabled;
    settings_->setValue(QStringLiteral("host"), host);
    settings_->setValue(QStringLiteral("port"), int(port));
    settings_->setValue(QStringLiteral("enabled"), enabled);
    if (client_) client_->setConfig(host, port, enabled);
}

QWidget* SignalKPlugin::createSettingsPage(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "Connect to a Signal K server. The plugin opens a WebSocket to "
        "ws://host:port/signalk/v1/stream and decodes navigation values and "
        "AIS targets. Default port is 80 (use 3000 for the signalk-server-node "
        "default)."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    // Host.
    auto* hostCap = new QLabel(QStringLiteral("Server IP address or hostname:"));
    hostCap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(hostCap);
    auto* hostEdit = new QLineEdit(host_);
    hostEdit->setPlaceholderText(QStringLiteral("e.g. 192.168.4.1"));
    hostEdit->setMinimumHeight(40);
    hostEdit->setStyleSheet(QStringLiteral("QLineEdit{ font-size:16px; padding:4px 8px; }"));
    col->addWidget(hostEdit);

    // Port.
    auto* portCap = new QLabel(QStringLiteral("Port:"));
    portCap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
    col->addWidget(portCap);
    auto* portBox = new TouchSpinBox;
    portBox->setRange(1, 65535);
    portBox->setSingleStep(1);
    portBox->setDecimals(0);
    portBox->setValue(port_ > 0 ? double(port_) : double(kDefaultPort));
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
    applyBtn->setMinimumHeight(44);
    btnRow->addWidget(applyBtn);
    btnRow->addStretch(1);
    col->addLayout(btnRow);

    QObject::connect(applyBtn, &QPushButton::clicked, page,
                     [this, hostEdit, portBox, enableBox] {
        applyConfig(hostEdit->text().trimmed(),
                    quint16(portBox->value()),
                    enableBox->isChecked());
    });

    return page;
}
