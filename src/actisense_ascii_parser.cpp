#include "actisense_ascii_parser.hpp"

#include <QList>
#include <QString>
#include <QTime>

namespace {
// Read two hex digits at offset `i` into `byte`. Returns false if the digits
// aren't valid hex or run past the end.
bool hexByte(const QByteArray& s, int i, quint8& byte) {
    if (i + 1 >= s.size()) return false;
    bool ok1 = false, ok2 = false;
    const int hi = QString::fromLatin1(s.mid(i, 1)).toInt(&ok1, 16);
    const int lo = QString::fromLatin1(s.mid(i + 1, 1)).toInt(&ok2, 16);
    if (!ok1 || !ok2) return false;
    byte = quint8((hi << 4) | lo);
    return true;
}
} // namespace

bool ActisenseAsciiParser::parse(const QByteArray& raw, N2kFrame& out,
                                 const QDateTime& nowUtc) {
    // Trim CR/LF/whitespace from both ends; spec says "decoders should only
    // parse data up to the last whitespace character", so we work with the
    // visible content only.
    QByteArray line = raw.trimmed();
    if (line.isEmpty() || line.at(0) != 'A') return false;

    // Split on whitespace. We expect at least: <time> <SSDDP> <PPPPP> <hex>.
    const QList<QByteArray> toks = line.split(' ');
    if (toks.size() < 4) return false;

    // Token 0 carries the message id ('A') jammed up against the timestamp,
    // e.g. "A173321.107". Strip the leading 'A' and parse hhmmss(.ddd).
    QByteArray timeTok = toks[0].mid(1);
    QTime t;
    if (timeTok.contains('.')) {
        // "hhmmss.ddd" — Qt parses the fractional seconds.
        t = QTime::fromString(QString::fromLatin1(timeTok), QStringLiteral("hhmmss.zzz"));
    } else if (timeTok.size() >= 6) {
        t = QTime::fromString(QString::fromLatin1(timeTok), QStringLiteral("hhmmss"));
    }
    // Compose a UTC instant on today's date when the time parsed; otherwise
    // fall back to the caller-supplied "now". The gateway's clock may not be
    // set, so the fallback is the common path in practice.
    if (t.isValid()) {
        out.time = QDateTime(nowUtc.date(), t, Qt::UTC);
    } else {
        out.time = nowUtc;
    }

    // Token 1 is SS DD P, 5 hex digits total. Top 3 bits of P may be used
    // for future expansion (see spec).
    const QByteArray& hdr = toks[1];
    if (hdr.size() < 5) return false;
    quint8 src = 0, dst = 0;
    if (!hexByte(hdr, 0, src)) return false;
    if (!hexByte(hdr, 2, dst)) return false;
    bool ok = false;
    const int prio = QString::fromLatin1(hdr.mid(4, 1)).toInt(&ok, 16);
    if (!ok) return false;
    out.src = src;
    out.dst = dst;
    out.prio = quint8(prio & 0x07);

    // Token 2 is the PGN in hex. Spec says 5 digits today, possibly more in
    // future; toUInt handles either.
    out.pgn = QString::fromLatin1(toks[2]).toUInt(&ok, 16);
    if (!ok) return false;

    // Tokens 3..n-1: the payload as concatenated hex byte pairs. There may be
    // whitespace inside the payload (the spec calls it "optional whitespace for
    // example purposes — receiver must ignore"), so we join the remaining
    // tokens before parsing pairs.
    QByteArray payloadHex;
    for (int i = 3; i < toks.size(); ++i) payloadHex.append(toks[i]);
    if (payloadHex.size() % 2) return false;
    QByteArray data;
    data.reserve(payloadHex.size() / 2);
    for (int i = 0; i < payloadHex.size(); i += 2) {
        quint8 b = 0;
        if (!hexByte(payloadHex, i, b)) return false;
        data.append(char(b));
    }
    out.data = std::move(data);
    return true;
}
