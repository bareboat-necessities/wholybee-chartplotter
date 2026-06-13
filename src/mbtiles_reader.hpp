#pragma once
#include <QString>
#include <QByteArray>
#include <QSqlDatabase>
#include <QMetaType>
#include "chart_loader.hpp"   // BBox

// Metadata describing one MBTiles raster chart, read from its `metadata` table.
//
// MBTiles (https://github.com/mapbox/mbtiles-spec) is a SQLite container of map
// tiles. We support the raster variants (format png/jpg/webp); vector tiles
// (format "pbf") need a whole vector-tile renderer and are rejected at open.
//
// sceneBounds is the chart's geographic coverage projected into the app's scene
// frame — the same frame BuiltPath geometry lives in: x = lonToX(lon),
// y = -latToY(lat) (Web Mercator metres, north-up). Invalid() when the file has
// no usable `bounds` metadata.
struct MbtilesMeta {
    QString path;        // absolute file path (identity)
    QString name;        // human label (metadata "name", else file base name)
    QString format;      // "png" / "jpg" / "webp" / "pbf" / ...
    int     minZoom = 0;
    int     maxZoom = 19;
    BBox    sceneBounds; // projected coverage (scene frame); !valid() if unknown

    bool isRaster() const {
        return format == QLatin1String("png")  || format == QLatin1String("jpg")
            || format == QLatin1String("jpeg") || format == QLatin1String("webp");
    }
};

// Opens one .mbtiles file read-only and serves tile blobs.
//
// Not thread-safe and tied to one thread: QSqlDatabase connections may only be
// used from the thread that opened them, so a reader must be opened and queried
// on the same (worker) thread. Each instance owns a uniquely-named connection.
class MbtilesReader {
public:
    MbtilesReader() = default;
    ~MbtilesReader();
    MbtilesReader(const MbtilesReader&) = delete;
    MbtilesReader& operator=(const MbtilesReader&) = delete;

    // Open and read metadata. Returns false (with err set) if the file can't be
    // opened or isn't a usable MBTiles container. Succeeds for raster formats
    // only — a vector ("pbf") file opens-and-reports via meta().isRaster()==false
    // so the caller can warn and skip it.
    bool open(const QString& path, QString& err);

    const MbtilesMeta& meta() const { return meta_; }

    // Raw tile blob for the XYZ tile (z, x, y) — the slippy-map convention with
    // y=0 at the north. Handles the MBTiles TMS row flip internally. Returns an
    // empty array when the tile is absent.
    QByteArray tile(int z, int x, int y);

    void close();

private:
    MbtilesMeta   meta_;
    QSqlDatabase  db_;
    QString       conn_;   // unique connection name
};

// Enable queueing MbtilesMeta (and vectors of it) across the worker-thread
// signal/slot boundary in MbtilesService.
Q_DECLARE_METATYPE(MbtilesMeta)
