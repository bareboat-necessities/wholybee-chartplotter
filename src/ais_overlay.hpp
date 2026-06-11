#pragma once
#include <functional>
#include "plugin_api.hpp"   // IChartOverlay, ChartViewport

class AisTargetStore;

// Draws AIS targets on the chart through the overlay API. Each vessel uses the
// same glyph as ownship (triangle + course-prediction line) but green, dimmed
// with a cancellation slash when its data is stale. Reads the core AIS store;
// staleness comes from the store's per-target freshness (configured for AIS
// timescales). Targets without a position are skipped; expired targets are
// already removed by the store.
class AisOverlay : public IChartOverlay {
public:
    explicit AisOverlay(const AisTargetStore* store) : store_(store) {}

    // Course-prediction length in minutes (kept in step with ownship's).
    void setPredictionMinutes(double minutes) { predMinutes_ = minutes; }
    // Uniform scale applied to the vessel glyph (1.0 = nominal).
    void setVesselScale(double scale) { vesselScale_ = scale; }

    // Invoked when the user clicks on a target's glyph (MMSI of that target).
    void setOnTargetClicked(std::function<void(quint32)> cb) { onClick_ = std::move(cb); }

    void paint(QPainter& painter, const ChartViewport& viewport) override;
    // Picks the nearest target within a few pixels of the click. Returns true
    // (and invokes the callback) when a target was hit so ChartView can stop.
    bool hitTest(const QPointF& screenPt) override;

private:
    const AisTargetStore* store_ = nullptr;
    double predMinutes_  = 6.0;
    double vesselScale_  = 1.0;
    // Camera snapshot from the most recent paint(); used by hitTest to project
    // target positions without re-deriving the view geometry.
    ChartViewport lastViewport_;
    bool haveViewport_ = false;
    std::function<void(quint32)> onClick_;
};
