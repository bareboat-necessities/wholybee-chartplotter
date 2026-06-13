#include "mbtiles_reader.hpp"
#include "projection.hpp"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QStringList>
#include <atomic>

namespace {
// Unique per-process connection name so multiple readers (and re-opens) never
// collide on QSqlDatabase's global name registry.
QString nextConnName() {
    static std::atomic<quint64> n{0};
    return QStringLiteral("mbtiles_%1").arg(n.fetch_add(1));
}
}  // namespace

MbtilesReader::~MbtilesReader() { close(); }

void MbtilesReader::close() {
    if (db_.isOpen()) db_.close();
    db_ = QSqlDatabase();              // drop our reference before removing
    if (!conn_.isEmpty()) {
        QSqlDatabase::removeDatabase(conn_);
        conn_.clear();
    }
}

bool MbtilesReader::open(const QString& path, QString& err) {
    close();
    conn_ = nextConnName();
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn_);
    db_.setDatabaseName(path);
    // Read-only, and don't create the file if it's missing.
    db_.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db_.open()) {
        err = db_.lastError().text();
        close();
        return false;
    }

    meta_ = MbtilesMeta{};
    meta_.path = path;

    // metadata is a simple key/value table in the MBTiles spec.
    QString boundsStr;
    bool haveMin = false, haveMax = false;
    {
        QSqlQuery q(db_);
        if (q.exec(QStringLiteral("SELECT name, value FROM metadata"))) {
            while (q.next()) {
                const QString k = q.value(0).toString().trimmed().toLower();
                const QString v = q.value(1).toString().trimmed();
                if (k == QLatin1String("name"))        meta_.name   = v;
                else if (k == QLatin1String("format"))  meta_.format = v.toLower();
                else if (k == QLatin1String("bounds"))  boundsStr    = v;
                else if (k == QLatin1String("minzoom")) { meta_.minZoom = v.toInt(); haveMin = true; }
                else if (k == QLatin1String("maxzoom")) { meta_.maxZoom = v.toInt(); haveMax = true; }
            }
        }
    }

    // Some files omit min/maxzoom; derive from the tile pyramid itself.
    if (!haveMin || !haveMax) {
        QSqlQuery q(db_);
        if (q.exec(QStringLiteral("SELECT min(zoom_level), max(zoom_level) FROM tiles"))
            && q.next()) {
            if (!haveMin && !q.value(0).isNull()) meta_.minZoom = q.value(0).toInt();
            if (!haveMax && !q.value(1).isNull()) meta_.maxZoom = q.value(1).toInt();
        }
    }
    if (meta_.maxZoom < meta_.minZoom) meta_.maxZoom = meta_.minZoom;

    if (meta_.name.isEmpty()) meta_.name = QFileInfo(path).completeBaseName();

    // bounds = "minLon,minLat,maxLon,maxLat" (WGS84 degrees) → scene frame.
    if (!boundsStr.isEmpty()) {
        const QStringList parts = boundsStr.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() == 4) {
            bool ok0, ok1, ok2, ok3;
            const double minLon = parts[0].toDouble(&ok0);
            const double minLat = parts[1].toDouble(&ok1);
            const double maxLon = parts[2].toDouble(&ok2);
            const double maxLat = parts[3].toDouble(&ok3);
            if (ok0 && ok1 && ok2 && ok3) {
                // Scene frame: x = lonToX, y = -latToY (north-up). expand()
                // tracks min/max regardless of corner order.
                meta_.sceneBounds.expand(proj::lonToX(minLon), -proj::latToY(minLat));
                meta_.sceneBounds.expand(proj::lonToX(maxLon), -proj::latToY(maxLat));
            }
        }
    }

    if (meta_.format.isEmpty()) {
        // No declared format: peek at one tile's magic bytes so we can still
        // classify common raster files that omit the metadata key.
        QSqlQuery q(db_);
        if (q.exec(QStringLiteral("SELECT tile_data FROM tiles LIMIT 1")) && q.next()) {
            const QByteArray b = q.value(0).toByteArray();
            if (b.startsWith("\x89PNG"))                       meta_.format = QStringLiteral("png");
            else if (b.size() >= 2 && (uchar)b[0] == 0xFF
                                   && (uchar)b[1] == 0xD8)     meta_.format = QStringLiteral("jpg");
            else if (b.startsWith("RIFF"))                     meta_.format = QStringLiteral("webp");
        }
    }

    return true;
}

QByteArray MbtilesReader::tile(int z, int x, int y) {
    if (!db_.isOpen()) return {};
    // MBTiles stores rows in TMS order (origin bottom-left); convert from the
    // XYZ/slippy y (origin top-left) the renderer uses.
    const int tmsRow = (1 << z) - 1 - y;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT tile_data FROM tiles "
        "WHERE zoom_level=? AND tile_column=? AND tile_row=?"));
    q.addBindValue(z);
    q.addBindValue(x);
    q.addBindValue(tmsRow);
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
    return {};
}
