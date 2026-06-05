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

struct CacheEntry { qint64 size; qint64 mtime; double minLon, minLat, maxLon, maxLat; };

QHash<QString, CacheEntry> loadCache(const QString& cachePath) {
    QHash<QString, CacheEntry> map;
    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly)) return map;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return map;
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        CacheEntry e;
        e.size   = (qint64)o.value("size").toDouble();
        e.mtime  = (qint64)o.value("mtime").toDouble();
        e.minLon = o.value("minLon").toDouble();
        e.minLat = o.value("minLat").toDouble();
        e.maxLon = o.value("maxLon").toDouble();
        e.maxLat = o.value("maxLat").toDouble();
        map.insert(o.value("path").toString(), e);
    }
    return map;
}

void saveCache(const QString& cachePath, const QHash<QString, CacheEntry>& map) {
    QJsonArray arr;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        QJsonObject o;
        o.insert("path",   it.key());
        o.insert("size",   (double)it.value().size);
        o.insert("mtime",  (double)it.value().mtime);
        o.insert("minLon", it.value().minLon);
        o.insert("minLat", it.value().minLat);
        o.insert("maxLon", it.value().maxLon);
        o.insert("maxLat", it.value().maxLat);
        arr.append(o);
    }
    QFile f(cachePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
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
            std::string err;
            if (chart::computeCellExtentLonLat(path.toStdString(),
                                               e.minLon, e.minLat, e.maxLon, e.maxLat, err)) {
                e.size = sz; e.mtime = mt;
                ok = true;
            }
        }

        if (ok) {
            r.bbox = projectExtent(e.minLon, e.minLat, e.maxLon, e.maxLat);
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
