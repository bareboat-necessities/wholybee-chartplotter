#pragma once
#include <QDialog>
#include <vector>

class AisTargetStore;
class QScrollArea;
class QWidget;
class QLabel;
class QVBoxLayout;
class QTimer;

// Modeless inspector for a single AIS target, opened by clicking the vessel on
// the chart. Lists every field the store currently has (identity, voyage,
// dimensions, dynamic, CPA/TCPA, provenance). Refreshes on the store's
// targetUpdated signal plus a 1 Hz tick to keep the Age column live, and stays
// open until the user closes it — if the target ages out (Lost / removed from
// the store) the window remains, displaying the last known fields with a "lost"
// status indicator.
//
// Rows live in a QScrollArea so the list scrolls by drag (kinetic) via QScroller,
// matching the side menu and the AIS target list. refresh() updates row widgets
// in place (reusing them, adding/removing only as the field count changes) so a
// refresh never resets the scroll position or flickers.
class AisTargetInfoWindow : public QDialog {
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
    // One reusable field/value row.
    struct Row {
        QWidget* widget = nullptr;
        QLabel*  field  = nullptr;
        QLabel*  value  = nullptr;
    };
    Row makeRow();

    quint32 mmsi_ = 0;
    const AisTargetStore* store_        = nullptr;
    QScrollArea*          scrollArea_   = nullptr;
    QWidget*              rowContainer_ = nullptr;
    QVBoxLayout*          rowLayout_    = nullptr;
    QTimer*               timer_        = nullptr;
    bool                  lost_         = false;
    std::vector<Row>      rows_;
};
