#pragma once
#include <QDialog>
#include <vector>

class AisTargetStore;
class QScrollArea;
class QWidget;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

// Modeless list of every tracked AIS target. Touch-first: rows are tall enough
// to tap, and the list scrolls by drag (kinetic) via QScroller on a QScrollArea
// so it behaves identically to the side menu. Columns: Name, MMSI, Call sign,
// Distance. Targets without a name or call sign show "unknown"; targets without
// a computed range show "—".
//
// Frameless, with the side-menu palette (light/dark aware) and a brand-navy
// title bar + close button, matching the chart-object chooser. The column
// headers are clickable: tapping one sorts by that column, tapping the active
// header again reverses the direction (shown by a ▲/▼ marker). Targets missing
// the sorted field always sink to the bottom regardless of direction.
//
// Rebuilt on a 250 ms coalescing timer triggered by targetUpdated/targetExpired,
// so bursts of AIS messages don't cause per-message redraws. The 1 Hz tick keeps
// the Distance column current as ownship moves between AIS updates.
//
// refresh() updates row widgets in place (reusing them, only adding/removing as
// the target count changes) so a refresh never resets the scroll position or
// flickers — critical because the list refreshes while the user may be dragging.
class AisTargetListDialog : public QDialog {
    Q_OBJECT
public:
    AisTargetListDialog(const AisTargetStore* store, QWidget* parent = nullptr);

signals:
    void targetActivated(quint32 mmsi);

private slots:
    void refresh();

private:
    enum class SortColumn { Name, Mmsi, Call, Distance };

    // One reusable row: a flat button (the tap target) holding four labels.
    struct Row {
        QPushButton* btn  = nullptr;
        QLabel*      name = nullptr;
        QLabel*      mmsi = nullptr;
        QLabel*      call = nullptr;
        QLabel*      dist = nullptr;
    };
    Row  makeRow();
    void scheduleRefresh();
    void setSort(SortColumn c);     // header click: pick column / toggle direction
    void updateHeaderLabels();      // refresh the ▲/▼ sort marker

    const AisTargetStore* store_        = nullptr;
    QScrollArea*          scrollArea_   = nullptr;
    QWidget*              rowContainer_ = nullptr;
    QVBoxLayout*          rowLayout_    = nullptr;
    QLabel*               countLabel_   = nullptr;
    QPushButton*          hdrName_      = nullptr;
    QPushButton*          hdrMmsi_      = nullptr;
    QPushButton*          hdrCall_      = nullptr;
    QPushButton*          hdrDist_      = nullptr;
    QTimer*               timer_        = nullptr;   // 1 Hz distance tick
    QTimer*               coalesce_     = nullptr;   // burst-coalescing rebuild trigger
    std::vector<Row>      rows_;                     // reused across refreshes

    SortColumn sortColumn_ = SortColumn::Distance;   // default: nearest first
    bool       sortAsc_    = true;
};
