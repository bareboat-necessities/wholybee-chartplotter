#include "chart_catalog.hpp"
#include "projection.hpp"

#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentRun>
#include <QHash>

ChartCatalog::ChartCatalog(QObject* parent) : QObject(parent) {}

int ChartCatalog::bandFromPath(const QString& path) {
    const QString base = QFileInfo(path).completeBaseName(); // e.g. "US5FL14M"
    if (base.size() >= 3 && base.at(2).isDigit())
        return base.at(2).digitValue();
    return 0;
}

void ChartCatalog::startScan(const QString& root) {
    if (scanning_.load()) return;
    scanning_.store(true);
    root_ = root;

    // Per-root cache file in the app data dir (computed here, on the UI thread).
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir + "/catalog_cache");
    QByteArray h = QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha1).toHex();
    QString cachePath = dir + "/catalog_cache/" + QString::fromLatin1(h) + ".json";

    QString r = root;
    (void)QtConcurrent::run([this, r, cachePath]() { runScan(r, cachePath); });
}

namespace {

// Bump when the on-disk schema changes; older files are ignored (forces one
// rescan). v2 added projected bbox + coverage rings (was lon/lat extent only).
constexpr int kCacheVersion = 2;

// One cached cell footprint: file identity plus the projected bbox and the
// M_COVR coverage rings (empty ⇒ no coverage layer; treat bbox as coverage).
struct CacheEntry {
    qint64 size = 0;
    qint64 mtime = 0;
    BBox   bbox;
    std::vector<std::vector<Pt>> coverage;
};

QHash<QString, CacheEntry> loadCache(const QString& cachePath) {
    QHash<QString, CacheEntry> map;
    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly)) return map;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return map;
    const QJsonObject root = doc.object();
    if (root.value("v").toInt() != kCacheVersion) return map;   // schema changed
    for (const QJsonValue& v : root.value("cells").toArray()) {
        const QJsonObject o = v.toObject();
        CacheEntry e;
        e.size  = (qint64)o.value("size").toDouble();
        e.mtime = (qint64)o.value("mtime").toDouble();
        const QJsonObject b = o.value("bbox").toObject();
        e.bbox.minx = b.value("minx").toDouble();
        e.bbox.miny = b.value("miny").toDouble();
        e.bbox.maxx = b.value("maxx").toDouble();
        e.bbox.maxy = b.value("maxy").toDouble();
        for (const QJsonValue& rv : o.value("coverage").toArray()) {
            const QJsonArray ra = rv.toArray();
            std::vector<Pt> ring;
            ring.reserve(ra.size() / 2);
            for (int i = 0; i + 1 < ra.size(); i += 2)
                ring.push_back({ra.at(i).toDouble(), ra.at(i + 1).toDouble()});
            if (ring.size() >= 3) e.coverage.push_back(std::move(ring));
        }
        map.insert(o.value("path").toString(), e);
    }
    return map;
}

void saveCache(const QString& cachePath, const QHash<QString, CacheEntry>& map) {
    QJsonArray arr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        const CacheEntry& e = it.value();
        QJsonObject o;
        o.insert("path",  it.key());
        o.insert("size",  (double)e.size);
        o.insert("mtime", (double)e.mtime);
        QJsonObject b;
        b.insert("minx", e.bbox.minx); b.insert("miny", e.bbox.miny);
        b.insert("maxx", e.bbox.maxx); b.insert("maxy", e.bbox.maxy);
        o.insert("bbox", b);
        QJsonArray cov;
        for (const std::vector<Pt>& ring : e.coverage) {
            QJsonArray ra;
            for (const Pt& p : ring) { ra.append(p.x); ra.append(p.y); }
            cov.append(ra);
        }
        o.insert("coverage", cov);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("v", kCacheVersion);
    root.insert("cells", arr);
    QFile f(cachePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

BBox projectExtent(double minLon, double minLat, double maxLon, double maxLat) {
    BBox b;
    b.expand(proj::lonToX(minLon), proj::latToY(minLat));
    b.expand(proj::lonToX(maxLon), proj::latToY(maxLat));
    return b;
}

} // namespace

void ChartCatalog::runScan(QString root, QString cachePath) {
    std::vector<CellRecord> recs;
    BBox bounds;

    // 1) Enumerate base cells (*.000), recursively.
    QStringList files;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        if (QFileInfo(f).suffix().compare(QStringLiteral("000"), Qt::CaseInsensitive) == 0)
            files << f;
    }
    files.sort();

    const int total = files.size();
    if (total == 0) {
        cells_.clear();
        bounds_ = BBox{};
        scanning_.store(false);
        emit finished(false, QStringLiteral("No ENC cells (*.000) found under:\n") + root);
        return;
    }

    // 2) Resolve each footprint, using the disk cache where the file is unchanged.
    QHash<QString, CacheEntry> cache = loadCache(cachePath);
    QHash<QString, CacheEntry> updated;

    int done = 0;
    for (const QString& path : files) {
        CellRecord r;
        r.path = path;
        r.band = bandFromPath(path);

        const QFileInfo fi(path);
        const qint64 sz = fi.size();
        const qint64 mt = fi.lastModified().toSecsSinceEpoch();

        CacheEntry e;
        auto cached = cache.constFind(path);
        bool ok = false;
        if (cached != cache.constEnd() && cached->size == sz && cached->mtime == mt) {
            e = *cached;
            ok = true;
        } else {
            e.size = sz; e.mtime = mt;
            std::string err;
            // Prefer the real M_COVR coverage footprint; its rings double as the
            // bbox. Fall back to the plain extent box (no coverage rings) when a
            // cell has no usable M_COVR.
            if (chart::computeCellCoverage(path.toStdString(), e.coverage, e.bbox, err)) {
                ok = true;
            } else {
                double minLon, minLat, maxLon, maxLat;
                if (chart::computeCellExtentLonLat(path.toStdString(),
                                                   minLon, minLat, maxLon, maxLat, err)) {
                    e.bbox = projectExtent(minLon, minLat, maxLon, maxLat);
                    e.coverage.clear();
                    ok = true;
                }
            }
        }

        if (ok) {
            r.bbox = e.bbox;
            r.coverage = e.coverage;
            r.extentValid = r.bbox.valid();
            if (r.extentValid) bounds.expand(r.bbox);
            updated.insert(path, e);
        }
        recs.push_back(std::move(r));

        if ((++done % 8) == 0 || done == total)
            emit progress(done, total);
    }

    saveCache(cachePath, updated);

    cells_  = std::move(recs);
    bounds_ = bounds;
    scanning_.store(false);
    emit finished(true, QStringLiteral("%1 cell(s) cataloged").arg(total));
}
