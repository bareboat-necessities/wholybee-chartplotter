#include "instruments_settings_page.hpp"
#include "instruments_plugin.hpp"
#include "instrument_config.hpp"
#include "touch_spin_box.hpp"
#include "theme.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QHash>

namespace {
QHash<QString, InstrumentDef> byId(const QList<InstrumentDef>& defs) {
    QHash<QString, InstrumentDef> m;
    for (const InstrumentDef& d : defs) m.insert(d.id, d);
    return m;
}
}  // namespace

InstrumentsSettingsPage::InstrumentsSettingsPage(InstrumentsPlugin* plugin, QWidget* parent)
    : QWidget(parent), plugin_(plugin) {
    order_ = plugin_->order();
    const QStringList sel = plugin_->selected();
    selected_ = QSet<QString>(sel.begin(), sel.end());

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(12);

    auto* intro = new QLabel(QStringLiteral(
        "Choose which instruments appear on the bar and in what order. The list "
        "of available instruments is defined in instruments.xml; edit that file "
        "to add or change instruments."));
    intro->setWordWrap(true);
    col->addWidget(intro);

    // ---- Orientation -------------------------------------------------------
    {
        auto* cap = new QLabel(QStringLiteral("Orientation:"));
        cap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
        col->addWidget(cap);

        auto* row = new QHBoxLayout;
        auto* horiz = new QRadioButton(QStringLiteral("Horizontal"));
        auto* vert  = new QRadioButton(QStringLiteral("Vertical"));
        for (QRadioButton* b : { horiz, vert }) {
            b->setMinimumHeight(40);
            b->setStyleSheet(QStringLiteral(
                "QRadioButton{ font-size:15px; }"
                "QRadioButton::indicator{ width:24px; height:24px; }"));
        }
        horiz->setChecked(plugin_->isHorizontal());
        vert->setChecked(!plugin_->isHorizontal());
        auto* group = new QButtonGroup(this);
        group->addButton(horiz);
        group->addButton(vert);
        row->addWidget(horiz);
        row->addWidget(vert);
        row->addStretch(1);
        col->addLayout(row);

        connect(horiz, &QRadioButton::toggled, this, [this](bool on) {
            if (plugin_) plugin_->setHorizontal(on);
        });
    }

    // ---- Scale -------------------------------------------------------------
    {
        auto* cap = new QLabel(QStringLiteral("Size:"));
        cap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
        col->addWidget(cap);

        auto* scaleBox = new TouchSpinBox;
        scaleBox->setRange(0.5, 3.0);
        scaleBox->setSingleStep(0.1);
        scaleBox->setDecimals(1);
        scaleBox->setSuffix(QStringLiteral("×"));
        scaleBox->setValue(plugin_->scale());
        col->addWidget(scaleBox);

        connect(scaleBox, &TouchSpinBox::valueChanged, this, [this](double v) {
            if (plugin_) plugin_->setScale(v);
        });
    }

    // ---- Instrument list ---------------------------------------------------
    {
        auto* cap = new QLabel(QStringLiteral("Instruments (tick to show, use ▲▼ to reorder):"));
        cap->setWordWrap(true);
        cap->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(theme::textMuted()));
        col->addWidget(cap);

        // Scrollable, drag-to-scroll list — same pattern as the AIS target list.
        auto* scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(220);

        rowContainer_ = new QWidget;
        rowLayout_ = new QVBoxLayout(rowContainer_);
        rowLayout_->setContentsMargins(0, 0, 0, 0);
        rowLayout_->setSpacing(0);
        rowLayout_->addStretch(1);            // keep rows packed to the top
        scroll->setWidget(rowContainer_);

        QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);
        col->addWidget(scroll, 1);
    }

    rebuildList();
}

void InstrumentsSettingsPage::rebuildList() {
    if (!rowLayout_) return;

    // Clear existing rows (everything before the trailing stretch).
    while (rowLayout_->count() > 1) {
        QLayoutItem* it = rowLayout_->takeAt(0);
        if (QWidget* w = it->widget()) w->deleteLater();
        delete it;
    }

    const QHash<QString, InstrumentDef> defs = byId(plugin_->catalog());

    for (int i = 0; i < order_.size(); ++i) {
        const QString id = order_.at(i);
        const auto dit = defs.constFind(id);
        if (dit == defs.constEnd()) continue;   // stale id, skip

        auto* rowW = new QWidget(rowContainer_);
        rowW->setStyleSheet(QStringLiteral(
            "QWidget{ border-bottom:1px solid palette(mid); }"));
        auto* hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(4, 2, 4, 2);
        hl->setSpacing(6);

        auto* check = new QCheckBox(dit.value().name, rowW);
        check->setChecked(selected_.contains(id));
        check->setMinimumHeight(44);
        check->setStyleSheet(QStringLiteral(
            "QCheckBox{ font-size:15px; border:none; }"
            "QCheckBox::indicator{ width:24px; height:24px; }"));
        connect(check, &QCheckBox::toggled, this, [this, id](bool on) {
            if (on) selected_.insert(id); else selected_.remove(id);
            pushToPlugin();
        });
        hl->addWidget(check, 1);

        auto* up = new QPushButton(QStringLiteral("▲"), rowW);
        auto* down = new QPushButton(QStringLiteral("▼"), rowW);
        for (QPushButton* b : { up, down }) {
            b->setFixedSize(44, 44);
            b->setStyleSheet(QStringLiteral("QPushButton{ font-size:16px; border:none; }"
                "QPushButton:pressed{ background:palette(highlight); }"
                "QPushButton:disabled{ color:palette(mid); }"));
        }
        up->setEnabled(i > 0);
        down->setEnabled(i < order_.size() - 1);
        connect(up,   &QPushButton::clicked, this, [this, i] { moveRow(i, -1); });
        connect(down, &QPushButton::clicked, this, [this, i] { moveRow(i, +1); });
        hl->addWidget(up);
        hl->addWidget(down);

        rowLayout_->insertWidget(i, rowW);
    }
}

void InstrumentsSettingsPage::moveRow(int index, int delta) {
    const int to = index + delta;
    if (index < 0 || index >= order_.size() || to < 0 || to >= order_.size()) return;
    order_.move(index, to);
    rebuildList();
    pushToPlugin();
}

void InstrumentsSettingsPage::pushToPlugin() {
    if (!plugin_) return;
    QStringList selectedOrdered;
    for (const QString& id : order_)
        if (selected_.contains(id)) selectedOrdered.push_back(id);
    plugin_->applyInstruments(order_, selectedOrdered);
}
