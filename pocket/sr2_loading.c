/* SPDX-License-Identifier: MIT. Original title is read from owned LIB1 at boot.
 * No title pixels or DOS data are embedded in the release. */
#include "sr2_loading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned le16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t le32(const uint8_t *p){return le16(p)|(uint32_t)le16(p+2)<<16;}
bool sr2_loading_decode(const uint8_t *lib,size_t size,const uint8_t counts[16],
                        const uint8_t values[16],uint8_t pixels[64000]){
    if(size<114 || le16(lib)!=4)return false;
    const uint8_t *e=lib+2;
    if(le16(e)!=320 || le16(e+2)!=200 || e[4]!=4 || e[5])return false;
    uint32_t offset=le32(e+22),packed=le16(e+26);
    if(offset>size-114 || packed<2 || packed>=size-114-offset)return false;
    const uint8_t *p=lib+114+offset;
    if(p[0]!=0xbc || p[packed]!=0xbc)return false;
    uint8_t *planes=malloc(32000);if(!planes)return false;
    uint8_t token[256]={0};for(unsigned i=0;i<16;i++)token[e[6+i]]=(uint8_t)(i+1);
    size_t in=1,out=0;bool ok=true;
    while(in<packed && ok){
        unsigned byte=p[in++],count=1;
        if(byte==0 || byte==255){
            if(in>=packed){ok=false;break;}
            unsigned n=p[in++];
            if(!n)byte=e[6+(byte==255)];else count=n+1;
        }else if(token[byte]){unsigned k=token[byte]-1;count=counts[k];byte=values[k];}
        if(!count || count>32000-out){ok=false;break;}
        memset(planes+out,(int)byte,count);out+=count;
    }
    ok=ok && out==32000;
    if(ok)for(unsigned y=0;y<200;y++)for(unsigned x=0;x<320;x++){
        unsigned at=y*40+x/8,bit=7-(x&7),color=0;
        for(unsigned plane=0;plane<4;plane++)color|=((planes[plane*8000+at]>>bit)&1u)<<plane;
        pixels[y*320+x]=(uint8_t)color;
    }
    free(planes);return ok;
}
#ifdef SR2_WITH_LOADING
#include "of.h"
#include "pocket_controls.h"
static uint8_t *title;
static unsigned last_percent,last_stage;
static bool showing;
static void rect(uint8_t *fb,int x,int y,int w,int h,unsigned c){
    for(int row=0;row<h;row++)memset(fb+(y+row)*320+x,(int)c,(size_t)w);
}
static void centered(uint8_t *fb,int y,const char *text,unsigned c){
    srp_draw_text(fb,(320-(int)strlen(text)*6)/2,y,text,(uint8_t)c);
}
static void draw(unsigned stage,unsigned percent){
    uint8_t *fb=of_video_surface();if(!fb)return;
    memset(fb,0,76800);
    if(title)memcpy(fb,title,64000);
    else{
        /* Keep the branded garage placard if optional artwork cannot decode. */
        rect(fb,14,35,292,123,8);rect(fb,17,38,286,117,7);
        rect(fb,20,41,280,111,0);rect(fb,24,47,272,3,12);
        centered(fb,77,"STREET ROD 2",15);centered(fb,99,"THE NEXT GENERATION",7);
        rect(fb,24,141,272,3,12);
    }
    /* A compact chrome placard sits below the untouched 320x200 title. */
    rect(fb,0,200,320,40,8);rect(fb,0,200,320,1,15);
    rect(fb,0,202,320,37,0);
    const char *label=stage==SR2_LOAD_MUSIC?"LOADING MUSIC":stage==SR2_LOAD_PICTURES?"LOADING GARAGE":"LOADING SOUNDS";
    srp_draw_text(fb,10,205,label,7);
    char number[8];snprintf(number,sizeof number,"%3u%%",percent);
    srp_draw_text(fb,284,205,number,15);
    static const uint8_t percent_glyph[5]={0x63,0x13,0x08,0x64,0x63};
    for(unsigned x=0;x<5;x++)for(unsigned y=0;y<7;y++)
        if(percent_glyph[x]&(1u<<y))fb[(205+y)*320+302+x]=15;
    rect(fb,10,217,300,10,7);rect(fb,11,218,298,8,0);
    unsigned fill=296*percent/100;
    if(fill){rect(fb,12,219,(int)fill,6,4);rect(fb,12,219,(int)fill,1,12);}
    of_video_flip();
}
void sr2_loading_begin(const uint8_t counts[16],const uint8_t values[16]){
    showing=true;last_percent=0;last_stage=SR2_LOAD_MUSIC;
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    draw(SR2_LOAD_MUSIC,0);
    FILE *f=fopen("LIB1","rb");if(!f)f=fopen("slot:4","rb");
    if(f){
        long n=-1;if(!fseek(f,0,SEEK_END))n=ftell(f);
        if(n>=114 && n<=65536 && !fseek(f,0,SEEK_SET)){
            uint8_t *lib=malloc((size_t)n);title=malloc(64000);
            if(!lib || !title || fread(lib,1,(size_t)n,f)!=(size_t)n ||
               !sr2_loading_decode(lib,(size_t)n,counts,values,title)){free(title);title=NULL;}
            free(lib);
        }
        fclose(f);
    }
    draw(SR2_LOAD_MUSIC,0);
}
void sr2_loading_progress(unsigned stage,uint32_t done,uint32_t total){
    if(!showing || stage>SR2_LOAD_EFFECTS)return;
    /* Weighted stages, driven by bytes actually read/validated. No fake timer. */
    unsigned base=stage==SR2_LOAD_MUSIC?0:stage==SR2_LOAD_PICTURES?85:90;
    unsigned span=stage==SR2_LOAD_MUSIC?85:stage==SR2_LOAD_PICTURES?5:9;
    if(done>total)done=total;
    unsigned percent=base+(total?(unsigned)((uint64_t)done*span/total):0);
    if(percent<last_percent)percent=last_percent;
    if(stage!=last_stage || percent>=last_percent+4 || done==total){
        last_stage=stage;last_percent=percent;draw(stage,percent);
    }
}
void sr2_loading_end(void){
    if(showing)draw(SR2_LOAD_EFFECTS,100);
    free(title);title=NULL;showing=false;
}
#endif
