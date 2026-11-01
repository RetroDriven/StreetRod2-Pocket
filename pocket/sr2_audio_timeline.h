#ifndef SR2_AUDIO_TIMELINE_H
#define SR2_AUDIO_TIMELINE_H
#include <stdint.h>
/* Game ticks and audio rendered by the file-idle hook have independent
 * progress. Never move the tick origin forward to catch up with audio:
 * subsequent old ticks would then synthesize that elapsed time twice. */
static inline uint64_t sr2_audio_next_tick(uint64_t *tick_clock,
                                         uint64_t audio_clock,uint32_t divisor){
    *tick_clock+=divisor;
    return *tick_clock>audio_clock?*tick_clock:audio_clock;
}
#endif
