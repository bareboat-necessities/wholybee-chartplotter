#pragma once
#include <cmath>
#include <vector>
#include "chart_loader.hpp"   // Pt, BBox

// Pure geometry: clipping polygons and polylines to an axis-aligned rectangle,
// in projected (Mercator metre) coordinates. No Qt, no GDAL — just math, so it
// can be unit-tested in isolation and reused by the renderer.
//
// The renderer caches each cell's full parse and clips at scene-build time to a
// region a little larger than the viewport. A coarse cell that only shows a
// sliver via gap-fill then contributes a screen-sized polygon instead of a
// basin-spanning one, so Qt traverses and rasterizes far less per frame. The
// clip region is always larger than the visible viewport, so the straight edges
// clipping introduces fall off-screen.
namespace geom {

// Sutherland–Hodgman: clip one polygon ring to a rect. The rect is convex, so a
// single ring clips to a single ring (possibly empty). An explicit closing
// duplicate vertex is tolerated and dropped.
inline std::vector<Pt> clipRingToRect(const std::vector<Pt>& in, const BBox& r) {
    if (in.size() < 3) return {};
    std::vector<Pt> poly(in.begin(), in.end());
    if (poly.size() >= 2 &&
        poly.front().x == poly.back().x && poly.front().y == poly.back().y)
        poly.pop_back();
    if (poly.size() < 3) return {};

    enum Side { L, R, B, T };
    auto inside = [&](const Pt& p, Side s) {
        switch (s) {
            case L: return p.x >= r.minx;
            case R: return p.x <= r.maxx;
            case B: return p.y >= r.miny;
            case T: return p.y <= r.maxy;
        }
        return true;
    };
    // Only called when endpoints straddle the boundary, so the denominator is
    // nonzero.
    auto isect = [&](const Pt& a, const Pt& b, Side s) {
        Pt o{}; double t;
        switch (s) {
            case L: t = (r.minx - a.x) / (b.x - a.x); o.x = r.minx; o.y = a.y + t * (b.y - a.y); break;
            case R: t = (r.maxx - a.x) / (b.x - a.x); o.x = r.maxx; o.y = a.y + t * (b.y - a.y); break;
            case B: t = (r.miny - a.y) / (b.y - a.y); o.y = r.miny; o.x = a.x + t * (b.x - a.x); break;
            case T: t = (r.maxy - a.y) / (b.y - a.y); o.y = r.maxy; o.x = a.x + t * (b.x - a.x); break;
        }
        return o;
    };
    for (Side s : {L, R, B, T}) {
        if (poly.empty()) break;
        std::vector<Pt> out;
        out.reserve(poly.size() + 4);
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Pt& cur  = poly[i];
            const Pt& prev = poly[(i + n - 1) % n];
            const bool curIn  = inside(cur, s);
            const bool prevIn = inside(prev, s);
            if (curIn) {
                if (!prevIn) out.push_back(isect(prev, cur, s));
                out.push_back(cur);
            } else if (prevIn) {
                out.push_back(isect(prev, cur, s));
            }
        }
        poly.swap(out);
    }
    return poly;
}

inline int outcode(const Pt& p, const BBox& r) {
    int c = 0;
    if (p.x < r.minx) c |= 1; else if (p.x > r.maxx) c |= 2;
    if (p.y < r.miny) c |= 4; else if (p.y > r.maxy) c |= 8;
    return c;
}

// Cohen–Sutherland clip of one segment to a rect.
inline bool clipSegment(Pt a, Pt b, const BBox& r, Pt& oa, Pt& ob) {
    int ca = outcode(a, r), cb = outcode(b, r);
    for (;;) {
        if (!(ca | cb)) { oa = a; ob = b; return true; }   // both inside
        if (ca & cb)    return false;                       // share an outside half-plane
        const int c = ca ? ca : cb;
        Pt p{};
        if      (c & 8) { p.x = a.x + (b.x - a.x) * (r.maxy - a.y) / (b.y - a.y); p.y = r.maxy; }
        else if (c & 4) { p.x = a.x + (b.x - a.x) * (r.miny - a.y) / (b.y - a.y); p.y = r.miny; }
        else if (c & 2) { p.y = a.y + (b.y - a.y) * (r.maxx - a.x) / (b.x - a.x); p.x = r.maxx; }
        else            { p.y = a.y + (b.y - a.y) * (r.minx - a.x) / (b.x - a.x); p.x = r.minx; }
        if (c == ca) { a = p; ca = outcode(a, r); }
        else         { b = p; cb = outcode(b, r); }
    }
}

// Clip a polyline to a rect, stitching adjacent visible segments into
// contiguous runs so the renderer emits one subpath per run, not per segment.
inline std::vector<std::vector<Pt>> clipPolylineToRect(const std::vector<Pt>& line,
                                                       const BBox& r) {
    std::vector<std::vector<Pt>> runs;
    if (line.size() < 2) return runs;
    constexpr double eps = 1e-6;
    for (std::size_t i = 1; i < line.size(); ++i) {
        Pt a, b;
        if (!clipSegment(line[i - 1], line[i], r, a, b)) continue;
        if (!runs.empty()) {
            const Pt& last = runs.back().back();
            if (std::fabs(last.x - a.x) <= eps && std::fabs(last.y - a.y) <= eps) {
                runs.back().push_back(b);
                continue;
            }
        }
        runs.push_back({a, b});
    }
    return runs;
}

inline bool pointInRect(const Pt& p, const BBox& r) {
    return p.x >= r.minx && p.x <= r.maxx && p.y >= r.miny && p.y <= r.maxy;
}

} // namespace geom
