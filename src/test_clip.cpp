// Standalone unit tests for the pure pieces of the cache/clipping work:
//   - geom_clip.hpp : polygon/polyline/point clipping math
//   - feature_cache.hpp : LRU eviction, byte/entry budgets, pinning, MRU touch
//
// These are the parts whose correctness the GUI can't easily reveal — in
// particular the clip math, where a bug could erase visible geometry (blank
// water). Build/run with the accompanying run_tests.sh.

#include "src/geom_clip.hpp"
#include "src/feature_cache.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("  FAIL: %s  (line %d)\n", #cond, __LINE__); ++g_failures; } \
} while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Unit square clip rect for most geometry tests.
static BBox unitRect() { BBox r; r.minx = 0; r.miny = 0; r.maxx = 10; r.maxy = 10; return r; }

// Signed area magnitude, to compare polygons without depending on vertex order.
static double area(const std::vector<Pt>& p) {
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Pt& u = p[i];
        const Pt& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return std::fabs(a) * 0.5;
}

static void test_ring_fully_inside() {
    std::printf("ring fully inside (unchanged)\n");
    std::vector<Pt> ring = {{2,2},{8,2},{8,8},{2,8}};
    auto out = geom::clipRingToRect(ring, unitRect());
    CHECK(out.size() == 4);
    CHECK(near(area(out), 36.0));   // 6x6
}

static void test_ring_fully_outside() {
    std::printf("ring fully outside (empty)\n");
    std::vector<Pt> ring = {{20,20},{30,20},{30,30},{20,30}};
    auto out = geom::clipRingToRect(ring, unitRect());
    CHECK(out.empty());
}

static void test_ring_straddling() {
    std::printf("ring straddling boundary (clipped to overlap)\n");
    // Square (-5..5)^2 overlaps the rect in the (0..5)^2 corner = area 25.
    std::vector<Pt> ring = {{-5,-5},{5,-5},{5,5},{-5,5}};
    auto out = geom::clipRingToRect(ring, unitRect());
    CHECK(!out.empty());
    CHECK(near(area(out), 25.0));
    for (const Pt& p : out) {
        CHECK(p.x >= -1e-9 && p.x <= 10 + 1e-9);
        CHECK(p.y >= -1e-9 && p.y <= 10 + 1e-9);
    }
}

static void test_ring_with_closing_duplicate() {
    std::printf("ring with explicit closing duplicate vertex\n");
    std::vector<Pt> ring = {{2,2},{8,2},{8,8},{2,8},{2,2}};  // closed
    auto out = geom::clipRingToRect(ring, unitRect());
    CHECK(near(area(out), 36.0));
}

static void test_degenerate_ring() {
    std::printf("degenerate ring (<3 distinct verts) -> empty\n");
    std::vector<Pt> line = {{1,1},{2,2}};
    CHECK(geom::clipRingToRect(line, unitRect()).empty());
}

static void test_polyline_fully_inside() {
    std::printf("polyline fully inside (one run, unchanged)\n");
    std::vector<Pt> line = {{1,1},{5,5},{9,1}};
    auto runs = geom::clipPolylineToRect(line, unitRect());
    CHECK(runs.size() == 1);
    CHECK(runs[0].size() == 3);
}

static void test_polyline_fully_outside() {
    std::printf("polyline fully outside (no runs)\n");
    std::vector<Pt> line = {{-5,-5},{-2,-3}};
    auto runs = geom::clipPolylineToRect(line, unitRect());
    CHECK(runs.empty());
}

static void test_polyline_crossing_in_out() {
    std::printf("polyline crossing in/out then back -> two runs\n");
    // Starts inside, exits right, comes back inside: two separate visible runs.
    std::vector<Pt> line = {{5,5},{15,5},{15,8},{5,8}};
    auto runs = geom::clipPolylineToRect(line, unitRect());
    CHECK(runs.size() == 2);
    // Each run's points must lie on/inside the rect.
    for (const auto& run : runs)
        for (const Pt& p : run) {
            CHECK(p.x >= -1e-9 && p.x <= 10 + 1e-9);
            CHECK(p.y >= -1e-9 && p.y <= 10 + 1e-9);
        }
}

static void test_polyline_run_stitching() {
    std::printf("contiguous interior segments stitch into one run\n");
    // All points inside; three segments should become a single 4-point run,
    // not three 2-point runs.
    std::vector<Pt> line = {{1,1},{3,3},{6,2},{9,9}};
    auto runs = geom::clipPolylineToRect(line, unitRect());
    CHECK(runs.size() == 1);
    CHECK(runs[0].size() == 4);
}

static void test_point_in_rect() {
    std::printf("pointInRect inclusive of edges\n");
    BBox r = unitRect();
    CHECK(geom::pointInRect({5,5}, r));
    CHECK(geom::pointInRect({0,0}, r));    // corner, inclusive
    CHECK(geom::pointInRect({10,10}, r));  // corner, inclusive
    CHECK(!geom::pointInRect({-0.1,5}, r));
    CHECK(!geom::pointInRect({5,10.1}, r));
}

// ---- FeatureCache --------------------------------------------------------

using FeaturesPtr = FeatureCache::FeaturesPtr;

static FeaturesPtr makeFeatures(int nRings = 1) {
    auto v = std::make_shared<std::vector<Feature>>();
    Feature f;
    for (int i = 0; i < nRings; ++i) f.rings.push_back({{0,0},{1,0},{1,1}});
    v->push_back(f);
    return v;
}

static void test_cache_hit_miss() {
    std::printf("cache get: hit returns features, miss returns null\n");
    FeatureCache c;
    CHECK(c.get("nope") == nullptr);
    c.put("a", makeFeatures(), 100);
    CHECK(c.get("a") != nullptr);
    CHECK(c.contains("a"));
    CHECK(c.count() == 1);
}

static void test_cache_entry_limit_eviction() {
    std::printf("entry-count limit evicts LRU\n");
    FeatureCache c;
    c.setLimits(1u << 30, 2);              // big bytes, max 2 entries
    c.put("a", makeFeatures(), 10);
    c.put("b", makeFeatures(), 10);
    c.put("c", makeFeatures(), 10);        // pushes over -> evict LRU ("a")
    CHECK(c.count() == 2);
    CHECK(!c.contains("a"));
    CHECK(c.contains("b"));
    CHECK(c.contains("c"));
}

static void test_cache_byte_budget_eviction() {
    std::printf("byte budget evicts LRU until it fits\n");
    FeatureCache c;
    c.setLimits(250, 1000);                // 250-byte budget
    c.put("a", makeFeatures(), 100);
    c.put("b", makeFeatures(), 100);
    c.put("c", makeFeatures(), 100);       // 300 > 250 -> evict "a"
    CHECK(!c.contains("a"));
    CHECK(c.contains("b"));
    CHECK(c.contains("c"));
    CHECK(c.bytes() == 200);
}

static void test_cache_get_touches_mru() {
    std::printf("get() promotes entry to MRU (protects it from next eviction)\n");
    FeatureCache c;
    c.setLimits(1u << 30, 2);
    c.put("a", makeFeatures(), 10);
    c.put("b", makeFeatures(), 10);
    CHECK(c.get("a") != nullptr);          // "a" now MRU, "b" is LRU
    c.put("c", makeFeatures(), 10);        // evict LRU -> "b", keep "a"
    CHECK(c.contains("a"));
    CHECK(!c.contains("b"));
    CHECK(c.contains("c"));
}

static void test_cache_pinning() {
    std::printf("pinned (on-screen) entries are never evicted\n");
    FeatureCache c;
    c.setLimits(1u << 30, 1);              // only room for 1 unpinned
    // Pin "keep": it represents an on-screen cell.
    c.setPinned([](const QString& p){ return p == "keep"; });
    c.put("keep", makeFeatures(), 10);
    c.put("x", makeFeatures(), 10);        // over entry limit, but...
    c.put("y", makeFeatures(), 10);
    CHECK(c.contains("keep"));             // pinned survives despite limit=1
    // The soft limit means we may exceed maxEntries to retain pinned data.
    CHECK(c.count() >= 1);
}

static void test_cache_put_replace_updates_bytes() {
    std::printf("re-put same key updates byte total, not count\n");
    FeatureCache c;
    c.put("a", makeFeatures(), 100);
    CHECK(c.bytes() == 100);
    c.put("a", makeFeatures(), 250);       // replace
    CHECK(c.count() == 1);
    CHECK(c.bytes() == 250);
}

static void test_cache_remove_clear() {
    std::printf("remove and clear maintain byte/count invariants\n");
    FeatureCache c;
    c.put("a", makeFeatures(), 100);
    c.put("b", makeFeatures(), 100);
    c.remove("a");
    CHECK(!c.contains("a"));
    CHECK(c.bytes() == 100);
    CHECK(c.count() == 1);
    c.clear();
    CHECK(c.count() == 0);
    CHECK(c.bytes() == 0);
}

int main() {
    std::printf("== geom_clip ==\n");
    test_ring_fully_inside();
    test_ring_fully_outside();
    test_ring_straddling();
    test_ring_with_closing_duplicate();
    test_degenerate_ring();
    test_polyline_fully_inside();
    test_polyline_fully_outside();
    test_polyline_crossing_in_out();
    test_polyline_run_stitching();
    test_point_in_rect();

    std::printf("== FeatureCache ==\n");
    test_cache_hit_miss();
    test_cache_entry_limit_eviction();
    test_cache_byte_budget_eviction();
    test_cache_get_touches_mru();
    test_cache_pinning();
    test_cache_put_replace_updates_bytes();
    test_cache_remove_clear();

    if (g_failures == 0) { std::printf("\nALL TESTS PASSED\n"); return 0; }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
