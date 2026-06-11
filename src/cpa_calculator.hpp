#pragma once
#include <QObject>

class NavDataStore;
class AisTargetStore;
class QTimer;

// Computes CPA (closest point of approach, metres) and TCPA (time to CPA,
// seconds; < 0 once the contact is opening) for every AIS target against the
// ownship, and writes them back into the store via AisTargetStore::setCpaTcpa.
//
// It reads ownship position/COG/SOG from NavDataStore and each target's
// position/COG/SOG from AisTargetStore, treating both as straight-line tracks in
// a local east/north plane centred on the ownship. Runs on a 1 Hz tick and also
// recomputes immediately when the ownship updates, so the numbers stay live as
// either vessel moves. When the ownship fix (or a target's course/speed) is not
// available the corresponding CPA/TCPA are cleared rather than left stale.
//
// The configured own MMSI (if set) is skipped so the boat's own AIS echo never
// shows a collision with itself.
class CpaCalculator : public QObject {
    Q_OBJECT
public:
    CpaCalculator(NavDataStore* nav, AisTargetStore* ais, QObject* parent = nullptr);

public slots:
    // The user's own MMSI (0 = unknown/none); a matching target is skipped.
    void setOwnshipMmsi(quint32 mmsi);

private slots:
    void recompute();

private:
    NavDataStore*   nav_ = nullptr;
    AisTargetStore* ais_ = nullptr;
    QTimer*         timer_ = nullptr;
    quint32         ownMmsi_ = 0;
    bool            hadOwnship_ = false;   // were CPA/TCPA last published as valid?
};
