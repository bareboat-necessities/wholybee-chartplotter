#pragma once
#include "n2k_frame.hpp"

// Parses the "N2K ASCII" output format used by Actisense W2K / W2K-1 gateways.
//
// One line per fully-reassembled PGN, e.g.
//     A173321.107 23FF7 1F513 012F3070002F30709F<CR><LF>
// where the fields are: 'A' identifier, hhmmss.ddd time, src+dst+prio (5 hex
// digits packed: SS DD P), 5-hex-digit PGN, then hex payload bytes. The
// gateway does Fast-Packet / ISO Transport reassembly upstream, so the data
// blob is the full PGN payload regardless of how many CAN frames it took to
// carry. See docs/NMEA 2000 ASCII Output format.docx for the canonical spec.
class ActisenseAsciiParser : public IN2kFrameParser {
public:
    bool parse(const QByteArray& line, N2kFrame& out, const QDateTime& nowUtc) override;
};
