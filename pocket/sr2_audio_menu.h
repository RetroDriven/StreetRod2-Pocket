/* RetroDriven boot audio menu. No game state or backend changes after boot. */
#ifndef SR2_AUDIO_MENU_H
#define SR2_AUDIO_MENU_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "pocket_controls.h"

enum {
    SR2_MENU_A=1u, SR2_MENU_B=2u, SR2_MENU_X=4u, SR2_MENU_Y=8u,
    SR2_MENU_UP=16u, SR2_MENU_DOWN=32u, SR2_MENU_START=64u,
    SR2_MENU_L1=128u, SR2_MENU_SELECT=256u
};
#define SR2_MENU_AUTOSTART_FRAMES 188u
typedef struct {
    unsigned row, frames, previous;
    int mode;
    bool debug, done;
} sr2_audio_menu_t;
static const int sr2_menu_modes[6]={3,5,6,0,1,4};
static const char *const sr2_menu_labels[6]={
    "FULL AUDIO - RECOMMENDED",
    "MUSIC ONLY - COMPARISON",
    "EFFECTS ONLY - COMPARISON",
    "PC SPEAKER - DIAGNOSTIC",
    "FAST FM - LEGACY DIAGNOSTIC",
    "PCM PLUS FAST FM - DIAGNOSTIC"
};
static inline void sr2_audio_menu_init(sr2_audio_menu_t *s){
    memset(s,0,sizeof *s);s->mode=3;
}
static inline void sr2_audio_menu_step(sr2_audio_menu_t *s,unsigned buttons){
    if(s->done)return;
    unsigned pressed=buttons & ~s->previous;s->previous=buttons;
    if(pressed & SR2_MENU_X){s->mode=(buttons & SR2_MENU_L1)?5:3;s->done=true;return;}
    if(!s->debug){
        if(pressed & (SR2_MENU_Y|SR2_MENU_SELECT|SR2_MENU_UP|SR2_MENU_DOWN)){
            s->debug=true;
            if((pressed & SR2_MENU_UP) && !(pressed & SR2_MENU_DOWN))s->row=5;
            else if((pressed & SR2_MENU_DOWN) && !(pressed & SR2_MENU_UP))s->row=1;
        }else if(pressed & (SR2_MENU_A|SR2_MENU_START|SR2_MENU_B))s->done=true;
        else if(++s->frames>=SR2_MENU_AUTOSTART_FRAMES)s->done=true;
    }else{
        bool up=(pressed & SR2_MENU_UP)!=0,down=(pressed & SR2_MENU_DOWN)!=0;
        if(up!=down)s->row=(s->row+(up?5u:1u))%6u;
        if(pressed & SR2_MENU_B){s->mode=3;s->done=true;}
        else if(pressed & (SR2_MENU_A|SR2_MENU_START)){
            s->mode=sr2_menu_modes[s->row];s->done=true;
        }
    }
}
/* Uses the game's existing EGA palette; no palette restoration is needed. */
static inline void sr2_menu_text(uint8_t *fb,int x,int y,const char *text,uint8_t fg){
    for(;*text;text++,x+=6){
        if(*text=='.'){
            if(x+2<320 && y+6<240 && x>=0 && y>=0)fb[(y+6)*320+x+2]=fg;
        }else srp_draw_char(fb,x,y,*text,fg);
    }
}
static inline void sr2_menu_rect(uint8_t *fb,int x,int y,int width,int height,uint8_t fg){
    for(int yy=y;yy<y+height;yy++)memset(fb+yy*320+x,fg,(unsigned)width);
}
static inline void sr2_audio_menu_draw(uint8_t *fb,const sr2_audio_menu_t *s){
    memset(fb,0,320u*240u);
    sr2_menu_rect(fb,12,10,296,35,1);
    sr2_menu_text(fb,22,16,"RETRODRIVEN",11);
    sr2_menu_text(fb,22,32,"STREET ROD 2 - V1.0.0",15);
    if(!s->debug){
        sr2_menu_text(fb,20,58,"NORMAL AUDIO",10);
        sr2_menu_rect(fb,12,73,296,84,1);
        sr2_menu_text(fb,22,85,"FULL AUDIO - RECOMMENDED",15);
        sr2_menu_text(fb,22,105,"NUKED MUSIC AND ACCURATE OPAL EFFECTS",11);
        sr2_menu_text(fb,22,125,"USE THIS FOR GARAGE TESTING",7);
        sr2_menu_text(fb,20,168,"AUTO PLAY - NO BUTTON NEEDED",7);
        sr2_menu_rect(fb,20,183,280,3,8);
        unsigned fill=280u*(s->frames>SR2_MENU_AUTOSTART_FRAMES?SR2_MENU_AUTOSTART_FRAMES:s->frames)/SR2_MENU_AUTOSTART_FRAMES;
        if(fill)sr2_menu_rect(fb,20,183,(int)fill,3,10);
        sr2_menu_text(fb,20,202,"A OR START TO PLAY",15);
        sr2_menu_text(fb,20,215,"Y FOR AUDIO DEBUG MENU",14);
        sr2_menu_text(fb,20,228,"X FULL AUDIO   L1 X MUSIC ONLY",7);
    }else{
        sr2_menu_text(fb,20,54,"AUDIO DEBUG MENU",14);
        for(unsigned i=0;i<6;i++){
            int y=70+(int)i*18;
            if(i==s->row)sr2_menu_rect(fb,12,y-4,296,17,1);
            sr2_menu_text(fb,22,y,sr2_menu_labels[i],i==s->row?15:7);
            if(i==s->row)sr2_menu_text(fb,14,y,">",11);
        }
        const char *hint=s->row==0?"TEST GARAGE EFFECTS AND MUSIC RECOVERY":
            s->row==1?"SAME MUSIC - ALL LIVE EFFECTS DISABLED":
            s->row==2?"ALL MUSIC MUTED - LIVE EFFECTS ONLY":
            s->row==3?"ORIGINAL PC SPEAKER SOUND PATH":
            s->row==4?"LOW QUALITY FM - COMPARISON ONLY":
                       "LEGACY OVERLAY - MUSIC MAY BE DOUBLED";
        sr2_menu_text(fb,20,184,hint,11);
        sr2_menu_text(fb,20,205,"UP DOWN SELECT   A OR START PLAY",15);
        sr2_menu_text(fb,20,218,"B PLAY NORMAL AUDIO",7);
        sr2_menu_text(fb,20,231,"IN GAME - SELECT R1 PROFILING",7);
    }
}
#endif
