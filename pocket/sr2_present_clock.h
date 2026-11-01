#ifndef SR2_PRESENT_CLOCK_H
#define SR2_PRESENT_CLOCK_H
#include <stdint.h>
/* Preserve the display deadline phase after a late VGA retrace. Resetting
 * to actual presentation time quantized the 30 Hz cap down to ~23 Hz at
 * the original 70.1 Hz VGA cadence. Skip missed slots without burst flips. */
static inline int sr2_present_due(uint32_t *anchor,uint32_t now,uint32_t interval){
    uint32_t elapsed=now-*anchor;
    if(!interval || elapsed<interval)return 0;
    *anchor=now-elapsed%interval;
    return 1;
}
#endif
