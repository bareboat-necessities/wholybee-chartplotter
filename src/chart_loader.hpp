#pragma once
#include <string>
#include <vector>

// A projected point, in Mercator metres.
struct Pt {
    double x;
    double y;
};

// Axis-aligned bounding box in projected coordinates (north-up: +y is north).
struct BBox {
    double minx =  1e30, miny =  1e30;
    double maxx = -1e30, maxy = -1e30;

    void expand(double x, double y) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    void expand(const BBox& b) {
        if (!b.valid()) return;
        expand(b.minx, b.miny);
        expand(b.maxx, b.maxy);
    }
    bool valid() const { return maxx >= minx && maxy >= miny; }
    bool intersects(const BBox& o) const {
        return !(o.minx > maxx || o.maxx < minx || o.miny > maxy || o.maxy < miny);
    }
    // True if every point of o lies within this box (used to decide whether a
    // feature/cell is already fully inside a clip region, so clipping can be
    // skipped, and to detect when a cached clip no longer covers the view).
    bool contains(const BBox& o) const {
        return valid() && o.valid() &&
               o.minx >= minx && o.maxx <= maxx &&
               o.miny >= miny && o.maxy <= maxy;
    }
};

enum class FeatureKind {
    DepthArea, LandArea, OtherArea,
    DepthContour, Coastline, OtherLine,
    Sounding, Point
};

struct Feature {
    FeatureKind kind = FeatureKind::Point;
    int zorder = 0;
    std::vector<std::vector<Pt>> rings;
    double depth = 0.0;
    bool hasDepth = false;
    BBox bbox;
};

namespace chart {

// Call once at startup (registers GDAL drivers + sets S-57 options). Safe before
// spawning worker threads; the config it sets is process-global.
void init();

// Read all geometry of one ENC cell into `out` (projected), with the cell's
// bbox. Thread-safe: opens and closes its own GDAL handle. Heavy — call from a
// worker thread.
bool loadCellFeatures(const std::string& path,
                      std::vector<Feature>& out, BBox& bbox, std::string& err);

// Cheaply determine a cell's geographic extent (longitude/latitude degrees),
// preferring the small M_COVR coverage layer. Thread-safe. Used to build the
// catalog without reading full geometry.
bool computeCellExtentLonLat(const std::string& path,
                             double& minLon, double& minLat,
                             double& maxLon, double& maxLat, std::string& err);

} // namespace chart
