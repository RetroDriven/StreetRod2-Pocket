/* Experimental low-CPU FM approximation for the Pocket's RISC-V application
 * CPU. This reads original SR2 OPL2 registers and approximates nine two-op FM
 * channels; it is NOT a fully accurate YM3812. No table/malloc/file I/O in the
 * render hot path. PC Speaker synthesis remains completely separate. */
#include "fast_opl2.h"
#include <string.h>
#include <math.h>

#define RATE 24000u
#define PI 3.14159265358979323846
#define FX_NOISE_FRAMES 2400u /* short 100 ms percussion burst at 24 kHz */
static int16_t sine_table[1024];
static bool table_ready;
static const uint8_t mod_slot[9]={0,1,2,6,7,8,12,13,14};
static const uint8_t car_slot[9]={3,4,5,9,10,11,15,16,17};
static const uint8_t slot_reg[18]={0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21};
/* OPL multiple 0 is half-frequency; multipliers are stored as twice value. */
static const uint8_t twice_mult[16]={1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30};

static uint32_t voice_step(const rd_opl2_t *s, unsigned ch, unsigned slot){
    unsigned fnum=s->regs[0xa0+ch]|((unsigned)(s->regs[0xb0+ch]&3)<<8);
    unsigned block=(s->regs[0xb0+ch]>>2)&7;
    unsigned mul=twice_mult[s->regs[0x20+slot_reg[slot]]&15];
    /* 32-bit phase increment. f(Hz)=fnum*49716/2^(20-block). */
    uint64_t n=(uint64_t)fnum*49716u*mul*4294967296ull;
    uint64_t d=(uint64_t)RATE<< (21u-block);
    if(!d)return 0;
    uint64_t step=n/d;
    return (uint32_t)(step>0x7fffffffu?0x7fffffffu:step);
}
static int slot_from_offset(unsigned off){
    for(int i=0;i<18;i++)if(slot_reg[i]==off)return i;
    return -1;
}
static void update_step(rd_opl2_t *s, unsigned ch){
    s->step[mod_slot[ch]]=voice_step(s,ch,mod_slot[ch]);
    s->step[car_slot[ch]]=voice_step(s,ch,car_slot[ch]);
}
static void update_steps_for_slot(rd_opl2_t *s,unsigned slot){
    for(unsigned i=0;i<9;i++)if(mod_slot[i]==slot||car_slot[i]==slot){update_step(s,i);return;}
}
void rd_opl2_reset(rd_opl2_t *s){
    if(!table_ready){
        for(unsigned i=0;i<1024;i++)sine_table[i]=(int16_t)(sin((2.0*PI*i)/1024.0)*32760.0);
        table_ready=true;
    }
    memset(s,0,sizeof(*s));
    s->noise=0xace1u;
}
void rd_opl2_fx_mode(rd_opl2_t *s,bool on){
    s->fx_mode=on;
    s->fx_noise_remaining=0;
}
void rd_opl2_enable(rd_opl2_t *s,bool on){
    s->enabled=on;
    if(!on){for(int i=0;i<18;i++)s->state[i]=s->envelope[i]=0;s->fx_noise_remaining=0;}
}
void rd_opl2_write(rd_opl2_t *s,uint8_t reg,uint8_t value){
    uint8_t old=s->regs[reg];
    s->regs[reg]=value;
    s->writes++;
    if(s->fx_mode && reg==0xbdu){
        /* This low-cost renderer has no real OPL percussion envelopes.
           In X's isolated effects bus, let a new noise-drum key-on produce
           a brief transient instead of injecting permanent full-band hiss
           for as long as a BD register bit stays latched. Fast FM (B) is
           deliberately unchanged for comparison. */
        if((value&0x20u) && ((value&0x0bu)&~(old&0x0bu)))
            s->fx_noise_remaining=FX_NOISE_FRAMES;
        if(!(value&0x20u) || !(value&0x0bu))
            s->fx_noise_remaining=0;
    }
    if(reg>=0xa0 && reg<=0xa8)update_step(s,reg-0xa0);
    else if(reg>=0xb0 && reg<=0xb8){
        unsigned ch=reg-0xb0;
        update_step(s,ch);
        if((old^value)&0x20){
            unsigned m=mod_slot[ch],c=car_slot[ch];
            if(value&0x20){s->state[m]=s->state[c]=1;s->envelope[m]=s->envelope[c]=0;s->keys++;}
            else{s->state[m]=s->state[c]=4;}
        }
    }else if(reg>=0x20 && reg<=0x35){
        int sl=slot_from_offset(reg-0x20);
        if(sl>=0)update_steps_for_slot(s,(unsigned)sl);
    }
}
static int32_t op_sample(rd_opl2_t *s,unsigned slot, int32_t phase_mod){
    if(!s->state[slot])return 0;
    unsigned off=slot_reg[slot];
    unsigned attack=(s->regs[0x60+off]>>4)&15;
    unsigned decay=s->regs[0x60+off]&15;
    unsigned release=s->regs[0x80+off]&15;
    unsigned sustain=(s->regs[0x80+off]>>4)&15;
    uint32_t env=s->envelope[slot];
    switch(s->state[slot]){
    case 1:{unsigned step=attack?(1u<<(attack>11?11:attack)):1u;env+=step;if(env>=65535u){env=65535u;s->state[slot]=2;}break;}
    case 2:{unsigned target=(15u-sustain)*4096u;unsigned d=decay?decay*2u:1u;if(env>target+d)env-=d;else{env=target;s->state[slot]=3;}break;}
    case 3:break;
    case 4:{unsigned d=release?release*12u:1u;if(env>d)env-=d;else{env=0;s->state[slot]=0;}break;}
    }
    s->envelope[slot]=(uint16_t)env;
    s->phase[slot]+=s->step[slot];
    unsigned idx=((s->phase[slot]+(uint32_t)phase_mod)>>22)&1023u;
    uint8_t wave=s->regs[0xe0+off]&3u;
    int32_t val=sine_table[idx];
    if(wave==1 && idx>=512)val=0;
    else if(wave==2 && val<0)val=-val;
    else if(wave==3 && idx>=256)val=0;
    unsigned tl=s->regs[0x40+off]&63u;
    int32_t amp=(int32_t)((63u-tl)*(63u-tl));
    val=(val*(int32_t)env)>>16;
    return (val*amp)>>12;
}
int16_t rd_opl2_sample(rd_opl2_t *s){
    if(!s->enabled)return 0;
    int32_t total=0;
    unsigned rhythm=s->regs[0xbd]&0x20u;
    for(unsigned ch=0;ch<9;ch++){
        unsigned m=mod_slot[ch],c=car_slot[ch];
        if(!s->state[m] && !s->state[c])continue;
        int32_t mod=op_sample(s,m,(int32_t)s->prev_mod[ch] << 14);
        s->prev_mod[ch]=(int16_t)mod;
        unsigned additive=s->regs[0xc0+ch]&1u;
        int32_t car=op_sample(s,c,additive?0:(mod<<12));
        total+=additive?(mod+car)/2:car;
    }
    if(rhythm){
        /* Low-cost percussion fallback; not cycle-accurate YM3812 rhythm. */
        unsigned drums=s->regs[0xbd]&31u;
        if(drums){
            uint32_t x=s->noise;x^=x<<13;x^=x>>17;x^=x<<5;s->noise=x;
            int32_t n=(x&1u)?1:-1;
            int32_t noise_component=0;
            if(drums&0x08)noise_component+=n*500;
            if(drums&0x01)noise_component+=n*200;
            if(drums&0x02)noise_component+=n*200;
            if(s->fx_mode){
                if(s->fx_noise_remaining){
                    noise_component=(int32_t)((int64_t)noise_component*
                        s->fx_noise_remaining/FX_NOISE_FRAMES);
                    s->fx_noise_remaining--;
                }else noise_component=0;
            }
            total+=noise_component;
        }
    }
    total/=3;
    if(total>30000)total=30000;
    if(total< -30000)total=-30000;
    return (int16_t)total;
}
