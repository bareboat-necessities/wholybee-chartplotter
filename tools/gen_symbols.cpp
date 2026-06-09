// tools/gen_symbols.cpp
//
// Build-time tool: parse chartsymbols.xml → symbols.bin
//
// Usage: gen_symbols <chartsymbols.xml> <symbols.bin>
//
// Binary layout (little-endian, packed):
//
//   Header (16 bytes)
//     char     magic[4]    = "SYM\x01"
//     uint32_t symCount    number of SymRecord entries
//     uint32_t lookCount   number of LookRecord entries
//     uint32_t reserved    = 0
//
//   SymRecord × symCount (36 bytes each)
//     char    name[24]     null-padded symbol name (e.g. "ACHARE02")
//     int16_t atlas_x      top-left X in the atlas PNG
//     int16_t atlas_y      top-left Y in the atlas PNG
//     int16_t width        tile width in pixels
//     int16_t height       tile height in pixels
//     int16_t pivot_x      geographic-anchor X offset from tile top-left
//     int16_t pivot_y      geographic-anchor Y offset from tile top-left
//
//   LookRecord × lookCount (32 bytes each)
//     char objClass[8]     null-padded S-57 object-class name (e.g. "LIGHTS")
//     char symName[24]     null-padded first symbol referenced by SY() in the
//                          lookup instruction for that class
//
// Symbol names are drawn from <symbols><symbol><name> elements.
// Lookup entries collect the first SY(NAME) call from <lookups><lookup> entries.
// Multiple lookups for the same object class are collapsed to the first SY() seen.

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QMap>
#include <QString>
#include <QByteArray>
#include <QVector>

#include <cstdio>
#include <cstdint>
#include <cstring>

// ---- binary record types (packed, no padding) --------------------------------

#pragma pack(push, 1)

struct Header {
    char     magic[4];    // "SYM\x01"
    uint32_t symCount;
    uint32_t lookCount;
    uint32_t reserved;
};
static_assert(sizeof(Header) == 16, "Header size");

struct SymRecord {
    char    name[24];
    int16_t atlas_x, atlas_y;
    int16_t width, height;
    int16_t pivot_x, pivot_y;
};
static_assert(sizeof(SymRecord) == 36, "SymRecord size");

struct LookRecord {
    char objClass[8];
    char symName[24];
};
static_assert(sizeof(LookRecord) == 32, "LookRecord size");

#pragma pack(pop)

// ---- helpers -----------------------------------------------------------------

static void padCopy(char* dst, int dstSize, const QByteArray& src)
{
    std::memset(dst, 0, static_cast<std::size_t>(dstSize));
    int n = std::min(static_cast<int>(src.size()), dstSize - 1);
    std::memcpy(dst, src.constData(), static_cast<std::size_t>(n));
}

// Return the name from the first SY(NAME[,...]) call in an S-52 instruction
// string, e.g. "SY(LIGHTS13,LITGN,067)" → "LIGHTS13".  Returns empty if none.
static QByteArray firstSY(const QString& instruction)
{
    static const QRegularExpression re(QStringLiteral(R"(SY\(([^,)]+))"));
    const auto m = re.match(instruction);
    return m.hasMatch() ? m.captured(1).trimmed().toLatin1() : QByteArray{};
}

// ---- main --------------------------------------------------------------------

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::fprintf(stderr, "Usage: gen_symbols <chartsymbols.xml> <symbols.bin>\n");
        return 1;
    }

    QFile xmlFile(QString::fromLocal8Bit(argv[1]));
    if (!xmlFile.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "Cannot open input: %s\n", argv[1]);
        return 1;
    }

    // ---- parse ----------------------------------------------------------------

    QVector<SymRecord> syms;
    // QMap gives deterministic (sorted) iteration order, which makes the output
    // binary reproducible for identical inputs.
    QMap<QByteArray, QByteArray> objToSym;   // objClass → first SY() symName

    QXmlStreamReader xml(&xmlFile);

    bool inSymbols = false;   // inside <symbols>…</symbols>
    bool inLookups = false;   // inside <lookups>…</lookups>
    bool inBitmap  = false;   // inside <bitmap>…</bitmap> within a <symbol>

    // Current <symbol> being accumulated
    struct {
        QByteArray name;
        int16_t ax = 0, ay = 0;
        int16_t w  = 0, h  = 0;
        int16_t px = 0, py = 0;
        bool hasGraphicsLoc = false;
    } sym;

    // Current <lookup> being accumulated
    struct {
        QByteArray objClass;
        QString    instruction;
    } look;

    while (!xml.atEnd()) {
        xml.readNext();

        // ---- StartElement ------------------------------------------------
        if (xml.isStartElement()) {
            const auto tag = xml.name();

            if      (tag == u"symbols") { inSymbols = true; }
            else if (tag == u"lookups") { inLookups = true; }

            // -- symbol section --
            else if (inSymbols) {
                if (tag == u"symbol") {
                    // Reset accumulator for a new <symbol>
                    sym = {};
                }
                else if (tag == u"name") {
                    // <name> is a text element directly inside <symbol>
                    sym.name = xml.readElementText().trimmed().toLatin1();
                }
                else if (tag == u"bitmap") {
                    sym.w = static_cast<int16_t>(
                        xml.attributes().value(u"width").toInt());
                    sym.h = static_cast<int16_t>(
                        xml.attributes().value(u"height").toInt());
                    inBitmap = true;
                }
                else if (inBitmap && tag == u"graphics-location") {
                    sym.ax = static_cast<int16_t>(
                        xml.attributes().value(u"x").toInt());
                    sym.ay = static_cast<int16_t>(
                        xml.attributes().value(u"y").toInt());
                    sym.hasGraphicsLoc = true;
                }
                else if (inBitmap && tag == u"pivot") {
                    sym.px = static_cast<int16_t>(
                        xml.attributes().value(u"x").toInt());
                    sym.py = static_cast<int16_t>(
                        xml.attributes().value(u"y").toInt());
                }
            }

            // -- lookup section --
            else if (inLookups) {
                if (tag == u"lookup") {
                    look = {};
                    look.objClass =
                        xml.attributes().value(u"name").trimmed().toLatin1();
                }
                else if (tag == u"instruction") {
                    look.instruction = xml.readElementText().trimmed();
                }
            }
        }

        // ---- EndElement --------------------------------------------------
        else if (xml.isEndElement()) {
            const auto tag = xml.name();

            if      (tag == u"symbols") { inSymbols = false; }
            else if (tag == u"lookups") { inLookups = false; }
            else if (inSymbols && tag == u"bitmap") { inBitmap = false; }
            else if (inSymbols && tag == u"symbol") {
                if (!sym.name.isEmpty() && sym.hasGraphicsLoc &&
                    sym.w > 0 && sym.h > 0)
                {
                    SymRecord r{};
                    padCopy(r.name, sizeof(r.name), sym.name);
                    r.atlas_x = sym.ax; r.atlas_y = sym.ay;
                    r.width   = sym.w;  r.height  = sym.h;
                    r.pivot_x = sym.px; r.pivot_y = sym.py;
                    syms.append(r);
                }
            }
            else if (inLookups && tag == u"lookup") {
                if (!look.objClass.isEmpty() && !look.instruction.isEmpty()) {
                    const QByteArray sn = firstSY(look.instruction);
                    if (!sn.isEmpty() && !objToSym.contains(look.objClass))
                        objToSym[look.objClass] = sn;
                }
            }
        }
    }

    if (xml.hasError()) {
        std::fprintf(stderr, "XML error at line %lld: %s\n",
                     (long long)xml.lineNumber(),
                     xml.errorString().toUtf8().constData());
        return 1;
    }

    std::fprintf(stdout,
                 "gen_symbols: %d symbols, %d object-class lookups parsed\n",
                 static_cast<int>(syms.size()),
                 static_cast<int>(objToSym.size()));

    // ---- write binary --------------------------------------------------------

    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "Cannot write output: %s\n", argv[2]);
        return 1;
    }

    Header hdr{};
    hdr.magic[0] = 'S'; hdr.magic[1] = 'Y';
    hdr.magic[2] = 'M'; hdr.magic[3] = '\x01';
    hdr.symCount  = static_cast<uint32_t>(syms.size());
    hdr.lookCount = static_cast<uint32_t>(objToSym.size());
    hdr.reserved  = 0;

    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(syms.constData()),
              syms.size() * static_cast<qsizetype>(sizeof(SymRecord)));

    for (auto it = objToSym.cbegin(); it != objToSym.cend(); ++it) {
        LookRecord lr{};
        padCopy(lr.objClass, sizeof(lr.objClass), it.key());
        padCopy(lr.symName,  sizeof(lr.symName),  it.value());
        out.write(reinterpret_cast<const char*>(&lr), sizeof(lr));
    }

    out.close();

    const qint64 fileSize = QFileInfo(out).size();
    std::fprintf(stdout, "gen_symbols: wrote %s (%lld bytes)\n",
                 argv[2], (long long)fileSize);
    return 0;
}
