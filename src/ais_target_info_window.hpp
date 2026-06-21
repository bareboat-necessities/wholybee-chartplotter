#pragma once
#include "frameless_info_dialog.hpp"
#include <QHash>
#include <QString>
#include <QVector>

class AisTargetStore;
class QLabel;
class QFrame;
class QWidget;
class QGridLayout;
class QTimer;

// Modeless inspector for a single AIS target, opened by clicking the vessel on
// the chart. It shows the same dark "instrument panel" styling as the core nav
// display window and the instruments plugin, laid out for glanceability rather
// than as a scrolling list:
//
//   • a header with the vessel name, an identity subtitle (MMSI / class / type)
//     and a freshness dot (green current, amber stale, red lost);
//   • a row of compact metric tiles for the navigation-critical dynamics
//     (SOG / COG / HDG / distance / CPA / TCPA), matching the digital
//     instrument tiles;
//   • a two-column details grid for the static / voyage / position fields.
//
// Only the fields the store currently has are shown, so the window stays small
// and never needs scrolling. It refreshes on the store's targetUpdated signal
// plus a 1 Hz tick (to keep the Age field live) and stays open until closed — if
// the target ages out (Lost / removed) the window remains, displaying the last
// known fields with a "Lost" status. The widget tree is only rebuilt when the
// set of available fields changes; otherwise values update in place so a refresh
// never flickers or resizes the window.
//
// The window is frameless (FramelessInfoDialog), so it matches the dark panel
// rather than the system chrome: a custom close button sits in the header and
// the whole panel is draggable, like the instrument bar.
class AisTargetInfoWindow : public FramelessInfoDialog {
    Q_OBJECT
public:
    AisTargetInfoWindow(quint32 mmsi, const AisTargetStore* store,
                        QWidget* parent = nullptr);

    quint32 mmsi() const { return mmsi_; }

private slots:
    void onTargetUpdated(quint32 mmsi);
    void onTargetExpired(quint32 mmsi);
    void refresh();

private:
    // A navigation-critical read-out shown as a tile (caption / value / unit).
    struct Metric { QString caption, value, unit; };
    // A static/voyage/position field shown as a caption/value pair; `wide` ones
    // take a full row, the rest pack two per row.
    struct Detail { QString caption, value; bool wide = false; };

    void rebuildMetrics(const QVector<Metric>& metrics);
    void rebuildDetails(const QVector<Detail>& details);
    static void clearGrid(QGridLayout* grid);
    QFrame* makeSeparator();

    quint32 mmsi_ = 0;
    const AisTargetStore* store_ = nullptr;
    bool                  lost_  = false;
    QTimer*               timer_ = nullptr;

    QLabel*      nameLabel_     = nullptr;
    QLabel*      subtitleLabel_ = nullptr;
    QLabel*      statusDot_     = nullptr;
    QLabel*      statusLabel_   = nullptr;
    QFrame*      headerSep_     = nullptr;
    QWidget*     metricsBox_    = nullptr;
    QGridLayout* metricsGrid_   = nullptr;
    QFrame*      detailsSep_    = nullptr;
    QWidget*     detailsBox_    = nullptr;
    QGridLayout* detailsGrid_   = nullptr;

    // Signature of the currently built field set; a change triggers a rebuild.
    QString                  lastSig_;
    QHash<QString, QLabel*>  metricValues_;   // caption -> live value label
    QHash<QString, QLabel*>  detailValues_;
};
