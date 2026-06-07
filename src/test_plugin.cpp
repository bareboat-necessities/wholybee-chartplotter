#include "test_plugin.hpp"
#include "nav_data_store.hpp"
#include "touch_spin_box.hpp"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QRect>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QDateTime>
#include <cmath>

// ---- HelloWorldOverlay -----------------------------------------------------

void HelloWorldOverlay::paint(QPainter& p, const ChartViewport& vp) {
    if (!enabled_) return;

    // Anchor to ownship if we have a valid fix; otherwise centre it.
    QPointF anchor(vp.viewportSize().width() / 2.0, vp.viewportSize().height() / 2.0);
    if (nav_) {
        const OwnshipState& s = nav_->ownship();
        if (s.latitudeDeg.valid() && s.longitudeDeg.valid())
            anchor = vp.geoToScreen(s.latitudeDeg.value, s.longitudeDeg.value)
                     - QPointF(0, 28);   // float just above the boat
    }

    p.save();
    QFont f = p.font();
    f.setPointSize(22);
    f.setBold(true);
    p.setFont(f);
    const QString text = QStringLiteral("Hello World");
    const QFontMetrics fm(f);
    // Centre horizontally on the anchor, baseline at the anchor's y.
    const QPointF at(anchor.x() - fm.horizontalAdvance(text) / 2.0, anchor.y());
    p.setPen(QColor(255, 255, 255, 220));
    p.drawText(at + QPointF(1, 1), text);   // light halo
    p.setPen(QColor(200, 30, 30));
    p.drawText(at, text);
    p.restore();
}

// ---- DepthEntryDialog ------------------------------------------------------

DepthEntryDialog::DepthEntryDialog(const NavDataStore* store, INavDataPublisher* publisher,
                                   QWidget* parent)
    : QDialog(parent), store_(store), publisher_(publisher) {
    setWindowTitle(QStringLiteral("Publish Depth"));
    resize(420, 240);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    current_ = new QLabel;
    current_->setWordWrap(true);
    current_->setStyleSheet(QStringLiteral("font-size:14px;"));
    col->addWidget(current_);

    auto* cap = new QLabel(QStringLiteral("New depth value:"));
    cap->setStyleSheet(QStringLiteral("font-size:13px; color:#444;"));
    col->addWidget(cap);

    auto* row = new QHBoxLayout;
    input_ = new TouchSpinBox;
    input_->setRange(0.0, 2000.0);
    input_->setSingleStep(0.5);
    input_->setDecimals(1);
    input_->setSuffix(QStringLiteral(" m"));
    input_->setValue(10.0);
    row->addWidget(input_, 1);
    auto* publishBtn = new QPushButton(QStringLiteral("Publish"));
    publishBtn->setMinimumHeight(56);
    row->addWidget(publishBtn);
    col->addLayout(row);

    col->addStretch(1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setMinimumHeight(40);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    footer->addWidget(closeBtn);
    col->addLayout(footer);

    connect(publishBtn, &QPushButton::clicked, this, &DepthEntryDialog::publish);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::close);
    if (store_)
        connect(store_, &NavDataStore::ownshipChanged, this, &DepthEntryDialog::refresh);

    // Keep the Age value live even with no new data.
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &DepthEntryDialog::refresh);
    timer_->start();

    refresh();
}

void DepthEntryDialog::refresh() {
    if (!store_) return;
    const NavValue& d = store_->ownship().depthMeters;
    if (!d.valid()) {
        current_->setText(QStringLiteral("Current depth: —  (no data)"));
        current_->setStyleSheet(QStringLiteral("font-size:14px; color:#999;"));
        return;
    }
    const double age = d.timestampUtc.isValid()
        ? d.timestampUtc.msecsTo(QDateTime::currentDateTimeUtc()) / 1000.0 : 0.0;
    current_->setText(QStringLiteral("Current depth: %1 m\nSource: %2     Age: %3 s%4")
        .arg(d.value, 0, 'f', 1)
        .arg(d.source.isEmpty() ? QStringLiteral("—") : d.source)
        .arg(age, 0, 'f', 1)
        .arg(d.stale() ? QStringLiteral("   (stale)") : QString()));
    current_->setStyleSheet(d.stale()
        ? QStringLiteral("font-size:14px; color:#999;")
        : QStringLiteral("font-size:14px; color:#111;"));
}

void DepthEntryDialog::publish() {
    if (!publisher_) return;
    NavValueMeta m;
    m.source = QStringLiteral("test-plugin");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    publisher_->publishDepth(input_->value(), m);
    refresh();
}

// ---- TestPluginSettingsDialog ----------------------------------------------

TestPluginSettingsDialog::TestPluginSettingsDialog(bool enabled,
                                                   std::function<void(bool)> onToggled,
                                                   QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Test Plugin"));
    resize(360, 160);
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);
    col->setSpacing(14);

    auto* intro = new QLabel(QStringLiteral(
        "When enabled, the Test Plugin acts as a navigation data source, "
        "publishing a depth value once per second."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    auto* box = new QCheckBox(QStringLiteral("Enable as data source"));
    box->setChecked(enabled);
    box->setMinimumHeight(40);
    box->setStyleSheet(QStringLiteral(
        "QCheckBox{ font-size:16px; }"
        "QCheckBox::indicator{ width:24px; height:24px; }"));
    connect(box, &QCheckBox::toggled, this,
            [fn = std::move(onToggled)](bool on) { if (fn) fn(on); });
    col->addWidget(box);

    col->addStretch(1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setMinimumHeight(40);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    footer->addWidget(closeBtn);
    col->addLayout(footer);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

// ---- TestPlugin ------------------------------------------------------------

TestPlugin::TestPlugin() = default;
TestPlugin::~TestPlugin() = default;

void TestPlugin::initialize(ICoreApi* core) {
    core_ = core;

    overlay_ = std::make_unique<HelloWorldOverlay>(core_->navData());
    core_->addChartOverlay(overlay_.get());

    // Checkable item: toggles the Hello World overlay.
    core_->addMenuToggle(QStringLiteral("Hello World"), false, [this](bool on) {
        if (overlay_) overlay_->setEnabled(on);
        core_->requestChartRepaint();
    });

    // Plain item: opens the depth publish/inspect dialog (modeless, self-deleting).
    core_->addMenuAction(QStringLiteral("Publish Depth…"), [this] {
        auto* dlg = new DepthEntryDialog(core_->navData(), core_->navPublisher(),
                                         core_->dialogParent());
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });

    // Register as a data source: the core adds a Data Connections item whose
    // status dot we drive, and routes clicks to our settings dialog.
    dataSource_ = core_->registerDataSource(QStringLiteral("Test Plugin"),
                                            [this] { openSettings(); });

    publishTimer_ = std::make_unique<QTimer>();
    publishTimer_->setInterval(1000);   // 1 Hz depth stream while enabled
    QObject::connect(publishTimer_.get(), &QTimer::timeout, publishTimer_.get(),
                     [this] { publishDepthTick(); });
}

void TestPlugin::openSettings() {
    auto* dlg = new TestPluginSettingsDialog(
        sourceEnabled_, [this](bool on) { setSourceEnabled(on); }, core_->dialogParent());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void TestPlugin::setSourceEnabled(bool on) {
    if (on == sourceEnabled_) return;
    sourceEnabled_ = on;
    if (dataSource_) dataSource_->setActive(on);   // green dot
    if (on) publishTimer_->start();
    else    publishTimer_->stop();
}

void TestPlugin::publishDepthTick() {
    depthPhase_ += 0.3;
    const double depth = 12.0 + 3.0 * std::sin(depthPhase_);   // visibly varying
    NavValueMeta m;
    m.source = QStringLiteral("test-plugin");
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    core_->navPublisher()->publishDepth(depth, m);
}

void TestPlugin::shutdown() {
    if (publishTimer_) publishTimer_->stop();
    if (dataSource_) dataSource_->setActive(false);
    if (core_ && overlay_) core_->removeChartOverlay(overlay_.get());
    overlay_.reset();
}
