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

// Result of resolving a feature against the lookup tables. May carry zero,
// one, or both of a symbol (with optional rotation) and a line style.
//
// rotationDeg follows S-57 ORIENT convention (degrees clockwise from true
// north). 0 means draw upright; QPainter's coordinate system shares this
// convention since our scene has Y flipped north-up.
//
// lineStyle is taken from the chosen lookup's LS() instruction:
//   For Line features it is the styling of the line itself.
//   For Area features it is the styling of the area's boundary.
struct SymLineStyle {
    enum Pattern : uint8_t { Solid = 0, Dash = 1, Dot = 2 };
    uint8_t pattern = Solid;
    uint8_t width   = 1;        // S-52 line-width units (~pixels)
    uint8_t r = 0, g = 0, b = 0;
};

// Translucent area-color wash from an AC() instruction.
// Alpha is resolved at bake time from the S-52 transparency factor (τ 0..4).
struct SymFillStyle {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct SymHit {
    uint16_t      symIdx       = 0xFFFFu;
    float         rotationDeg  = 0.0f;
    bool          hasLine      = false;
    SymLineStyle  line;
    bool          hasFill      = false;
    SymFillStyle  fill;
};

class SymAtlas
{
public:
    static constexpr uint16_t kNoSymbol = 0xFFFFu;

    // A feature's symbology-relevant attributes: (6-char acronym, value string).
    // Value is the normalized S-57 value, e.g. "4" or "3,4,3".
    using AttrList = std::vector<std::pair<std::string, std::string>>;

    bool load(const QString& binPath, const QString& pngPath);
    bool isLoaded() const { return !atlas_.isNull(); }

    // The set of S-57 attribute acronyms that appear in any lookup condition
    // OR are referenced by an instruction (e.g. ORIENT for rotation). The
    // chart loader reads exactly these from each feature (and no others).
    const std::vector<std::string>& relevantAttrs() const { return attrs_; }

    // Resolve a feature (object class + geometry + attributes) via S-52
    // best-match: among the class's lookups whose every condition matches the
    // feature, the one with the most conditions wins; the class's no-condition
    // default is used as fallback. The chosen lookup also dictates rotation
    // (e.g. SY(TSSLPT51,ORIENT) -> rotated by the feature's ORIENT attribute).
    SymHit symbolForFeature(const QByteArray& objClass, SymGeom geom,
                            const AttrList& attrs) const;

    // Resolve a symbol name (e.g. "BOYPIL61") to its table index.
    uint16_t findSymbol(const QByteArray& name) const;

    // Draw symbol symIdx at screen point d, honouring the pivot offset.
    // rotationDeg rotates the symbol around its pivot (degrees CW from north,
    // matching the S-57 ORIENT convention).
    void draw(QPainter& p, uint16_t symIdx, QPointF d,
              float rotationDeg = 0.0f) const;

private:
    // One lookup: attribute conditions (a slice of conds_) + resulting symbol,
    // line style, and/or fill style.
    struct Lup {
        uint8_t  geom;
        uint8_t  dispCat;
        uint8_t  nConds;
        uint8_t  rotMode;        // 0=none, 1=rotate by ORIENT
        uint16_t condStart;
        uint16_t symIdx;         // 0xFFFF when no atlas tile
        uint16_t lineStyleIdx;   // 0xFFFF when no LS() in the instruction
        uint16_t fillStyleIdx;   // 0xFFFF when no AC() in the instruction
    };
    struct Cond { std::string attr; std::string value; };

    QPixmap atlas_;
    std::vector<QRect>  rects_;
    std::vector<QPoint> pivots_;
    QHash<QByteArray, uint16_t> nameIndex_;

    std::vector<Lup>          lups_;
    std::vector<Cond>         conds_;
    std::vector<std::string>  attrs_;   // relevant-attribute acronyms
    std::vector<SymLineStyle> lineStyles_;
    std::vector<SymFillStyle> fillStyles_;

    // (objClass|geom) -> contiguous [first,count) range into lups_.
    QHash<QByteArray, std::pair<uint32_t, uint32_t>> lupIndex_;

    static QByteArray key(const QByteArray& objClass, SymGeom geom);
};
