#ifndef OF_FOCUS_STATS_H
#define OF_FOCUS_STATS_H
#include "of_perf.h"
#define OF_FOCUS_SAMPLES 192
/* One window's correlated frame samples, in nanoseconds. */
typedef struct {
    uint64_t work[OF_FOCUS_SAMPLES];
    uint64_t total, flip, present;
    unsigned count;
    OfPerfTotals totals;
} OfFocusStats;
bool ofFocusStatsAdd(OfFocusStats *stats, uint64_t total, uint64_t flip,
                     uint64_t present, const OfPerfTotals *metrics);
uint64_t ofFocusStatsPercentile(const OfFocusStats *stats, unsigned percentile);
uint64_t ofFocusStatsBlockMean(const OfFocusStats *stats, unsigned first, unsigned count);
#endif
