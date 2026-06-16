#include "gpx_io.hpp"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace gpx {
namespace {

// GPX stores coordinates as decimal degrees; 7 dp is ~1 cm, plenty for marine
// use and the de-facto convention for GPX writers.
QString coord(double v) { return QString::number(v, 'f', 7); }

QString isoUtc(const QDateTime& t) {
    return t.isValid() ? t.toUTC().toString(Qt::ISODate) : QString();
}

// Write the child elements shared by <wpt> and <rtept>: emit in GPX schema
// order (time, name, desc, sym), skipping empties.
void writePointBody(QXmlStreamWriter& w, const QDateTime& time, const QString& name,
                    const QString& desc, const QString& sym) {
    if (time.isValid())  w.writeTextElement(QStringLiteral("time"), isoUtc(time));
    if (!name.isEmpty()) w.writeTextElement(QStringLiteral("name"), name);
    if (!desc.isEmpty()) w.writeTextElement(QStringLiteral("desc"), desc);
    if (!sym.isEmpty())  w.writeTextElement(QStringLiteral("sym"),  sym);
}

}  // namespace

QByteArray write(const QVector<Route>& routes, const QVector<Waypoint>& waypoints) {
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("gpx"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    w.writeAttribute(QStringLiteral("creator"), QStringLiteral("Chartplotter"));
    w.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));

    // Standalone waypoints first (GPX puts <wpt> before <rte>).
    for (const Waypoint& wp : waypoints) {
        w.writeStartElement(QStringLiteral("wpt"));
        w.writeAttribute(QStringLiteral("lat"), coord(wp.lat));
        w.writeAttribute(QStringLiteral("lon"), coord(wp.lon));
        writePointBody(w, wp.createdUtc, wp.name, wp.description, wp.symbol);
        w.writeEndElement();  // wpt
    }

    for (const Route& r : routes) {
        w.writeStartElement(QStringLiteral("rte"));
        if (!r.name.isEmpty())        w.writeTextElement(QStringLiteral("name"), r.name);
        if (!r.description.isEmpty())  w.writeTextElement(QStringLiteral("desc"), r.description);
        for (const RoutePoint& p : r.points) {
            w.writeStartElement(QStringLiteral("rtept"));
            w.writeAttribute(QStringLiteral("lat"), coord(p.lat));
            w.writeAttribute(QStringLiteral("lon"), coord(p.lon));
            if (!p.name.isEmpty()) w.writeTextElement(QStringLiteral("name"), p.name);
            w.writeEndElement();  // rtept
        }
        w.writeEndElement();  // rte
    }

    w.writeEndElement();  // gpx
    w.writeEndDocument();
    return out;
}

namespace {

// Pull lat/lon off the current start element (<wpt>/<rtept>). Returns false if
// either is missing/unparseable so the caller can skip a malformed point.
bool readLatLon(const QXmlStreamReader& r, double& lat, double& lon) {
    const auto a = r.attributes();
    bool okLat = false, okLon = false;
    lat = a.value(QStringLiteral("lat")).toDouble(&okLat);
    lon = a.value(QStringLiteral("lon")).toDouble(&okLon);
    return okLat && okLon;
}

}  // namespace

bool read(const QByteArray& data, Document& out, QString& err) {
    QXmlStreamReader r(data);
    out.routes.clear();
    out.waypoints.clear();

    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        const QStringView el = r.name();

        if (el == QLatin1String("wpt")) {
            Waypoint wp;
            if (!readLatLon(r, wp.lat, wp.lon)) { r.skipCurrentElement(); continue; }
            // Walk the wpt's children until its end tag.
            while (!(r.isEndElement() && r.name() == QLatin1String("wpt")) && !r.atEnd()) {
                r.readNext();
                if (!r.isStartElement()) continue;
                const QStringView c = r.name();
                if      (c == QLatin1String("name")) wp.name        = r.readElementText();
                else if (c == QLatin1String("desc")) wp.description = r.readElementText();
                else if (c == QLatin1String("sym"))  wp.symbol      = r.readElementText();
                else if (c == QLatin1String("time"))
                    wp.createdUtc = QDateTime::fromString(r.readElementText(), Qt::ISODate);
                else r.skipCurrentElement();
            }
            out.waypoints.push_back(std::move(wp));
        } else if (el == QLatin1String("rte")) {
            Route route;
            while (!(r.isEndElement() && r.name() == QLatin1String("rte")) && !r.atEnd()) {
                r.readNext();
                if (!r.isStartElement()) continue;
                const QStringView c = r.name();
                if (c == QLatin1String("name")) {
                    route.name = r.readElementText();
                } else if (c == QLatin1String("desc")) {
                    route.description = r.readElementText();
                } else if (c == QLatin1String("rtept")) {
                    RoutePoint p;
                    if (!readLatLon(r, p.lat, p.lon)) { r.skipCurrentElement(); continue; }
                    while (!(r.isEndElement() && r.name() == QLatin1String("rtept")) && !r.atEnd()) {
                        r.readNext();
                        if (r.isStartElement() && r.name() == QLatin1String("name"))
                            p.name = r.readElementText();
                        else if (r.isStartElement())
                            r.skipCurrentElement();
                    }
                    route.points.push_back(std::move(p));
                } else {
                    r.skipCurrentElement();
                }
            }
            out.routes.push_back(std::move(route));
        }
    }

    if (r.hasError()) {
        err = r.errorString();
        return false;
    }
    return true;
}

}  // namespace gpx
