// src/sym_atlas.cpp
#include "sym_atlas.hpp"

#include <QFile>
#include <QFileInfo>

#include <cstdint>
#include <cstring>

// ---- binary format (must match tools/gen_symbols.cpp) -----------------------

#pragma pack(push, 1)
struct BinHeader {
    char     magic[4];    // "SYM\x01"
    uint32_t symCount;
    uint32_t lookCount;
    uint32_t reserved;
};
struct BinSymRecord {
    char    name[24];
    int16_t atlas_x, atlas_y;
    int16_t width, height;
    int16_t pivot_x, pivot_y;
};
struct BinLookRecord {
    char objClass[8];
    char symName[24];
};
#pragma pack(pop)

// ---- SymAtlas::load ---------------------------------------------------------

bool SymAtlas::load(const QString& binPath, const QString& pngPath)
{
    // 1. Atlas PNG → QPixmap (GPU-backed on all Qt raster/GL backends).
    QPixmap pm(pngPath);
    if (pm.isNull())
        return false;

    // 2. Binary table.
    QFile f(binPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    f.close();

    if (static_cast<std::size_t>(raw.size()) < sizeof(BinHeader))
        return false;

    const auto* hdr = reinterpret_cast<const BinHeader*>(raw.constData());
    if (hdr->magic[0] != 'S' || hdr->magic[1] != 'Y' ||
        hdr->magic[2] != 'M' || hdr->magic[3] != '\x01')
        return false;

    const std::size_t symBytes  = hdr->symCount  * sizeof(BinSymRecord);
    const std::size_t lookBytes = hdr->lookCount * sizeof(BinLookRecord);
    if (static_cast<std::size_t>(raw.size()) <
            sizeof(BinHeader) + symBytes + lookBytes)
        return false;

    const auto* symRecs  = reinterpret_cast<const BinSymRecord*>(
        raw.constData() + sizeof(BinHeader));
    const auto* lookRecs = reinterpret_cast<const BinLookRecord*>(
        raw.constData() + sizeof(BinHeader) + symBytes);

    // 3. Build indexed arrays + name hash.
    rects_.resize(hdr->symCount);
    pivots_.resize(hdr->symCount);
    nameIndex_.reserve(static_cast<int>(hdr->symCount));

    for (uint32_t i = 0; i < hdr->symCount; ++i) {
        const BinSymRecord& s = symRecs[i];
        rects_[i]  = QRect(s.atlas_x, s.atlas_y, s.width, s.height);
        pivots_[i] = QPoint(s.pivot_x, s.pivot_y);
        // QByteArray(ptr) reads until the first '\0', which is correct for our
        // null-padded fixed-width name fields.
        nameIndex_[QByteArray(s.name)] = static_cast<uint16_t>(i);
    }

    // 4. Build object-class → symbol-index hash via the lookup table.
    objIndex_.reserve(static_cast<int>(hdr->lookCount));
    for (uint32_t i = 0; i < hdr->lookCount; ++i) {
        const BinLookRecord& lr = lookRecs[i];
        const QByteArray symName(lr.symName);
        const auto it = nameIndex_.constFind(symName);
        if (it != nameIndex_.constEnd())
            objIndex_[QByteArray(lr.objClass)] = it.value();
    }

    // 5. Commit — only after every step succeeds.
    atlas_ = std::move(pm);
    return true;
}

// ---- queries (thread-safe read-only after load) -----------------------------

uint16_t SymAtlas::symbolForObj(const QByteArray& objClass) const
{
    return objIndex_.value(objClass, kNoSymbol);
}

uint16_t SymAtlas::findSymbol(const QByteArray& name) const
{
    return nameIndex_.value(name, kNoSymbol);
}

// ---- drawing ----------------------------------------------------------------

void SymAtlas::draw(QPainter& p, uint16_t symIdx, QPointF d) const
{
    if (symIdx >= static_cast<uint16_t>(rects_.size()))
        return;
    const QRect&  src = rects_[symIdx];
    const QPoint& piv = pivots_[symIdx];
    // Offset so the pivot (geographic anchor within the tile) maps to 'd'.
    p.drawPixmap(QPointF(d.x() - piv.x(), d.y() - piv.y()), atlas_, src);
}
