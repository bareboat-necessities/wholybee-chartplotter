#pragma once
#include <QDialog>

class AisTargetStore;
class QTableWidget;
class QTimer;

// Modeless inspector for a single AIS target, opened by clicking the vessel on
// the chart. Lists every field the store currently has (identity, voyage,
// dimensions, dynamic, CPA/TCPA, provenance). Refreshes on the store's
// targetUpdated signal plus a 1 Hz tick to keep the Age column live, and stays
// open until the user closes it — if the target ages out (Lost / removed from
// the store) the window remains, displaying the last known fields with a "lost"
// status indicator.
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
    void setRow(int row, const QString& name, const QString& value);

    quint32 mmsi_ = 0;
    const AisTargetStore* store_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTimer*       timer_ = nullptr;
    bool          lost_ = false;
};
