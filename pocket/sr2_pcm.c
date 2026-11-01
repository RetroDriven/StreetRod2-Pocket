/* Original-DOS AdLib music, prepared offline, 24/48kHz mono PCM.
 * A finished track loops until the original game requests another song
 * or stops playback. Failed starts retain the previous stream. Missing music is silent.
 */
#include "sr2_pcm.h"
#include "sr2_loading.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#define MAGIC "SR2PCM1\0"
#define BUF 2048u
static bool selected,playing,busy,paused;
static bool refill_allowed=true;
static uint32_t deferred_refills;
bool sr2_pcm_allow_refill(bool allow){bool old=refill_allowed;refill_allowed=allow;return old;}
bool sr2_pcm_busy(void){return busy;}
static FILE *f;
static unsigned char *pack;
static uint32_t pack_size;
static bool memory_active;
static char names[6][16];
static uint32_t remain,track_offset,track_frames;
static uint32_t track_loops,read_errors;
static unsigned file_rate;
static int current_slot=-1;
static uint32_t starts,stops,resumes,start_failures;
static int16_t audio[BUF];
static unsigned n,pos;
static uint32_t le(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void close_stream(void){FILE *old=f;f=NULL;if(old){bool was=busy;busy=true;fclose(old);busy=was;}memory_active=false;current_slot=-1;playing=false;paused=false;file_rate=0;remain=0;pos=n=0;track_offset=track_frames=0;}
void sr2_pcm_get_stats(sr2_pcm_stats_t *out){
    if(out){out->track_loops=track_loops;out->read_errors=read_errors;
        out->playing=playing;out->paused=paused;out->stream_open=f!=NULL || memory_active;out->track_slot=current_slot;
        out->starts=starts;out->stops=stops;out->resumes=resumes;out->start_failures=start_failures;out->deferred_refills=deferred_refills;}
    if(out)out->preloaded_bytes=pack_size;
}
void sr2_pcm_clear_stats(void){track_loops=read_errors=starts=stops=resumes=start_failures=deferred_refills=0;}
/* Read-only boot-time diagnostics for the optional locally rendered archive.
 * The SDK version used by this port has no public of_file_size() helper;
 * standard fopen/fseek/ftell are supported and already used for playback.
 * Only invoked when X mode is selected. Neither open alters PCM state.
 */
static void probe_pack(void){
    const char *paths[]={"SR2_PCM.PAK","slot:31"};
    for(unsigned i=0;i<2;i++){
        errno=0;
        FILE *in=fopen(paths[i],"rb");
        if(!in){
            printf("[SR2-PCM-SLOT] path=%s open=FAIL errno=%d\n",paths[i],errno);
            continue;
        }
        if(fseek(in,0,SEEK_END)==0){
            long size=ftell(in);
            if(size>=0)printf("[SR2-PCM-SLOT] path=%s open=OK bytes=%ld\n",paths[i],size);
            else printf("[SR2-PCM-SLOT] path=%s open=OK ftell=FAIL errno=%d\n",paths[i],errno);
        }else{
            printf("[SR2-PCM-SLOT] path=%s open=OK fseek=FAIL errno=%d\n",paths[i],errno);
        }
        fclose(in);
    }
}
void sr2_pcm_probe(void){bool was=busy;busy=true;probe_pack();busy=was;}
bool sr2_pcm_preload(uint32_t budget){
    if(pack)return true;
    if(busy || f || memory_active || budget<216)return false;
    busy=true;
    FILE *in=fopen("SR2_PCM.PAK","rb");if(!in)in=fopen("slot:31","rb");
    unsigned char *candidate=NULL;long length=-1;
    if(in && !fseek(in,0,SEEK_END))length=ftell(in);
    bool ok=length>=216 && (uint64_t)length<=budget;
    if(ok)ok=fseek(in,0,SEEK_SET)==0;
    if(ok){candidate=malloc((size_t)length);ok=candidate && sr2_loading_read(in,candidate,(size_t)length,SR2_LOAD_MUSIC);}
    if(in)fclose(in);
    if(ok)ok=!memcmp(candidate,MAGIC,8) && le(candidate+8)==6 &&
        (le(candidate+12)==24000 || le(candidate+12)==48000) && le(candidate+16)==1;
    if(ok){pack=candidate;pack_size=(uint32_t)length;printf("[SR2-PCM-RAM] bytes=%lu; playback and track changes use memory\n",(unsigned long)pack_size);}
    else{free(candidate);printf("[SR2-PCM-RAM] streaming retained; budget=%lu\n",(unsigned long)budget);}
    busy=false;return ok;
}
void sr2_pcm_select(bool enable){bool was=busy;busy=true;close_stream();selected=enable;memset(names,0,sizeof names);sr2_pcm_clear_stats();busy=was;}
void sr2_pcm_bind(int slot,const char *name){
    if(slot<0||slot>=6||!name)return;
    const char *s=name;
    for(const char *c=name;*c;c++)if(*c=='/'||*c=='\\'||*c==':')s=c+1;
    size_t len=strlen(s);
    if(!len||len>=sizeof names[0])return;
    memset(names[slot],0,sizeof names[slot]);
    for(size_t i=0;i<len;i++)names[slot][i]=(char)toupper((unsigned char)s[i]);
}
static void start_track(int slot){
    if(!selected)return;
    if(slot<0||slot>=6||!names[slot][0])return;
    if(pack){
        for(unsigned i=0;i<6;i++){
            const unsigned char *e=pack+24+i*32;char id[17];
            memcpy(id,e,16);id[16]=0;
            uint32_t off=le(e+16),count=le(e+20);
            if(strcmp(id,names[slot]))continue;
            if(!count || off<216 || (uint64_t)off+(uint64_t)count*2>pack_size)break;
            close_stream();memory_active=true;current_slot=slot;starts++;
            remain=track_frames=count;track_offset=off;file_rate=le(pack+12);playing=true;
            printf("[SR2-PCM] track=%s frames=%lu rate=%u RAM\n",id,(unsigned long)count,file_rate);return;
        }
        printf("[SR2-PCM] track absent/invalid (%s), previous stream retained\n",names[slot]);return;
    }
    /* A named lookup can fail if SDK file-slot registration did not resolve
       its instance alias. Direct slot:31 is the documented SDK fallback.
       Keep it behind the failed named open so native/desktop tests also work. */
    errno=0;
    FILE *in=fopen("SR2_PCM.PAK","rb");
    int named_errno=in?0:errno;
    if(!in){
        errno=0;
        in=fopen("slot:31","rb");
        int slot_errno=in?0:errno;
        if(in)printf("[SR2-PCM] named open failed errno=%d; direct slot:31 OK\n",named_errno);
        else{
            printf("[SR2-PCM-ERROR] pack-unavailable track=%s named_errno=%d slot31_errno=%d; music silence; previous stream retained\n",
                   names[slot],named_errno,slot_errno);
            return;
        }
    }
    unsigned char h[24];
    if(fread(h,1,24,in)!=24||memcmp(h,MAGIC,8)||le(h+8)!=6||(le(h+12)!=24000&&le(h+12)!=48000)||le(h+16)!=1){
        fclose(in);printf("[SR2-PCM] invalid pack header; previous stream retained\n");return;
    }
    if(fseek(in,0,SEEK_END)){fclose(in);return;}
    long length=ftell(in);
    if(length<216||fseek(in,24,SEEK_SET)){fclose(in);return;}
    for(unsigned i=0;i<6;i++){
        unsigned char e[32];char id[17];
        if(fread(e,1,32,in)!=32)break;
        memcpy(id,e,16);id[16]=0;
        uint32_t offset=le(e+16),frames=le(e+20);
        if(!frames||strcmp(id,names[slot]))continue;
        if(offset<216 || (uint64_t)offset+(uint64_t)frames*2>(uint64_t)length)break;
        if(fseek(in,(long)offset,SEEK_SET))break;
        close_stream();
        f=in;current_slot=slot;starts++;remain=frames;track_frames=frames;track_offset=offset;file_rate=le(h+12);playing=true;
        printf("[SR2-PCM] track=%s frames=%lu rate=%u\n",id,(unsigned long)frames,file_rate);
        return;
    }
    fclose(in);printf("[SR2-PCM] track absent (%s), music silence; previous stream retained\n",names[slot]);
}
/* Unlike starting another song, a temporary engine-tuning / race interruption
 * must not discard the PCM read pointer or the already buffered samples. */
void sr2_pcm_start(int slot){
    bool was=busy;busy=true;
    uint32_t before=starts;
    start_track(slot);
    if(selected && before==starts)start_failures++;
    busy=was;
}
void sr2_pcm_stop(void){
    stops++;
    if(f || memory_active){playing=false;paused=true;}
    else close_stream();
}
void sr2_pcm_resume(void){
    if(selected && (f || memory_active) && paused){
        playing=true;paused=false;resumes++;
        printf("[SR2-PCM] resumed archived song at saved sample offset\n");
    }
}
bool sr2_pcm_playing(void){return playing;}
bool sr2_pcm_has_stream(void){return f!=NULL || memory_active;}
unsigned sr2_pcm_rate(void){return playing?file_rate:0u;}
int16_t sr2_pcm_sample(void){
    if(!playing)return 0;
    if(memory_active){
        if(!remain){remain=track_frames;track_loops++;}
        uint32_t at=track_offset+(track_frames-remain)*2;
        remain--;
        return (int16_t)((unsigned)pack[at]|(unsigned)pack[at+1]<<8);
    }
    if(pos==n){
        /* SDK file idle hooks MUST NOT issue a nested blocking DMA read or
         * seek. Keep the exact stream position and resume refills afterward. */
        if(!refill_allowed){deferred_refills++;return 0;}
        if(!remain){
            /* v0.5.5 left `playing` true at EOF while returning silence
               forever. Restart just this track: the engine remains in charge
               of mus_stop, mus_play and playlist transitions. */
            busy=true;
            int seek_ok=track_frames && fseek(f,(long)track_offset,SEEK_SET)==0;
            busy=false;
            if(!seek_ok){read_errors++;close_stream();return 0;}
            remain=track_frames;
            track_loops++;
        }
        unsigned count=remain<BUF?remain:BUF;
        unsigned char bytes[BUF*2];
        busy=true;
        size_t read_count=fread(bytes,2,count,f);
        busy=false;
        if(read_count!=count){read_errors++;close_stream();return 0;}
        for(unsigned i=0;i<count;i++)audio[i]=(int16_t)((unsigned)bytes[2*i]|(unsigned)bytes[2*i+1]<<8);
        remain-=count;pos=0;n=count;
    }
    return audio[pos++];
}
