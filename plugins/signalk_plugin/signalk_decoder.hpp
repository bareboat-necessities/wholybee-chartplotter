#pragma once
#include <QObject>
#include <QHash>
#include <QString>
#include <optional>

class INavDataPublisher;
class IAisPublisher;

// Decodes Signal K delta JSON into nav-data and AIS publishes.
//
// One decoder instance per plugin. The plugin feeds it raw text frames (one
// Signal K delta JSON object per frame) via handleMessage(); the decoder
// dispatches by `context` -- "vessels.self" goes through the nav publisher,
// "vessels.urn:mrn:imo:mmsi:NNN" through the AIS publisher.
//
// SI units on the wire: angles in radians, speeds in m/s, depths in metres,
// positions in decimal degrees. Coupled values (COG with SOG, wind angle with
// wind speed) often arrive in separate deltas; the decoder caches the latest of
// each so the publisher only sees fully-formed pairs.
class SignalKDecoder : public QObject {
    Q_OBJECT
public:
    SignalKDecoder(INavDataPublisher* nav, IAisPublisher* ais,
                   QString sourceId, QObject* parent = nullptr);

public slots:
    void handleMessage(const QString& json);
    // Full vessel-tree snapshot from the REST API (see SignalKClient::fetchSnapshot).
    // Walks every vessel under /vessels and synthesizes "name" / static-field
    // updates so AIS targets pick up names cached server-side even if no new
    // AIS static report arrives on the stream.
    void handleSnapshot(const QString& json);

private:
    // Per-vessel scratchpad for stitching paired values (COG/SOG, wind a/s).
    struct VesselCache {
        std::optional<double> cogDegTrue;
        std::optional<double> sogKnots;
        std::optional<double> apparentWindAngleDeg;
        std::optional<double> apparentWindSpeedKnots;
        std::optional<double> trueWindAngleDeg;
        std::optional<double> trueWindSpeedKnots;
        std::optional<double> trueWindDirectionDeg;
        // AIS static fields are sticky too: name and ship type can arrive in
        // their own deltas; cache so we only publish a static report when at
        // least the name is known (saves churn in the AIS target store).
        std::optional<QString> aisName;
        std::optional<QString> aisCallSign;
        std::optional<int>     aisShipType;
        std::optional<int>     aisImo;
        std::optional<int>     aisClass;   // 0 = A, 1 = B
        std::optional<double>  dimToBow;
        std::optional<double>  dimToStern;
        std::optional<double>  dimToPort;
        std::optional<double>  dimToStarboard;
    };

    void handleUpdate(const QString& context,
                      const struct QJsonObject& updateObj);
    void handleSelfValue(const QString& path,
                         const class QJsonValue& value);
    void handleAisValue(quint32 mmsi, const QString& path,
                        const class QJsonValue& value);

    INavDataPublisher* nav_ = nullptr;
    IAisPublisher*     ais_ = nullptr;
    QString            sourceId_;
    // The Signal K "hello" message tells us which vessel is "self" — usually a
    // URN like "vessels.urn:mrn:imo:mmsi:367123456". Subsequent delta updates
    // about own ship arrive with that URN as the context, not the literal
    // "vessels.self", so we remember it here and route those to nav.
    QString            selfContext_;
    VesselCache        selfCache_;
    QHash<quint32, VesselCache> aisCache_;  // keyed by MMSI
};
