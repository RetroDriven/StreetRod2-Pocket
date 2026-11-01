#ifndef RD2_TEXTMODE_RESAMPLE_H
#define RD2_TEXTMODE_RESAMPLE_H
#include <stdint.h>
#include <stddef.h>
/* First hardware milestone: 640x200 DOS text display -> 320x200 source.
 * Fixed 2:1 horizontal reduction, 200 lines preserved; no source overflow.
 * The Pocket scaler then maps 320x240 to the LCD's configured aspect. */
static inline void rd2_textmode_640_to_320(uint32_t *dst, const uint32_t *src){
    for(unsigned y=0;y<200;y++)
        for(unsigned x=0;x<320;x++)
            dst[y*320+x]=src[y*640+x*2];
}
#endif
