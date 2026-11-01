#ifndef SR2_PCM_H
#define SR2_PCM_H
#include <stdbool.h>
#include <stdint.h>
void sr2_pcm_select(bool enable);
void sr2_pcm_probe(void);
/* Boot only; bounded by the caller's capability-derived memory budget.
 * Keeps the existing streaming path if allocation/read/validation fails. */
bool sr2_pcm_preload(uint32_t budget);
void sr2_pcm_bind(int slot,const char *filename);
void sr2_pcm_start(int slot);
void sr2_pcm_stop(void);
void sr2_pcm_resume(void);
bool sr2_pcm_playing(void);
/* True also during a temporary pause: distinguish it from missing PCM. */
bool sr2_pcm_has_stream(void);
/* 24000 legacy or 48000 high-fidelity; zero if not playing. */
unsigned sr2_pcm_rate(void);
bool sr2_pcm_busy(void);
/* File-idle callbacks may consume decoded audio but must not start DMA reads.
 * Return the previous setting so callers can restore it. */
bool sr2_pcm_allow_refill(bool allow);
typedef struct {
    uint32_t track_loops,read_errors;
    uint32_t starts,stops,resumes,start_failures,deferred_refills;
    uint32_t preloaded_bytes;
    int track_slot;
    bool playing,paused,stream_open;
} sr2_pcm_stats_t;
void sr2_pcm_get_stats(sr2_pcm_stats_t *out);
void sr2_pcm_clear_stats(void);
int16_t sr2_pcm_sample(void);
#endif
