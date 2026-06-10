// src/sym_atlas.cpp
#include "sym_atlas.hpp"

#include <QFile>
#include <QTransform>

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ---- binary format (must match tools/gen_symbols.cpp) -----------------------

#pragma pack(push, 1)
struct BinHeader {
    char     magic[4];      // "SYM\x05"
    uint32_t symCount;
    uint32_t lupCount;
    uint32_t condCount;
    uint32_t attrCount;
    uint32_t lineCount;
    uint32_t fillCount;
};
struct BinSymRecord {
    char    name[24];
    int16_t atlas_x, atlas_y, width, height, pivot_x, pivot_y;
};
struct BinLupRecord {
    char     objClass[8];
    uint8_t  geomType, dispCat, nConds, rotMode;
    uint16_t condStart, symIdx, lineStyleIdx, fillStyleIdx;
};
struct BinCondRecord {
    char attr[8];
    char value[24];
};
struct BinAttrRecord {
    char acronym[8];
};
struct BinLineStyleRecord {
    uint8_t pattern, width, r, g, b, _pad[3];
};
struct BinFillStyleRecord {
    uint8_t r, g, b, a;
};
#pragma pack(pop)

// ---- key helper -------------------------------------------------------------

QByteArray SymAtlas::key(const QByteArray& objClass, SymGeom geom)
{
    QByteArray k = objClass;
    k += '|';
    k += char('0' + static_cast<int>(geom));
    return k;
}

// ---- load -------------------------------------------------------------------

bool SymAtlas::load(const QString& binPath, const QString& pngPath)
{
    QPixmap pm(pngPath);
    if (pm.isNull())
        return false;

    QFile f(binPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    f.close();

    if (static_cast<std::size_t>(raw.size()) < sizeof(BinHeader))
        return false;
    const auto* hdr = reinterpret_cast<const BinHeader*>(raw.constData());
    if (hdr->magic[0] != 'S' || hdr->magic[1] != 'Y' ||
        hdr->magic[2] != 'M' || hdr->magic[3] != '\x05')
        return false;

    const std::size_t symBytes  = std::size_t(hdr->symCount)  * sizeof(BinSymRecord);
    const std::size_t lupBytes  = std::size_t(hdr->lupCount)  * sizeof(BinLupRecord);
    const std::size_t condBytes = std::size_t(hdr->condCount) * sizeof(BinCondRecord);
    const std::size_t attrBytes = std::size_t(hdr->attrCount) * sizeof(BinAttrRecord);
    const std::size_t lineBytes = std::size_t(hdr->lineCount) * sizeof(BinLineStyleRecord);
    const std::size_t fillBytes = std::size_t(hdr->fillCount) * sizeof(BinFillStyleRecord);
    if (static_cast<std::size_t>(raw.size()) <
            sizeof(BinHeader) + symBytes + lupBytes + condBytes + attrBytes +
            lineBytes + fillBytes)
        return false;

    const char* base = raw.constData() + sizeof(BinHeader);
    const auto* symRecs  = reinterpret_cast<const BinSymRecord*>(base);
    const auto* lupRecs  = reinterpret_cast<const BinLupRecord*>(base + symBytes);
    const auto* condRecs = reinterpret_cast<const BinCondRecord*>(base + symBytes + lupBytes);
    const auto* attrRecs = reinterpret_cast<const BinAttrRecord*>(base + symBytes + lupBytes + condBytes);
    const auto* lineRecs = reinterpret_cast<const BinLineStyleRecord*>(
        base + symBytes + lupBytes + condBytes + attrBytes);
    const auto* fillRecs = reinterpret_cast<const BinFillStyleRecord*>(
        base + symBytes + lupBytes + condBytes + attrBytes + lineBytes);

    // Symbols.
    rects_.resize(hdr->symCount);
    pivots_.resize(hdr->symCount);
    nameIndex_.reserve(static_cast<int>(hdr->symCount));
    for (uint32_t i = 0; i < hdr->symCount; ++i) {
        const BinSymRecord& s = symRecs[i];
        rects_[i]  = QRect(s.atlas_x, s.atlas_y, s.width, s.height);
        pivots_[i] = QPoint(s.pivot_x, s.pivot_y);
        nameIndex_[QByteArray(s.name)] = static_cast<uint16_t>(i);
    }

    // Condition pool.
    conds_.resize(hdr->condCount);
    for (uint32_t i = 0; i < hdr->condCount; ++i) {
        conds_[i].attr  = std::string(condRecs[i].attr,
                                      strnlen(condRecs[i].attr, sizeof(condRecs[i].attr)));
        conds_[i].value = std::string(condRecs[i].value,
                                      strnlen(condRecs[i].value, sizeof(condRecs[i].value)));
    }

    // Lookups (records are grouped by class+geom; build the contiguous index).
    lups_.resize(hdr->lupCount);
    for (uint32_t i = 0; i < hdr->lupCount; ++i) {
        const BinLupRecord& l = lupRecs[i];
        lups_[i] = Lup{ l.geomType, l.dispCat, l.nConds, l.rotMode,
                        l.condStart, l.symIdx, l.lineStyleIdx, l.fillStyleIdx };
        const QByteArray k = key(QByteArray(l.objClass),
                                 static_cast<SymGeom>(l.geomType));
        auto it = lupIndex_.find(k);
        if (it == lupIndex_.end()) lupIndex_.insert(k, { i, 1u });
        else                       it.value().second += 1u;   // grouped => contiguous
    }

    // Relevant-attribute acronyms.
    attrs_.reserve(hdr->attrCount);
    for (uint32_t i = 0; i < hdr->attrCount; ++i)
        attrs_.emplace_back(attrRecs[i].acronym,
                            strnlen(attrRecs[i].acronym, sizeof(attrRecs[i].acronym)));

    // Dedup'd line styles (LS pattern + width + RGB) referenced by lookups.
    lineStyles_.resize(hdr->lineCount);
    for (uint32_t i = 0; i < hdr->lineCount; ++i) {
        const BinLineStyleRecord& l = lineRecs[i];
        lineStyles_[i] = SymLineStyle{ l.pattern, l.width, l.r, l.g, l.b };
    }

    // Dedup'd fill styles (AC area-color washes, RGBA) referenced by lookups.
    fillStyles_.resize(hdr->fillCount);
    for (uint32_t i = 0; i < hdr->fillCount; ++i) {
        const BinFillStyleRecord& f = fillRecs[i];
        fillStyles_[i] = SymFillStyle{ f.r, f.g, f.b, f.a };
    }

    atlas_ = std::move(pm);
    return true;
}

// ---- queries ----------------------------------------------------------------

uint16_t SymAtlas::findSymbol(const QByteArray& name) const
{
    return nameIndex_.value(name, kNoSymbol);
}

SymHit SymAtlas::symbolForFeature(const QByteArray& objClass, SymGeom geom,
                                  const AttrList& attrs) const
{
    SymHit miss;
    const auto it = lupIndex_.constFind(key(objClass, geom));
    if (it == lupIndex_.constEnd())
        return miss;

    const uint32_t first = it.value().first;
    const uint32_t count = it.value().second;

    // Linear lookup of a feature attribute value by acronym (few attrs/feature).
    auto featVal = [&attrs](const std::string& a) -> const std::string* {
        for (const auto& kv : attrs)
            if (kv.first == a) return &kv.second;
        return nullptr;
    };

    int        bestScore = -1;
    const Lup* bestLup   = nullptr;
    const Lup* defaultLup = nullptr;   // no-condition fallback for this class

    for (uint32_t i = first; i < first + count; ++i) {
        const Lup& l = lups_[i];
        if (l.nConds == 0) {
            if (!defaultLup) defaultLup = &l;
            continue;
        }
        bool matched = true;
        for (uint16_t c = 0; c < l.nConds; ++c) {
            const Cond& cond = conds_[l.condStart + c];
            const std::string* fv = featVal(cond.attr);
            // "*" is the presence sentinel: any value matches, but the
            // attribute must be present on the feature.
            if (!fv) { matched = false; break; }
            if (cond.value != "*" && *fv != cond.value) {
                matched = false; break;
            }
        }
        if (matched && static_cast<int>(l.nConds) > bestScore) {
            bestScore = l.nConds;
            bestLup   = &l;
        }
    }
    const Lup* chosen = bestLup ? bestLup : defaultLup;
    if (!chosen) return miss;

    SymHit hit;
    hit.symIdx = chosen->symIdx;
    if (chosen->rotMode == 1) {
        // S-57 ORIENT, degrees CW from true north. atof() returns 0 for a
        // missing/unparseable value, which is the harmless default (upright).
        if (const std::string* o = featVal("ORIENT"))
            hit.rotationDeg = static_cast<float>(std::atof(o->c_str()));
    }
    if (chosen->lineStyleIdx < lineStyles_.size()) {
        hit.line    = lineStyles_[chosen->lineStyleIdx];
        hit.hasLine = true;
    }
    if (chosen->fillStyleIdx < fillStyles_.size()) {
        hit.fill    = fillStyles_[chosen->fillStyleIdx];
        hit.hasFill = true;
    }
    return hit;
}

// ---- drawing ----------------------------------------------------------------

void SymAtlas::draw(QPainter& p, uint16_t symIdx, QPointF d,
                    float rotationDeg) const
{
    if (symIdx >= static_cast<uint16_t>(rects_.size()))
        return;
    const QRect&  src = rects_[symIdx];
    const QPoint& piv = pivots_[symIdx];

    if (rotationDeg == 0.0f) {
        p.drawPixmap(QPointF(d.x() - piv.x(), d.y() - piv.y()), atlas_, src);
        return;
    }
    // Rotate the symbol around its pivot. Our scene has Y flipped north-up and
    // QPainter rotation is CW from screen +x, so a CW-from-north S-57 ORIENT
    // maps directly to QPainter::rotate(orient): an orient of 90° points the
    // glyph's "up" toward screen-east, matching the compass convention.
    const QTransform saved = p.transform();
    QTransform t = saved;
    t.translate(d.x(), d.y());
    t.rotate(rotationDeg);
    t.translate(-piv.x(), -piv.y());
    p.setTransform(t);
    p.drawPixmap(QPointF(0, 0), atlas_, src);
    p.setTransform(saved);
}
