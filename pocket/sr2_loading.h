/* SPDX-License-Identifier: MIT. Boot-only UI, no game state changes. */
#ifndef SR2_LOADING_H
#define SR2_LOADING_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
enum { SR2_LOAD_MUSIC, SR2_LOAD_PICTURES, SR2_LOAD_EFFECTS };
#ifdef SR2_WITH_LOADING
void sr2_loading_begin(const uint8_t counts[16],const uint8_t values[16]);
void sr2_loading_progress(unsigned stage,uint32_t done,uint32_t total);
void sr2_loading_end(void);
#else
#define sr2_loading_progress(stage,done,total) ((void)0)
#endif
/* Chunk only boot reads, so progress does not require an extra asset pass. */
static inline bool sr2_loading_read(FILE *f,uint8_t *p,size_t size,unsigned stage){
#ifdef SR2_WITH_LOADING
    size_t done=0;sr2_loading_progress(stage,0,(uint32_t)size);
    while(done<size){
        size_t n=size-done;if(n>262144)n=262144;
        if(fread(p+done,1,n,f)!=n)return false;
        done+=n;sr2_loading_progress(stage,(uint32_t)done,(uint32_t)size);
    }
    return true;
#else
    (void)stage;return fread(p,1,size,f)==size;
#endif
}
/* Strict bounds checks around the original LIB1 title's run-token format. */
bool sr2_loading_decode(const uint8_t *lib,size_t size,const uint8_t counts[16],
                        const uint8_t values[16],uint8_t pixels[64000]);
#endif
