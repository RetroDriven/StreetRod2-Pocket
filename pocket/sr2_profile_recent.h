#ifndef SR2_PROFILE_RECENT_H
#define SR2_PROFILE_RECENT_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint32_t at_us,flips,fps,audio_pct;
    uint64_t audio_us;
    bool valid;
} sr2_profile_recent_t;
static inline void sr2_profile_recent_reset(sr2_profile_recent_t *s,uint32_t now,
                                            uint64_t audio,unsigned flips){
    s->at_us=now;s->audio_us=audio;s->flips=flips;s->fps=s->audio_pct=0;s->valid=true;
}
static inline void sr2_profile_recent_update(sr2_profile_recent_t *s,uint32_t now,
                                             uint64_t audio,unsigned flips){
    uint32_t elapsed=now-s->at_us;
    if(!s->valid || audio<s->audio_us || flips<s->flips){sr2_profile_recent_reset(s,now,audio,flips);return;}
    if(elapsed<500000u)return;
    s->fps=(uint32_t)((uint64_t)(flips-s->flips)*1000000u/elapsed);
    s->audio_pct=(uint32_t)((audio-s->audio_us)*100u/elapsed);
    s->at_us=now;s->audio_us=audio;s->flips=flips;
}
#endif
