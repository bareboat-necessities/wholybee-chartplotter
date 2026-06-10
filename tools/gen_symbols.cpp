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
//   Header (20 bytes)
//     char     magic[4]   = "SYM\x02"
//     uint32_t symCount   number of SymRecord
//     uint32_t lupCount   number of LupRecord
//     uint32_t condCount  number of CondRecord (attribute conditions pool)
//     uint32_t attrCount  number of AttrRecord (relevant-attribute acronyms)
//
//   SymRecord  x symCount  (36 bytes)  -- atlas tiles
//     char    name[24]; int16 atlas_x,atlas_y,width,height,pivot_x,pivot_y
//
//   LupRecord  x lupCount  (16 bytes)  -- one lookup
//     char     objClass[8]   null-padded S-57 class (e.g. "BOYLAT")
//     uint8_t  geomType      0=Point, 1=Line, 2=Area
//     uint8_t  dispCat       0=Displaybase, 1=Standard, 2=Other
//     uint8_t  nConds        attribute conditions for this lookup
//     uint8_t  _pad
//     uint16_t condStart     index of first CondRecord in the pool
//     uint16_t symIdx        resolved symbol index (0xFFFF if unresolved)
//
//   CondRecord x condCount  (32 bytes)  -- attribute condition pool
//     char attr[8]    6-char S-57 attribute acronym, null-padded
//     char value[24]  required value, e.g. "4" or "3,4,3"
//
//   AttrRecord x attrCount  (8 bytes)  -- union of acronyms used in conditions
//     char acronym[8]
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
};
static_assert(sizeof(Header) == 20, "Header size");

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
    uint8_t  _pad;
    uint16_t condStart;
    uint16_t symIdx;
};
static_assert(sizeof(LupRecord) == 16, "LupRecord size");

struct CondRecord {
    char attr[8];
    char value[24];
};
static_assert(sizeof(CondRecord) == 32, "CondRecord size");

struct AttrRecord {
    char acronym[8];
};
static_assert(sizeof(AttrRecord) == 8, "AttrRecord size");
#pragma pack(pop)

// ---- intermediate (parse-time) structures -----------------------------------

struct Cond { QByteArray attr; QByteArray value; };

struct Lup {
    QByteArray objClass;
    int        geomType = 0;   // 0 Point, 1 Line, 2 Area
    int        dispCat  = 1;   // 0 base, 1 standard, 2 other
    QByteArray table;          // "Paper", "Simplified", "Plain", "Symbolized", "Lines"
    QVector<Cond> conds;
    QByteArray symName;        // first SY() symbol; empty if none
};

// ---- helpers ----------------------------------------------------------------

static void padCopy(char* dst, int dstSize, const QByteArray& src) {
    std::memset(dst, 0, static_cast<std::size_t>(dstSize));
    int n = std::min(static_cast<int>(src.size()), dstSize - 1);
    std::memcpy(dst, src.constData(), static_cast<std::size_t>(n));
}

static QByteArray firstSY(const QString& instruction) {
    static const QRegularExpression re(QStringLiteral(R"(SY\(([^,)]+))"));
    const auto m = re.match(instruction);
    return m.hasMatch() ? m.captured(1).trimmed().toLatin1() : QByteArray{};
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

// Static fallback symbols for object classes whose lookups use only
// conditional symbology (CS) procedures and thus carry no direct SY() symbol.
// Inserted as a no-condition Point default only if the class ends up with no
// emitted point lookup at all.  Follows S-52 / INT 1 conventions.
static const struct { const char* obj; const char* sym; } kFallbacks[] = {
    { "LIGHTS", "LIGHTS11" },   // generic light
    { "UWTROC", "UWTROC03" },   // underwater rock, covers/uncovers
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

    QXmlStreamReader xml(&xmlFile);
    bool inSymbols = false, inLookups = false, inBitmap = false;

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
            if      (tag == u"symbols") inSymbols = true;
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
                    // e.g. "BOYSHP4" or "COLOUR3,4,3"
                    const QByteArray raw = xml.readElementText().trimmed().toLatin1();
                    if (raw.size() > 6) {
                        Cond c;
                        c.attr  = raw.left(6);
                        c.value = raw.mid(6);
                        c.value.replace(" ", "");   // normalize
                        look.conds.append(c);
                    }
                }
            }
        }
        else if (xml.isEndElement()) {
            const auto tag = xml.name();
            if      (tag == u"symbols") inSymbols = false;
            else if (tag == u"lookups") inLookups = false;
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
                l.symName  = firstSY(look.instruction);
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
    // Area:  prefer "Symbolized", else "Plain".
    // Line:  dropped.
    auto preferredTable = [](int geom, const QSet<QByteArray>& tablesPresent) -> QByteArray {
        if (geom == 0) {   // Point
            if (tablesPresent.contains("Paper"))      return "Paper";
            if (tablesPresent.contains("Simplified")) return "Simplified";
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
        if (l.geomType == 1) continue;   // skip lines
        tablesByKey[keyOf(l.objClass, l.geomType)].insert(l.table);
    }

    // Keep only lookups in the preferred table for their (class, geom), and
    // only those that resolve to a symbol (drop CS-only / SY-less lookups).
    QVector<Lup> kept;
    QSet<QByteArray> emittedPointClasses;   // classes with >=1 emitted Point lup
    for (const Lup& l : lups) {
        if (l.geomType == 1) continue;
        if (l.symName.isEmpty()) continue;                 // no direct symbol
        if (!symIndexByName.contains(l.symName)) continue; // symbol not in atlas
        const QByteArray want = preferredTable(l.geomType, tablesByKey[keyOf(l.objClass, l.geomType)]);
        if (want.isEmpty() || l.table != want) continue;
        kept.append(l);
        if (l.geomType == 0) emittedPointClasses.insert(l.objClass);
    }

    // Append CS-only fallbacks for point classes that ended up with no symbol.
    int fallbacksAdded = 0;
    for (const auto& fb : kFallbacks) {
        const QByteArray obj(fb.obj);
        const QByteArray symn(fb.sym);
        if (emittedPointClasses.contains(obj)) continue;
        if (!symIndexByName.contains(symn))    continue;
        Lup l;
        l.objClass = obj; l.geomType = 0; l.dispCat = 1;
        l.symName = symn;   // no conditions -> class default
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
    for (const Lup& l : kept) {
        for (const Cond& c : l.conds) attrSet.insert(c.attr);
    }
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
        lr.condStart = static_cast<uint16_t>(condPool.size());
        lr.symIdx    = static_cast<uint16_t>(symIndexByName.value(l.symName, 0xFFFF));
        for (int i = 0; i < lr.nConds; ++i) {
            CondRecord cr{};
            padCopy(cr.attr,  sizeof(cr.attr),  l.conds[i].attr);
            padCopy(cr.value, sizeof(cr.value), l.conds[i].value);
            condPool.append(cr);
        }
        lupRecs.append(lr);
    }

    Header hdr{};
    hdr.magic[0]='S'; hdr.magic[1]='Y'; hdr.magic[2]='M'; hdr.magic[3]='\x02';
    hdr.symCount  = static_cast<uint32_t>(syms.size());
    hdr.lupCount  = static_cast<uint32_t>(lupRecs.size());
    hdr.condCount = static_cast<uint32_t>(condPool.size());
    hdr.attrCount = static_cast<uint32_t>(attrList.size());

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
    out.close();

    std::fprintf(stdout,
        "gen_symbols: %d symbols, %d lookups (%d fallbacks), %d conditions, %d attrs\n",
        static_cast<int>(syms.size()), static_cast<int>(lupRecs.size()),
        fallbacksAdded, static_cast<int>(condPool.size()),
        static_cast<int>(attrList.size()));
    std::fprintf(stdout, "gen_symbols: wrote %s (%lld bytes)\n",
                 argv[2], (long long)QFileInfo(out).size());
    return 0;
}
