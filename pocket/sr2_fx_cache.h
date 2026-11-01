/* SPDX-License-Identifier: MIT. Locally rendered sustained effects, no Fast FM. */
#ifndef SR2_FX_CACHE_H
#define SR2_FX_CACHE_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*sr2_fx_write_fn)(uint8_t,uint8_t);
typedef void (*sr2_fx_mute_fn)(unsigned);
typedef struct {uint32_t loaded,starts,frames,active,transitions;} sr2_fx_cache_stats_t;
bool sr2_fx_cache_load(void);
void sr2_fx_cache_reset(void);
bool sr2_fx_cache_write(uint8_t reg,uint8_t value,sr2_fx_write_fn live,sr2_fx_mute_fn mute);
int16_t sr2_fx_cache_sample(void);
void sr2_fx_cache_skip(uint32_t frames);
void sr2_fx_cache_stats(sr2_fx_cache_stats_t *out);
uint8_t sr2_fx_cache_register(unsigned channel,unsigned index);
/* O(1) OPL2 melodic register mapping, including the operator-address holes. */
static inline bool sr2_fx_cache_decode_register(uint8_t reg,unsigned *channel,unsigned *index){
    if(reg>=0xa0 && reg<=0xa8){*channel=reg-0xa0;*index=11;return true;}
    if(reg>=0xb0 && reg<=0xb8){*channel=reg-0xb0;*index=12;return true;}
    if(reg>=0xc0 && reg<=0xc8){*channel=reg-0xc0;*index=10;return true;}
    unsigned group=reg&0xe0,offset=reg&31,base;
    switch(group){case 0x20:base=0;break;case 0x40:base=2;break;
        case 0x60:base=4;break;case 0x80:base=6;break;case 0xe0:base=8;break;
        default:return false;}
    static const int8_t slots[22]={0,2,4,1,3,5,-1,-1,6,8,10,7,9,11,-1,-1,12,14,16,13,15,17};
    if(offset>=22 || slots[offset]<0)return false;
    unsigned slot=(unsigned)slots[offset];*channel=slot/2;*index=base+(slot&1);return true;
}
#endif
