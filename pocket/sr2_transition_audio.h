/* SPDX-License-Identifier: MIT. Cooperative AUDIO ONLY during long CPU work.
 * Never dispatch input, game/PIT callbacks, presents or nested file reads. */
#ifndef SR2_TRANSITION_AUDIO_H
#define SR2_TRANSITION_AUDIO_H
#ifdef SR2_WITH_TRANSITION_AUDIO
void srp_transition_audio_checkpoint(void);
extern unsigned srp_transition_audio_race_depth;
/* Reject racing work before entering the host or reading the kernel clock. */
#define SR2_AUDIO_CHECKPOINT() do { if (!srp_transition_audio_race_depth) srp_transition_audio_checkpoint(); } while (0)
typedef struct { unsigned previous; } sr2_race_audio_scope;
static inline sr2_race_audio_scope sr2_race_audio_begin(void){
    sr2_race_audio_scope s={srp_transition_audio_race_depth};
    srp_transition_audio_race_depth++;return s;
}
static inline void sr2_race_audio_end(sr2_race_audio_scope *s){srp_transition_audio_race_depth=s->previous;}
#define SR2_RACE_AUDIO_SCOPE() sr2_race_audio_scope __attribute__((cleanup(sr2_race_audio_end))) sr2_race_audio_guard=sr2_race_audio_begin()
#else
#define SR2_AUDIO_CHECKPOINT() ((void)0)
#define SR2_RACE_AUDIO_SCOPE() ((void)0)
#endif
#endif
