#pragma once
#include <QString>
#include <QList>

struct OwnshipState;
struct NavValue;

// Which analogue rendering an analogue tile uses (display attribute):
//   Arc     standard min..max arc gauge with a needle ("analog")
//   Compass north-up compass rose; the needle points to the bearing ("compass")
//   Wind    bow-up wind gauge: 0 at the top (the bow), starboard to the right
//           (positive), port to the left (negative) ("wind")
enum class GaugeStyle { Arc, Compass, Wind };

// One configurable instrument tile, read from the instruments XML catalogue.
// A tile renders a single nav-data value either as a digital read-out or as one
// of the analogue gauges.
struct InstrumentDef {
    QString id;             // stable id, referenced by the saved selection/order
    QString name;           // full name shown in the settings list
    QString caption;        // short label drawn on the tile (defaults to name)
    QString path;           // nav-data field key (see navValueForPath)
    QString unit;           // unit label shown after the value
    double  factor = 1.0;   // multiply the stored value by this before display
    bool    analog = false; // analogue gauge vs. digital read-out
    GaugeStyle gauge = GaugeStyle::Arc;   // which gauge, when analog
    double  min = 0.0;      // arc gauge range, in display units (unused by rose gauges)
    double  max = 100.0;
    int     decimals = 1;   // digital decimal places
};

// Resolve the live nav value for an instrument path. The path is an OwnshipState
// field name (case-insensitive), e.g. "sogKnots", "cogDegTrue", "depthMeters".
// Returns nullptr for an unknown path.
const NavValue* navValueForPath(const OwnshipState& os, const QString& path);

// The set of instrument paths the loader understands, for documentation / the
// settings page. Each entry is a valid `path` attribute value.
QStringList knownInstrumentPaths();

// Loads and seeds the user-editable instrument catalogue (instruments.xml).
//
// Resolution order:
//   1. the user copy under the app config dir (created on first run);
//   2. the default bundled next to the executable;
//   3. a built-in fallback compiled into the plugin.
// On first run, if no user copy exists one is written (from the bundled default
// when present, else the built-in fallback) so the user always has a file to
// edit at a known location. The returned list is never empty.
class InstrumentCatalog {
public:
    static QList<InstrumentDef> load();

    // Absolute path of the user-editable instruments.xml (may not exist yet).
    static QString userConfigPath();

    // The built-in default catalogue as XML text (also the seed written on first
    // run when no bundled file is found).
    static QByteArray defaultXml();

    // Parse a catalogue from XML bytes. Malformed entries are skipped.
    static QList<InstrumentDef> parse(const QByteArray& xml);
};
