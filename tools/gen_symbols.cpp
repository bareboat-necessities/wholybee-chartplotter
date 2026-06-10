// tools/gen_symbols.cpp
//
// Build-time tool: parse chartsymbols.xml -> symbols.bin
//
// Usage: gen_symbols <chartsymbols.xml> <symbols.bin>
//
// This emits the S-52 look-up table (LUP) machinery, not just one symbol per
// object class.  Each S-57 object class has many lookups distinguished by
// attribute conditions (e.g. BOYLAT has 72: by BOYSHP, CATLAM, COLOUR, ...).
// The runtime picks among them by best-match against a feature's attributes.
//
// Table selection (matches the chosen display style):
//   Point features -> "Paper" table   (realistic buoy/beacon shapes)
//   Area  features -> "Symbolized" table (symbolized boundaries + centred SY)
//   Line  features -> dropped (line symbology is LC/LS styling, not handled)
// If a class has no lookup in the preferred table, the alternate point table
// ("Simplified") / area table ("Plain") is used as a fallback for that class.
//
// Binary layout (little-endian, packed):
//
//   Header (28 bytes)
//     char     magic[4]    = "SYM\x05"
//     uint32_t symCount    number of SymRecord
//     uint32_t lupCount    number of LupRecord
//     uint32_t condCount   number of CondRecord (attribute conditions pool)
//     uint32_t attrCount   number of AttrRecord (relevant-attribute acronyms)
//     uint32_t lineCount   number of LineStyleRecord (dedup'd LS() pool)
//     uint32_t fillCount   number of FillStyleRecord (dedup'd AC() pool)
//
//   SymRecord  x symCount  (36 bytes)  -- atlas tiles
//     char    name[24]; int16 atlas_x,atlas_y,width,height,pivot_x,pivot_y
//
//   LupRecord  x lupCount  (20 bytes)  -- one lookup
//     char     objClass[8]   null-padded S-57 class (e.g. "BOYLAT")
//     uint8_t  geomType      0=Point, 1=Line, 2=Area
//     uint8_t  dispCat       0=Displaybase, 1=Standard, 2=Other
//     uint8_t  nConds        attribute conditions for this lookup
//     uint8_t  rotMode       0=no rotation, 1=rotate by ORIENT attribute
//     uint16_t condStart     index of first CondRecord in the pool
//     uint16_t symIdx        resolved symbol index (0xFFFF if unresolved)
//     uint16_t lineStyleIdx  resolved line-style index (0xFFFF if no LS())
//     uint16_t fillStyleIdx  resolved fill-style index (0xFFFF if no AC())
//
//   CondRecord x condCount  (32 bytes)  -- attribute condition pool
//     char attr[8]    6-char S-57 attribute acronym, null-padded
//     char value[24]  required value, e.g. "4" or "3,4,3"
//
//   AttrRecord x attrCount  (8 bytes)  -- union of acronyms used in conditions
//     char acronym[8]
//
//   LineStyleRecord x lineCount  (8 bytes)  -- dedup'd LS() styles
//     uint8_t pattern  0=SOLD, 1=DASH, 2=DOTT
//     uint8_t width    S-52 line-width units (1..6)
//     uint8_t r,g,b    RGB resolved from the DAY_BRIGHT colour table
//     uint8_t _pad[3]
//
//   FillStyleRecord x fillCount  (4 bytes)  -- dedup'd AC() styles
//     uint8_t r,g,b,a  RGBA (alpha resolved from S-52 transparency factor)
//
// Note on ordering: LupRecords are grouped so that all lookups for one
// (objClass, geomType) are contiguous, which lets the runtime build a compact
// index. Conditions are pooled and referenced by (condStart, nConds).

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QMap>
#include <QSet>
#include <QString>
#include <QByteArray>
#include <QVector>
#include <QList>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ---- binary record types (packed) -------------------------------------------

#pragma pack(push, 1)
struct Header {
    char     magic[4];
    uint32_t symCount;
    uint32_t lupCount;
    uint32_t condCount;
    uint32_t attrCount;
    uint32_t lineCount;
    uint32_t fillCount;
};
static_assert(sizeof(Header) == 28, "Header size");

struct SymRecord {
    char    name[24];
    int16_t atlas_x, atlas_y;
    int16_t width, height;
    int16_t pivot_x, pivot_y;
};
static_assert(sizeof(SymRecord) == 36, "SymRecord size");

struct LupRecord {
    char     objClass[8];
    uint8_t  geomType;
    uint8_t  dispCat;
    uint8_t  nConds;
    uint8_t  rotMode;       // 0=no rotation, 1=rotate by ORIENT
    uint16_t condStart;
    uint16_t symIdx;
    uint16_t lineStyleIdx;  // 0xFFFF when the instruction has no LS()
    uint16_t fillStyleIdx;  // 0xFFFF when the instruction has no AC()
};
static_assert(sizeof(LupRecord) == 20, "LupRecord size");

struct CondRecord {
    char attr[8];
    char value[24];
};
static_assert(sizeof(CondRecord) == 32, "CondRecord size");

struct AttrRecord {
    char acronym[8];
};
static_assert(sizeof(AttrRecord) == 8, "AttrRecord size");

struct LineStyleRecord {
    uint8_t pattern;        // 0 SOLD, 1 DASH, 2 DOTT
    uint8_t width;          // S-52 line-width units
    uint8_t r, g, b;
    uint8_t _pad[3];
};
static_assert(sizeof(LineStyleRecord) == 8, "LineStyleRecord size");

struct FillStyleRecord {
    uint8_t r, g, b, a;     // alpha resolved from S-52 transparency factor
};
static_assert(sizeof(FillStyleRecord) == 4, "FillStyleRecord size");
#pragma pack(pop)

// ---- intermediate (parse-time) structures -----------------------------------

struct Cond { QByteArray attr; QByteArray value; };

struct LineStyle {
    uint8_t pattern = 0;
    uint8_t width   = 1;
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const LineStyle& o) const {
        return pattern==o.pattern && width==o.width &&
               r==o.r && g==o.g && b==o.b;
    }
};
inline uint qHash(const LineStyle& s, uint seed = 0) {
    return ::qHash(uint32_t(s.pattern) | (uint32_t(s.width)<<8) |
                   (uint32_t(s.r)<<16) | (uint32_t(s.g)<<24)) ^
           ::qHash(s.b, seed);
}

struct FillStyle {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const FillStyle& o) const {
        return r==o.r && g==o.g && b==o.b && a==o.a;
    }
};
inline uint qHash(const FillStyle& s, uint seed = 0) {
    return ::qHash(uint32_t(s.r) | (uint32_t(s.g)<<8) |
                   (uint32_t(s.b)<<16) | (uint32_t(s.a)<<24), seed);
}

struct Lup {
    QByteArray objClass;
    int        geomType = 0;   // 0 Point, 1 Line, 2 Area
    int        dispCat  = 1;   // 0 base, 1 standard, 2 other
    QByteArray table;          // "Paper", "Simplified", "Plain", "Symbolized", "Lines"
    QString    instruction;    // raw S-52 instruction; not written to the binary
    QVector<Cond> conds;
    QByteArray symName;        // first SY() symbol; empty if none
    uint8_t    rotMode = 0;    // 0=none, 1=rotate by ORIENT
    int        lineStyleIdx = -1;   // index into the dedup'd LineStyle pool, or -1
    int        fillStyleIdx = -1;   // index into the dedup'd FillStyle pool, or -1
};

// ---- helpers ----------------------------------------------------------------

static void padCopy(char* dst, int dstSize, const QByteArray& src) {
    std::memset(dst, 0, static_cast<std::size_t>(dstSize));
    int n = std::min(static_cast<int>(src.size()), dstSize - 1);
    std::memcpy(dst, src.constData(), static_cast<std::size_t>(n));
}

// Parse the first SY() call in an S-52 instruction. Returns the symbol name
// and, if a second argument is present, classifies it as a rotation source.
// Supported rotation sources:
//   "ORIENT" -> rotMode 1 (rotate by the feature's ORIENT attribute, degrees)
// Anything else (numeric literals, 'OBJNAM' text refs, etc.) -> rotMode 0.
struct SyParse { QByteArray name; uint8_t rotMode = 0; };
static SyParse firstSY(const QString& instruction) {
    static const QRegularExpression re(QStringLiteral(R"(SY\(([^)]+)\))"));
    const auto m = re.match(instruction);
    SyParse out;
    if (!m.hasMatch()) return out;
    const QStringList parts = m.captured(1).split(',');
    out.name = parts.value(0).trimmed().toLatin1();
    if (parts.size() > 1 && parts.at(1).trimmed() == QLatin1String("ORIENT"))
        out.rotMode = 1;
    return out;
}

// Packed 0x00RRGGBB so we can use plain uint32_t in QtCore-only code.
static inline uint32_t packRgb(int r, int g, int b) {
    return (uint32_t(r & 0xFF) << 16) | (uint32_t(g & 0xFF) << 8) |
            uint32_t(b & 0xFF);
}

// Resolve the line style for an S-52 instruction.  Prefers LS(pattern,width,
// color-token); when absent but LC(...) is present (Line Complex — a symbol
// stamped along the line, e.g. restricted-area patterned boundaries) emits a
// pragmatic fallback of DASH, width 2, CHMGF (light magenta — the universal
// "soft warning boundary" colour used by S-52 for restricted/anchorage/TSS
// areas).  Implementing LC fully would mean stroking the atlas symbol along
// the path; the fallback captures the visual intent at typical zoom levels.
// Returns false when neither LS nor LC is present, or when LS's colour token
// is unknown.
static bool firstLS(const QString& instruction,
                    const QHash<QByteArray, uint32_t>& colors,
                    LineStyle& out) {
    static const QRegularExpression reLS(QStringLiteral(R"(LS\(([^)]+)\))"));
    static const QRegularExpression reLC(QStringLiteral(R"(LC\(([^)]+)\))"));

    const auto mLS = reLS.match(instruction);
    if (mLS.hasMatch()) {
        const QStringList parts = mLS.captured(1).split(',');
        if (parts.size() >= 3) {
            const QString patStr = parts.at(0).trimmed();
            const QByteArray colTok = parts.at(2).trimmed().toLatin1();
            const auto it = colors.constFind(colTok);
            if (it != colors.constEnd()) {
                if      (patStr == QLatin1String("SOLD")) out.pattern = 0;
                else if (patStr == QLatin1String("DASH")) out.pattern = 1;
                else if (patStr == QLatin1String("DOTT")) out.pattern = 2;
                else                                      out.pattern = 0;
                out.width = static_cast<uint8_t>(
                    std::max(1, parts.at(1).trimmed().toInt()));
                const uint32_t rgb = it.value();
                out.r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
                out.g = static_cast<uint8_t>((rgb >>  8) & 0xFF);
                out.b = static_cast<uint8_t>( rgb        & 0xFF);
                return true;
            }
        }
    }

    // No usable LS — fall back to a generic LC representation if present.
    if (reLC.match(instruction).hasMatch()) {
        const auto it = colors.constFind(QByteArrayLiteral("CHMGF"));
        const uint32_t rgb = (it != colors.constEnd()) ? it.value() : 0xD3A6E9u;
        out.pattern = 1;   // DASH
        out.width   = 2;
        out.r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
        out.g = static_cast<uint8_t>((rgb >>  8) & 0xFF);
        out.b = static_cast<uint8_t>( rgb        & 0xFF);
        return true;
    }
    return false;
}

// Parse the first AC() call in an S-52 instruction into a FillStyle.
// Syntax: AC(color-token[, transparency-factor]).
// The S-52 transparency factor τ runs 0..4:
//   0 opaque, 1 ~25%, 2 ~50%, 3 ~75%, 4 fully transparent.
// We map it to an 8-bit alpha (255 * (1 - τ/4)), so an absent factor → 255.
// Returns false when no AC() is present or its colour token is unknown.
static bool firstAC(const QString& instruction,
                    const QHash<QByteArray, uint32_t>& colors,
                    FillStyle& out) {
    static const QRegularExpression re(QStringLiteral(R"(AC\(([^)]+)\))"));
    const auto m = re.match(instruction);
    if (!m.hasMatch()) return false;
    const QStringList parts = m.captured(1).split(',');
    if (parts.isEmpty()) return false;
    const QByteArray colTok = parts.at(0).trimmed().toLatin1();
    const auto it = colors.constFind(colTok);
    if (it == colors.constEnd()) return false;
    int tau = 0;
    if (parts.size() > 1) tau = std::clamp(parts.at(1).trimmed().toInt(), 0, 4);
    const uint32_t rgb = it.value();
    out.r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    out.g = static_cast<uint8_t>((rgb >>  8) & 0xFF);
    out.b = static_cast<uint8_t>( rgb        & 0xFF);
    out.a = static_cast<uint8_t>(255 - (tau * 255) / 4);
    return true;
}

static int geomFromType(const QString& t) {
    if (t == QLatin1String("Point")) return 0;
    if (t == QLatin1String("Line"))  return 1;
    if (t == QLatin1String("Area"))  return 2;
    return 0;
}

static int catFromDisp(const QString& c) {
    if (c == QLatin1String("Displaybase")) return 0;
    if (c == QLatin1String("Other"))       return 2;
    return 1;   // Standard (default)
}

// Static fallback lookups for object classes whose S-52 lookups are
// dominated by conditional symbology (CS) procedures we don't execute.
// Each entry is inserted *only* if the class has no no-condition default
// already kept for its geometry — so any direct SY() default from the XML
// always wins.  Entries with a condAttr/condValue become conditional
// lookups so best-match selection can pick between them by feature
// attribute (e.g. a light's COLOUR).
//
// `dashedBoundary` adds a fallback LS(DASH, 2, CHMGF) line style alongside
// the symbol, suitable for restricted/regulated areas whose Symbolized
// boundaries would otherwise rely on LC() (not yet implemented).
struct Fallback {
    const char* obj;
    const char* sym;
    int  geom;             // 0 Point, 2 Area
    bool dashedBoundary;
    const char* condAttr;  // nullptr -> no condition (class default)
    const char* condValue;
};
static const Fallback kFallbacks[] = {
    // LIGHTS — synthetic best-match for CS(LIGHTS05) we don't execute.  S-57
    // COLOUR values: 1=white, 3=red, 4=green, 6=yellow.  Maps to the colour
    // variants OpenCPN bakes into the atlas; multi-colour lights (e.g. "3,4")
    // don't match any single-value rule and fall to the magenta default.
    { "LIGHTS", "LIGHTS11", 0, false, "COLOUR", "3"     },   // red
    { "LIGHTS", "LIGHTS12", 0, false, "COLOUR", "4"     },   // green
    { "LIGHTS", "LIGHTS13", 0, false, "COLOUR", "1"     },   // white
    { "LIGHTS", "LIGHTS13", 0, false, "COLOUR", "6"     },   // yellow
    { "LIGHTS", "LIGHTS14", 0, false, nullptr,  nullptr },   // default magenta

    { "UWTROC", "UWTROC03", 0, false, nullptr,  nullptr },   // underwater rock
    // Restricted Area: features without CATREA fall through to CS(RESARE02)
    // and otherwise render with no symbol or boundary.  ENTRES61 (entry-
    // restricted glyph) + dashed magenta boundary mirrors what OpenCPN draws
    // for the common "Regulated Navigation Area" RESARE features.
    { "RESARE", "ENTRES61", 2, true,  nullptr,  nullptr },
};

// ---- main -------------------------------------------------------------------

int main(int argc, char** argv) {
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

    // ---- parse symbols + lookups --------------------------------------------

    QVector<SymRecord> syms;
    QMap<QByteArray, int> symIndexByName;   // symbol name -> index in syms
    QVector<Lup> lups;

    // Color tokens (e.g. "TRFCD" -> magenta) used by LS() instructions, packed
    // as 0x00RRGGBB.  Resolved eagerly from the DAY_BRIGHT colour table; the
    // night/dusk variants would need a runtime toggle we don't have yet.
    QHash<QByteArray, uint32_t> colors;

    QXmlStreamReader xml(&xmlFile);
    bool inSymbols = false, inLookups = false, inBitmap = false;
    bool inDayColorTable = false;

    struct {
        QByteArray name; int16_t ax=0, ay=0, w=0, h=0, px=0, py=0;
        bool hasLoc = false;
    } sym;

    struct {
        QByteArray objClass; QString type, table, disp, instruction;
        QVector<Cond> conds;
    } look;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            const auto tag = xml.name();
            if (tag == u"color-table") {
                inDayColorTable =
                    (xml.attributes().value(u"name") == u"DAY_BRIGHT");
            }
            else if (inDayColorTable && tag == u"color") {
                const auto a = xml.attributes();
                const QByteArray name = a.value(u"name").trimmed().toLatin1();
                const int r = a.value(u"r").toInt();
                const int g = a.value(u"g").toInt();
                const int b = a.value(u"b").toInt();
                colors.insert(name, packRgb(r, g, b));
            }
            else if (tag == u"symbols") inSymbols = true;
            else if (tag == u"lookups") inLookups = true;
            else if (inSymbols) {
                if      (tag == u"symbol") sym = {};
                else if (tag == u"name")   sym.name = xml.readElementText().trimmed().toLatin1();
                else if (tag == u"bitmap") {
                    sym.w = static_cast<int16_t>(xml.attributes().value(u"width").toInt());
                    sym.h = static_cast<int16_t>(xml.attributes().value(u"height").toInt());
                    inBitmap = true;
                }
                else if (inBitmap && tag == u"graphics-location") {
                    sym.ax = static_cast<int16_t>(xml.attributes().value(u"x").toInt());
                    sym.ay = static_cast<int16_t>(xml.attributes().value(u"y").toInt());
                    sym.hasLoc = true;
                }
                else if (inBitmap && tag == u"pivot") {
                    sym.px = static_cast<int16_t>(xml.attributes().value(u"x").toInt());
                    sym.py = static_cast<int16_t>(xml.attributes().value(u"y").toInt());
                }
            }
            else if (inLookups) {
                if (tag == u"lookup") {
                    look = {};
                    look.objClass = xml.attributes().value(u"name").trimmed().toLatin1();
                }
                else if (tag == u"type")        look.type  = xml.readElementText().trimmed();
                else if (tag == u"table-name")  look.table = xml.readElementText().trimmed();
                else if (tag == u"display-cat") look.disp  = xml.readElementText().trimmed();
                else if (tag == u"instruction") look.instruction = xml.readElementText().trimmed();
                else if (tag == u"attrib-code") {
                    // e.g. "BOYSHP4" or "COLOUR3,4,3".  When the entry is just
                    // the 6-char acronym (or acronym + whitespace), it is a
                    // *presence* marker — the lookup applies when the feature
                    // carries that attribute at all (e.g. ORIENT for rotated
                    // arrows, OBJNAM for labels).  Encoded with value "*" so
                    // the runtime can distinguish "attribute present" from
                    // "attribute equals empty string".
                    QByteArray raw = xml.readElementText().toLatin1();
                    raw.replace(' ', "");
                    if (raw.size() >= 6) {
                        Cond c;
                        c.attr  = raw.left(6);
                        c.value = raw.mid(6);
                        if (c.value.isEmpty()) c.value = "*";
                        look.conds.append(c);
                    }
                }
            }
        }
        else if (xml.isEndElement()) {
            const auto tag = xml.name();
            if      (tag == u"color-table") inDayColorTable = false;
            else if (tag == u"symbols")     inSymbols = false;
            else if (tag == u"lookups")     inLookups = false;
            else if (inSymbols && tag == u"bitmap") inBitmap = false;
            else if (inSymbols && tag == u"symbol") {
                if (!sym.name.isEmpty() && sym.hasLoc && sym.w > 0 && sym.h > 0) {
                    if (!symIndexByName.contains(sym.name)) {
                        SymRecord r{};
                        padCopy(r.name, sizeof(r.name), sym.name);
                        r.atlas_x = sym.ax; r.atlas_y = sym.ay;
                        r.width   = sym.w;  r.height  = sym.h;
                        r.pivot_x = sym.px; r.pivot_y = sym.py;
                        symIndexByName[sym.name] = syms.size();
                        syms.append(r);
                    }
                }
            }
            else if (inLookups && tag == u"lookup") {
                Lup l;
                l.objClass = look.objClass;
                l.geomType = geomFromType(look.type);
                l.dispCat  = catFromDisp(look.disp);
                l.table    = look.table.toLatin1();
                l.conds    = look.conds;
                const SyParse sy = firstSY(look.instruction);
                l.symName    = sy.name;
                l.rotMode    = sy.rotMode;
                l.instruction = look.instruction;   // for LS() resolution below
                if (!l.objClass.isEmpty())
                    lups.append(l);
            }
        }
    }

    if (xml.hasError()) {
        std::fprintf(stderr, "XML error at line %lld: %s\n",
                     (long long)xml.lineNumber(),
                     xml.errorString().toUtf8().constData());
        return 1;
    }

    // ---- select the preferred table per (class, geomType) -------------------
    //
    // Point: prefer "Paper", else "Simplified".
    // Line:  "Lines" (the only line table).
    // Area:  prefer "Symbolized", else "Plain".
    auto preferredTable = [](int geom, const QSet<QByteArray>& tablesPresent) -> QByteArray {
        if (geom == 0) {   // Point
            if (tablesPresent.contains("Paper"))      return "Paper";
            if (tablesPresent.contains("Simplified")) return "Simplified";
        } else if (geom == 1) {   // Line
            if (tablesPresent.contains("Lines"))      return "Lines";
        } else if (geom == 2) {   // Area
            if (tablesPresent.contains("Symbolized")) return "Symbolized";
            if (tablesPresent.contains("Plain"))      return "Plain";
        }
        return {};
    };

    // Gather, per (class|geom), the set of tables present.
    QMap<QByteArray, QSet<QByteArray>> tablesByKey;
    auto keyOf = [](const QByteArray& cls, int geom) {
        return cls + "|" + QByteArray::number(geom);
    };
    for (const Lup& l : lups) {
        tablesByKey[keyOf(l.objClass, l.geomType)].insert(l.table);
    }

    // Dedup'd LineStyle pool. Many lookups share the same LS() (e.g. dozens
    // of "SOLD,1,CHGRD" outlines), so pooling keeps the binary compact.
    QVector<LineStyle> lineStylePool;
    QHash<LineStyle, int> lineStyleIdx;
    auto internLineStyle = [&](const LineStyle& s) -> int {
        const auto it = lineStyleIdx.constFind(s);
        if (it != lineStyleIdx.constEnd()) return it.value();
        const int idx = lineStylePool.size();
        lineStylePool.append(s);
        lineStyleIdx.insert(s, idx);
        return idx;
    };

    QVector<FillStyle> fillStylePool;
    QHash<FillStyle, int> fillStyleIdx;
    auto internFillStyle = [&](const FillStyle& s) -> int {
        const auto it = fillStyleIdx.constFind(s);
        if (it != fillStyleIdx.constEnd()) return it.value();
        const int idx = fillStylePool.size();
        fillStylePool.append(s);
        fillStyleIdx.insert(s, idx);
        return idx;
    };

    // Keep lookups in the preferred table that produce at least one of:
    //   - a resolvable symbol (points + symbolized areas)
    //   - a line style  (line features + area outlines)
    //   - a fill style  (area-color washes like TSEZNE, RAPIDS, RUNWAY)
    // Lookups with none are dropped (e.g. CS-only point lookups that don't
    // resolve to an atlas tile).
    QVector<Lup> kept;
    QSet<QByteArray> emittedPointClasses;
    for (Lup l : lups) {
        const QByteArray want =
            preferredTable(l.geomType, tablesByKey[keyOf(l.objClass, l.geomType)]);
        if (want.isEmpty() || l.table != want) continue;

        const bool haveSym = !l.symName.isEmpty() &&
                             symIndexByName.contains(l.symName);

        LineStyle ls{};
        const bool haveLine = firstLS(l.instruction, colors, ls);
        if (haveLine) l.lineStyleIdx = internLineStyle(ls);

        FillStyle fs{};
        const bool haveFill = (l.geomType == 2) &&
                              firstAC(l.instruction, colors, fs);
        if (haveFill) l.fillStyleIdx = internFillStyle(fs);

        if (!haveSym) l.symName.clear();   // unresolved/missing: don't carry it

        if (!haveSym && !haveLine && !haveFill) continue;
        kept.append(l);
        if (l.geomType == 0 && haveSym) emittedPointClasses.insert(l.objClass);
    }

    // Append CS-only fallbacks for classes that ended up with no no-condition
    // default in `kept`.  A direct SY() default from the XML always wins; this
    // only fills the gap when the XML's default is a CS(...) procedure (which
    // we can't execute) and every conditional lookup failed to match.
    QSet<QByteArray> hasDefault;
    for (const Lup& l : kept)
        if (l.conds.isEmpty())
            hasDefault.insert(keyOf(l.objClass, l.geomType));

    LineStyle dashMagenta{};
    {
        const auto it = colors.constFind(QByteArrayLiteral("CHMGF"));
        const uint32_t rgb = (it != colors.constEnd()) ? it.value() : 0xD3A6E9u;
        dashMagenta.pattern = 1;   // DASH
        dashMagenta.width   = 2;
        dashMagenta.r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
        dashMagenta.g = static_cast<uint8_t>((rgb >>  8) & 0xFF);
        dashMagenta.b = static_cast<uint8_t>( rgb        & 0xFF);
    }

    int fallbacksAdded = 0;
    for (const auto& fb : kFallbacks) {
        const QByteArray obj(fb.obj);
        const QByteArray symn(fb.sym);
        if (hasDefault.contains(keyOf(obj, fb.geom))) continue;
        if (!symIndexByName.contains(symn))           continue;
        Lup l;
        l.objClass = obj;
        l.geomType = fb.geom;
        l.dispCat  = 1;
        l.symName  = symn;
        if (fb.condAttr) {
            Cond c;
            c.attr  = fb.condAttr;
            c.value = fb.condValue;
            l.conds.append(c);
        }
        if (fb.dashedBoundary)
            l.lineStyleIdx = internLineStyle(dashMagenta);
        kept.append(l);
        ++fallbacksAdded;
    }

    // Stable group by (class, geom) so each class's lookups are contiguous, and
    // within a group put more-specific (more conditions) first — purely for
    // deterministic, readable output; the runtime re-scores anyway.
    std::stable_sort(kept.begin(), kept.end(), [](const Lup& a, const Lup& b) {
        if (a.objClass != b.objClass) return a.objClass < b.objClass;
        if (a.geomType != b.geomType) return a.geomType < b.geomType;
        return a.conds.size() > b.conds.size();
    });

    // ---- build condition pool + relevant-attribute set ----------------------
    QVector<CondRecord> condPool;
    QSet<QByteArray> attrSet;
    bool needsOrient = false;
    for (const Lup& l : kept) {
        for (const Cond& c : l.conds) attrSet.insert(c.attr);
        if (l.rotMode == 1) needsOrient = true;
    }
    // ORIENT is read at runtime to drive rotation for SY(...,ORIENT) lookups,
    // even when no condition tests it.  Make sure the loader reads it.
    if (needsOrient) attrSet.insert(QByteArrayLiteral("ORIENT"));
    QList<QByteArray> attrList = attrSet.values();
    std::sort(attrList.begin(), attrList.end());

    // ---- write binary -------------------------------------------------------
    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "Cannot write output: %s\n", argv[2]);
        return 1;
    }

    // Build LupRecords (and fill condPool as we go).
    QVector<LupRecord> lupRecs;
    lupRecs.reserve(kept.size());
    for (const Lup& l : kept) {
        LupRecord lr{};
        padCopy(lr.objClass, sizeof(lr.objClass), l.objClass);
        lr.geomType = static_cast<uint8_t>(l.geomType);
        lr.dispCat  = static_cast<uint8_t>(l.dispCat);
        lr.nConds   = static_cast<uint8_t>(std::min<int>(static_cast<int>(l.conds.size()), 255));
        lr.rotMode  = l.rotMode;
        lr.condStart = static_cast<uint16_t>(condPool.size());
        lr.symIdx    = l.symName.isEmpty()
                       ? uint16_t(0xFFFF)
                       : static_cast<uint16_t>(symIndexByName.value(l.symName, 0xFFFF));
        lr.lineStyleIdx = (l.lineStyleIdx < 0)
                       ? uint16_t(0xFFFF)
                       : static_cast<uint16_t>(l.lineStyleIdx);
        lr.fillStyleIdx = (l.fillStyleIdx < 0)
                       ? uint16_t(0xFFFF)
                       : static_cast<uint16_t>(l.fillStyleIdx);
        for (int i = 0; i < lr.nConds; ++i) {
            CondRecord cr{};
            padCopy(cr.attr,  sizeof(cr.attr),  l.conds[i].attr);
            padCopy(cr.value, sizeof(cr.value), l.conds[i].value);
            condPool.append(cr);
        }
        lupRecs.append(lr);
    }

    Header hdr{};
    hdr.magic[0]='S'; hdr.magic[1]='Y'; hdr.magic[2]='M'; hdr.magic[3]='\x05';
    hdr.symCount  = static_cast<uint32_t>(syms.size());
    hdr.lupCount  = static_cast<uint32_t>(lupRecs.size());
    hdr.condCount = static_cast<uint32_t>(condPool.size());
    hdr.attrCount = static_cast<uint32_t>(attrList.size());
    hdr.lineCount = static_cast<uint32_t>(lineStylePool.size());
    hdr.fillCount = static_cast<uint32_t>(fillStylePool.size());

    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(syms.constData()),
              syms.size() * static_cast<qsizetype>(sizeof(SymRecord)));
    out.write(reinterpret_cast<const char*>(lupRecs.constData()),
              lupRecs.size() * static_cast<qsizetype>(sizeof(LupRecord)));
    out.write(reinterpret_cast<const char*>(condPool.constData()),
              condPool.size() * static_cast<qsizetype>(sizeof(CondRecord)));
    for (const QByteArray& a : attrList) {
        AttrRecord ar{};
        padCopy(ar.acronym, sizeof(ar.acronym), a);
        out.write(reinterpret_cast<const char*>(&ar), sizeof(ar));
    }
    for (const LineStyle& s : lineStylePool) {
        LineStyleRecord lr{};
        lr.pattern = s.pattern; lr.width = s.width;
        lr.r = s.r; lr.g = s.g; lr.b = s.b;
        out.write(reinterpret_cast<const char*>(&lr), sizeof(lr));
    }
    for (const FillStyle& s : fillStylePool) {
        FillStyleRecord fr{};
        fr.r = s.r; fr.g = s.g; fr.b = s.b; fr.a = s.a;
        out.write(reinterpret_cast<const char*>(&fr), sizeof(fr));
    }
    out.close();

    std::fprintf(stdout,
        "gen_symbols: %d syms, %d lookups (%d fallbacks), %d conds, %d attrs, %d lines, %d fills\n",
        static_cast<int>(syms.size()), static_cast<int>(lupRecs.size()),
        fallbacksAdded, static_cast<int>(condPool.size()),
        static_cast<int>(attrList.size()),
        static_cast<int>(lineStylePool.size()),
        static_cast<int>(fillStylePool.size()));
    std::fprintf(stdout, "gen_symbols: wrote %s (%lld bytes)\n",
                 argv[2], (long long)QFileInfo(out).size());
    return 0;
}
