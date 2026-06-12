#pragma once
#include "n2k_frame.hpp"
#include <QString>

class INavDataPublisher;
class IAisPublisher;

// Decodes the navigation and AIS PGNs we care about into the same publisher
// contracts the NMEA 0183 client uses (INavDataPublisher / IAisPublisher).
//
// Transport-agnostic — fed already-reassembled N2kFrame payloads. Knowing the
// PGN catalogue lives here, not in the parser. The supported set is the nav
// PGNs needed to drive ownship (position, COG/SOG, heading, depth, speed,
// wind) and the AIS PGNs that mirror the AIVDM messages already handled.
class N2kDecoder {
public:
    // Either publisher may be null; the decoder simply skips PGNs that have
    // nothing to publish to (e.g. an AIS-only feed would still get nav PGNs
    // dropped if no nav publisher were supplied — not the configuration we
    // actually use, but it keeps the contract simple).
    N2kDecoder(INavDataPublisher* nav, IAisPublisher* ais, QString source);

    // Decode one frame and call the publishers as appropriate. Returns true if
    // this PGN is one we recognised (regardless of whether the payload had
    // valid data) — the client uses this to decide whether the frame counts
    // as "decoding activity" for the status dot.
    bool decode(const N2kFrame& f);

private:
    INavDataPublisher* nav_ = nullptr;
    IAisPublisher*     ais_ = nullptr;
    QString            source_;
};
