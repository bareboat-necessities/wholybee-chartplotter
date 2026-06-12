#pragma once
#include <QDialog>

class AisTargetStore;
class QTableWidget;
class QLabel;
class QTimer;

// Modeless list of every tracked AIS target. Touch-first: rows are tall enough
// to tap, and the table scrolls by drag (kinetic) so the user can flick through
// a long list without aiming at a scrollbar. Columns: Name, MMSI, Call sign,
// Distance. Targets without a name or call sign show "unknown"; targets without
// a computed range show "—".
//
// Rebuilt on every targetUpdated/targetExpired signal and on a 1 Hz tick so the
// distance column stays current as ownship and targets move. Sorted by distance
// ascending (closest first); contacts whose distance isn't yet solvable sink to
// the bottom of the list.
class AisTargetListDialog : public QDialog {
    Q_OBJECT
public:
    AisTargetListDialog(const AisTargetStore* store, QWidget* parent = nullptr);

signals:
    // Emitted when the user taps a row. The receiver is expected to open the
    // full info window for that MMSI.
    void targetActivated(quint32 mmsi);

private slots:
    void refresh();

private:
    const AisTargetStore* store_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel*       countLabel_ = nullptr;
    QTimer*       timer_ = nullptr;
};
