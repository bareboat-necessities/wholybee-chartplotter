#pragma once
// src/sym_atlas.hpp
//
// Runtime symbol atlas + S-52 look-up table (LUP) engine.
//
// Loads the prebaked symbols.bin (atlas tiles + per-class lookup tables with
// attribute conditions) and the rastersymbols-*.png sprite sheet.  Resolves a
// feature to a symbol by best-match scoring against its S-57 attributes, then
// draws the symbol as a single drawPixmap() blit at constant on-screen size.
//
// Thread safety: load() must run once on the GUI thread before any worker calls
// symbolForFeature()/findSymbol().  After load() the data is immutable, so the
// query methods are safe to call concurrently from multiple worker threads.

#include <QPixmap>
#include <QRect>
#include <QPoint>
#include <QHash>
#include <QByteArray>
#include <QPainter>
#include <QPointF>
#include <vector>
#include <utility>
#include <string>
#include <cstdint>

// Geometry primitive of a feature, used to select the correct lookup table.
enum class SymGeom : uint8_t { Point = 0, Line = 1, Area = 2 };

class SymAtlas
{
public:
    static constexpr uint16_t kNoSymbol = 0xFFFFu;

    // A feature's symbology-relevant attributes: (6-char acronym, value string).
    // Value is the normalized S-57 value, e.g. "4" or "3,4,3".
    using AttrList = std::vector<std::pair<std::string, std::string>>;

    bool load(const QString& binPath, const QString& pngPath);
    bool isLoaded() const { return !atlas_.isNull(); }

    // The set of S-57 attribute acronyms that appear in any lookup condition.
    // The chart loader reads exactly these from each feature (and no others).
    const std::vector<std::string>& relevantAttrs() const { return attrs_; }

    // Resolve a feature (object class + geometry + attributes) to a symbol via
    // S-52 best-match: among the class's lookups whose every condition matches
    // the feature, the one with the most conditions wins; if none match, the
    // class's no-condition default is used.  Returns kNoSymbol if unresolved.
    uint16_t symbolForFeature(const QByteArray& objClass, SymGeom geom,
                              const AttrList& attrs) const;

    // Resolve a symbol name (e.g. "BOYPIL61") to its table index.
    uint16_t findSymbol(const QByteArray& name) const;

    // Draw symbol symIdx at screen point d, honouring the pivot offset.
    void draw(QPainter& p, uint16_t symIdx, QPointF d) const;

private:
    // One lookup: attribute conditions (a slice of conds_) + resulting symbol.
    struct Lup {
        uint8_t  geom;
        uint8_t  dispCat;
        uint8_t  nConds;
        uint16_t condStart;
        uint16_t symIdx;
    };
    struct Cond { std::string attr; std::string value; };

    QPixmap atlas_;
    std::vector<QRect>  rects_;
    std::vector<QPoint> pivots_;
    QHash<QByteArray, uint16_t> nameIndex_;

    std::vector<Lup>  lups_;
    std::vector<Cond> conds_;
    std::vector<std::string> attrs_;   // relevant-attribute acronyms

    // (objClass|geom) -> contiguous [first,count) range into lups_.
    QHash<QByteArray, std::pair<uint32_t, uint32_t>> lupIndex_;

    static QByteArray key(const QByteArray& objClass, SymGeom geom);
};
