/* SPDX-License-Identifier: MIT. Exact fast cases for the original EGA model.
 * Unsupported register states, wrapping and hazardous copy overlap retain
 * the original byte-cycle implementation. No timing/animation changes. */
#ifndef SR2_VGA_FAST_H
#define SR2_VGA_FAST_H
#include "platform/vga.h"
#include "platform/ega.h"
#include <string.h>
/* A decoded run writes one constant byte, so memset retains in-place decode
 * ordering even if that run covers compressed bytes consumed later. */
static inline bool sr2_vga_fast_repeat(u16 seg,u16 *off,u8 value,u16 count){
    if(is_vram(seg) || !count || (unsigned)*off+count>65536u)return false;
    memset(mp(seg,*off),value,count);*off=(u16)(*off+count);return true;
}
static inline bool sr2_vga_fast_copy(u16 ss,u16 so,u16 ds,u16 dest,u16 n){
    if(!n || (unsigned)so+n>65536u || (unsigned)dest+n>65536u)return false;
    bool sv=is_vram(ss),dv=is_vram(ds);
    if(!sv && !dv){
        unsigned s=lin(ss,so),d=lin(ds,dest);
        /* rep movsb propagates bytes on hazardous forward overlap; memmove
         * deliberately does not, so retain the original loop in that case. */
        if(d>s && d<s+n)return false;
        memmove(mem+d,mem+s,n);return true;
    }
    if(!sv && dv){
        unsigned d=vram_off(ds,dest);
        if(d+n>65536u || (vga.mode&3u) || vga.enable_sr ||
           (vga.func&24u) || vga.bit_mask!=255)return false;
        for(unsigned p=0;p<4;p++)if(vga.map_mask&(1u<<p))
            memcpy(ega_plane((int)p)+d,mp(ss,so),n);
        /* RAM reads leave the latches untouched, even for a multi-plane write. */
        ega_touch();return true;
    }
    if(sv && !dv){
        unsigned s=vram_off(ss,so);
        if(s+n>65536u || (vga.mode&8u))return false;
        memcpy(mp(ds,dest),ega_plane(vga.read_map&3u)+s,n);
        for(unsigned p=0;p<4;p++)vga.latch[p]=ega_plane((int)p)[s+n-1];
        return true;
    }
    if((vga.mode&3)!=1)return false;
    unsigned s=vram_off(ss,so),d=vram_off(ds,dest);
    if((unsigned)so+n>65536u || (unsigned)dest+n>65536u ||
       s+n>65536u || d+n>65536u || (d>s && d<s+n))return false;
    for(unsigned p=0;p<4;p++){
        u8 *plane=ega_plane((int)p);
        /* Original final read loads all four latches, even disabled planes. */
        vga.latch[p]=plane[s+n-1];
        if(vga.map_mask&(1u<<p))memmove(plane+d,plane+s,n);
    }
    ega_touch();return true;
}
static inline bool sr2_vga_fast_mask_or(FarPtr d,u16 seg,u16 s,u16 m,
                                      s16 stride,s16 w,s16 h,s16 dstride){
    unsigned p=vga.read_map&3u;
    if(w<=0 || h<=0 || is_vram(seg) || !is_vram(d.seg) ||
       (vga.mode&11u) || vga.enable_sr || (vga.func&24u) ||
       vga.bit_mask!=255 || vga.map_mask!=(1u<<p))return false;
    u16 so=s,mo=m,dd=d.off;
    /* Preflight all rows before touching anything; retain 16-bit row steps. */
    for(int y=0;y<h;y++){
        if((unsigned)so+(unsigned)w>65536u || (unsigned)mo+(unsigned)w>65536u ||
           (unsigned)dd+(unsigned)w>65536u || (unsigned)vram_off(d.seg,dd)+(unsigned)w>65536u)return false;
        so=(u16)(so+stride);mo=(u16)(mo+stride);dd=(u16)(dd+dstride);
    }
    u8 *plane=ega_plane((int)p);
    for(int y=0;y<h;y++){
        unsigned off=vram_off(d.seg,d.off);
        if(y==h-1){
            /* Word reads load the high-byte latches; an odd-width row ends
             * with the separate byte at column zero. Capture before writing. */
            unsigned last=off+((w&1)?0u:1u);
            for(unsigned k=0;k<4;k++)vga.latch[k]=ega_plane((int)k)[last];
        }
        const u8 *src=mp(seg,s),*mask=mp(seg,m);u8 *dst=plane+off;
        for(int x=0;x<w;x++)dst[x]=(u8)((dst[x]&mask[x])|src[x]);
        s=(u16)(s+stride);m=(u16)(m+stride);d.off=(u16)(d.off+dstride);
    }
    ega_touch();return true;
}
#endif
