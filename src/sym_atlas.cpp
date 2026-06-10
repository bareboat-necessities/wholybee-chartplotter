// src/sym_atlas.cpp
#include "sym_atlas.hpp"

#include <QFile>

#include <cstdint>
#include <cstring>

// ---- binary format (must match tools/gen_symbols.cpp) -----------------------

#pragma pack(push, 1)
struct BinHeader {
    char     magic[4];      // "SYM\x02"
    uint32_t symCount;
    uint32_t lupCount;
    uint32_t condCount;
    uint32_t attrCount;
};
struct BinSymRecord {
    char    name[24];
    int16_t atlas_x, atlas_y, width, height, pivot_x, pivot_y;
};
struct BinLupRecord {
    char     objClass[8];
    uint8_t  geomType, dispCat, nConds, _pad;
    uint16_t condStart, symIdx;
};
struct BinCondRecord {
    char attr[8];
    char value[24];
};
struct BinAttrRecord {
    char acronym[8];
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
        hdr->magic[2] != 'M' || hdr->magic[3] != '\x02')
        return false;

    const std::size_t symBytes  = std::size_t(hdr->symCount)  * sizeof(BinSymRecord);
    const std::size_t lupBytes  = std::size_t(hdr->lupCount)  * sizeof(BinLupRecord);
    const std::size_t condBytes = std::size_t(hdr->condCount) * sizeof(BinCondRecord);
    const std::size_t attrBytes = std::size_t(hdr->attrCount) * sizeof(BinAttrRecord);
    if (static_cast<std::size_t>(raw.size()) <
            sizeof(BinHeader) + symBytes + lupBytes + condBytes + attrBytes)
        return false;

    const char* base = raw.constData() + sizeof(BinHeader);
    const auto* symRecs  = reinterpret_cast<const BinSymRecord*>(base);
    const auto* lupRecs  = reinterpret_cast<const BinLupRecord*>(base + symBytes);
    const auto* condRecs = reinterpret_cast<const BinCondRecord*>(base + symBytes + lupBytes);
    const auto* attrRecs = reinterpret_cast<const BinAttrRecord*>(base + symBytes + lupBytes + condBytes);

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
        lups_[i] = Lup{ l.geomType, l.dispCat, l.nConds, l.condStart, l.symIdx };
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

    atlas_ = std::move(pm);
    return true;
}

// ---- queries ----------------------------------------------------------------

uint16_t SymAtlas::findSymbol(const QByteArray& name) const
{
    return nameIndex_.value(name, kNoSymbol);
}

uint16_t SymAtlas::symbolForFeature(const QByteArray& objClass, SymGeom geom,
                                    const AttrList& attrs) const
{
    const auto it = lupIndex_.constFind(key(objClass, geom));
    if (it == lupIndex_.constEnd())
        return kNoSymbol;

    const uint32_t first = it.value().first;
    const uint32_t count = it.value().second;

    // Linear lookup of a feature attribute value by acronym (few attrs/feature).
    auto featVal = [&attrs](const std::string& a) -> const std::string* {
        for (const auto& kv : attrs)
            if (kv.first == a) return &kv.second;
        return nullptr;
    };

    int      bestScore  = -1;
    uint16_t bestSym    = kNoSymbol;
    uint16_t defaultSym = kNoSymbol;
    bool     haveDefault = false;

    for (uint32_t i = first; i < first + count; ++i) {
        const Lup& l = lups_[i];
        if (l.nConds == 0) {
            // Class default (no conditions). Lowest priority; remember it.
            if (!haveDefault) { defaultSym = l.symIdx; haveDefault = true; }
            continue;
        }
        // A lookup matches only if every condition is satisfied.
        bool matched = true;
        for (uint16_t c = 0; c < l.nConds; ++c) {
            const Cond& cond = conds_[l.condStart + c];
            const std::string* fv = featVal(cond.attr);
            if (!fv || *fv != cond.value) { matched = false; break; }
        }
        if (matched && static_cast<int>(l.nConds) > bestScore) {
            bestScore = l.nConds;
            bestSym   = l.symIdx;
        }
    }

    if (bestSym != kNoSymbol) return bestSym;
    return haveDefault ? defaultSym : kNoSymbol;
}

// ---- drawing ----------------------------------------------------------------

void SymAtlas::draw(QPainter& p, uint16_t symIdx, QPointF d) const
{
    if (symIdx >= static_cast<uint16_t>(rects_.size()))
        return;
    const QRect&  src = rects_[symIdx];
    const QPoint& piv = pivots_[symIdx];
    p.drawPixmap(QPointF(d.x() - piv.x(), d.y() - piv.y()), atlas_, src);
}
