/* SPDX-License-Identifier: MIT
 * Cache only exact original-bank motor/paint register signatures. The original
 * AdLib driver still supplies frequency, carrier total level and key commands.
 * Samples come from the real Opal bridge, rendered offline from owned data.
 * Pitch resampling/loop seams/key-off ramps are sample playback, not chip-exact.
 * Unsupported timbres and rhythm voices remain on the accurate live renderer.
 * Load once at boot. No file I/O, allocation or DSP synthesis during playback.
 */
#include "sr2_fx_cache.h"
#include "sr2_loading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__riscv)
#include "of_fastram.h"
#define CACHE_TEXT OF_FASTTEXT
#define CACHE_DATA OF_FASTDATA
#define CACHE_RODATA OF_FASTRODATA
#else
#define CACHE_TEXT
#define CACHE_DATA
#define CACHE_RODATA
#endif
#define RATE 24000u
#define Q 4096u
#define MAX_BYTES (4u*1024u*1024u)
typedef struct {
    const int16_t *pcm;
    uint32_t frames,loop_start,loop_end,freq,release,flags;
    uint8_t signature[11];
} patch_t;
typedef struct {
    uint8_t reg[13];
    uint16_t valid;
    patch_t *patch;
    uint32_t phase,step,gain,release_left;
    bool playing,key;
} voice_t;
CACHE_DATA static patch_t patches[32];
CACHE_DATA static voice_t voices[13];
CACHE_DATA static bool waves,rhythm,ready;
static unsigned patch_count;
CACHE_DATA static unsigned tail_cursor;
CACHE_DATA static unsigned char tail_source[4];
CACHE_DATA static uint32_t starts,frames,transitions;
/* Exact OPL total-level attenuation (0.75 dB/step), rounded to Q15.
 * Only the carrier is volume-controlled by the original AdLib driver. */
CACHE_RODATA static const uint16_t levels[64]={
32768,30048,27554,25268,23170,21247,19484,17867,
16384,15024,13777,12634,11585,10624,9742,8933,
8192,7512,6889,6317,5793,5312,4871,4467,
4096,3756,3444,3158,2896,2656,2435,2233,
2048,1878,1722,1579,1448,1328,1218,1117,
1024,939,861,790,724,664,609,558,
512,470,431,395,362,332,304,279,
256,235,215,197,181,166,152,140};
static uint32_t get32(const uint8_t *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
uint8_t sr2_fx_cache_register(unsigned ch,unsigned i){
    static const uint8_t mod[9]={0,1,2,8,9,10,16,17,18};
    static const uint8_t group[5]={0x20,0x40,0x60,0x80,0xe0};
    if(i<10)return group[i/2]+mod[ch]+(i&1u?3:0);
    return (uint8_t)((i==10?0xc0:i==11?0xa0:0xb0)+ch);
}
void sr2_fx_cache_reset(void){
    memset(voices,0,sizeof voices);waves=rhythm=false;
    starts=frames=transitions=tail_cursor=0;
}
bool sr2_fx_cache_load(void){
    if(ready)return true;
    FILE *f=fopen("SR2_FX.PAK","rb");if(!f)f=fopen("slot:32","rb");
    if(!f)return false;
    uint8_t h[32];uint8_t *b=NULL;
    bool ok=fread(h,1,sizeof h,f)==sizeof h && !memcmp(h,"SR2FXC1\0",8) &&
        get32(h+8)==1 && get32(h+12)==RATE && get32(h+16)>=2 && get32(h+16)<=32 && get32(h+20)==64;
    uint32_t size=ok?get32(h+24):0;
    if(ok)ok=size>=get32(h+16)*64u+4u && size<=MAX_BYTES && !(size&1u);
    if(ok){b=malloc(size);ok=b && sr2_loading_read(f,b,size,SR2_LOAD_EFFECTS) && fgetc(f)==EOF;}
    fclose(f);
    if(ok){
        uint32_t hash=2166136261u;
        for(uint32_t i=0;i<size;i++)hash=(hash^b[i])*16777619u;
        ok=hash==get32(h+28);
    }
    unsigned count=ok?get32(h+16):0;
    patch_t candidate[32]={0};
    if(ok)for(unsigned i=0;i<count;i++){
        const uint8_t *e=b+64*i;
        uint32_t id=get32(e),off=get32(e+4),n=get32(e+8),a=get32(e+12),z=get32(e+16);
        uint32_t freq=get32(e+20),release=get32(e+24);
        if(id!=i+1 || off<32+64*count || (off&1u) || off-32>size ||
           n>768000u || !n || n>(size-(off-32))/2u ||
           a>=z || z>n || z-a<64 || !freq || freq>131072u ||
           !release || release>RATE){ok=false;break;}
        candidate[i].pcm=(const int16_t*)(b+off-32);candidate[i].frames=n;
        candidate[i].loop_start=a;candidate[i].loop_end=z;
        candidate[i].freq=freq;candidate[i].release=release;candidate[i].flags=get32(e+28);
        if(candidate[i].flags&~15u){ok=false;break;}
        memcpy(candidate[i].signature,e+32,11);
    }
    if(!ok){free(b);return false;}
    memcpy(patches,candidate,sizeof patches);patch_count=count;ready=true;sr2_fx_cache_reset();
    printf("[SR2-FX-CACHE] ready=1 profiles=%u bytes=%u rate=%u\n",count,size,RATE);
    return true;
}
static patch_t *match(const voice_t *v,unsigned ch,bool instant){
    if(!ready || !waves || (rhythm && ch>=6) || (v->valid&2047u)!=2047u)return NULL;
    patch_t *best=NULL;uint32_t distance=UINT32_MAX;
    uint32_t freq=(v->reg[11]|(uint32_t)(v->reg[12]&3u)<<8)<<((v->reg[12]>>2)&7u);
    for(unsigned k=0;k<patch_count;k++){
        unsigned i;
        for(i=0;i<11;i++){
            uint8_t mask=i==3?0xc0u:0xffu;
            if((v->reg[i]&mask)!=(patches[k].signature[i]&mask))break;
        }
        if(i==11){
            if(!(patches[k].flags&1u))return &patches[k];
            /* A wrench patch must not swallow distant notes of the same
             * bank timbre (e.g. spray/tyres). Preserve accurate live FM there. */
            if(freq*2u<patches[k].freq || freq>patches[k].freq*2u)continue;
            uint32_t d=freq>patches[k].freq?freq-patches[k].freq:patches[k].freq-freq;
            if(d<distance || (d==distance && (((patches[k].flags&4u)!=0)==instant))){distance=d;best=&patches[k];}
        }
    }
    return best;
}
static void update(voice_t *v){
    uint32_t f=(v->reg[11]|(uint32_t)(v->reg[12]&3u)<<8)<<((v->reg[12]>>2)&7u);
    uint32_t ref=v->patch->freq;
    v->step=(f*Q+ref/2u)/ref;
    if(v->step>Q*8u)v->step=Q*8u;
    if(!(v->playing && !v->key && (v->patch->flags&4u)))v->gain=levels[v->reg[3]&63u];
}
static void sync(unsigned ch,voice_t *v,sr2_fx_write_fn live){
    for(unsigned i=0;i<13;i++)if(v->valid&(1u<<i))live(sr2_fx_cache_register(ch,i),v->reg[i]);
}
static void detach(unsigned channel,voice_t *v){
    if(!v->playing || !v->patch || !(v->patch->flags&4u))return;
    unsigned slot=9;
    for(;slot<13;slot++)if(voices[slot].playing && tail_source[slot-9]==channel)break;
    if(slot==13)for(slot=9;slot<13;slot++)if(!voices[slot].playing)break;
    if(slot==13){slot=9+tail_cursor;tail_cursor=(tail_cursor+1)%4;}
    tail_source[slot-9]=(unsigned char)channel;voices[slot]=*v;v->playing=false;
}
bool sr2_fx_cache_write(uint8_t reg,uint8_t value,sr2_fx_write_fn live,sr2_fx_mute_fn mute){
    if(!ready)return false;
    if(reg==1 || reg==0xbd){
        if(reg==1)waves=(value&0x20u)!=0;else rhythm=(value&0x20u)!=0;
        for(unsigned ch=0;ch<9;ch++){
            voice_t *v=&voices[ch];
            if(v->patch && !match(v,ch,false)){
                detach(ch,v);v->patch=NULL;v->playing=false;sync(ch,v,live);transitions++;
            }
        }
        return false;
    }
    unsigned ch,i;
    if(!sr2_fx_cache_decode_register(reg,&ch,&i))return false;
    {
        voice_t *v=&voices[ch];bool was=v->key;

        v->reg[i]=value;v->valid|=(uint16_t)(1u<<i);
        if(i==12)v->key=(value&0x20u)!=0;
        patch_t *next=match(v,ch,i==12 && was && !v->key && v->phase==0);
        if(i==12 && was && !v->key && v->phase==0 && next && (next->flags&4u) && v->playing){v->patch=next;update(v);v->release_left=0;return true;}
        if(v->patch && next!=v->patch){
            if(i<11)detach(ch,v);
            v->patch=NULL;v->playing=false;sync(ch,v,live);transitions++;
            if(!next)return true; /* complete register replay already sent this write */
        }
        if(next){
            if(!v->patch){v->patch=next;mute(ch);was=false;}
            update(v);
            if(v->key && !was){
                v->phase=0;v->playing=true;v->release_left=0;starts++;
            }else if(!v->key && was && v->playing){if(!(v->patch->flags&4u))v->release_left=v->patch->release;}
            return true;
        }
        return false;
    }
    return false;
}
static uint32_t wrap(const patch_t *p,uint32_t phase){
    uint32_t end=p->loop_end*Q;
    if(phase>=end){
        uint32_t begin=p->loop_start*Q;
        phase=begin+(phase-end)%(end-begin);
    }
    return phase;
}
CACHE_TEXT int16_t sr2_fx_cache_sample(void){
    int32_t mix=0;bool any=false;
    for(unsigned ch=0;ch<13;ch++){
        voice_t *v=&voices[ch];patch_t *p=v->patch;
        if(!p || !v->playing)continue;
        any=true;
        uint32_t phase=v->phase,i=phase>>12,frac=phase&(Q-1);
        int32_t sample=p->pcm[i];
        if(frac){
            uint32_t j=i+1;if(j>=p->loop_end)j=p->loop_start;
            int32_t b=p->pcm[j];
            sample+=((b-sample)*(int32_t)frac)/(int32_t)Q;
        }
        sample=(sample*(int32_t)v->gain)/32768;
        if(p->flags&8u)sample=(sample*7)/8;
        if(v->release_left){
            sample=(sample*(int32_t)v->release_left)/(int32_t)p->release;
            if(!--v->release_left)v->playing=false;
        }
        mix+=sample;
        uint32_t next=phase+v->step;
        if((p->flags&2u) && next>=p->loop_end*Q)v->playing=false;
        else v->phase=wrap(p,next);
    }
    if(any)frames++;
    if(mix>32767)mix=32767;
    if(mix< -32768)mix=-32768;
    return (int16_t)mix;
}
void sr2_fx_cache_skip(uint32_t n){
    for(unsigned ch=0;ch<13;ch++){
        voice_t *v=&voices[ch];if(!v->patch || !v->playing)continue;
        /* Constant work even after a long OS-menu pause. */
        uint64_t phase=(uint64_t)v->phase+(uint64_t)n*v->step;
        uint32_t end=v->patch->loop_end*Q,begin=v->patch->loop_start*Q;
        if(phase>=end && (v->patch->flags&2u)){v->playing=false;continue;}
        if(phase>=end)phase=begin+(phase-end)%(end-begin);
        v->phase=(uint32_t)phase;
        if(v->release_left){
            if(n>=v->release_left){v->release_left=0;v->playing=false;}
            else v->release_left-=n;
        }
    }
}
void sr2_fx_cache_stats(sr2_fx_cache_stats_t *out){
    if(!out)return;
    out->loaded=ready?1u:0u;out->starts=starts;out->frames=frames;out->transitions=transitions;
    out->active=0;for(unsigned i=0;i<13;i++)if(voices[i].playing)out->active++;
}
