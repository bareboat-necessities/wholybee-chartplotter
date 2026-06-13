#include "n2k_decoder.hpp"
#include "nav_data_store.hpp"
#include "ais_target_store.hpp"

#include <QDateTime>
#include <cmath>
#include <optional>

namespace {
constexpr double kMs_to_Kn  = 1.9438444924406046;     // 1 m/s -> knots
constexpr double kRad_to_Deg = 57.29577951308232;     // 180/pi

// Resolution-aware "not available" tests. NMEA 2000 uses all-ones for unsigned
// fields and the maximum positive value (0x7F...F) for signed fields to flag
// "data not available".
bool nau16(quint16 v) { return v == 0xFFFF; }
bool nau32(quint32 v) { return v == 0xFFFFFFFFu; }
bool nai16(qint16 v)  { return v == 0x7FFF; }
bool nai32(qint32 v)  { return v == 0x7FFFFFFF; }

// Wrap a heading/course to [0, 360).
double wrap360(double d) {
    d = std::fmod(d, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

// Freshness is driven by when we received the frame, not by the gateway's
// self-reported time. The Actisense timestamp (carried in f.time) comes from
// the gateway's own clock, which is frequently unset or free-running and can be
// many minutes off real UTC — using it here would age every value out the
// instant it arrived. This mirrors the NMEA 0183 path, which stamps with the
// receive time. (f.time is retained on the frame for debug/logging only.)
NavValueMeta meta(const QString& src) {
    NavValueMeta m;
    m.source = src;
    m.timestampUtc = QDateTime::currentDateTimeUtc();
    return m;
}

// PGN 129025 - Position, Rapid Update. 8-byte payload: int32 lat, int32 lon,
// each in 1e-7 degree units.
void pgn129025(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 8) return;
    N2kReader r(f.data);
    const qint32 lat = qint32(r.i(32));
    const qint32 lon = qint32(r.i(32));
    if (nai32(lat) || nai32(lon)) return;
    nav->publishOwnshipPosition(lat * 1e-7, lon * 1e-7, meta(src));
}

// PGN 129026 - COG & SOG, Rapid Update. SID(8), COG ref(2)+reserved(6),
// COG(16, 1e-4 rad), SOG(16, 1e-2 m/s), reserved(16).
void pgn129026(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 8) return;
    N2kReader r(f.data);
    r.skip(8);                              // SID
    const int ref = int(r.u(2));            // 0 = True, 1 = Magnetic
    r.skip(6);                              // reserved
    const quint16 cog = quint16(r.u(16));
    const quint16 sog = quint16(r.u(16));
    if (nau16(cog) || nau16(sog)) return;
    if (ref != 0) return;                   // only publish true-referenced COG
    const double cogDeg = wrap360(cog * 1e-4 * kRad_to_Deg);
    const double sogKn  = sog * 1e-2 * kMs_to_Kn;
    nav->publishCogSog(cogDeg, sogKn, meta(src));
}

// PGN 127250 - Vessel Heading. SID(8), Heading(16, 1e-4 rad), Deviation(16),
// Variation(16), Reference(2)+reserved(6). Reference: 0=True, 1=Magnetic.
void pgn127250(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 8) return;
    N2kReader r(f.data);
    r.skip(8);                              // SID
    const quint16 hdgRaw = quint16(r.u(16));
    const qint16  devRaw = qint16(r.i(16));
    const qint16  varRaw = qint16(r.i(16));
    const int     ref    = int(r.u(2));
    if (nau16(hdgRaw)) return;
    const double hdg = wrap360(hdgRaw * 1e-4 * kRad_to_Deg);
    const NavValueMeta m = meta(src);
    std::optional<double> variation;
    if (!nai16(varRaw)) {
        variation = varRaw * 1e-4 * kRad_to_Deg;
        nav->publishVariation(*variation, m);
    }
    if (ref == 0) {                         // true
        nav->publishHeading(hdg, std::nullopt, m);
    } else if (ref == 1) {                  // magnetic
        std::optional<double> trueHdg;
        if (variation) trueHdg = wrap360(hdg + *variation);
        nav->publishHeading(trueHdg, hdg, m);
    }
    (void)devRaw;                           // deviation not consumed
}

// PGN 128267 - Water Depth. SID(8), Depth below transducer(32, 0.01 m),
// Offset to waterline / keel(16, int, 0.001 m), Range(8, 10 m).
void pgn128267(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 8) return;
    N2kReader r(f.data);
    r.skip(8);                              // SID
    const quint32 depthRaw  = quint32(r.u(32));
    const qint16  offsetRaw = qint16(r.i(16));
    if (nau32(depthRaw)) return;
    double depth = depthRaw * 0.01;
    if (!nai16(offsetRaw)) depth += offsetRaw * 0.001;   // shift to waterline
    nav->publishDepth(depth, meta(src));
}

// PGN 128259 - Speed (Water Referenced). SID(8), Water referenced(16, 0.01
// m/s), Ground referenced(16, 0.01 m/s), SWRT(8), reserved(16).
void pgn128259(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 8) return;
    N2kReader r(f.data);
    r.skip(8);                              // SID
    const quint16 swr = quint16(r.u(16));
    if (nau16(swr)) return;
    nav->publishWaterSpeed(swr * 0.01 * kMs_to_Kn, meta(src));
}

// PGN 130306 - Wind Data. SID(8), Speed(16, 0.01 m/s), Angle(16, 1e-4 rad),
// Reference(3)+reserved(5). Reference: 0=True (North), 1=Magnetic, 2=Apparent,
// 3=True (boat referenced), 4=True (water referenced).
void pgn130306(const N2kFrame& f, INavDataPublisher* nav, const QString& src) {
    if (!nav || f.data.size() < 6) return;
    N2kReader r(f.data);
    r.skip(8);                              // SID
    const quint16 spdRaw = quint16(r.u(16));
    const quint16 angRaw = quint16(r.u(16));
    const int     ref    = int(r.u(3));
    if (nau16(spdRaw) || nau16(angRaw)) return;
    const double kn  = spdRaw * 0.01 * kMs_to_Kn;
    const double ang = wrap360(angRaw * 1e-4 * kRad_to_Deg);
    const NavValueMeta m = meta(src);
    switch (ref) {
        case 2:  nav->publishApparentWind(kn, ang, m); break;          // apparent
        case 3:                                                         // true (boat)
        case 4:  nav->publishTrueWind(kn, ang, m); break;              // true (water)
        case 0:  nav->publishTrueWindDirection(ang, kn, m); break;     // true, north-ref
        default: break;                                                 // magnetic etc.
    }
}

// --- AIS PGNs --------------------------------------------------------------
//
// The AIS PGNs carry the same content as the AIVDM messages decoded in
// ais_decoder.cpp, but laid out by NMEA 2000 packing rules (LE, byte-aligned
// where possible) rather than the AIVDM 6-bit ASCII scheme. We mirror the
// existing publisher contract so the AIS overlay / target list code doesn't
// care which transport produced a target.

// AIS lat/lon use 1e-7 degree fixed-point in N2K (vs 1e-4 minute in AIVDM).
std::optional<double> n2kAisLon(qint32 raw) {
    return nai32(raw) ? std::nullopt : std::optional<double>(raw * 1e-7);
}
std::optional<double> n2kAisLat(qint32 raw) {
    return nai32(raw) ? std::nullopt : std::optional<double>(raw * 1e-7);
}

// PGN 129038 - AIS Class A Position Report (messages 1/2/3 equivalent).
void pgn129038(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 28) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat Indicator
    AisPositionReport rep;
    rep.mmsi = quint32(r.u(32));
    rep.cls = AisClass::A;
    rep.longitudeDeg = n2kAisLon(qint32(r.i(32)));
    rep.latitudeDeg  = n2kAisLat(qint32(r.i(32)));
    r.skip(1);                              // Position accuracy
    r.skip(1);                              // RAIM
    r.skip(6);                              // Time stamp (UTC seconds)
    const quint16 cog = quint16(r.u(16));
    const quint16 sog = quint16(r.u(16));
    r.skip(19);                             // Communication state
    r.skip(5);                              // Transceiver info
    const quint16 hdg = quint16(r.u(16));
    const qint16  rot = qint16(r.i(16));    // 3.125e-5 rad/s units in N2K
    const int     nav = int(r.u(4));
    if (!nau16(cog)) rep.cogDegTrue   = wrap360(cog * 1e-4 * kRad_to_Deg);
    if (!nau16(sog)) rep.sogKnots     = sog * 1e-2 * kMs_to_Kn;
    if (!nau16(hdg)) rep.headingDegTrue = wrap360(hdg * 1e-4 * kRad_to_Deg);
    if (!nai16(rot)) rep.rotDegPerMin = rot * 3.125e-5 * kRad_to_Deg * 60.0;
    if (nav <= 15)   rep.navStatus    = AisNavStatus(nav);
    ais->publishAisPosition(rep, src);
}

// PGN 129039 - AIS Class B Position Report (message 18 equivalent).
void pgn129039(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 26) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat
    AisPositionReport rep;
    rep.mmsi = quint32(r.u(32));
    rep.cls = AisClass::B;
    rep.longitudeDeg = n2kAisLon(qint32(r.i(32)));
    rep.latitudeDeg  = n2kAisLat(qint32(r.i(32)));
    r.skip(1);                              // Position accuracy
    r.skip(1);                              // RAIM
    r.skip(6);                              // Time stamp
    const quint16 cog = quint16(r.u(16));
    const quint16 sog = quint16(r.u(16));
    r.skip(19);                             // Communication state
    r.skip(5);                              // Transceiver info
    const quint16 hdg = quint16(r.u(16));
    if (!nau16(cog)) rep.cogDegTrue     = wrap360(cog * 1e-4 * kRad_to_Deg);
    if (!nau16(sog)) rep.sogKnots       = sog * 1e-2 * kMs_to_Kn;
    if (!nau16(hdg)) rep.headingDegTrue = wrap360(hdg * 1e-4 * kRad_to_Deg);
    ais->publishAisPosition(rep, src);
}

// Read a 6-bit-ASCII string of `bytes` bytes (NMEA 2000 packs AIS strings as
// 6-bit ASCII into N bytes, top-aligned and '@'-padded). The encoding matches
// the AIVDM payload table used in ais_decoder.cpp.
QString sixBitAscii(N2kReader& r, int bytes) {
    static const char* kTable =
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_ !\"#$%&'()*+,-./0123456789:;<=>?";
    QString s;
    const int chars = (bytes * 8) / 6;
    for (int i = 0; i < chars; ++i)
        s += QLatin1Char(kTable[int(r.u(6)) & 63]);
    // Trim '@' and trailing spaces (the AIS pad characters).
    while (!s.isEmpty() && (s.back() == QLatin1Char('@') || s.back() == QLatin1Char(' ')))
        s.chop(1);
    // The reader's bit position may not be byte-aligned after a 6-bit string;
    // realign for the next field.
    const int over = r.pos() % 8;
    if (over) r.skip(8 - over);
    return s;
}

// Read a variable-length string field encoded as: byte 0 = total length
// including the two-byte header, byte 1 = control code (0 = Unicode, 1 = ASCII
// 8-bit), then (length-2) data bytes. NMEA 2000 string-LAU encoding.
QString lauString(N2kReader& r) {
    const int len  = int(r.u(8));
    const int code = int(r.u(8));
    if (len < 2) return {};
    const int payload = len - 2;
    QByteArray bytes;
    bytes.reserve(payload);
    for (int i = 0; i < payload; ++i) bytes.append(char(r.u(8)));
    if (code == 0) return QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),
                                             bytes.size() / 2);
    return QString::fromLatin1(bytes);   // ASCII (treat as Latin-1 to keep all bytes)
}

// Helper: read dimensions block (toBow, toStern, toPort, toStarboard) as four
// uint16 fields in 0.1 m units. Returns an empty AisDimensions if all four are
// unavailable.
AisDimensions readDims(N2kReader& r) {
    AisDimensions d;
    const quint16 lenRaw  = quint16(r.u(16));   // Length overall
    const quint16 beamRaw = quint16(r.u(16));   // Beam
    const quint16 posStb  = quint16(r.u(16));   // Position of ref. from starboard
    const quint16 posBow  = quint16(r.u(16));   // Position of ref. from bow
    if (!nau16(lenRaw) && !nau16(posBow)) {
        const double L = lenRaw * 0.1;
        d.toBow   = posBow * 0.1;
        d.toStern = std::max(0.0, L - d.toBow);
    }
    if (!nau16(beamRaw) && !nau16(posStb)) {
        const double B = beamRaw * 0.1;
        d.toStarboard = posStb * 0.1;
        d.toPort      = std::max(0.0, B - d.toStarboard);
    }
    return d;
}

// PGN 129794 - AIS Class A Static and Voyage Related Data (message 5).
void pgn129794(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 24) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat
    AisStaticData d;
    d.mmsi = quint32(r.u(32));
    d.cls = AisClass::A;
    const quint32 imo = quint32(r.u(32));
    if (!nau32(imo) && imo != 0) d.imoNumber = int(imo);
    d.callSign = sixBitAscii(r, 7);         // 7 bytes * 8 / 6 = 9 chars (one bit padded)
    d.name     = sixBitAscii(r, 20);        // 20 bytes * 8 / 6 ≈ 26 chars
    const int shipType = int(r.u(8));
    if (shipType) d.shipType = shipType;
    d.dimensions = readDims(r);
    r.skip(16);                             // ETA date (days since 1970)
    r.skip(32);                             // ETA time
    const quint16 draught = quint16(r.u(16));
    if (!nau16(draught)) d.draughtMeters = draught * 0.01;
    d.destination = sixBitAscii(r, 20);
    ais->publishAisStatic(d, src);
}

// PGN 129809 - AIS Class B "CS" Static Data Report, Part A (message 24 part A).
void pgn129809(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 27) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat
    AisStaticData d;
    d.mmsi = quint32(r.u(32));
    d.cls = AisClass::B;
    d.name = sixBitAscii(r, 20);
    ais->publishAisStatic(d, src);
}

// PGN 129810 - AIS Class B "CS" Static Data Report, Part B (message 24 part B).
void pgn129810(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 34) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat
    AisStaticData d;
    d.mmsi = quint32(r.u(32));
    d.cls = AisClass::B;
    const int shipType = int(r.u(8));
    if (shipType) d.shipType = shipType;
    // Vendor ID (42 bits / 7 6-bit chars).
    sixBitAscii(r, 6);                      // ignore (vendor id field)
    d.callSign = sixBitAscii(r, 7);
    d.dimensions = readDims(r);
    ais->publishAisStatic(d, src);
}

// PGN 129040 - AIS Class B Extended Position Report (message 19 equivalent).
// Same position fields as 129039, plus name and dimensions.
void pgn129040(const N2kFrame& f, IAisPublisher* ais, const QString& src) {
    if (!ais || f.data.size() < 28) return;
    N2kReader r(f.data);
    r.skip(6);                              // Message ID
    r.skip(2);                              // Repeat
    AisPositionReport rep;
    rep.mmsi = quint32(r.u(32));
    rep.cls = AisClass::B;
    rep.longitudeDeg = n2kAisLon(qint32(r.i(32)));
    rep.latitudeDeg  = n2kAisLat(qint32(r.i(32)));
    r.skip(1);                              // Position accuracy
    r.skip(1);                              // RAIM
    r.skip(6);                              // Time stamp
    const quint16 cog = quint16(r.u(16));
    const quint16 sog = quint16(r.u(16));
    r.skip(8);                              // Regional reserved
    r.skip(8);                              // Type of ship at byte offset varies in the wild
    const quint16 hdg = quint16(r.u(16));
    if (!nau16(cog)) rep.cogDegTrue     = wrap360(cog * 1e-4 * kRad_to_Deg);
    if (!nau16(sog)) rep.sogKnots       = sog * 1e-2 * kMs_to_Kn;
    if (!nau16(hdg)) rep.headingDegTrue = wrap360(hdg * 1e-4 * kRad_to_Deg);
    ais->publishAisPosition(rep, src);

    // Static portion: name (20 bytes 6-bit ASCII), then ship type / dimensions.
    AisStaticData d;
    d.mmsi = rep.mmsi;
    d.cls = AisClass::B;
    d.name = sixBitAscii(r, 20);
    const int shipType = int(r.u(8));
    if (shipType) d.shipType = shipType;
    d.dimensions = readDims(r);
    ais->publishAisStatic(d, src);
}

} // namespace

N2kDecoder::N2kDecoder(INavDataPublisher* nav, IAisPublisher* ais, QString source)
    : nav_(nav), ais_(ais), source_(std::move(source)) {}

bool N2kDecoder::decode(const N2kFrame& f) {
    switch (f.pgn) {
        case 129025: pgn129025(f, nav_, source_); return true;
        case 129026: pgn129026(f, nav_, source_); return true;
        case 127250: pgn127250(f, nav_, source_); return true;
        case 128267: pgn128267(f, nav_, source_); return true;
        case 128259: pgn128259(f, nav_, source_); return true;
        case 130306: pgn130306(f, nav_, source_); return true;
        case 129038: pgn129038(f, ais_, source_); return true;
        case 129039: pgn129039(f, ais_, source_); return true;
        case 129040: pgn129040(f, ais_, source_); return true;
        case 129794: pgn129794(f, ais_, source_); return true;
        case 129809: pgn129809(f, ais_, source_); return true;
        case 129810: pgn129810(f, ais_, source_); return true;
        default: return false;
    }
}
