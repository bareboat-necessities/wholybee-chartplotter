#pragma once
#include <functional>
#include "plugin_api.hpp"   // IChartOverlay, ChartViewport

class AisTargetStore;
class NavDataStore;
struct AisTarget;

// User-configured rules deciding whether a target is "dangerous". CPA is the
// base trigger; the others refine it. Thresholds are in the dialog's units
// (nautical miles, minutes); the overlay converts to metres/seconds.
struct DangerRules {
    bool   ignoreFarEnabled = false;   // skip targets beyond ignoreFarNm
    double ignoreFarNm      = 20.0;
    bool   cpaEnabled  = false;        // dangerous if CPA < cpaNm
    double cpaNm       = 2.0;
    bool   tcpaEnabled = false;        // ...and TCPA within [0, tcpaMin)
    double tcpaMin     = 30.0;
};

// Draws AIS targets on the chart through the overlay API. Each vessel uses the
// same glyph as ownship (triangle + course-prediction line) but green, dimmed
// with a cancellation slash when its data is stale. Reads the core AIS store;
// staleness comes from the store's per-target freshness (configured for AIS
// timescales). Targets without a position are skipped; expired targets are
// already removed by the store.
class AisOverlay : public IChartOverlay {
public:
    // The nav store supplies ownship's current position, where the ownship leg
    // of the CPA encounter graphics starts.
    AisOverlay(const AisTargetStore* store, const NavDataStore* nav)
        : store_(store), nav_(nav) {}

    // Course-prediction length in minutes (kept in step with ownship's).
    void setPredictionMinutes(double minutes) { predMinutes_ = minutes; }
    // Uniform scale applied to the vessel glyph (1.0 = nominal).
    void setVesselScale(double scale) { vesselScale_ = scale; }
    // Rules deciding which targets are drawn as dangerous (red + highlight).
    void setDangerRules(const DangerRules& rules) { danger_ = rules; }
    // Master visibility switch. When off the overlay paints nothing and ignores
    // clicks; the store/CpaCalculator/danger logic keep running underneath so
    // turning it back on shows the current state without any warm-up.
    void setVisible(bool on) { visible_ = on; }

    // Invoked when the user clicks on a target's glyph (MMSI of that target).
    void setOnTargetClicked(std::function<void(quint32)> cb) { onClick_ = std::move(cb); }

    void paint(QPainter& painter, const ChartViewport& viewport) override;
    // Picks the nearest target within a few pixels of the click. Returns true
    // (and invokes the callback) when a target was hit so ChartView can stop.
    bool hitTest(const QPointF& screenPt) override;

private:
    bool isDangerous(const AisTarget& t) const;

    const AisTargetStore* store_ = nullptr;
    const NavDataStore*   nav_   = nullptr;
    double predMinutes_  = 6.0;
    double vesselScale_  = 1.0;
    DangerRules danger_;
    bool visible_ = true;
    // Camera snapshot from the most recent paint(); used by hitTest to project
    // target positions without re-deriving the view geometry.
    ChartViewport lastViewport_;
    bool haveViewport_ = false;
    std::function<void(quint32)> onClick_;
};
