#pragma once
#include <QByteArray>
#include <QString>
#include <QDateTime>
#include <algorithm>

// A decoded NMEA 2000 PGN frame, already reassembled from any Fast-Packet
// fragments by the upstream parser. The data layout follows the standard CAN
// packing rules: multi-byte numbers are little-endian, bitfields fill bytes
// LSB-first, and unsigned all-ones / signed 0x7F...F values mean "not
// available". Decoders consume this frame, not the raw transport format.
struct N2kFrame {
    quint32  pgn = 0;        // 18-bit PGN (kept in a quint32 for future OneNet
                             // extended PGNs that may use more bits)
    quint8   src = 0xFF;     // source address (0..251; 254 = null, 255 = global)
    quint8   dst = 0xFF;     // destination address (0xFF = broadcast)
    quint8   prio = 0;       // 0..7
    QDateTime time;          // when the frame arrived (UTC, monotonic if no
                             // timestamp in the transport)
    QByteArray data;         // PGN payload, fully reassembled
};

// Pluggable parser interface so we can add other NMEA 2000 transport encodings
// later (Yacht Devices RAW ASCII, Actisense binary, etc.) without changing the
// client. Each parser knows how to turn one incoming line into a frame.
class IN2kFrameParser {
public:
    virtual ~IN2kFrameParser() = default;
    // Try to parse one line. Returns true on success, false if the line is
    // empty, malformed, or not in this parser's format. The frame's `time` is
    // set to the parser's best-effort timestamp (the transport's clock or, if
    // none, the caller-supplied "now").
    virtual bool parse(const QByteArray& line, N2kFrame& out, const QDateTime& nowUtc) = 0;
};

// Little-endian, bit-streaming reader over a PGN data payload.
//
// NMEA 2000 packs fields LSB-first within each byte and across bytes — a
// 16-bit field at offset 0 reads byte 0 then byte 1 as the high half; a
// 6-bit field followed by a 2-bit field reads bits 0..5 then bits 6..7 of the
// same byte. Reading past the end yields 0 (so optional trailing fields are
// safely "not available").
class N2kReader {
public:
    explicit N2kReader(const QByteArray& data) : data_(data) {}

    int  pos() const { return bitPos_; }
    void seek(int bitOffset) { bitPos_ = bitOffset; }
    void skip(int len) { bitPos_ += len; }
    int  remaining() const { return std::max(0, int(data_.size()) * 8 - bitPos_); }

    // Read `len` bits (1..64) as unsigned, advancing the position.
    quint64 u(int len) {
        quint64 result = 0;
        int outBit = 0;
        while (len > 0) {
            const int byteIdx = bitPos_ / 8;
            if (byteIdx >= data_.size()) { bitPos_ += len; break; }
            const int bitInByte = bitPos_ % 8;
            const int take = std::min(8 - bitInByte, len);
            const quint64 mask = (1ULL << take) - 1;
            const quint64 v = (quint8(data_[byteIdx]) >> bitInByte) & mask;
            result |= v << outBit;
            outBit  += take;
            bitPos_ += take;
            len     -= take;
        }
        return result;
    }

    // Read `len` bits as a two's-complement signed integer.
    qint64 i(int len) {
        quint64 v = u(len);
        if (len > 0 && len < 64 && (v & (quint64(1) << (len - 1))))
            v |= ~((quint64(1) << len) - 1);
        return qint64(v);
    }

    // "Not available" tests for the common NMEA 2000 widths. Unsigned: all
    // ones; signed: maximum positive (0x7F...F). The decoder uses these instead
    // of magic numbers at each call site.
    static bool isNaU(quint64 v, int len) {
        if (len <= 0 || len >= 64) return false;
        return v == ((1ULL << len) - 1);
    }
    static bool isNaI(qint64 v, int len) {
        if (len <= 0 || len >= 64) return false;
        return v == qint64((1ULL << (len - 1)) - 1);
    }

private:
    const QByteArray& data_;
    int bitPos_ = 0;
};
