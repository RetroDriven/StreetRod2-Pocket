/* SPDX-License-Identifier: MIT
 * Tiny original picture archives, kept read-only in RAM. No save interception.
 * Windows desktop retains disk stdio; the target musl provides fmemopen.
 */
#include "sr2_asset_cache.h"
#include "sr2_loading.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
static struct {unsigned char *bytes;uint32_t size;} libraries[3];
static const char *names[3]={"LIB1","LIB2","LIB3"};
unsigned sr2_asset_cache_preload(uint32_t budget){
    unsigned loaded=0;
    for(unsigned i=0;i<3;i++){
        if(libraries[i].bytes){loaded++;continue;}
        FILE *f=fopen(names[i],"rb");
        if(!f){char slot[12];snprintf(slot,sizeof slot,"slot:%u",4+i);f=fopen(slot,"rb");}
        if(!f)continue;
        long n=-1;if(!fseek(f,0,SEEK_END))n=ftell(f);
        unsigned char *p=NULL;
        if(n>0 && (uint64_t)n<=budget && !fseek(f,0,SEEK_SET)){
            p=malloc((size_t)n);
            if(p && fread(p,1,(size_t)n,f)!=(size_t)n){free(p);p=NULL;}
        }
        fclose(f);
        if(p){libraries[i].bytes=p;libraries[i].size=(uint32_t)n;budget-=(uint32_t)n;loaded++;}
        sr2_loading_progress(SR2_LOAD_PICTURES,i+1,3);
    }
    printf("[SR2-ASSET-RAM] libraries=%u\n",loaded);return loaded;
}
FILE *sr2_asset_cache_open(const char *name){
    const char *base=name;
    for(const char *s=name;*s;s++)if(*s==':' || *s=='/' || *s=='\\')base=s+1;
    for(unsigned i=0;i<3;i++){
        if(!libraries[i].bytes)continue;
        unsigned j=0;
        while(base[j] && names[i][j]){
            unsigned char c=(unsigned char)base[j];if(c>='a' && c<='z')c-=32;
            if(c!=(unsigned char)names[i][j])break;
            j++;
        }
        if(!base[j] && !names[i][j])return fmemopen(libraries[i].bytes,libraries[i].size,"rb");
    }
    return NULL;
}
#else
unsigned sr2_asset_cache_preload(uint32_t budget){(void)budget;return 0;}
FILE *sr2_asset_cache_open(const char *name){(void)name;return NULL;}
#endif
