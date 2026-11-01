 


#ifndef RETRODRIVEN_PC_AUDIO_H
#define RETRODRIVEN_PC_AUDIO_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint32_t writes, dropped_buffers, empty_checks;
    uint32_t empty_checks_total;
    uint32_t pcm_saved_by_room, pcm_full_drops, pcm_legacy_cap_drops;
    uint32_t minimum_pending_pairs, maximum_pending_pairs;
    uint32_t initial_lead_pairs;
    uint32_t pit2_writes, port61_writes, gate_starts, data_enables;
    uint32_t first_gate_ms, first_data_ms;
    uint32_t startup_fades;
    /* Lightweight FM telemetry, collected only when the user enables profiling. */
    uint32_t fm_enable_calls, fm_register_writes, fm_keyon_writes;
    uint32_t fm_samples, fm_nonzero_samples, fm_skipped_samples;
    uint32_t fm_enabled_now, fm_mode;
    uint32_t hybrid_live_nonzero, hybrid_clips;
    uint32_t fx_samples, fx_clips;
    uint32_t fx_peak, fx_output_buffers, fx_dropped_buffers;
    uint32_t fx_queue_high_water, fx_queue_overflows;
    uint32_t fx_cache_loaded,fx_cached_frames,fx_cached_voices;
    uint32_t write_requested, short_writes;
    int32_t write_result;
    uint32_t fx_synth_calls, fx_idle_stops, missing_pcm_samples;
    uint32_t fx_native_ticks,fx_operator_steps;
    uint32_t fx_keys, fx_drums, fx_awake;

} srp_audio_stats_t;
void srp_audio_snapshot(srp_audio_stats_t *out);
void srp_audio_peek(srp_audio_stats_t *out); /* read counters without resetting */
uint32_t srp_audio_pending_pairs(void); /* current queue, not lifetime peak */
void srp_audio_init(void);
/* Backend is chosen once at boot. Do not change it in the running game. */
/* 0=PC Speaker; 1=Fast FM; 2=Opal (diagnostic only); 3=Original AdLib PCM with accurate Opal FX; 4=PCM+Fast FM DIAGNOSTIC; 5=PCM-only test; 6=accurate effects only. */
void srp_audio_select_backend(int mode);
void srp_audio_opl_enable(bool on);
void srp_audio_opl_write(uint8_t reg,uint8_t value);
/* The original MUS interpreter writes to the same YM3812 as SFX. Tag its
 * calls so archived music can coexist with live FM effects without doubling
 * the recorded soundtrack. Nested calls are supported. */
bool srp_audio_split_music(void);
bool srp_audio_music_active(void);
void srp_audio_music_begin(void);
void srp_audio_music_end(void);
void srp_audio_set_enabled(bool on);
void srp_audio_advance(uint64_t pit_clocks);
void srp_audio_flush(void);
void srp_audio_pit2(uint64_t at, uint16_t divisor);
void srp_audio_port61(uint64_t at, uint8_t value);
#endif
