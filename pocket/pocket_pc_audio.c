/* PC-speaker PIT and port 61h emulation. */
#include "pocket_pc_audio.h"
#include "of_audio.h"
#include "fast_opl2.h"
#include "sr2_pcm.h"
#ifdef SR2_WITH_FX_CACHE
#include "sr2_fx_cache.h"
#endif
#ifdef SR2_WITH_OPAL
#include "opal/opal.h"
#endif
#include <stdbool.h>
#include <string.h>
#if defined(__riscv)
#include "of_fastram.h"
#define SR2_FX_STATE OF_FASTDATA
#else
#define SR2_FX_STATE
#endif
#if defined(__riscv) && !defined(SR2_WITH_OPAL)
#error "SR2 v0.6.11 Pocket X effects require Opal; do not build Fast FM fallback"
#endif
#if defined(__riscv) && (!defined(SR2_OPAL_FX_API) || SR2_OPAL_FX_API < 3)
#error "SR2 v0.6.11 requires the cached Opal FX API; do not ship the older full-scan FX path"
#endif

#define SR_PIT_HZ 1193182ull
#define SR_AUDIO_RATE 48000ull
#define SR_AUDIO_SYNTH_RATE 24000ull /* 24k PIT sampling, duplicate at 48k output */
#define SR_FX_FAST_RATE 12000u /* balance: accurate Opal at 12k, interpolate to 24k */
#define SR_AUDIO_BUF_PAIRS 512u
#define SR_AUDIO_GAIN 10000
#define SR_AUDIO_MAX_QUEUE 7200 /* at most 150ms queued; preserve responsiveness */
#define SR_AUDIO_LEAD_PAIRS 2880 /* 60ms pre-roll to cover 40-50ms heavy-frame stalls */

static uint8_t port61;
static uint32_t pit2_latch=65536, pit2_count=65536;
static bool out2=true;
static uint64_t out2_next;
static uint64_t rendered, next_sample=SR_PIT_HZ, accum_hi;
static int32_t hp_prev_x,hp_prev_y,lp_prev;
static int16_t pcm[SR_AUDIO_BUF_PAIRS*2];
static unsigned pcm_count;
static bool pcm_has_fx;
/* Legacy Fast FM is still available as B or the optional Y diagnostic.
 * X uses the separate accurate Opal effects renderer below. */
static bool backend_adlib;
static bool backend_pcm;
static bool backend_pcm_fx_only;
static bool backend_pcm_music_only; /* diagnostic X+L1: no live effects */
static bool backend_hybrid; /* diagnostic mix: prerecorded music plus quiet live FM */
static bool backend_opal;
#ifdef SR2_WITH_OPAL
static Opal opal_chip;
#endif
static bool opl_enabled;
static rd_opl2_t opl_chip;
/* Isolated effects/engine bus (locally rendered PCM soundtrack).
 * The legacy approximator is retained only for explicit B/Y diagnostics. */
#ifdef SR2_WITH_OPAL
/* v0.6.11: Opal still models the AdLib/YM3812 effects, never Fast FM.
 * Retain the existing 12k output reconstruction. Real work is bounded by
 * natural envelope completion and an exact idle-envelope fast path in Opal. */
SR2_FX_STATE static Opal fx_opal_chip;
#if defined(__riscv)
_Static_assert(sizeof(Opal)<8192,"Compact buffered Opal state required for app BRAM");
#endif
#if SR2_OPAL_FX_API >= 2
SR2_FX_STATE static OpalFXPlan fx_opal_plan;
#endif
static bool fx_awake;
static unsigned fx_activity_countdown;
static uint32_t fx_synth_calls,fx_idle_stops,fx_native_ticks,fx_operator_steps;
static uint32_t fx_queue_high_water,fx_queue_overflows;
/* Opal runs internally at 49,716 Hz even with a lower output rate. Use
 * actual envelope state, including keyed one-shots, to stop idle work.
 * No duration cap: sustained motors remain audible until original key-off. */
#define FX_RELEASE_TAIL 24000u /* render one second of release after last key-off */
static uint16_t fx_keys;
static uint8_t fx_drums;
static uint32_t fx_release_left;
static uint8_t fx_phase;
static int16_t fx_previous,fx_current;
static bool fx_audible(void){
#ifdef OPAL_VERSION
    if(fx_opal_chip.writeBuf[fx_opal_chip.writeBufCur].reg & 0x200u)return true;
    /* A silent modulator may sustain forever (the original motor bank does).
     * Only operators actually routed to a channel output can keep the bus
     * audible. Include additive/percussion outputs; never gate by carrier ID.
     * Tremolo only adds attenuation, so omit it from the minimum below. */
    static const unsigned ksl_shift[4]={8,1,2,0};
    unsigned channels=fx_opal_chip.opl3Enabled?OPAL_CHANNEL_COUNT:9u;
    for(unsigned ch=0;ch<channels;ch++)for(unsigned i=0;i<4u;i++){
        unsigned slot=fx_opal_chip.chan[ch].outSource[i];
        if(slot==OPAL_OP_NONE)continue;
        const OpalOperator *o=&fx_opal_chip.op[slot];
        if((unsigned)o->envelopeLevel+o->outputLevel+
               (o->egKsl>>ksl_shift[o->keyScaleReg])<0x1ffu ||
           (o->key && (o->envelopeStage==OPAL_ENVELOPE_STAGE_ATTACK ||
                       o->envelopeStage==OPAL_ENVELOPE_STAGE_RELEASE)))return true;
    }
    return false;
#else
    return fx_keys || fx_drums || fx_release_left; /* old native mocks */
#endif
}
static void fx_reset(void){
#ifdef SR2_WITH_FX_CACHE
    sr2_fx_cache_reset();
#endif
    opalInit(&fx_opal_chip,(int)SR_FX_FAST_RATE);
#if SR2_OPAL_FX_API >= 2
    memset(&fx_opal_plan,0,sizeof fx_opal_plan);
#endif
    fx_keys=0;fx_drums=0;fx_release_left=0;
    fx_awake=false;fx_activity_countdown=0;fx_synth_calls=fx_idle_stops=fx_native_ticks=fx_operator_steps=0;
    fx_phase=0;fx_previous=fx_current=0;
    fx_queue_high_water=fx_queue_overflows=0;
}
static void fx_write_live(uint8_t reg,uint8_t value){
#if SR2_OPAL_FX_API >= 2
    fx_opal_plan.valid=false; /* also covers direct writes and buffer-overflow flushes */
#endif
#ifdef OPAL_VERSION
    /* Preserve rapid key-on/key-off and timbre order at the real chip clock. */
    if(fx_opal_chip.writeBuf[fx_opal_chip.writeBufLast].reg&0x200u)fx_queue_overflows++;
    opalWriteRegBuffered(&fx_opal_chip,(uint16_t)reg,value);
    unsigned pending=(fx_opal_chip.writeBufLast+OPAL_WRITEBUF_SIZE-fx_opal_chip.writeBufCur)%OPAL_WRITEBUF_SIZE;
    if(pending>fx_queue_high_water)fx_queue_high_water=pending;
#else
    opalWriteReg(&fx_opal_chip,(uint16_t)reg,value);
#endif
    fx_awake=true;fx_activity_countdown=64u;
    if(reg>=0xb0u && reg<=0xb8u){
        unsigned bit=1u<<(reg-0xb0u);
        uint16_t before=fx_keys;
        if(value&0x20u)fx_keys|=(uint16_t)bit;
        else fx_keys&=(uint16_t)~bit;
        if(before!=fx_keys)fx_release_left=FX_RELEASE_TAIL;
    }else if(reg==0xbdu){
        uint8_t before=fx_drums;
        fx_drums=(value&0x20u)?(value&0x1fu):0;
        if(before!=fx_drums)fx_release_left=FX_RELEASE_TAIL;
    }
}
static int16_t fx_sample_live(void){
    if(!opl_enabled || !fx_awake){
        fx_phase=0;fx_previous=fx_current=0;
        return 0;
    }
    int16_t out;
    if(!fx_phase){
        int16_t l=0,r=0;
#ifdef SR2_OPAL_FX_API
        uint64_t before=fx_opal_chip.writeBufSampleCount;
#if SR2_OPAL_FX_API >= 2
        fx_operator_steps+=opalSampleFXPlanned(&fx_opal_chip,&fx_opal_plan,&l,&r);
#else
        fx_operator_steps+=opalSampleFX(&fx_opal_chip,&l,&r);
#endif
        fx_native_ticks+=(uint32_t)(fx_opal_chip.writeBufSampleCount-before);
#else
        opalSample(&fx_opal_chip,&l,&r);
#endif
        fx_synth_calls++;
        if(fx_activity_countdown)fx_activity_countdown--;
        else if(!fx_audible()){
            fx_awake=false;fx_idle_stops++;
            fx_phase=0;fx_previous=fx_current=0;
            return 0;
        }else fx_activity_countdown=64u;
        fx_previous=fx_current;
        fx_current=(int16_t)(((int32_t)l+(int32_t)r)/2);
    }
    /* Two-point interpolation (12k -> 24k); no synth calls on odd ticks. */
    out=fx_phase?fx_current:(int16_t)(((int32_t)fx_previous+(int32_t)fx_current)/2);
    fx_phase^=1u;
    if(!fx_keys && !fx_drums && fx_release_left)fx_release_left--;
    return out;
}
#else
/* Legacy native unit tests compile without optional Opal dependency.
 * A Pocket release must enable SR2_WITH_OPAL (enforced by build scripts). */
static rd_opl2_t fx_chip;
static void fx_reset(void){rd_opl2_reset(&fx_chip);rd_opl2_fx_mode(&fx_chip,true);}
static void fx_write_live(uint8_t reg,uint8_t value){rd_opl2_write(&fx_chip,reg,value);}
static int16_t fx_sample_live(void){return rd_opl2_sample(&fx_chip);}
#endif
#ifdef SR2_WITH_FX_CACHE
static void fx_cache_mute(unsigned ch){
#if defined(SR2_WITH_OPAL) && defined(OPAL_VERSION)
    /* A cached voice must leave no sustaining modulator/carrier in live FM.
     * Strip pending key-ons too, without punching holes in Opal's FIFO. */
    for(unsigned n=0;n<OPAL_WRITEBUF_SIZE;n++){
        OpalWriteBuf *w=&fx_opal_chip.writeBuf[n];
        if((w->reg&0x200u) && (w->reg&0x1ffu)==0xb0u+ch)w->data&=(uint8_t)~0x20u;
    }
    for(unsigned n=0;n<2;n++){
        OpalOperator *o=&fx_opal_chip.op[fx_opal_chip.chan[ch].opSlot[n]];
        o->key=0;o->envelopeLevel=511;o->envelopeStage=OPAL_ENVELOPE_STAGE_RELEASE;
        o->out=o->prevOut=0;o->phaseReset=false;
    }
    fx_keys&=(uint16_t)~(1u<<ch);
#if SR2_OPAL_FX_API >= 2
    fx_opal_plan.valid=false;
#endif
#else
    fx_write_live((uint8_t)(0xb0u+ch),0);
#endif
}
#endif
static void fx_write(uint8_t reg,uint8_t value){
#ifdef SR2_WITH_FX_CACHE
    if(sr2_fx_cache_write(reg,value,fx_write_live,fx_cache_mute))return;
#endif
    fx_write_live(reg,value);
}
static int16_t fx_sample(void){
    int32_t value=fx_sample_live();
#ifdef SR2_WITH_FX_CACHE
    if(opl_enabled)value+=sr2_fx_cache_sample();
#endif
    if(value>32767)value=32767;
    if(value< -32768)value=-32768;
    return (int16_t)value;
}
static unsigned music_write_depth;
SR2_FX_STATE static srp_audio_stats_t audio_stats;
bool srp_audio_split_music(void){return backend_pcm && !backend_hybrid;}
bool srp_audio_music_active(void){return srp_audio_split_music() && music_write_depth!=0;}
void srp_audio_music_begin(void){if(music_write_depth<32u)music_write_depth++;}
void srp_audio_music_end(void){if(music_write_depth)music_write_depth--;}
static int16_t add_fx(int16_t recorded,int16_t effects){
    /* Playback-only level recovery. Existing normalized archive peaks at
     * 26000, so 1.25x music peaks at 32500 without solo-track clipping.
     * Original-bank effects need 3.5x relative gain to match that music;
     * the common output gain also applies to them. No idle noise boost. */
    uint32_t peak=effects<0?(uint32_t)-(int32_t)effects:(uint32_t)effects;
    if(peak>audio_stats.fx_peak)audio_stats.fx_peak=peak;
    if(effects)pcm_has_fx=true;
    int32_t effects_level=((int32_t)effects*7)/2;
    int32_t mixed=(((int32_t)recorded+effects_level)*5)/4;
    if(mixed>32767){audio_stats.fx_clips++;mixed=32767;}
    if(mixed< -32768){audio_stats.fx_clips++;mixed=-32768;}
    if(effects)audio_stats.fx_samples++;
    return (int16_t)mixed;
}
static uint64_t opl_sample_count;
static uint32_t audio_pending_min=UINT32_MAX;
static void opl_advance(uint64_t pit_clocks);
/* Only used in diagnostic Y mode; saturate to protect the output ring. */
static int16_t mix_hybrid(int16_t recorded, int16_t live){
    int32_t mixed=(int32_t)recorded+(int32_t)live/4; /* -12 dB live overlay */
    if(mixed>32767){audio_stats.hybrid_clips++;mixed=32767;}
    else if(mixed<-32768){audio_stats.hybrid_clips++;mixed=-32768;}
    if(live)audio_stats.hybrid_live_nonzero++;
    return (int16_t)mixed;
}
void srp_audio_select_backend(int mode){
    /* 0=speaker,1=Fast FM,2=Opal,3=PCM+balanced FX,4=hybrid,5=PCM only. */
    backend_adlib=(mode!=0);
    backend_pcm=(mode==3 || mode==4 || mode==5 || mode==6);
    backend_pcm_fx_only=(mode==6);
    backend_pcm_music_only=(mode==5);
    backend_hybrid=(mode==4);
#ifdef SR2_WITH_OPAL
    backend_opal=(mode==2);
    if(backend_opal)opalInit(&opal_chip, (int)SR_AUDIO_SYNTH_RATE);
#else
    backend_opal=false;
#endif
    opl_enabled=false;
    opl_sample_count=0;
    pcm_count=0;pcm_has_fx=false;
    music_write_depth=0;
    audio_stats.fm_enabled_now=0;
    audio_stats.fm_mode=(uint32_t)(backend_pcm_fx_only?6:backend_pcm_music_only?5:backend_hybrid?4:backend_pcm?3:backend_opal?2:backend_adlib?1:0);
    if(backend_adlib && !backend_opal && (!backend_pcm || backend_hybrid))rd_opl2_reset(&opl_chip);
    if(backend_pcm)fx_reset();
}
void srp_audio_opl_enable(bool on){
    if(!backend_adlib)return;
    opl_enabled=on;
    audio_stats.fm_enable_calls++;
    audio_stats.fm_enabled_now=on?1u:0u;
    if(backend_opal){
#ifdef SR2_WITH_OPAL
        if(!on)for(unsigned i=0;i<9;i++)opalWriteReg(&opal_chip,(uint16_t)(0xB0u+i),0);
#endif
    }else{
        if(!backend_pcm || backend_hybrid)rd_opl2_enable(&opl_chip,on);
        if(backend_pcm){
#ifdef SR2_WITH_OPAL
            if(!on){
                fx_reset(); /* discard queued key-ons as well as current voices */
            }
#else
            rd_opl2_enable(&fx_chip,on);
#endif
        }
        if(!on && (!backend_pcm || backend_hybrid)){
            /* Stop all nine OPL2 voices without reinitializing the output ring. */
            for(unsigned i=0;i<9;i++){
                rd_opl2_write(&opl_chip,(uint8_t)(0xB0u+i),0);
                /* FX key-off is handled above; do not clear its release envelope. */
            }
        }
    }
}
void srp_audio_opl_write(uint8_t reg,uint8_t value){
    if(backend_adlib){
        audio_stats.fm_register_writes++;
        if(reg>=0xB0u && reg<=0xB8u && (value&0x20u))
            audio_stats.fm_keyon_writes++;
        if(backend_opal){
#ifdef SR2_WITH_OPAL
            opalWriteReg(&opal_chip,reg,value);
#endif
        }else{
            if(!backend_pcm || backend_hybrid)rd_opl2_write(&opl_chip,reg,value);

        }
        /* MUS writes never enter the effects-only Opal chip. */
        if(backend_pcm && !backend_pcm_music_only && !music_write_depth)
            fx_write(reg,value);
    }
}

static int ring_capacity;
static bool synthesis_enabled=true;


 

#define SR_START_FADE_SAMPLES 120u
static unsigned startup_fade_left;
static bool startup_fade_used, first_gate_seen, first_data_seen;

static uint32_t half_period(uint32_t count, bool high){
    uint32_t h=high?(count+1u)/2u:count/2u;
    return h?h:1u;
}
static void flush_pcm(void){
    if(!pcm_count)return;
    /* Do not accumulate seconds of sound after a slowdown or SD I/O stall. */
    int free_pairs=of_audio_free();
    int pending=(ring_capacity>free_pairs)?ring_capacity-free_pairs:0;
    if(pending>=0){
        if((uint32_t)pending<audio_pending_min)audio_pending_min=(uint32_t)pending;
        if((uint32_t)pending>audio_stats.maximum_pending_pairs)
            audio_stats.maximum_pending_pairs=(uint32_t)pending;
    }
    if(free_pairs>=ring_capacity && ring_capacity>0){audio_stats.empty_checks++;audio_stats.empty_checks_total++;}
    /* v0.5.9: The older 150-ms *software* ceiling threw away otherwise
       writable PCM music buffers. Real-hardware capture showed hundreds of
       drops with a healthy source archive (and no read errors). For the
       prerendered track, let the actual hardware free-space control the
       queue: preserve samples whenever a full buffer will fit. A/B preserve
       their previously tested 150-ms latency ceiling. PCM archive playback
       is still governed by the original game's start/stop commands. */
    bool recorded_track=backend_pcm && sr2_pcm_playing();
    if(free_pairs>=(int)pcm_count && (recorded_track || pending<SR_AUDIO_MAX_QUEUE)){
        if(recorded_track && pending>=SR_AUDIO_MAX_QUEUE)
            audio_stats.pcm_saved_by_room++;
        int written=of_audio_write(pcm,(int)pcm_count);
        audio_stats.write_requested=pcm_count;audio_stats.write_result=written;
        if(written!=(int)pcm_count)audio_stats.short_writes++;
        audio_stats.writes++;
        if(pcm_has_fx)audio_stats.fx_output_buffers++;
    }else{
        audio_stats.dropped_buffers++;
        if(pcm_has_fx)audio_stats.fx_dropped_buffers++;
        if(free_pairs<(int)pcm_count)audio_stats.pcm_full_drops++;
        else audio_stats.pcm_legacy_cap_drops++;
    }
    pcm_count=0;pcm_has_fx=false;
}
static void opl_advance(uint64_t pit_clocks){
    /* 24k synthesis + exact 2x stereo duplication into Pocket's 48k PCM.
       All work stays in the same serial audio writer as PC Speaker. */
    uint64_t end=(pit_clocks*SR_AUDIO_SYNTH_RATE)/SR_PIT_HZ;
    if(end<=opl_sample_count)return;
    /* An inactive OPL must NEVER spend CPU cycles rendering silence. */
    if(!opl_enabled){opl_sample_count=end;pcm_count=0;return;}
    /* Keep callbacks bounded during pauses/SD stalls. Dropping the stale
       portion is safer than blocking the whole game trying to catch up. */
    if(end-opl_sample_count>SR_AUDIO_SYNTH_RATE/12u){
        uint64_t skip=end-opl_sample_count-SR_AUDIO_SYNTH_RATE/12u;
        audio_stats.fm_skipped_samples+=(uint32_t)(skip>UINT32_MAX?UINT32_MAX:skip);
#ifdef SR2_WITH_FX_CACHE
        sr2_fx_cache_skip((uint32_t)(skip>UINT32_MAX?UINT32_MAX:skip));
#endif
        opl_sample_count=end-SR_AUDIO_SYNTH_RATE/12u;
        pcm_count=0;pcm_has_fx=false;
    }
    while(opl_sample_count<end){
        int16_t left=0;
        /* Full-rate recorded AdLib feeds two successive 48 kHz samples to
           the Pocket output. Legacy 24 kHz packs and Fast FM retain the
           original duplicate-sample behavior. This keeps the timebase and
           CPU-bound OPL render rate unchanged. */
        bool archived=backend_pcm && sr2_pcm_has_stream();
        bool pcm_full_rate=archived && sr2_pcm_playing() && sr2_pcm_rate()==48000u;
        /* Compute live effects once per 24k tick; duplicate into the two
           48k frames while preserving the recorded high-rate music data. */
        int16_t fx=(backend_pcm && !backend_pcm_music_only)?fx_sample():0;
        if(pcm_full_rate){
            for(unsigned i=0;i<2u;i++){
                int16_t v=sr2_pcm_sample();
                /* Consume one live FM sample per 24 kHz synth tick, not 48 kHz. */
                if(backend_hybrid){
                    if(i==0)left=rd_opl2_sample(&opl_chip);
                    v=mix_hybrid(v,left);
                }else v=add_fx(v,fx);
                if(v)audio_stats.fm_nonzero_samples++;
                pcm[pcm_count*2]=v;
                pcm[pcm_count*2+1]=v;
                if(++pcm_count==SR_AUDIO_BUF_PAIRS)flush_pcm();
            }
        }else{
            int16_t left,right;
            if(archived){
                /* While PCM is paused, render ONLY live effects/engine. Never
                   accidentally resurrect the lower-quality FM soundtrack. */
                left=right=sr2_pcm_playing()?sr2_pcm_sample():0;
                if(backend_hybrid && sr2_pcm_playing()){
                    int16_t live=rd_opl2_sample(&opl_chip);
                    left=right=mix_hybrid(left,live);
                }else left=right=add_fx(left,fx);
            }else if(backend_pcm){
                /* Missing archive: preserve independent effects, safe music silence. */
                left=right=add_fx(0,fx);
                if(!backend_pcm_fx_only)audio_stats.missing_pcm_samples++;
            }else if(backend_opal){
#ifdef SR2_WITH_OPAL
                /* No low-quality FM fallback for X if a song archive is missing. */
                opalSample(&opal_chip,&left,&right);
#else
                left=right=rd_opl2_sample(&opl_chip);
#endif
            }else left=right=rd_opl2_sample(&opl_chip);
            if(left || right)audio_stats.fm_nonzero_samples++;
            for(unsigned i=0;i<2u;i++){
                pcm[pcm_count*2]=left;
                pcm[pcm_count*2+1]=right;
                if(++pcm_count==SR_AUDIO_BUF_PAIRS)flush_pcm();
            }
        }
        audio_stats.fm_samples++;
        opl_sample_count++;
    }
}
static void emit_sample(void){

    int32_t raw=(port61&2u)?
        (int32_t)((((int64_t)2*(int64_t)accum_hi-(int64_t)SR_PIT_HZ)
                    *SR_AUDIO_GAIN)/(int64_t)SR_PIT_HZ):0;
    int32_t hp=raw-hp_prev_x+(int32_t)(((int64_t)hp_prev_y*32673)>>15);
    hp_prev_x=raw;hp_prev_y=hp;
    lp_prev+=(int32_t)(((int64_t)(hp-lp_prev)*24913)>>15);
    int32_t v=lp_prev;
    if(startup_fade_left){
        unsigned elapsed=SR_START_FADE_SAMPLES-startup_fade_left+1u;
        v=(int32_t)((int64_t)v*elapsed/SR_START_FADE_SAMPLES);
        startup_fade_left--;
    }
    if(v>32767)v=32767;
    if(v< -32768)v=-32768;

    for(unsigned repeat=0;repeat<SR_AUDIO_RATE/SR_AUDIO_SYNTH_RATE;repeat++){
        pcm[pcm_count*2]=(int16_t)v;
        pcm[pcm_count*2+1]=(int16_t)v;
        if(++pcm_count==SR_AUDIO_BUF_PAIRS)flush_pcm();
    }
    accum_hi=0;
    next_sample+=SR_PIT_HZ;
}
void srp_audio_advance(uint64_t pit_clocks){
    if(backend_adlib){opl_advance(pit_clocks);return;}
    uint64_t target=pit_clocks*SR_AUDIO_SYNTH_RATE;
    if(target<rendered)return;

    if(!synthesis_enabled){
        rendered=target;
        next_sample=(target/SR_PIT_HZ+1u)*SR_PIT_HZ;
        accum_hi=0;pcm_count=0;
        out2=true;
        out2_next=pit_clocks+half_period(pit2_latch,true);
        return;
    }
    while(rendered<target){
        bool gate=(port61&1u)!=0;
        uint64_t end=target<next_sample?target:next_sample;
        uint64_t transition=out2_next*SR_AUDIO_SYNTH_RATE;
        if(gate && transition<end)end=transition;
        /* Guard against zero-progress if a new gate transition equals rendered. */
        if(end<rendered)end=rendered;
        if((port61&2u) && (!gate||out2))accum_hi+=end-rendered;
        rendered=end;
        if(gate&&rendered==transition){
            out2=!out2;
            pit2_count=pit2_latch;
            out2_next+=half_period(pit2_count,out2);
        }
        if(rendered==next_sample)emit_sample();
    }
}
void srp_audio_flush(void){flush_pcm();}
void srp_audio_set_enabled(bool on){
    if(synthesis_enabled==on)return;
    synthesis_enabled=on;
    pcm_count=0;pcm_has_fx=false;
    accum_hi=0;
    hp_prev_x=hp_prev_y=lp_prev=0;
}
void srp_audio_pit2(uint64_t at,uint16_t divisor){
    if(backend_adlib)return;
    srp_audio_advance(at);
    pit2_latch=divisor?divisor:65536u;
    audio_stats.pit2_writes++;
}
void srp_audio_port61(uint64_t at,uint8_t value){
    if(backend_adlib)return;
    srp_audio_advance(at);
    bool was=(port61&1u)!=0, gate=(value&1u)!=0;
    bool old_data=(port61&2u)!=0, new_data=(value&2u)!=0;
    port61=value;
    audio_stats.port61_writes++;
    if(gate&&!was){
        audio_stats.gate_starts++;
        if(!first_gate_seen){first_gate_seen=true;audio_stats.first_gate_ms=(uint32_t)(at*1000u/SR_PIT_HZ);}
    }
    if(new_data&&!old_data){
        audio_stats.data_enables++;
        if(!first_data_seen){
            first_data_seen=true;
            audio_stats.first_data_ms=(uint32_t)(at*1000u/SR_PIT_HZ);
        }
        if(!startup_fade_used){
            startup_fade_used=true;
            startup_fade_left=SR_START_FADE_SAMPLES;
            audio_stats.startup_fades++;
        }
    }
    if(gate&&!was){
        out2=true;
        pit2_count=pit2_latch;
        out2_next=at+half_period(pit2_count,true);
    }else if(!gate){
        out2=true;
    }
}
void srp_audio_init(void){
    of_audio_init();
    synthesis_enabled=true;
    /* Preserve the menu-selected backend if the SR2 engine reinitializes host. */
    opl_enabled=false;opl_sample_count=0;
    if(backend_adlib){
        if(backend_opal){
#ifdef SR2_WITH_OPAL
            opalInit(&opal_chip,(int)SR_AUDIO_SYNTH_RATE);
#endif
        }
        if(!backend_opal && (!backend_pcm || backend_hybrid))rd_opl2_reset(&opl_chip);
        if(backend_pcm)fx_reset();
    }
    music_write_depth=0;
    port61=0;
    pit2_latch=pit2_count=65536;
    out2=true;out2_next=0;
    rendered=0;next_sample=SR_PIT_HZ;accum_hi=0;
    hp_prev_x=hp_prev_y=lp_prev=0;
    pcm_count=0;pcm_has_fx=false;
    startup_fade_left=0;
    startup_fade_used=first_gate_seen=first_data_seen=false;
    memset(&audio_stats,0,sizeof(audio_stats));
    audio_pending_min=UINT32_MAX;
    ring_capacity=of_audio_free();
    audio_stats.fm_mode=(uint32_t)(backend_pcm_fx_only?6:backend_pcm_music_only?5:backend_hybrid?4:backend_pcm?3:backend_opal?2:backend_adlib?1:0);
     

    static const int16_t silence[SR_AUDIO_LEAD_PAIRS*2]={0};
    unsigned room=ring_capacity>0?(unsigned)ring_capacity:0u;
    unsigned lead=(room>=SR_AUDIO_LEAD_PAIRS+SR_AUDIO_BUF_PAIRS)?
        SR_AUDIO_LEAD_PAIRS:(room>=960u+SR_AUDIO_BUF_PAIRS?960u:0u);
    if(lead)(void)of_audio_write(silence,(int)lead);
    audio_stats.initial_lead_pairs=lead;
}
void srp_audio_peek(srp_audio_stats_t *out){
    if(out){
        *out=audio_stats;
#ifdef SR2_WITH_FX_CACHE
        sr2_fx_cache_stats_t c;sr2_fx_cache_stats(&c);
        out->fx_cache_loaded=c.loaded;out->fx_cached_frames=c.frames;out->fx_cached_voices=c.active;
#endif
#ifdef SR2_WITH_OPAL
        out->fx_queue_high_water=fx_queue_high_water;out->fx_queue_overflows=fx_queue_overflows;
#endif
    }
}
uint32_t srp_audio_pending_pairs(void){
    int free_pairs=of_audio_free();
    return ring_capacity>0 && free_pairs>=0 && free_pairs<ring_capacity?(uint32_t)(ring_capacity-free_pairs):0;
}
void srp_audio_snapshot(srp_audio_stats_t *out){
    if(!out)return;
    *out=audio_stats;
#ifdef SR2_WITH_FX_CACHE
    sr2_fx_cache_stats_t c;sr2_fx_cache_stats(&c);
    out->fx_cache_loaded=c.loaded;out->fx_cached_frames=c.frames;out->fx_cached_voices=c.active;
#endif
#ifdef SR2_WITH_OPAL
    out->fx_native_ticks=fx_native_ticks;out->fx_operator_steps=fx_operator_steps;
    out->fx_queue_high_water=fx_queue_high_water;out->fx_queue_overflows=fx_queue_overflows;
    fx_queue_high_water=fx_queue_overflows=0;
    out->fx_synth_calls=fx_synth_calls;out->fx_idle_stops=fx_idle_stops;
    out->fx_keys=fx_keys;out->fx_drums=fx_drums;out->fx_awake=fx_awake;
    fx_synth_calls=fx_idle_stops=fx_native_ticks=fx_operator_steps=0;
#endif
    audio_stats.missing_pcm_samples=0;
    out->minimum_pending_pairs=(audio_pending_min==UINT32_MAX)?0u:audio_pending_min;
    audio_stats.writes=audio_stats.dropped_buffers=audio_stats.empty_checks=0;
    audio_stats.pcm_saved_by_room=audio_stats.pcm_full_drops=audio_stats.pcm_legacy_cap_drops=0;
    audio_stats.pit2_writes=audio_stats.port61_writes=0;
    audio_stats.gate_starts=audio_stats.data_enables=0;
    audio_stats.startup_fades=0;
    audio_stats.maximum_pending_pairs=0;
    audio_stats.fm_enable_calls=audio_stats.fm_register_writes=audio_stats.fm_keyon_writes=0;
    audio_stats.fm_samples=audio_stats.fm_nonzero_samples=audio_stats.fm_skipped_samples=0;
    audio_stats.hybrid_live_nonzero=audio_stats.hybrid_clips=0;
    audio_stats.fx_samples=audio_stats.fx_clips=0;
    audio_stats.fx_peak=audio_stats.fx_output_buffers=audio_stats.fx_dropped_buffers=0;
    audio_stats.short_writes=0;
    audio_pending_min=UINT32_MAX;
}
