#pragma once
// src/sym_atlas.hpp
//
// Runtime symbol atlas: loads the prebaked symbols.bin lookup table and the
// corresponding rastersymbols-*.png sprite sheet, then draws symbols at
// constant on-screen size via single drawPixmap() blits.
//
// Thread safety: load() must be called once from the GUI thread before any
// worker thread calls symbolForObj() or findSymbol().  After load() returns,
// all three query methods are read-only and therefore safe to call from
// multiple threads simultaneously.

#include <QPixmap>
#include <QRect>
#include <QPoint>
#include <QHash>
#include <QByteArray>
#include <QPainter>
#include <QPointF>
#include <vector>
#include <cstdint>

class SymAtlas
{
public:
    // Sentinel returned when no symbol is found for a given name or class.
    static constexpr uint16_t kNoSymbol = 0xFFFFu;

    // Load the binary lookup table and the atlas PNG.
    // Returns true on success; on failure the atlas stays in its default
    // (un-loaded) state and rendering falls back to the magenta dot.
    bool load(const QString& binPath, const QString& pngPath);

    bool isLoaded() const { return !atlas_.isNull(); }

    // -- Lookups (thread-safe read-only after load()) -------------------------

    // Resolve an S-57 object-class name (e.g. "LIGHTS", "UWTROC") to the
    // index of the symbol that should be drawn at that point.
    uint16_t symbolForObj(const QByteArray& objClass) const;

    // Resolve a symbol name (e.g. "LIGHTS11") to its table index.
    uint16_t findSymbol(const QByteArray& name) const;

    // -- Drawing (call from the GUI thread, after resetTransform()) -----------

    // Draw symbol symIdx at screen point 'd', honouring the pivot offset so
    // the geographic position stays pinned to the correct pixel of the glyph.
    // Does nothing if symIdx == kNoSymbol or the atlas is not loaded.
    void draw(QPainter& p, uint16_t symIdx, QPointF d) const;

private:
    QPixmap atlas_;
    std::vector<QRect>  rects_;    // atlas source rect per symbol index
    std::vector<QPoint> pivots_;   // pivot (anchor) offset per symbol index
    QHash<QByteArray, uint16_t> nameIndex_;   // symbol name  → index
    QHash<QByteArray, uint16_t> objIndex_;    // S-57 class   → index
};
