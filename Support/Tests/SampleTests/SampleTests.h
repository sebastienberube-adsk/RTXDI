#pragma once

#include <ImageComparison.h>

// Public defaults used by SampleTests image comparisons.
static StochasticThresholds GetThresholds()
{
    StochasticThresholds t;
    t.tileSize = 32;
    t.absAvgDeltaThreshold = 0.075f;
    t.relDifferenceThreshold = 0.35f;
    t.absStdDevDeltaThreshold = 0.150f;
    // Whole-image average gate (per channel): abs OR relative, same policy as tiles.
    // Calibrated from repeated self-comparison runs with safety margin.
    t.imageAbsAvgDeltaThreshold = 0.006f;
    t.imageRelDifferenceThreshold = 0.008f; // 0.8%
    return t;
}
