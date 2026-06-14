#include "signalk_decoder.hpp"
#include "nav_data_store.hpp"
#include "ais_target_store.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QRegularExpression>
#include <cmath>

namespace {
constexpr double kRadToDeg  = 57.29577951308232;     // 180/pi
constexpr double kMsToKnots = 1.9438444924406046;    // 1 m/s -> knots

double wrap360(double d) {
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

NavValueMeta makeMeta(const QString& source) {
    NavValueMeta m;
    m.source = source;
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    return m;
}

// Pull an MMSI integer out of a Signal K vessel context like
// "vessels.urn:mrn:imo:mmsi:367123456". Returns 0 if not present, so callers
// can drop messages that don't carry an MMSI (e.g. our own vessel, or atoNs).
quint32 contextMmsi(const QString& ctx) {
    static const QRegularExpression re(QStringLiteral("mmsi[:.](\\d{6,9})"));
    const auto m = re.match(ctx);
    return m.hasMatch() ? m.captured(1).toUInt() : 0;
}
}  // namespace

SignalKDecoder::SignalKDecoder(INavDataPublisher* nav, IAisPublisher* ais,
                               QString sourceId, QObject* parent)
    : QObject(parent), nav_(nav), ais_(ais), sourceId_(std::move(sourceId)) {}

// Each leaf in the REST snapshot is either a raw value or an object wrapping
// {value, timestamp, ...}. This unwraps that wrapper so a snapshot path that
// stores "name" as {"value":"...","timestamp":"..."} reads the same as a delta
// that sends "name" as a plain string.
static QJsonValue unwrapLeaf(const QJsonValue& v) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("value"))) return o.value(QStringLiteral("value"));
    }
    return v;
}

void SignalKDecoder::handleSnapshot(const QString& json) {
    if (!ais_) return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject root = doc.object();
    // The tree under /vessels is keyed by URN ("urn:mrn:imo:mmsi:367123456").
    // Each vessel object holds nested groups (name, design, registrations, ...)
    // mirroring the delta path tree. Feed the same handler used for live deltas
    // so the cache stitching and publish guards apply consistently.
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString key = it.key();
        if (!it.value().isObject()) continue;
        const quint32 mmsi = contextMmsi(key);
        if (mmsi == 0) continue;     // skip "self" or atoNs without an MMSI
        const QJsonObject v = it.value().toObject();

        // Common static-data leaves. unwrapLeaf handles both {value: x} and x.
        if (v.contains(QStringLiteral("name")))
            handleAisValue(mmsi, QStringLiteral("name"), unwrapLeaf(v.value(QStringLiteral("name"))));
        if (v.contains(QStringLiteral("communication"))) {
            const QJsonObject cmt = v.value(QStringLiteral("communication")).toObject();
            if (cmt.contains(QStringLiteral("callsignVhf")))
                handleAisValue(mmsi, QStringLiteral("communication.callsignVhf"),
                               unwrapLeaf(cmt.value(QStringLiteral("callsignVhf"))));
        }
        if (v.contains(QStringLiteral("design"))) {
            const QJsonObject des = v.value(QStringLiteral("design")).toObject();
            if (des.contains(QStringLiteral("aisShipType")))
                handleAisValue(mmsi, QStringLiteral("design.aisShipType"),
                               unwrapLeaf(des.value(QStringLiteral("aisShipType"))));
            if (des.contains(QStringLiteral("length")))
                handleAisValue(mmsi, QStringLiteral("design.length"),
                               unwrapLeaf(des.value(QStringLiteral("length"))));
            if (des.contains(QStringLiteral("beam")))
                handleAisValue(mmsi, QStringLiteral("design.beam"),
                               unwrapLeaf(des.value(QStringLiteral("beam"))));
        }
        if (v.contains(QStringLiteral("registrations"))) {
            const QJsonObject reg = v.value(QStringLiteral("registrations")).toObject();
            if (reg.contains(QStringLiteral("imo")))
                handleAisValue(mmsi, QStringLiteral("registrations.imo"),
                               unwrapLeaf(reg.value(QStringLiteral("imo"))));
        }
        if (v.contains(QStringLiteral("sensors"))) {
            const QJsonObject sen = v.value(QStringLiteral("sensors")).toObject();
            const QJsonObject ais = sen.value(QStringLiteral("ais")).toObject();
            if (ais.contains(QStringLiteral("class")))
                handleAisValue(mmsi, QStringLiteral("sensors.ais.class"),
                               unwrapLeaf(ais.value(QStringLiteral("class"))));
        }
    }
}

void SignalKDecoder::handleMessage(const QString& json) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject root = doc.object();
    // Server hello: `{"version":..., "self":"vessels.urn:...:mmsi:NNN", ...}`.
    // Delta updates for own ship use that URN as their context, not the
    // literal "vessels.self", so we remember it here and use it to route deltas.
    // The spec allows the value with or without a leading "vessels." — normalize
    // by storing exactly what the server sends and matching both forms below.
    if (root.contains(QStringLiteral("self"))) {
        const QString s = root.value(QStringLiteral("self")).toString();
        if (!s.isEmpty()) selfContext_ = s;
    }
    // Delta messages carry `updates`; ignore hello / subscribe acks here.
    const QJsonArray updates = root.value(QStringLiteral("updates")).toArray();
    if (updates.isEmpty()) return;
    const QString context = root.value(QStringLiteral("context"))
                                 .toString(QStringLiteral("vessels.self"));
    for (const QJsonValue& u : updates) {
        if (u.isObject()) handleUpdate(context, u.toObject());
    }
}

void SignalKDecoder::handleUpdate(const QString& context, const QJsonObject& obj) {
    const QJsonArray values = obj.value(QStringLiteral("values")).toArray();
    // Self test: the literal "vessels.self"; or the URN the hello announced
    // (with or without the "vessels." prefix, since servers vary).
    auto isSelfContext = [&](const QString& c) {
        if (c == QLatin1String("vessels.self")) return true;
        if (c.startsWith(QLatin1String("self"))) return true;
        if (selfContext_.isEmpty()) return false;
        if (c == selfContext_) return true;
        // hello "self": "urn:..."  vs  context "vessels.urn:..."  (or vice versa)
        if (c == QStringLiteral("vessels.") + selfContext_) return true;
        if (QStringLiteral("vessels.") + c == selfContext_) return true;
        return false;
    };
    const bool isSelf = isSelfContext(context);
    const quint32 mmsi = isSelf ? 0 : contextMmsi(context);

    for (const QJsonValue& v : values) {
        if (!v.isObject()) continue;
        const QJsonObject vo = v.toObject();
        const QString path = vo.value(QStringLiteral("path")).toString();
        const QJsonValue value = vo.value(QStringLiteral("value"));
        if (path.isEmpty()) continue;
        if (isSelf)         handleSelfValue(path, value);
        else if (mmsi != 0) handleAisValue(mmsi, path, value);
    }
}

// ---- ownship --------------------------------------------------------------

void SignalKDecoder::handleSelfValue(const QString& path, const QJsonValue& value) {
    if (!nav_) return;
    const NavValueMeta meta = makeMeta(sourceId_);

    if (path == QLatin1String("navigation.position") && value.isObject()) {
        const QJsonObject p = value.toObject();
        const double lat = p.value(QStringLiteral("latitude")).toDouble();
        const double lon = p.value(QStringLiteral("longitude")).toDouble();
        nav_->publishOwnshipPosition(lat, lon, meta);
        return;
    }
    if (path == QLatin1String("navigation.courseOverGroundTrue")) {
        selfCache_.cogDegTrue = wrap360(value.toDouble() * kRadToDeg);
        if (selfCache_.sogKnots)
            nav_->publishCogSog(*selfCache_.cogDegTrue, *selfCache_.sogKnots, meta);
        return;
    }
    if (path == QLatin1String("navigation.speedOverGround")) {
        selfCache_.sogKnots = value.toDouble() * kMsToKnots;
        if (selfCache_.cogDegTrue)
            nav_->publishCogSog(*selfCache_.cogDegTrue, *selfCache_.sogKnots, meta);
        return;
    }
    if (path == QLatin1String("navigation.headingTrue")) {
        nav_->publishHeading(wrap360(value.toDouble() * kRadToDeg),
                             std::nullopt, meta);
        return;
    }
    if (path == QLatin1String("navigation.headingMagnetic")) {
        nav_->publishHeading(std::nullopt,
                             wrap360(value.toDouble() * kRadToDeg), meta);
        return;
    }
    if (path == QLatin1String("navigation.magneticVariation")) {
        nav_->publishVariation(value.toDouble() * kRadToDeg, meta);
        return;
    }
    if (path == QLatin1String("environment.depth.belowTransducer")
        || path == QLatin1String("environment.depth.belowSurface")
        || path == QLatin1String("environment.depth.belowKeel")) {
        nav_->publishDepth(value.toDouble(), meta);
        return;
    }
    if (path == QLatin1String("navigation.speedThroughWater")) {
        nav_->publishWaterSpeed(value.toDouble() * kMsToKnots, meta);
        return;
    }
    // ---- wind: angle/speed pairs are stitched -----------------------------
    if (path == QLatin1String("environment.wind.angleApparent")) {
        selfCache_.apparentWindAngleDeg = wrap360(value.toDouble() * kRadToDeg);
        if (selfCache_.apparentWindSpeedKnots)
            nav_->publishApparentWind(*selfCache_.apparentWindSpeedKnots,
                                      *selfCache_.apparentWindAngleDeg, meta);
        return;
    }
    if (path == QLatin1String("environment.wind.speedApparent")) {
        selfCache_.apparentWindSpeedKnots = value.toDouble() * kMsToKnots;
        if (selfCache_.apparentWindAngleDeg)
            nav_->publishApparentWind(*selfCache_.apparentWindSpeedKnots,
                                      *selfCache_.apparentWindAngleDeg, meta);
        return;
    }
    if (path == QLatin1String("environment.wind.angleTrueWater")
        || path == QLatin1String("environment.wind.angleTrueGround")) {
        selfCache_.trueWindAngleDeg = wrap360(value.toDouble() * kRadToDeg);
        if (selfCache_.trueWindSpeedKnots)
            nav_->publishTrueWind(*selfCache_.trueWindSpeedKnots,
                                  *selfCache_.trueWindAngleDeg, meta);
        return;
    }
    if (path == QLatin1String("environment.wind.speedTrue")) {
        selfCache_.trueWindSpeedKnots = value.toDouble() * kMsToKnots;
        if (selfCache_.trueWindAngleDeg)
            nav_->publishTrueWind(*selfCache_.trueWindSpeedKnots,
                                  *selfCache_.trueWindAngleDeg, meta);
        if (selfCache_.trueWindDirectionDeg)
            nav_->publishTrueWindDirection(*selfCache_.trueWindDirectionDeg,
                                           *selfCache_.trueWindSpeedKnots, meta);
        return;
    }
    if (path == QLatin1String("environment.wind.directionTrue")) {
        selfCache_.trueWindDirectionDeg = wrap360(value.toDouble() * kRadToDeg);
        if (selfCache_.trueWindSpeedKnots)
            nav_->publishTrueWindDirection(*selfCache_.trueWindDirectionDeg,
                                           *selfCache_.trueWindSpeedKnots, meta);
        return;
    }
    // Unknown paths silently ignored — Signal K servers emit many extras.
}

// ---- AIS ------------------------------------------------------------------

void SignalKDecoder::handleAisValue(quint32 mmsi, const QString& path,
                                    const QJsonValue& value) {
    if (!ais_) return;
    VesselCache& c = aisCache_[mmsi];

    auto cls = [&] {
        if (c.aisClass) return *c.aisClass == 1 ? AisClass::B : AisClass::A;
        return AisClass::Unknown;
    };

    auto publishStatic = [&] {
        // Only push when we have at least a name or ship type: the AIS store
        // merges fields, so partial pushes are fine, but we avoid empty pushes.
        if (!c.aisName && !c.aisShipType && !c.aisCallSign && !c.aisImo) return;
        AisStaticData d;
        d.mmsi = mmsi;
        d.cls  = cls();
        if (c.aisName)     d.name     = *c.aisName;
        if (c.aisCallSign) d.callSign = *c.aisCallSign;
        if (c.aisShipType) d.shipType = *c.aisShipType;
        if (c.aisImo)      d.imoNumber = *c.aisImo;
        AisDimensions dims;
        if (c.dimToBow)       dims.toBow       = *c.dimToBow;
        if (c.dimToStern)     dims.toStern     = *c.dimToStern;
        if (c.dimToPort)      dims.toPort      = *c.dimToPort;
        if (c.dimToStarboard) dims.toStarboard = *c.dimToStarboard;
        d.dimensions = dims;
        ais_->publishAisStatic(d, sourceId_);
    };

    if (path == QLatin1String("navigation.position") && value.isObject()) {
        const QJsonObject p = value.toObject();
        AisPositionReport rep;
        rep.mmsi = mmsi;
        rep.cls  = cls();
        rep.latitudeDeg  = p.value(QStringLiteral("latitude")).toDouble();
        rep.longitudeDeg = p.value(QStringLiteral("longitude")).toDouble();
        if (c.cogDegTrue) rep.cogDegTrue     = *c.cogDegTrue;
        if (c.sogKnots)   rep.sogKnots       = *c.sogKnots;
        ais_->publishAisPosition(rep, sourceId_);
        return;
    }
    if (path == QLatin1String("navigation.courseOverGroundTrue")) {
        c.cogDegTrue = wrap360(value.toDouble() * kRadToDeg);
        return;
    }
    if (path == QLatin1String("navigation.speedOverGround")) {
        c.sogKnots = value.toDouble() * kMsToKnots;
        return;
    }
    if (path == QLatin1String("navigation.headingTrue")) {
        // Heading-only updates (no position): publish as a position report so
        // the AIS store records the heading without waiting for the next fix.
        AisPositionReport rep;
        rep.mmsi = mmsi;
        rep.cls  = cls();
        rep.headingDegTrue = wrap360(value.toDouble() * kRadToDeg);
        ais_->publishAisPosition(rep, sourceId_);
        return;
    }
    if (path == QLatin1String("name")) {
        c.aisName = value.toString();
        publishStatic();
        return;
    }
    if (path == QLatin1String("communication.callsignVhf")) {
        c.aisCallSign = value.toString();
        publishStatic();
        return;
    }
    if (path == QLatin1String("design.aisShipType") && value.isObject()) {
        c.aisShipType = value.toObject().value(QStringLiteral("id")).toInt();
        publishStatic();
        return;
    }
    if (path == QLatin1String("registrations.imo")) {
        // SK conventionally prefixes with "IMO " — strip and parse.
        QString s = value.toString();
        s.remove(QStringLiteral("IMO"), Qt::CaseInsensitive);
        const int imo = s.trimmed().toInt();
        if (imo > 0) c.aisImo = imo;
        publishStatic();
        return;
    }
    if (path == QLatin1String("sensors.ais.class")) {
        const QString s = value.toString();
        c.aisClass = (s.compare(QLatin1String("B"), Qt::CaseInsensitive) == 0) ? 1 : 0;
        return;
    }
    if (path == QLatin1String("design.length") && value.isObject()) {
        // Length overall in metres; SK reports it under .overall.
        const double L = value.toObject().value(QStringLiteral("overall")).toDouble();
        if (L > 0.0) {
            // Without a reference point we split evenly — refined when SK
            // publishes the dimensions block (sensors.ais.fromCenter etc.).
            c.dimToBow   = L * 0.5;
            c.dimToStern = L * 0.5;
            publishStatic();
        }
        return;
    }
    if (path == QLatin1String("design.beam")) {
        const double B = value.toDouble();
        if (B > 0.0) {
            c.dimToPort      = B * 0.5;
            c.dimToStarboard = B * 0.5;
            publishStatic();
        }
        return;
    }
    // Other AIS paths are silently ignored for now.
}
