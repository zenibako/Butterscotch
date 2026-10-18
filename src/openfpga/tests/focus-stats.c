/* The timestamp bookkeeping must reject underflow and preserve both wall time
 * and same-frame presentation costs, independent of a display backend. */
#include "of_focus_stats.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    OfFocusStats stats={0}; OfPerfTotals metrics={0};
    metrics.phaseNanos[OF_PHASE_STEP]=30; metrics.phaseNanos[OF_PHASE_DRAW]=50;
    metrics.phaseNanos[OF_PHASE_OUT]=999999; /* prior frame, not subtracted */
    assert(ofFocusStatsAdd(&stats,150,50,10,&metrics));
    assert(stats.count==1 && stats.work[0]==100 && stats.total==150 && stats.flip==50 && stats.present==10);
    assert(stats.totals.phaseNanos[OF_PHASE_STEP]==30 && stats.totals.phaseNanos[OF_PHASE_OUT]==999999);
    OfFocusStats before=stats;
    assert(!ofFocusStatsAdd(&stats,10,11,0,&metrics));
    assert(!memcmp(&before,&stats,sizeof(stats)));
    assert(!ofFocusStatsAdd(&stats,10,0,11,&metrics));
    assert(!memcmp(&before,&stats,sizeof(stats)));
    for (int i=1;i<20;++i) assert(ofFocusStatsAdd(&stats,150+i*10,50,10,&metrics));
    assert(ofFocusStatsPercentile(&stats,50)==190 && ofFocusStatsPercentile(&stats,95)==280);
    assert(stats.count==20 && stats.total==4900 && stats.flip==1000);
    assert(ofFocusStatsBlockMean(&stats,0,10)==145 && ofFocusStatsBlockMean(&stats,10,10)==245);
    assert(ofFocusStatsBlockMean(&stats,20,10)==0);
    while(stats.count<OF_FOCUS_SAMPLES) assert(ofFocusStatsAdd(&stats,100,0,0,&metrics));
    before=stats; assert(!ofFocusStatsAdd(&stats,100,0,0,&metrics)); assert(!memcmp(&stats,&before,sizeof(stats)));
    OfFocusStats empty={0}; assert(ofFocusStatsPercentile(&empty,95)==0);
    puts("PASS: focused frame/flip accounting, percentiles, blocks and bounds");
}
