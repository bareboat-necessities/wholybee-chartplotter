#include "chart_loader.hpp"
#include "projection.hpp"

#include <gdal.h>
#include <ogr_api.h>
#include <cpl_conv.h>

#include <algorithm>
#include <string>

namespace {

int zorderFor(FeatureKind k) {
    switch (k) {
        case FeatureKind::DepthArea:    return 0;
        case FeatureKind::OtherArea:    return 5;
        case FeatureKind::LandArea:     return 10;
        case FeatureKind::OtherLine:    return 20;
        case FeatureKind::DepthContour: return 21;
        case FeatureKind::Coastline:    return 22;
        case FeatureKind::Sounding:     return 30;
        case FeatureKind::Point:        return 40;
    }
    return 50;
}

FeatureKind classify(const std::string& n, OGRwkbGeometryType t) {
    if (n == "DEPARE" || n == "DRGARE") return FeatureKind::DepthArea;
    if (n == "LNDARE")                  return FeatureKind::LandArea;
    if (n == "COALNE" || n == "SLCONS") return FeatureKind::Coastline;
    if (n == "DEPCNT")                  return FeatureKind::DepthContour;
    if (n == "SOUNDG")                  return FeatureKind::Sounding;
    switch (t) {
        case wkbPolygon:
        case wkbMultiPolygon:    return FeatureKind::OtherArea;
        case wkbLineString:
        case wkbMultiLineString:
        case wkbLinearRing:      return FeatureKind::OtherLine;
        default:                 return FeatureKind::Point;
    }
}

bool skipLayer(const std::string& name) {
    if (name.size() >= 2 && name[0] == 'M' && name[1] == '_') return true; // M_COVR, M_QUAL...
    if (name == "DSID") return true;
    return false;
}

void projectSimple(OGRGeometryH g, std::vector<Pt>& out, BBox& bbox) {
    int n = OGR_G_GetPointCount(g);
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double x = proj::lonToX(OGR_G_GetX(g, i));
        double y = proj::latToY(OGR_G_GetY(g, i));
        out.push_back({x, y});
        bbox.expand(x, y);
    }
}

// Load every polygon in a shapefile as features of one kind/zorder. Returns
// false only if the file can't be opened (e.g. an absent optional level).
bool loadPolygonShp(const std::string& path, FeatureKind kind, int zorder,
                    std::vector<Feature>& out);

void extractGeometry(OGRGeometryH g, Feature& f) {
    if (!g) return;
    OGRwkbGeometryType t = wkbFlatten(OGR_G_GetGeometryType(g));
    switch (t) {
        case wkbPoint:
        case wkbLineString:
        case wkbLinearRing: {
            std::vector<Pt> pts;
            projectSimple(g, pts, f.bbox);
            if (!pts.empty()) f.rings.push_back(std::move(pts));
            break;
        }
        case wkbPolygon:
        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
        case wkbGeometryCollection: {
            int count = OGR_G_GetGeometryCount(g);
            for (int i = 0; i < count; ++i)
                extractGeometry(OGR_G_GetGeometryRef(g, i), f);
            break;
        }
        default:
            break;
    }
}

bool loadPolygonShp(const std::string& path, FeatureKind kind, int zorder,
                    std::vector<Feature>& out) {
    GDALDatasetH ds = GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                 nullptr, nullptr, nullptr);
    if (!ds) return false;

    int layerCount = GDALDatasetGetLayerCount(ds);
    for (int li = 0; li < layerCount; ++li) {
        OGRLayerH layer = GDALDatasetGetLayer(ds, li);
        if (!layer) continue;
        OGR_L_ResetReading(layer);
        OGRFeatureH feat;
        while ((feat = OGR_L_GetNextFeature(layer)) != nullptr) {
            OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
            if (geom) {
                Feature f;
                f.kind = kind;
                f.zorder = zorder;
                extractGeometry(geom, f);
                if (!f.rings.empty()) out.push_back(std::move(f));
            }
            OGR_F_Destroy(feat);
        }
    }
    GDALClose(ds);
    return true;
}

} // namespace

namespace chart {

void init(const std::string& gdalDataDir) {
    // Point GDAL at the bundled data directory before registering drivers.
    // This resolves S-57 object-class codes (OBJL) → named strings so that
    // the layer classifier (classify()) can match "DEPARE", "LNDARE" etc.
    // Without this, a machine without GDAL installed system-wide renders charts
    // as grey outlines only — geometry loads but all fill/colour is lost.
    if (!gdalDataDir.empty())
        CPLSetConfigOption("GDAL_DATA", gdalDataDir.c_str());

    CPLSetConfigOption("OGR_S57_OPTIONS",
        "SPLIT_MULTIPOINT=ON,ADD_SOUNDG_DEPTH=ON,RETURN_PRIMITIVES=OFF,"
        "RETURN_LINKAGES=OFF,LNAM_REFS=OFF");
    GDALAllRegister();
}

bool loadCellFeatures(const std::string& path,
                      std::vector<Feature>& out, BBox& bbox, std::string& err) {
    GDALDatasetH ds = GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                 nullptr, nullptr, nullptr);
    if (!ds) { err = "GDAL could not open: " + path; return false; }

    int layerCount = GDALDatasetGetLayerCount(ds);
    for (int li = 0; li < layerCount; ++li) {
        OGRLayerH layer = GDALDatasetGetLayer(ds, li);
        if (!layer) continue;
        const char* rawName = OGR_L_GetName(layer);
        std::string layerName = rawName ? rawName : "";
        if (skipLayer(layerName)) continue;

        OGR_L_ResetReading(layer);
        OGRFeatureH feat;
        while ((feat = OGR_L_GetNextFeature(layer)) != nullptr) {
            OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
            if (geom) {
                OGRwkbGeometryType gt = wkbFlatten(OGR_G_GetGeometryType(geom));
                Feature f;
                f.kind = classify(layerName, gt);
                f.zorder = zorderFor(f.kind);
                // Preserve the object-class name for Point features so the
                // cell-build step can resolve a symbol from the atlas.
                if (f.kind == FeatureKind::Point)
                    f.objClass = layerName;
                extractGeometry(geom, f);
                if (!f.rings.empty()) {
                    if (f.kind == FeatureKind::DepthArea) {
                        int idx = OGR_F_GetFieldIndex(feat, "DRVAL1");
                        if (idx >= 0 && OGR_F_IsFieldSetAndNotNull(feat, idx)) {
                            f.depth = OGR_F_GetFieldAsDouble(feat, idx);
                            f.hasDepth = true;
                        }
                    } else if (f.kind == FeatureKind::Sounding) {
                        int idx = OGR_F_GetFieldIndex(feat, "DEPTH");
                        if (idx >= 0 && OGR_F_IsFieldSetAndNotNull(feat, idx)) {
                            f.depth = OGR_F_GetFieldAsDouble(feat, idx);
                            f.hasDepth = true;
                        }
                    }
                    bbox.expand(f.bbox);
                    out.push_back(std::move(f));
                }
            }
            OGR_F_Destroy(feat);
        }
    }
    GDALClose(ds);

    // Painter's order within the cell: areas, then lines, then points.
    std::stable_sort(out.begin(), out.end(),
                     [](const Feature& a, const Feature& b) { return a.zorder < b.zorder; });
    return true;
}

bool computeCellExtentLonLat(const std::string& path,
                             double& minLon, double& minLat,
                             double& maxLon, double& maxLat, std::string& err) {
    GDALDatasetH ds = GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                 nullptr, nullptr, nullptr);
    if (!ds) { err = "GDAL could not open: " + path; return false; }

    OGREnvelope total;
    total.MinX = total.MinY =  1e30;
    total.MaxX = total.MaxY = -1e30;
    bool have = false;

    auto absorb = [&](OGRLayerH layer) {
        OGREnvelope env;
        if (layer && OGR_L_GetExtent(layer, &env, TRUE) == OGRERR_NONE) {
            if (env.MinX < total.MinX) total.MinX = env.MinX;
            if (env.MinY < total.MinY) total.MinY = env.MinY;
            if (env.MaxX > total.MaxX) total.MaxX = env.MaxX;
            if (env.MaxY > total.MaxY) total.MaxY = env.MaxY;
            have = true;
        }
    };

    // Prefer the small coverage layer; it is the true cell footprint.
    OGRLayerH cov = GDALDatasetGetLayerByName(ds, "M_COVR");
    if (cov) {
        absorb(cov);
    }
    if (!have) {
        int n = GDALDatasetGetLayerCount(ds);
        for (int i = 0; i < n; ++i)
            absorb(GDALDatasetGetLayer(ds, i));
    }
    GDALClose(ds);

    if (!have) { err = "No extent found in: " + path; return false; }
    minLon = total.MinX; minLat = total.MinY;
    maxLon = total.MaxX; maxLat = total.MaxY;
    return true;
}

bool loadBasemap(const std::string& gshhgRoot, const std::string& tier,
                 std::vector<Feature>& out, std::string& err) {
    const std::string base =
        gshhgRoot + "/GSHHS_shp/" + tier + "/GSHHS_" + tier + "_";
    // L1 = land (required). zorder 0 so it sits beneath the lakes.
    if (!loadPolygonShp(base + "L1.shp", FeatureKind::LandArea, 0, out)) {
        err = "Could not open basemap shoreline: " + base + "L1.shp";
        return false;
    }
    // L2 = lakes, drawn as water over the land. L3/L4 (islands-in-lakes, ponds)
    // and L5/L6 (Antarctica) can be layered in later. Optional — ignore if absent.
    loadPolygonShp(base + "L2.shp", FeatureKind::DepthArea, 1, out);
    return !out.empty();
}

} // namespace chart
