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

// Douglas–Peucker line simplification: drop vertices that lie within `tol`
// (projected metres) of the polyline they'd otherwise add detail to. Endpoints
// are always kept, so closed rings stay closed. Iterative (explicit stack) to
// avoid deep recursion on long coastlines. With tol <= 0 the input is returned
// unchanged. Used at scene-build time to shed vertices that would be smaller
// than a fraction of a pixel at the band's display scale.
inline std::vector<Pt> simplify(const std::vector<Pt>& pts, double tol) {
    const int n = static_cast<int>(pts.size());
    if (n < 3 || tol <= 0.0) return pts;

    const double tol2 = tol * tol;
    std::vector<bool> keep(n, false);
    keep[0] = keep[n - 1] = true;

    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(0, n - 1);
    while (!stack.empty()) {
        const int a = stack.back().first, b = stack.back().second;
        stack.pop_back();

        const double ax = pts[a].x, ay = pts[a].y;
        const double dx = pts[b].x - ax, dy = pts[b].y - ay;
        const double len2 = dx * dx + dy * dy;

        double maxD2 = 0.0;
        int idx = -1;
        for (int i = a + 1; i < b; ++i) {
            double d2;
            if (len2 <= 0.0) {                       // a == b: distance to the point
                const double px = pts[i].x - ax, py = pts[i].y - ay;
                d2 = px * px + py * py;
            } else {
                double t = ((pts[i].x - ax) * dx + (pts[i].y - ay) * dy) / len2;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                const double ex = pts[i].x - (ax + t * dx);
                const double ey = pts[i].y - (ay + t * dy);
                d2 = ex * ex + ey * ey;
            }
            if (d2 > maxD2) { maxD2 = d2; idx = i; }
        }

        if (idx > 0 && maxD2 > tol2) {
            keep[idx] = true;
            stack.emplace_back(a, idx);
            stack.emplace_back(idx, b);
        }
    }

    std::vector<Pt> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        if (keep[i]) out.push_back(pts[i]);
    return out;
}

} // namespace geom
