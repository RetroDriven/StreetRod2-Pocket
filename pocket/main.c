/* RetroDriven Street Rod 2 entry: native Pocket runtime. */
#include "of.h"
#include "pocket_pc_audio.h"
#include "sr2_pcm.h"
#ifdef SR2_WITH_FX_CACHE
#include "sr2_fx_cache.h"
#endif
#include "engine/host.h"
#include "engine/mem.h"
#include "engine/platform/ega.h"
#include "engine/game/game.h"
#include "srp_save_id.h"
#include "pocket_exit_screen.h"
#ifndef SR2_WITH_LOADING
#include "pocket_controls.h"
#endif
#ifdef SR2_DEVELOPMENT_TOOLS
#include "sr2_audio_menu.h"
#endif
#include "sr2_loading.h"
#ifdef SR2_WITH_LOADING
#include "engine/symbols.h"
#endif
#include <stdio.h>
#include <string.h>
#ifdef SR2_WITH_ASSET_CACHE
#include "sr2_asset_cache.h"
#endif
#include <unistd.h>

__attribute__((noreturn)) void srp_pocket_quit_now(void){
    printf("[SR2-EXIT] game returned; displaying terminal screen\n");
    host_shutdown();
    srp_show_exit_screen();
    for(;;)usleep(100000);
}

#ifdef SR2_DEVELOPMENT_TOOLS
/* Custom Pocket Interact values remain unreliable in this SDK. This menu
 * chooses a backend before the game starts; it never switches a live chip. */
static unsigned sr2_audio_menu_buttons(void){
    return (of_btn(OF_BTN_A)?SR2_MENU_A:0u) |
        (of_btn(OF_BTN_B)?SR2_MENU_B:0u) |
        (of_btn(OF_BTN_X)?SR2_MENU_X:0u) |
        (of_btn(OF_BTN_Y)?SR2_MENU_Y:0u) |
        (of_btn(OF_BTN_UP)?SR2_MENU_UP:0u) |
        (of_btn(OF_BTN_DOWN)?SR2_MENU_DOWN:0u) |
        (of_btn(OF_BTN_START)?SR2_MENU_START:0u) |
        (of_btn(OF_BTN_L1)?SR2_MENU_L1:0u) |
        (of_btn(OF_BTN_SELECT)?SR2_MENU_SELECT:0u);
}
static int sr2_boot_sound_choice(void){
#if defined(SR2_FORCE_FAST_FM)
    printf("[SR2-BOOT-AUDIO] forced Fast FM via build option\n");
    return 1;
#else
    sr2_audio_menu_t menu;
    sr2_audio_menu_init(&menu);
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    printf("[SR2-BOOT-AUDIO] v1.0.0 default=PCM+Opal; Y opens audio debug menu\n");
    printf("Pocket Sound Device menu is bypassed; use the boot audio debug menu.\n");
    while(!menu.done){
        of_input_poll();
        sr2_audio_menu_step(&menu,sr2_audio_menu_buttons());
        uint8_t *fb=of_video_surface();
        if(fb){sr2_audio_menu_draw(fb,&menu);of_video_flip();}
        usleep(16000);
    }
    /* Consume the menu buttons until released, including D-pad and Start.
     * A held confirmation must not click through the original DOS title. */
    do{of_input_poll();if(!sr2_audio_menu_buttons())break;usleep(10000);}while(1);
    int mode=menu.mode;
    const char *name=mode==6?"ACCURATE OPAL EFFECTS ONLY":mode==5?"ORIGINAL ADLIB PCM MUSIC ONLY":mode==4?"PCM + LIVE FM DIAGNOSTIC":mode==3?"ORIGINAL ADLIB PCM + OPAL FX":mode==2?"OPAL OPL2 TEST":mode==1?"ADLIB FAST FM":"PC SPEAKER";
    printf("[SR2-BOOT-AUDIO] selected=%s (controller; no Interact read)\n",name);
    fflush(stdout);
    return mode;
#endif
}

#else
/* Release playback: Nuked OPL3 music plus accurate cached/Opal effects.
 * No selector delay or diagnostic controller shortcuts in normal builds. */
static int sr2_boot_sound_choice(void){return 3;}
#endif

int main(void){
    of_file_slot_register(4,"LIB1");
    of_file_slot_register(5,"LIB2");
    of_file_slot_register(6,"LIB3");
    of_file_slot_register(20,"SR.EXE");
    of_file_slot_register(21,"SR2_DATA");
    of_file_slot_register(22,"SR2_SHP");
    of_file_slot_register(23,"SR2_END");
    of_file_slot_register(24,"MSR.SND");
    of_file_slot_register(25,"MSRBEG.MUS");
    of_file_slot_register(26,"MSR2.MUS");
    of_file_slot_register(27,"MSR20.MUS");
    of_file_slot_register(28,"MSR21.MUS");
    of_file_slot_register(29,"MSR24.MUS");
    of_file_slot_register(30,"PAUSE.MUS");
    of_file_slot_register(31,"SR2_PCM.PAK"); /* optional locally rendered music */
#ifdef SR2_WITH_FX_CACHE
    of_file_slot_register(32,"SR2_FX.PAK");
#endif
    srp_save_set_instance(SRP_GAME_ORIGINAL);
    of_video_init();
    char err[256]={0};
    if(!mem_load_exe("SR.EXE",err,sizeof(err))){
        host_init(".",1,true);
        host_fatal("Street Rod 2 DOS executable could not load: %s",err);
    }
    if(!host_init(".",1,true))host_fatal("SR2 Pocket host_init failed");
    of_video_set_display_mode(OF_DISPLAY_TERMINAL);
    int mode=sr2_boot_sound_choice();
#ifdef SR2_WITH_LOADING
    sr2_loading_begin(&DSB(DS_run_counts),&DSB(DS_run_values));
#endif
    if(mode==3 || mode==4 || mode==5){
#ifndef SR2_WITH_LOADING
        of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
        uint8_t *fb=of_video_surface();
        if(fb){
            memset(fb,0,320*240);
            const char *title="RETRODRIVEN STREET ROD 2",*loading="LOADING GAME DATA";
            srp_draw_text(fb,(320-(int)strlen(title)*6)/2,90,title,15);
            srp_draw_text(fb,(320-(int)strlen(loading)*6)/2,108,loading,12);
            of_video_flip();
        }
#endif
        const struct of_capabilities *caps=of_get_caps();
        /* Keep at least half the app heap available for gameplay and assets.
         * No platform memory addresses or physical RAM sizes are assumed. */
        /* Successful preload already verifies the archive; avoid opening it
         * two more times for a redundant default-mode diagnostic. */
        if(!caps || !sr2_pcm_preload(caps->heap_size/2u))sr2_pcm_probe();
    }
    sr2_loading_progress(SR2_LOAD_PICTURES,0,1);
#ifdef SR2_WITH_ASSET_CACHE
    {const struct of_capabilities *caps=of_get_caps();
     if(caps)sr2_asset_cache_preload(caps->heap_size/16u);}
#endif
    sr2_loading_progress(SR2_LOAD_EFFECTS,0,1);
#ifdef SR2_WITH_FX_CACHE
    if((mode==3 || mode==6) && !sr2_fx_cache_load())
        printf("[SR2-FX-CACHE] missing or invalid; accurate live FX retained (high CPU). Reinstall owner-generated SR2_FX.PAK.\n");
#endif
#ifdef SR2_WITH_LOADING
    sr2_loading_end();
#endif
    srp_audio_select_backend(mode);
    sr2_pcm_select(mode==3 || mode==4 || mode==5);
    printf("[SR2-AUDIO] backend=%s; game argv=%s\n",
           mode==6?"Accurate Opal EFFECTS ONLY":mode==5?"Original PCM MUSIC ONLY":mode==4?"PCM + Fast FM TEST":mode==3?"Original AdLib PCM":mode==2?"Opal OPL2":mode==1?"Fast FM":"PC Speaker",
           mode?"adlib":"speaker");
    of_video_set_display_mode(OF_DISPLAY_FRAMEBUFFER);
    ega_init();
    char *argv[]={"streetrod2","v",mode?"adlib":"speaker",NULL};
    int rc=game_main(3,argv);
    printf("[SR2] game_main returned: %d\n",rc);
    srp_pocket_quit_now();
}
