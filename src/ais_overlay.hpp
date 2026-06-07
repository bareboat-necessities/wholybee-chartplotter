#pragma once
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

    void paint(QPainter& painter, const ChartViewport& viewport) override;

private:
    const AisTargetStore* store_ = nullptr;
    double predMinutes_ = 6.0;
};
